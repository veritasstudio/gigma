#include "GigmaAooEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>

#if defined(_WIN32)
 #include <winsock2.h>
 #include <ws2tcpip.h>
 using socklen_t = int;
#else
 #include <arpa/inet.h>
 #include <fcntl.h>
 #include <netdb.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <unistd.h>
#endif

#if GIGMA_HAS_AOO
 #include "aoo.h"
 #include "aoo_source.hpp"
 #include "aoo_sink.hpp"
 #include "codec/aoo_pcm.h"
 #include "codec/aoo_opus.h"
 #include "opus.h"
#endif

namespace {
#if !defined(_WIN32)
int closesocket (int s) { return ::close (s); }
#endif

constexpr int kAooId = 1;                 // every Gigma plugin uses source id 1 / sink id 1
constexpr size_t kRingSamples = 1 << 18;  // ~2.7 s stereo @48k
constexpr int kRelayFramesPerPacket = 240;

/** "host:port" → sockaddr_in bytes. Empty on failure / 0.0.0.0. */
std::vector<unsigned char> resolveHostPort (const std::string& hp)
{
    auto colon = hp.rfind (':');
    if (colon == std::string::npos || colon == 0)
        return {};
    auto host = hp.substr (0, colon);
    auto port = hp.substr (colon + 1);
    if (host == "0.0.0.0" || port.empty())
        return {};

    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo (host.c_str(), port.c_str(), &hints, &res) != 0 || res == nullptr)
        return {};
    std::vector<unsigned char> out ((const unsigned char*) res->ai_addr,
                                    (const unsigned char*) res->ai_addr + res->ai_addrlen);
    freeaddrinfo (res);
    return out;
}

void putU32 (unsigned char* p, uint32_t v)
{
    p[0] = (unsigned char) (v & 0xff);
    p[1] = (unsigned char) ((v >> 8) & 0xff);
    p[2] = (unsigned char) ((v >> 16) & 0xff);
    p[3] = (unsigned char) ((v >> 24) & 0xff);
}

void putFixed16 (unsigned char* p, const std::string& s)
{
    std::memset (p, '0', 16);
    std::memcpy (p, s.data(), std::min<size_t> (16, s.size()));
}

constexpr const char* kRelayPrefix = "relay:";
constexpr size_t kPeerHdrOut = 52;   // GGR1 | session | secret | dst
constexpr size_t kPeerHdrIn = 20;    // GGR1 | src
const std::string kZeroId (16, '0');

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds> (
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool sameAddr (const std::vector<unsigned char>& a, const void* b, size_t blen)
{
    if (a.size() < sizeof (sockaddr_in) || blen < sizeof (sockaddr_in))
        return false;
    auto* x = (const sockaddr_in*) a.data();
    auto* y = (const sockaddr_in*) b;
    return x->sin_family == y->sin_family && x->sin_port == y->sin_port
        && x->sin_addr.s_addr == y->sin_addr.s_addr;
}
} // namespace

#if GIGMA_HAS_AOO
namespace {
/** aoo_initialize() only runs once per process, but aoo_terminate() wipes the
    codec registry. Calling terminate on every DAW re-prepare (or when one of
    several instances goes away) silently left every later source/sink with no
    Opus/PCM codec. Initialise once, never terminate from a plugin instance. */
void ensureAooInitialised()
{
    static std::once_flag once;
    std::call_once (once, [] { aoo_initialize (nullptr); });
}
} // namespace

struct GigmaAooEngine::AooState
{
    AooSource::Ptr source;
    AooSink::Ptr sink;
};

AooInt32 AOO_CALL GigmaAooEngine::udpSend (void* user, const AooByte* data, AooInt32 size,
                                           const void* address, AooAddrSize addrlen, AooFlag)
{
    auto* self = static_cast<GigmaAooEngine*> (user);
    if (self->udpSock < 0)
        return -1;
    if (self->sendViaPeerRelay (data, (size_t) size, address, (size_t) addrlen))
        return size;
    return (AooInt32) ::sendto (self->udpSock, data, (size_t) size, 0,
                                (const sockaddr*) address, (socklen_t) addrlen);
}
#endif

#if GIGMA_HAS_AOO
void AOO_CALL GigmaAooEngine::handleAooEvent (void* user, const AooEvent* e, AooThreadLevel)
{
    auto* self = static_cast<GigmaAooEngine*> (user);
    switch (e->type)
    {
        case kAooEventBufferUnderrun: self->stUnderruns.fetch_add (1, std::memory_order_relaxed); break;
        case kAooEventBlockDrop:      self->stDrops.fetch_add ((uint64_t) std::max (1, (int) e->blockDrop.count), std::memory_order_relaxed); break;
        case kAooEventBlockResend:    self->stResends.fetch_add ((uint64_t) std::max (1, (int) e->blockResend.count), std::memory_order_relaxed); break;
        case kAooEventBlockXRun:      self->stXruns.fetch_add ((uint64_t) std::max (1, (int) e->blockXRun.count), std::memory_order_relaxed); break;
        default: break;
    }
}
#endif

namespace {
/** Soft clip above -1 dBFS so hot VST peaks never hard-clip in the codec / PCM16
    browser path (hard clipping is heard as crackle). Unity below the knee. */
inline float softLimit (float x)
{
    constexpr float t = 0.891f;              // -1 dBFS knee
    const float a = std::fabs (x);
    if (a <= t || ! std::isfinite (x))
        return std::isfinite (x) ? x : 0.0f;
    const float y = t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t));
    return x < 0 ? -y : y;
}
} // namespace

void GigmaAooEngine::setQuality (const Quality& nq)
{
    Quality c = nq;
    c.bufferMs = std::clamp (c.bufferMs, 10, 1000);
    c.bitrateKbps = std::clamp (c.bitrateKbps, 32, 320);
    c.frameMs = c.frameMs <= 10 ? 10 : 20;
    c.redundancy = std::clamp (c.redundancy, 1, 3);
    {
        std::lock_guard<std::mutex> lock (qualityMutex);
        q = c;
    }
    limiterOn = c.limiter;
    guestBufMs = c.bufferMs;
    std::lock_guard<std::mutex> lock (peerMutex);
    applyQualityLocked();
}

GigmaAooEngine::Quality GigmaAooEngine::quality() const
{
    std::lock_guard<std::mutex> lock (qualityMutex);
    return q;
}

GigmaAooEngine::Stats GigmaAooEngine::stats() const
{
    Stats s;
    s.underruns = stUnderruns.load();
    s.drops = stDrops.load();
    s.resends = stResends.load();
    s.xruns = stXruns.load();
    s.sendOverflows = stSendOverflows.load();
    return s;
}

void GigmaAooEngine::applyQualityLocked()
{
#if GIGMA_HAS_AOO
    if (! aoo || ! aoo->source || ! aoo->sink)
        return;
    const auto cq = quality();
    const double lat = cq.bufferMs / 1000.0;
    // Jitter buffer: target latency + headroom so a late burst over the relay
    // still lands instead of being dropped (dropouts on sustained synths = crackle).
    aoo->sink->setLatency (lat);
    aoo->sink->setBufferSize (std::max (0.2, lat * 2.0 + 0.1));
    // Resend lost packets only when the buffer leaves time for a round trip.
    aoo->sink->setResendData (lat >= 0.08 ? kAooTrue : kAooFalse);
    aoo->source->setRedundancy (cq.redundancy);

    if (appliedFrameMs != cq.frameMs)
    {
        AooFormatOpus fmt {};
        const int frame = (int) std::lrint (48000.0 * cq.frameMs / 1000.0);   // Opus runs at 48 kHz
        AooFormatOpus_init (&fmt, channels, 48000, frame, OPUS_APPLICATION_AUDIO);
        aoo->source->setFormat (fmt.header);   // recreates the encoder
        appliedFrameMs = cq.frameMs;
    }
    opus_int32 br = (opus_int32) cq.bitrateKbps * 1000;
    aoo->source->codecControl (kAooCodecOpus, OPUS_SET_BITRATE_REQUEST, 0, &br, sizeof (br));
    opus_int32 sig = OPUS_SIGNAL_MUSIC;
    aoo->source->codecControl (kAooCodecOpus, OPUS_SET_SIGNAL_REQUEST, 0, &sig, sizeof (sig));
    opus_int32 cx = 10;
    aoo->source->codecControl (kAooCodecOpus, OPUS_SET_COMPLEXITY_REQUEST, 0, &cx, sizeof (cx));
#endif
}


// ---- Browser guest mics (talk-back) -------------------------------------------
//
// The broker sends each unmuted browser guest's mic as "GGB1" datagrams from the
// relay port (same socket our GGR1 keepalives keep open):
//   0 "GGB1" | 4 talker u16 LE | 6 codec u8 | 7 0 | 8 seq u32 LE | 12 payload
// codec 1 = one Opus packet (48 kHz mono, 20 ms), codec 2 = PCM16 LE 16 kHz mono.
// The network thread decodes (Opus PLC on gaps), resamples to the DAW rate and
// writes a per-talker SPSC ring; the audio thread mixes every primed ring on top
// of the channel output with a small jitter buffer. Never sent onward (no echo).
namespace {
constexpr int kMaxGuestVoices = 8;
constexpr size_t kVoiceRing = 1 << 16;     // mono samples (~1.3 s @ 48k)
constexpr size_t kGmHdr = 12;
constexpr int kOpusCodec = 1, kPcmCodec = 2;

uint32_t getU32 (const unsigned char* p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}
} // namespace

struct GigmaAooEngine::GuestVoice
{
    // network thread
    uint16_t talker = 0;
    int64_t lastMs = 0;
    uint32_t lastSeq = 0;
    bool haveSeq = false;
    double pos = 0.0;      // resampler read position (0 = `prev`)
    float prev = 0.0f;
#if GIGMA_HAS_AOO
    OpusDecoder* dec = nullptr;
#endif
    std::vector<float> tmp = std::vector<float> (5760 * 2);
    std::vector<float> rs = std::vector<float> (5760 * 4);

    // shared (SPSC)
    std::vector<float> ring = std::vector<float> (kVoiceRing, 0.0f);
    std::atomic<size_t> w { 0 }, r { 0 };
    std::atomic<int64_t> liveMs { 0 };
    std::atomic<bool> flush { false };    // network → audio: drop everything buffered

    // audio thread
    bool primed = false;

    ~GuestVoice()
    {
#if GIGMA_HAS_AOO
        if (dec) opus_decoder_destroy (dec);
#endif
    }

    void reset (uint16_t id)
    {
        talker = id;
        haveSeq = false;
        pos = 0.0;
        prev = 0.0f;
#if GIGMA_HAS_AOO
        if (dec) opus_decoder_ctl (dec, OPUS_RESET_STATE);
#endif
        flush.store (true, std::memory_order_release);
    }

    void push (const float* in, int n, double inRate, double outRate)
    {
        if (n <= 0 || outRate <= 0)
            return;
        const double step = inRate / outRate;
        int k = 0;
        const int cap = (int) rs.size();
        while (pos < (double) n && k < cap)
        {
            const int i = (int) pos;
            const float f = (float) (pos - i);
            const float a = i == 0 ? prev : in[i - 1];
            const float b = in[i];
            rs[(size_t) k++] = a + (b - a) * f;
            pos += step;
        }
        pos -= (double) n;
        prev = in[n - 1];
        const size_t wr = w.load (std::memory_order_relaxed);
        const size_t rd = r.load (std::memory_order_acquire);
        if (kVoiceRing - (wr - rd) < (size_t) k)
            return;   // audio thread stalled: drop
        for (int j = 0; j < k; ++j)
            ring[(wr + (size_t) j) % kVoiceRing] = rs[(size_t) j];
        w.store (wr + (size_t) k, std::memory_order_release);
    }
};

int GigmaAooEngine::liveGuestMics() const
{
    const auto t = nowMs();
    int c = 0;
    for (auto& v : voices)
        if (v && t - v->liveMs.load() < 1000)
            ++c;
    return c;
}

bool GigmaAooEngine::handleGuestMicPacket (const unsigned char* data, size_t n, const void* from, size_t fromLen)
{
    if (n < 4 || std::memcmp (data, "GGB1", 4) != 0)
        return false;
    {
        std::lock_guard<std::mutex> lock (prMutex);
        if (! sameAddr (prAddr, from, fromLen))
            return true;   // only trust the broker relay
    }
    if (n <= kGmHdr || voices.empty())
        return true;
    const uint16_t talker = (uint16_t) (data[4] | (data[5] << 8));
    const int codec = data[6];
    const uint32_t seq = getU32 (data + 8);
    const unsigned char* payload = data + kGmHdr;
    const int len = (int) (n - kGmHdr);
    const int64_t t = nowMs();

    GuestVoice* v = nullptr;
    for (auto& x : voices)
        if (x->talker == talker && t - x->lastMs < 10000) { v = x.get(); break; }
    if (v == nullptr)
    {
        GuestVoice* oldest = nullptr;
        for (auto& x : voices)
            if (! oldest || x->lastMs < oldest->lastMs) oldest = x.get();
        v = oldest;
        v->reset (talker);
    }
    v->lastMs = t;

    int lost = 0;
    if (v->haveSeq)
    {
        const int32_t d = (int32_t) (seq - v->lastSeq);
        if (d <= 0)
            return true;               // duplicate / reordered-late
        lost = std::min (d - 1, 5);    // > 100 ms gap: don't invent more than that
    }
    v->haveSeq = true;
    v->lastSeq = seq;
    gmPackets.fetch_add (1, std::memory_order_relaxed);
    v->liveMs.store (t);

    if (codec == kOpusCodec)
    {
#if GIGMA_HAS_AOO
        if (v->dec == nullptr)
        {
            int err = 0;
            v->dec = opus_decoder_create (48000, 1, &err);
            if (err != OPUS_OK) { v->dec = nullptr; return true; }
        }
        float* pcm = v->tmp.data();
        for (int i = 0; i < lost; ++i)
        {
            const int k = opus_decode_float (v->dec, nullptr, 0, pcm, 960, 0);   // PLC
            if (k > 0) v->push (pcm, k, 48000.0, sampleRate);
        }
        const int k = opus_decode_float (v->dec, payload, len, pcm, 5760, 0);
        if (k > 0) v->push (pcm, k, 48000.0, sampleRate);
#endif
    }
    else if (codec == kPcmCodec)
    {
        const int k = std::min (len / 2, (int) v->tmp.size());
        float* pcm = v->tmp.data();
        for (int i = 0; i < k; ++i)
            pcm[i] = (float) (int16_t) (payload[2 * i] | (payload[2 * i + 1] << 8)) / 32768.0f;
        static const float zeros[1024] = {};
        for (int i = 0; i < lost && k > 0; ++i)   // no PLC for PCM: fill the gap with silence
            v->push (zeros, std::min (k, 1024), 16000.0, sampleRate);
        v->push (pcm, k, 16000.0, sampleRate);
    }
    return true;
}

void GigmaAooEngine::mixGuestMics (float* const* outs, int ch, int n)
{
    if (voices.empty() || n <= 0)
        return;
    const float gain = guestGain.load (std::memory_order_relaxed);
    const int bufMs = std::clamp (guestBufMs.load (std::memory_order_relaxed), 60, 400);
    const size_t target = (size_t) (sampleRate * bufMs / 1000.0);
    const size_t maxBuffered = target + (size_t) (sampleRate * 0.2);
    for (auto& vp : voices)
    {
        auto& v = *vp;
        if (v.flush.exchange (false, std::memory_order_acq_rel))
        {
            v.r.store (v.w.load (std::memory_order_acquire), std::memory_order_release);
            v.primed = false;
        }
        const size_t wr = v.w.load (std::memory_order_acquire);
        size_t rd = v.r.load (std::memory_order_relaxed);
        size_t avail = wr - rd;
        if (! v.primed)
        {
            if (avail < target)
                continue;
            v.primed = true;
        }
        if (avail > maxBuffered)   // clock drift / burst after a stall: catch up
        {
            rd = wr - target;
            avail = target;
        }
        const int k = (int) std::min<size_t> (avail, (size_t) n);
        if (gain > 0.0f)
            for (int i = 0; i < k; ++i)
            {
                const float s = v.ring[(rd + (size_t) i) % kVoiceRing] * gain;
                for (int c = 0; c < ch; ++c)
                    if (outs[c]) outs[c][i] += s;
            }
        v.r.store (rd + (size_t) k, std::memory_order_release);
        if (k < n)
            v.primed = false;   // underrun: re-buffer before playing again
    }
}

GigmaAooEngine::GigmaAooEngine()
{
    for (int i = 0; i < kMaxGuestVoices; ++i)
        voices.push_back (std::make_unique<GuestVoice>());
}

GigmaAooEngine::~GigmaAooEngine()
{
    release();
}

std::string GigmaAooEngine::detectLanIp()
{
    int s = (int) ::socket (AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return "127.0.0.1";
    sockaddr_in dst {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons (53);
    inet_pton (AF_INET, "8.8.8.8", &dst.sin_addr);
    std::string ip = "127.0.0.1";
    // connect() on UDP sends nothing; it just picks the outbound interface.
    if (::connect (s, (sockaddr*) &dst, sizeof (dst)) == 0)
    {
        sockaddr_in local {};
        socklen_t len = sizeof (local);
        if (getsockname (s, (sockaddr*) &local, &len) == 0)
        {
            char buf[INET_ADDRSTRLEN] {};
            if (inet_ntop (AF_INET, &local.sin_addr, buf, sizeof (buf)) && std::strcmp (buf, "0.0.0.0") != 0)
                ip = buf;
        }
    }
    closesocket (s);
    return ip;
}

bool GigmaAooEngine::prepare (double sr, int maxBlockSize, int numChannels)
{
    release();
    sampleRate = sr > 0 ? sr : 48000.0;
    maxBlock = maxBlockSize > 0 ? maxBlockSize : 512;
    channels = numChannels > 0 ? numChannels : 2;

    channelScratchIn.assign ((size_t) channels, std::vector<float> ((size_t) maxBlock, 0.0f));
    channelScratchOut.assign ((size_t) channels, std::vector<float> ((size_t) maxBlock, 0.0f));
    channelScratchSend.assign ((size_t) channels, std::vector<float> ((size_t) maxBlock, 0.0f));
    planarSend.resize ((size_t) channels);
    planarIn.resize ((size_t) channels);
    planarOut.resize ((size_t) channels);
    ring.assign (kRingSamples, 0);
    ringWrite = 0;
    ringRead = 0;

#if GIGMA_HAS_AOO
    std::lock_guard<std::mutex> lock (peerMutex);
    ensureAooInitialised();
    aoo = std::make_unique<AooState>();
    aoo->source = AooSource::create (kAooId);
    aoo->sink = AooSink::create (kAooId);
    if (! aoo->source || ! aoo->sink)
    {
        aoo.reset();
        ready = true;   // still pass dry audio through
        return false;
    }
    aoo->source->setEventHandler (&GigmaAooEngine::handleAooEvent, this, kAooEventModePoll);
    aoo->sink->setEventHandler (&GigmaAooEngine::handleAooEvent, this, kAooEventModePoll);
    aoo->source->setup (channels, sampleRate, maxBlock, 0);
    aoo->sink->setup (channels, sampleRate, maxBlock, 0);
    // Send ring (audio thread → network thread). AOO's 25 ms default overflows when
    // Logic renders in bursts (process-buffer range / live-mode switches), and each
    // overflow becomes an empty block on the far end. Bigger ring ≠ more latency:
    // the network thread drains it immediately.
    aoo->source->setBufferSize (0.25);
    aoo->source->setPacketSize (1000);   // + 52 B relay header stays far below MTU
    stUnderruns = 0; stDrops = 0; stResends = 0; stXruns = 0; stSendOverflows = 0;
    appliedFrameMs = 0;
    applyQualityLocked();                // format (Opus frame), bitrate, buffer
    aoo->source->startStream (0, nullptr);
#endif
    ready = true;
    return true;
}

void GigmaAooEngine::release()
{
    ready = false;
    stopNetwork();
    {
        std::lock_guard<std::mutex> lock (peerMutex);
#if GIGMA_HAS_AOO
        if (aoo)
        {
            aoo->source.reset();
            aoo->sink.reset();
            aoo.reset();
        }
#endif
        // Sinks are gone with the source; next syncPeers() re-adds them.
        peers.clear();
    }
    channelScratchIn.clear();
    channelScratchOut.clear();
    channelScratchSend.clear();
}

std::string GigmaAooEngine::startNetwork (int preferredPort)
{
    stopNetwork();

#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup (MAKEWORD (2, 2), &wsa);
#endif

    udpSock = (int) ::socket (AF_INET, SOCK_DGRAM, 0);
    if (udpSock < 0)
        throw std::runtime_error ("udp socket failed");

#if !defined(_WIN32)
    int yes = 1;
    setsockopt (udpSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
    int flags = fcntl (udpSock, F_GETFL, 0);
    fcntl (udpSock, F_SETFL, flags | O_NONBLOCK);
#endif

    auto tryBind = [this] (int port)
    {
        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl (INADDR_ANY);
        addr.sin_port = htons ((uint16_t) port);
        return ::bind (udpSock, (sockaddr*) &addr, sizeof (addr)) == 0;
    };
    // Keep the same port across DAW re-prepares so peers' addSink stays valid.
    if (! (preferredPort > 0 && tryBind (preferredPort)) && ! tryBind (0))
    {
        closesocket (udpSock);
        udpSock = -1;
        throw std::runtime_error ("udp bind failed");
    }

    sockaddr_in bound {};
    socklen_t len = sizeof (bound);
    getsockname (udpSock, (sockaddr*) &bound, &len);
    {
        std::lock_guard<std::mutex> lock (endpointMutex);
        boundHost = detectLanIp();
    }
    boundPort = ntohs (bound.sin_port);

    networkRunning = true;
    netThread = std::thread ([this] { networkThreadMain(); });
    return endpoint();
}

void GigmaAooEngine::stopNetwork()
{
    networkRunning = false;
    if (netThread.joinable())
        netThread.join();
    if (udpSock >= 0)
    {
        closesocket (udpSock);
        udpSock = -1;
    }
    boundPort = 0;
}

std::string GigmaAooEngine::endpoint() const
{
    std::lock_guard<std::mutex> lock (endpointMutex);
    const auto p = boundPort.load();
    if (p == 0)
        return {};
    return boundHost + ":" + std::to_string (p);
}

void GigmaAooEngine::syncPeers (const std::set<std::string>& wanted)
{
    std::lock_guard<std::mutex> lock (peerMutex);
    const auto self = endpoint();

    for (auto it = peers.begin(); it != peers.end();)
    {
        if (wanted.count (it->first) == 0)
        {
#if GIGMA_HAS_AOO
            if (aoo && aoo->source)
            {
                AooEndpoint ep { it->second.data(), (AooAddrSize) it->second.size(), kAooId };
                aoo->source->removeSink (ep);
            }
#endif
            it = peers.erase (it);
        }
        else
            ++it;
    }

    for (const auto& hp : wanted)
    {
        if (hp == self || peers.count (hp))
            continue;
        auto addr = hp.rfind (kRelayPrefix, 0) == 0 ? relayAddrFor (hp.substr (std::strlen (kRelayPrefix)))
                                                     : resolveHostPort (hp);
        if (addr.empty())
            continue;
#if GIGMA_HAS_AOO
        if (! aoo || ! aoo->source)
            continue;
        AooEndpoint ep { addr.data(), (AooAddrSize) addr.size(), kAooId };
        if (aoo->source->addSink (ep, kAooTrue) != kAooOk)
            continue;
#endif
        peers.emplace (hp, std::move (addr));
    }
}

size_t GigmaAooEngine::numPeers() const
{
    std::lock_guard<std::mutex> lock (peerMutex);
    return peers.size();
}

void GigmaAooEngine::setRelay (const std::string& relayHostPort, const std::string& sessionId,
                               const std::string& hostId)
{
    auto addr = resolveHostPort (relayHostPort);
    std::lock_guard<std::mutex> lock (relayMutex);
    relayAddr = std::move (addr);
    relaySession = sessionId;
    relayHost = hostId;
    relayConfigured = ! relayAddr.empty() && ! sessionId.empty() && ! hostId.empty();
}

void GigmaAooEngine::setPeerRelay (const std::string& relayHostPort, const std::string& sessionId,
                                   const std::string& secret)
{
    auto addr = relayHostPort.empty() ? std::vector<unsigned char>() : resolveHostPort (relayHostPort);
    std::lock_guard<std::mutex> lock (prMutex);
    const bool changed = addr != prAddr || sessionId != prSession || secret != prSecret;
    prAddr = std::move (addr);
    prSession = sessionId;
    prSecret = secret;
    peerRelayOn = ! prAddr.empty() && sessionId.size() == 16 && secret.size() == 16;
    if (changed)
    {
        prLastAckMs = 0;
        prLastKeepaliveMs = 0;   // register right away
    }
}

bool GigmaAooEngine::peerRelayUp() const
{
    const auto t = prLastAckMs.load();
    return peerRelayOn.load() && t > 0 && nowMs() - t < 5000;
}

std::vector<unsigned char> GigmaAooEngine::relayAddrFor (const std::string& peerId)
{
    if (peerId.size() != 16)
        return {};
    std::lock_guard<std::mutex> lock (prMutex);
    uint16_t idx;
    if (auto it = prIdToIdx.find (peerId); it != prIdToIdx.end())
        idx = it->second;
    else
    {
        if (prIdxToId.size() >= 0xffff)
            return {};
        idx = (uint16_t) prIdxToId.size();
        prIdxToId.push_back (peerId);
        prIdToIdx.emplace (peerId, idx);
    }
    // 240.0.0.0/4 is reserved (never routable), so it can't collide with a real peer.
    sockaddr_in sa {};
    sa.sin_family = AF_INET;
    sa.sin_port = htons (1);
    sa.sin_addr.s_addr = htonl ((240u << 24) | (uint32_t) idx);
#if defined(__APPLE__)
    sa.sin_len = sizeof (sa);
#endif
    return std::vector<unsigned char> ((const unsigned char*) &sa, (const unsigned char*) &sa + sizeof (sa));
}

std::string GigmaAooEngine::relayIdFor (const void* addr, size_t len)
{
    if (addr == nullptr || len < sizeof (sockaddr_in))
        return {};
    auto* sa = (const sockaddr_in*) addr;
    if (sa->sin_family != AF_INET)
        return {};
    const uint32_t ip = ntohl (sa->sin_addr.s_addr);
    if ((ip >> 24) != 240u || ntohs (sa->sin_port) != 1)
        return {};
    const auto idx = (size_t) (ip & 0xffffu);
    std::lock_guard<std::mutex> lock (prMutex);
    return idx > 0 && idx < prIdxToId.size() ? prIdxToId[idx] : std::string();
}

bool GigmaAooEngine::sendViaPeerRelay (const unsigned char* data, size_t size, const void* addr, size_t len)
{
    const auto id = relayIdFor (addr, len);
    if (id.empty())
        return false;   // real address: caller sends directly
    std::lock_guard<std::mutex> lock (prMutex);
    if (! peerRelayOn.load() || udpSock < 0)
        return true;    // placeholder but no relay: drop (never sendto 240.x)
    prScratch.resize (kPeerHdrOut + size);
    std::memcpy (prScratch.data(), "GGR1", 4);
    putFixed16 (prScratch.data() + 4, prSession);
    putFixed16 (prScratch.data() + 20, prSecret);
    putFixed16 (prScratch.data() + 36, id);
    std::memcpy (prScratch.data() + kPeerHdrOut, data, size);
    ::sendto (udpSock, prScratch.data(), prScratch.size(), 0,
              (const sockaddr*) prAddr.data(), (socklen_t) prAddr.size());
    return true;
}

void GigmaAooEngine::sendPeerKeepalive()
{
    if (! peerRelayOn.load())
        return;
    const auto t = nowMs();
    std::lock_guard<std::mutex> lock (prMutex);
    // 1 s cadence: registers our public address with the relay and keeps the
    // NAT mapping open even when no audio is flowing.
    if (prAddr.empty() || t - prLastKeepaliveMs < 1000)
        return;
    prLastKeepaliveMs = t;
    unsigned char pkt[kPeerHdrOut];
    std::memcpy (pkt, "GGR1", 4);
    putFixed16 (pkt + 4, prSession);
    putFixed16 (pkt + 20, prSecret);
    std::memcpy (pkt + 36, kZeroId.data(), 16);
    ::sendto (udpSock, pkt, sizeof (pkt), 0, (const sockaddr*) prAddr.data(), (socklen_t) prAddr.size());
}

bool GigmaAooEngine::handlePeerRelayPacket (const unsigned char* data, size_t n, const void* from, size_t fromLen)
{
    if (n < kPeerHdrIn || std::memcmp (data, "GGR1", 4) != 0)
        return false;
    {
        std::lock_guard<std::mutex> lock (prMutex);
        if (! sameAddr (prAddr, from, fromLen))
            return false;
    }
    const std::string src ((const char*) data + 4, 16);
    if (src == kZeroId)
    {
        prLastAckMs = nowMs();
        return true;
    }
    prLastAckMs = nowMs();   // relay is clearly up if it forwards to us
#if GIGMA_HAS_AOO
    auto fake = relayAddrFor (src);
    if (fake.empty() || n == kPeerHdrIn)
        return true;
    const auto* payload = (const AooByte*) data + kPeerHdrIn;
    const auto len = (AooInt32) (n - kPeerHdrIn);
    std::lock_guard<std::mutex> lock (peerMutex);
    if (aoo)
    {
        AooMsgType type; AooId id; AooInt32 off;
        if (aoo_parsePattern (payload, len, &type, &id, &off) == kAooOk)
        {
            if (type == kAooMsgTypeSource && aoo->source)
                aoo->source->handleMessage (payload, len, fake.data(), (AooAddrSize) fake.size());
            else if (type == kAooMsgTypeSink && aoo->sink)
                aoo->sink->handleMessage (payload, len, fake.data(), (AooAddrSize) fake.size());
        }
    }
#endif
    return true;
}

void GigmaAooEngine::process (const float* const* inputs, float* const* outputs,
                              int numChannels, int numSamples)
{
    // Invariant: Gigma is an insert FX, so the channel's own audio must ALWAYS
    // reach the output. Remote peers are only ever *added* on top. Anything we
    // can't handle (not prepared, extra channels) is passed through untouched.
    auto passThrough = [&] (int c, int from, int to)
    {
        if (! (outputs && outputs[c]) || to <= from)
            return;
        if (inputs && inputs[c])
        {
            if (inputs[c] != outputs[c])
                std::memmove (outputs[c] + from, inputs[c] + from, (size_t) (to - from) * sizeof (float));
        }
        else
            std::memset (outputs[c] + from, 0, (size_t) (to - from) * sizeof (float));
    };

    const bool isReady = ready.load (std::memory_order_acquire) && ! channelScratchIn.empty();
    const int ch = isReady ? std::min (numChannels, channels) : 0;
    if (ch <= 0 || numSamples <= 0)
    {
        for (int c = 0; c < numChannels; ++c)
            passThrough (c, 0, numSamples);
        return;
    }

    // Hosts may hand us a block larger than prepareToPlay promised (Logic does on
    // some live/prerender switches). Process it in maxBlock chunks instead of
    // dropping the tail from the stream (that gap was audible on the far end).
    for (int off = 0; off < numSamples; off += maxBlock)
        sendChunk (0, ch, off, std::min (maxBlock, numSamples - off), inputs, outputs);

    for (int c = ch; c < numChannels; ++c)
        passThrough (c, 0, numSamples);
}

void GigmaAooEngine::sendChunk (int, int ch, int off, int n, const float* const* inputs, float* const* outputs)
{
    const bool limit = limiterOn.load (std::memory_order_relaxed);
    for (int c = 0; c < ch; ++c)
    {
        auto& in = channelScratchIn[(size_t) c];
        auto& snd = channelScratchSend[(size_t) c];
        if (inputs && inputs[c])
            std::memcpy (in.data(), inputs[c] + off, (size_t) n * sizeof (float));
        else
            std::memset (in.data(), 0, (size_t) n * sizeof (float));
        for (int i = 0; i < n; ++i)
            snd[(size_t) i] = limit ? softLimit (in[(size_t) i]) : (std::isfinite (in[(size_t) i]) ? in[(size_t) i] : 0.0f);
        planarIn[(size_t) c] = in.data();
        planarSend[(size_t) c] = snd.data();
        planarOut[(size_t) c] = channelScratchOut[(size_t) c].data();
        std::memset (channelScratchOut[(size_t) c].data(), 0, (size_t) n * sizeof (float));
    }

    // Browser relay: interleave → int16 into the SPSC ring (drop if full).
    if (relayActive())
    {
        const size_t need = (size_t) n * (size_t) ch;
        const size_t w = ringWrite.load (std::memory_order_relaxed);
        const size_t r = ringRead.load (std::memory_order_acquire);
        if (kRingSamples - (w - r) >= need)
        {
            size_t pos = w;
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < ch; ++c)
                {
                    float v = std::clamp (channelScratchSend[(size_t) c][(size_t) i], -1.0f, 1.0f);
                    ring[pos++ % kRingSamples] = (int16_t) std::lrint (v * 32767.0f);
                }
            ringWrite.store (pos, std::memory_order_release);
        }
    }

#if GIGMA_HAS_AOO
    if (aoo && aoo->source)
    {
        if (aoo->source->process (const_cast<AooSample**> (planarSend.data()), n, aoo_getCurrentNtpTime()) == kAooErrorOverflow)
            stSendOverflows.fetch_add (1, std::memory_order_relaxed);
    }
    if (aoo && aoo->sink)
        aoo->sink->process (planarOut.data(), n, aoo_getCurrentNtpTime(), nullptr, nullptr);
#endif
    mixGuestMics (planarOut.data(), ch, n);

    // Output = dry input (the producer keeps hearing their track) + remote peers.
    for (int c = 0; c < ch; ++c)
    {
        if (! (outputs && outputs[c]))
            continue;
        const float* dry = channelScratchIn[(size_t) c].data();
        const float* wet = channelScratchOut[(size_t) c].data();
        float* o = outputs[c] + off;
        for (int i = 0; i < n; ++i)
        {
            const float w = wet[i];
            o[i] = dry[i] + (std::isfinite (w) ? w : 0.0f);
        }
    }
}

void GigmaAooEngine::drainRelay()
{
    if (! relayActive())
    {
        ringRead.store (ringWrite.load (std::memory_order_acquire), std::memory_order_release);
        return;
    }
    std::lock_guard<std::mutex> lock (relayMutex);
    if (relayAddr.empty())
        return;

    const size_t samplesPerPkt = (size_t) kRelayFramesPerPacket * (size_t) channels;
    unsigned char pkt[48 + kRelayFramesPerPacket * 8 * 2];
    while (true)
    {
        const size_t r = ringRead.load (std::memory_order_relaxed);
        const size_t w = ringWrite.load (std::memory_order_acquire);
        if (w - r < samplesPerPkt)
            break;
        std::memcpy (pkt, "GGM1", 4);
        putFixed16 (pkt + 4, relaySession);
        putFixed16 (pkt + 20, relayHost);
        putU32 (pkt + 36, relaySeq++);
        putU32 (pkt + 40, (uint32_t) std::lrint (sampleRate));
        pkt[44] = (unsigned char) channels;
        pkt[45] = pkt[46] = pkt[47] = 0;
        auto* out = pkt + 48;
        for (size_t i = 0; i < samplesPerPkt; ++i)
        {
            const auto v = (uint16_t) ring[(r + i) % kRingSamples];
            out[2 * i] = (unsigned char) (v & 0xff);
            out[2 * i + 1] = (unsigned char) (v >> 8);
        }
        ringRead.store (r + samplesPerPkt, std::memory_order_release);
        ::sendto (udpSock, pkt, 48 + samplesPerPkt * 2, 0,
                  (const sockaddr*) relayAddr.data(), (socklen_t) relayAddr.size());
    }
}

void GigmaAooEngine::networkThreadMain()
{
    std::vector<char> buf (65536);
    while (networkRunning.load())
    {
#if GIGMA_HAS_AOO
        {
            std::lock_guard<std::mutex> lock (peerMutex);
            if (aoo && aoo->source)
                aoo->source->send (&GigmaAooEngine::udpSend, this);
            if (aoo && aoo->sink)
                aoo->sink->send (&GigmaAooEngine::udpSend, this);
            if (aoo && aoo->sink)
                aoo->sink->pollEvents();
            if (aoo && aoo->source)
                aoo->source->pollEvents();
        }
#endif
        drainRelay();
        sendPeerKeepalive();

        bool got = false;
        for (int k = 0; k < 64; ++k)
        {
            sockaddr_in from {};
            socklen_t fromLen = sizeof (from);
            const auto n = ::recvfrom (udpSock, buf.data(), (int) buf.size(), 0,
                                       (sockaddr*) &from, &fromLen);
            if (n <= 0)
                break;
            got = true;
            if (handleGuestMicPacket ((const unsigned char*) buf.data(), (size_t) n, &from, (size_t) fromLen))
                continue;
            if (handlePeerRelayPacket ((const unsigned char*) buf.data(), (size_t) n, &from, (size_t) fromLen))
                continue;
#if GIGMA_HAS_AOO
            std::lock_guard<std::mutex> lock (peerMutex);
            if (aoo)
            {
                AooMsgType type; AooId id; AooInt32 off;
                const auto* data = (const AooByte*) buf.data();
                if (aoo_parsePattern (data, (AooInt32) n, &type, &id, &off) == kAooOk)
                {
                    if (type == kAooMsgTypeSource && aoo->source)
                        aoo->source->handleMessage (data, (AooInt32) n, &from, (AooAddrSize) fromLen);
                    else if (type == kAooMsgTypeSink && aoo->sink)
                        aoo->sink->handleMessage (data, (AooInt32) n, &from, (AooAddrSize) fromLen);
                }
            }
#endif
        }
        if (! got)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}
