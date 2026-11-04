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
 * Browser guests can't speak AOO, so the engine can also push a plain PCM16
 * stream to the broker's UDP relay (see internal/relay in the Go broker).
 *
 * Compile with -DGIGMA_HAS_AOO=1 and link libaoo + Opus for the real path.
 * Without that flag, process() is a local loopback.
 */
class GigmaAooEngine
{
public:
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

    /** Make the set of AOO peers (each "host:port") exactly `peers`. Thread-safe. */
    void syncPeers (const std::set<std::string>& peers);
    size_t numPeers() const;

    /** Browser relay: PCM16 to the broker's UDP ingest. Thread-safe. */
    void setRelay (const std::string& relayHostPort, const std::string& sessionId,
                   const std::string& hostId);
    void setRelayEnabled (bool on) { relayEnabled = on; }
    bool relayActive() const { return relayEnabled.load() && relayConfigured.load(); }

    /** Best-effort LAN IPv4 of the default route ("127.0.0.1" if offline). */
    static std::string detectLanIp();

private:
    void networkThreadMain();
    void drainRelay();

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

#if GIGMA_HAS_AOO
    struct AooState;
    std::unique_ptr<AooState> aoo;
    static int32_t udpSend (void* user, const unsigned char* data, int32_t size,
                            const void* address, uint32_t addrlen, uint32_t flags);
#endif
};
