#include "GigmaAooEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
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
} // namespace

#if GIGMA_HAS_AOO
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
    return (AooInt32) ::sendto (self->udpSock, data, (size_t) size, 0,
                                (const sockaddr*) address, (socklen_t) addrlen);
}
#endif

GigmaAooEngine::GigmaAooEngine() = default;

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
    planarIn.resize ((size_t) channels);
    planarOut.resize ((size_t) channels);
    ring.assign (kRingSamples, 0);
    ringWrite = 0;
    ringRead = 0;

#if GIGMA_HAS_AOO
    std::lock_guard<std::mutex> lock (peerMutex);
    aoo_initialize (nullptr);
    aoo = std::make_unique<AooState>();
    aoo->source = AooSource::create (kAooId);
    aoo->sink = AooSink::create (kAooId);
    if (! aoo->source || ! aoo->sink)
        return false;
    aoo->source->setup (channels, sampleRate, maxBlock, 0);
    aoo->sink->setup (channels, sampleRate, maxBlock, 0);
    aoo->sink->setLatency (0.05);
    aoo->sink->setBufferSize (0.1);

    AooFormatOpus fmt {};
    AooFormatOpus_init (&fmt, channels, (AooInt32) sampleRate, maxBlock, OPUS_APPLICATION_AUDIO);
    aoo->source->setFormat (fmt.header);
    aoo->source->startStream (0, nullptr);
#endif
    return true;
}

void GigmaAooEngine::release()
{
    stopNetwork();
    {
        std::lock_guard<std::mutex> lock (peerMutex);
#if GIGMA_HAS_AOO
        if (aoo)
        {
            aoo->source.reset();
            aoo->sink.reset();
            aoo.reset();
            aoo_terminate();
        }
#endif
        // Sinks are gone with the source; next syncPeers() re-adds them.
        peers.clear();
    }
    channelScratchIn.clear();
    channelScratchOut.clear();
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
        auto addr = resolveHostPort (hp);
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

void GigmaAooEngine::process (const float* const* inputs, float* const* outputs,
                              int numChannels, int numSamples)
{
    const int ch = std::min (numChannels, channels);
    const int n = std::min (numSamples, maxBlock);
    if (ch <= 0 || n <= 0 || channelScratchIn.empty())
        return;

    for (int c = 0; c < ch; ++c)
    {
        if (inputs && inputs[c])
            std::memcpy (channelScratchIn[(size_t) c].data(), inputs[c], (size_t) n * sizeof (float));
        else
            std::memset (channelScratchIn[(size_t) c].data(), 0, (size_t) n * sizeof (float));
        planarIn[(size_t) c] = channelScratchIn[(size_t) c].data();
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
                    float v = std::clamp (channelScratchIn[(size_t) c][(size_t) i], -1.0f, 1.0f);
                    ring[pos++ % kRingSamples] = (int16_t) std::lrint (v * 32767.0f);
                }
            ringWrite.store (pos, std::memory_order_release);
        }
    }

#if GIGMA_HAS_AOO
    if (aoo && aoo->source)
        aoo->source->process (const_cast<AooSample**> (planarIn.data()), n, aoo_getCurrentNtpTime());
    if (aoo && aoo->sink)
        aoo->sink->process (planarOut.data(), n, aoo_getCurrentNtpTime(), nullptr, nullptr);
#endif

    // Output = dry input (the producer keeps hearing their track) + remote peers.
    for (int c = 0; c < ch; ++c)
    {
        if (! (outputs && outputs[c]))
            continue;
        const float* dry = channelScratchIn[(size_t) c].data();
        const float* wet = channelScratchOut[(size_t) c].data();
        for (int i = 0; i < n; ++i)
            outputs[c][i] = dry[i] + wet[i];
    }
    for (int c = ch; c < numChannels; ++c)
        if (outputs && outputs[c])
            std::memset (outputs[c], 0, (size_t) numSamples * sizeof (float));
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
        }
#endif
        drainRelay();

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
