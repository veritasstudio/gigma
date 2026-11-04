#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

/**
 * DAW-side AOO (Audio over OSC) endpoint, following the SonoBus pattern:
 * capture from processBlock → aoo source → UDP; UDP → aoo sink → processBlock out.
 *
 * Peers (other Gigma plugins) are learned from the broker and added with
 * AooSource::addSink, so audio leaves the DAW straight to them over UDP.
 *
 * Across NATs, peers are addressed as "relay:<peerId>": datagrams to them are
 * wrapped ("GGR1" header) and sent to the broker's UDP relay, which forwards
 * them to that peer's public address (see internal/relay/peer.go). Inside AOO
 * each relayed peer appears as a stable placeholder address in 240.0.0.0/4,
 * so AOO's own protocol (pings, resends, format negotiation) is untouched.
 *
 * Browser guests can't speak AOO, so the engine can also push a plain PCM16
 * stream to the broker's UDP relay (see internal/relay in the Go broker).
 *
 * Compile with -DGIGMA_HAS_AOO=1 and link libaoo + Opus for the real path.
 * Without that flag, process() is a local loopback.
 */
class GigmaAooEngine
{
public:
    /** Audio quality knobs (see plugin UI). Receive buffer applies to what *this*
        plugin hears; bitrate / frame / redundancy / limiter apply to what it sends. */
    struct Quality
    {
        int bufferMs = 150;       // AOO sink jitter buffer (latency) for incoming audio
        int bitrateKbps = 160;    // Opus target bitrate for outgoing audio
        int frameMs = 20;         // Opus frame: 10 (lower latency) or 20 (more robust)
        int redundancy = 1;       // send each packet N times (1..3), helps lossy links
        bool limiter = true;      // soft-clip the *send* copy only (dry track untouched)

        bool operator== (const Quality& o) const
        {
            return bufferMs == o.bufferMs && bitrateKbps == o.bitrateKbps && frameMs == o.frameMs
                && redundancy == o.redundancy && limiter == o.limiter;
        }
        bool operator!= (const Quality& o) const { return ! (*this == o); }
    };

    /** Cumulative network/audio health counters (since prepare). */
    struct Stats
    {
        uint64_t underruns = 0;   // sink jitter buffer ran dry → audible gap
        uint64_t drops = 0;       // blocks lost / skipped (late or never arrived)
        uint64_t resends = 0;     // blocks recovered by resend
        uint64_t xruns = 0;       // empty blocks because the *sender* overflowed
        uint64_t sendOverflows = 0; // our own send buffer overflowed (DAW burst)
    };

    /** Thread-safe; applies live (frame change restarts the outgoing stream). */
    void setQuality (const Quality& q);
    Quality quality() const;
    Stats stats() const;

    GigmaAooEngine();
    ~GigmaAooEngine();

    bool prepare (double sampleRate, int maxBlockSize, int numChannels);
    void release();

    /** Bind UDP + start network thread. Returns "lanIp:port" for the broker. */
    std::string startNetwork (int preferredPort = 0);
    void stopNetwork();

    void process (const float* const* inputs, float* const* outputs,
                  int numChannels, int numSamples);

    bool isNetworkRunning() const { return networkRunning.load(); }
    /** Reachable "ip:port" (LAN IPv4 of the default route), empty if not bound. */
    std::string endpoint() const;
    uint16_t port() const { return boundPort; }

    /** Make the set of AOO peers exactly `peers`. Each is "host:port" (direct UDP,
        LAN/VPN) or "relay:<16-char peerId>" (via the broker relay). Thread-safe. */
    void syncPeers (const std::set<std::string>& peers);
    size_t numPeers() const;

    /** Browser relay: PCM16 to the broker's UDP ingest. Thread-safe. */
    void setRelay (const std::string& relayHostPort, const std::string& sessionId,
                   const std::string& hostId);
    void setRelayEnabled (bool on) { relayEnabled = on; }
    bool relayActive() const { return relayEnabled.load() && relayConfigured.load(); }

    /** Internet path: broker relay "ip:port", session, and our relay secret
        (host: hostId, guest: join token). Empty relay disables. Thread-safe. */
    void setPeerRelay (const std::string& relayHostPort, const std::string& sessionId,
                       const std::string& secret);
    bool peerRelayConfigured() const { return peerRelayOn.load(); }
    /** True if the relay acked a keepalive in the last few seconds. */
    bool peerRelayUp() const;

    /** Browser guests' mics (GGB1 from the broker relay, see internal/relay/mic.go),
        decoded and mixed on top of the channel output. Thread-safe. */
    void setGuestMicGain (float g) { guestGain.store (g); }
    float guestMicGain() const { return guestGain.load(); }
    /** Browser mics heard in the last second. */
    int liveGuestMics() const;
    uint64_t guestMicPackets() const { return gmPackets.load(); }

    /** Best-effort LAN IPv4 of the default route ("127.0.0.1" if offline). */
    static std::string detectLanIp();

private:
    void networkThreadMain();
    void drainRelay();
    void sendPeerKeepalive();
    /** Placeholder sockaddr for a relayed peer id (allocated on first use). */
    std::vector<unsigned char> relayAddrFor (const std::string& peerId);
    /** Peer id for a placeholder sockaddr, empty if `addr` isn't one. */
    std::string relayIdFor (const void* addr, size_t len);
    bool sendViaPeerRelay (const unsigned char* data, size_t size, const void* addr, size_t len);
    bool handlePeerRelayPacket (const unsigned char* data, size_t n, const void* from, size_t fromLen);

    bool handleGuestMicPacket (const unsigned char* data, size_t n, const void* from, size_t fromLen);
    void mixGuestMics (float* const* outs, int ch, int n);

    void applyQualityLocked();   // needs peerMutex
    void sendChunk (int c0, int ch, int offset, int n, const float* const* inputs, float* const* outputs);

    mutable std::mutex qualityMutex;
    Quality q;
    int appliedFrameMs = 0;
    std::atomic<bool> limiterOn { true };
    std::atomic<uint64_t> stUnderruns { 0 }, stDrops { 0 }, stResends { 0 }, stXruns { 0 }, stSendOverflows { 0 };

    double sampleRate = 48000.0;
    int maxBlock = 512;
    int channels = 2;

    std::atomic<bool> ready { false };      // prepare() done; process() passes dry through otherwise
    std::atomic<bool> networkRunning { false };
    std::thread netThread;
    int udpSock = -1;
    std::atomic<uint16_t> boundPort { 0 };
    std::string boundHost;

    std::vector<std::vector<float>> channelScratchIn;
    std::vector<std::vector<float>> channelScratchOut;
    std::vector<std::vector<float>> channelScratchSend;   // limited copy fed to the network
    std::vector<const float*> planarSend;
    std::vector<const float*> planarIn;
    std::vector<float*> planarOut;

    mutable std::mutex endpointMutex;

    // Peers we've addSink'ed: "host:port" → sockaddr storage (kept alive).
    mutable std::mutex peerMutex;
    std::map<std::string, std::vector<unsigned char>> peers;

    // Relay: SPSC ring of interleaved int16 (audio thread → network thread).
    std::atomic<bool> relayEnabled { false };
    std::atomic<bool> relayConfigured { false };
    std::mutex relayMutex;
    std::vector<unsigned char> relayAddr;
    std::string relaySession, relayHost;
    std::vector<int16_t> ring;
    std::atomic<size_t> ringWrite { 0 }, ringRead { 0 };
    uint32_t relaySeq = 0;

    // Plugin↔plugin relay (see setPeerRelay). Lock order: peerMutex → prMutex.
    mutable std::mutex prMutex;
    std::atomic<bool> peerRelayOn { false };
    std::vector<unsigned char> prAddr;
    std::string prSession, prSecret;
    std::map<std::string, uint16_t> prIdToIdx;
    std::vector<std::string> prIdxToId { std::string() };   // index 0 unused
    std::vector<unsigned char> prScratch;
    std::atomic<int64_t> prLastAckMs { 0 };
    int64_t prLastKeepaliveMs = 0;

    // Browser mic voices: network thread decodes into per-voice SPSC rings,
    // audio thread mixes them (see GigmaAooEngine.cpp).
    struct GuestVoice;
    std::vector<std::unique_ptr<GuestVoice>> voices;
    std::atomic<float> guestGain { 1.0f };
    std::atomic<int> guestBufMs { 150 };
    std::atomic<uint64_t> gmPackets { 0 };

#if GIGMA_HAS_AOO
    struct AooState;
    std::unique_ptr<AooState> aoo;
    static void handleAooEvent (void* user, const union AooEvent* event, int32_t level);
    static int32_t udpSend (void* user, const unsigned char* data, int32_t size,
                            const void* address, uint32_t addrlen, uint32_t flags);
#endif
};
