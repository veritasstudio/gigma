#include "GigmaAooEngine.h"

#include <cstring>
#include <chrono>
#include <algorithm>
#include <stdexcept>

#if defined(_WIN32)
 #include <winsock2.h>
 #include <ws2tcpip.h>
 using socklen_t = int;
#else
 #include <arpa/inet.h>
 #include <fcntl.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <unistd.h>
#endif

#if GIGMA_HAS_AOO
 #include "aoo/aoo.hpp"
 #include "aoo/aoo_pcm.h"
 #include "aoo/aoo_opus.h"
#endif

namespace {
#if !defined(_WIN32)
int closesocket (int s) { return ::close (s); }
#endif
}

GigmaAooEngine::GigmaAooEngine() = default;

GigmaAooEngine::~GigmaAooEngine()
{
    release();
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

#if GIGMA_HAS_AOO
    aoo_initialize();
    aoo = std::make_unique<AooState>();
    aoo->source.reset (aoo::isource::create (1));
    aoo->sink.reset (aoo::isink::create (1));
    if (! aoo->source || ! aoo->sink)
        return false;
    aoo->source->setup ((int32_t) sampleRate, maxBlock, channels);
    aoo->sink->setup ((int32_t) sampleRate, maxBlock, channels);

    aoo_format_pcm fmt {};
    AOO_FORMAT_PCM_INIT (fmt, channels, (int32_t) sampleRate, maxBlock, AOO_PCM_FLOAT32);
    aoo->source->set_format (fmt.header);
#endif
    return true;
}

void GigmaAooEngine::release()
{
    stopNetwork();
#if GIGMA_HAS_AOO
    if (aoo)
    {
        aoo->source.reset();
        aoo->sink.reset();
        aoo.reset();
        aoo_terminate();
    }
#endif
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

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_ANY);
    addr.sin_port = htons ((uint16_t) (preferredPort > 0 ? preferredPort : 0));
    if (::bind (udpSock, (sockaddr*) &addr, sizeof (addr)) != 0)
    {
        closesocket (udpSock);
        udpSock = -1;
        throw std::runtime_error ("udp bind failed");
    }

    sockaddr_in bound {};
    socklen_t len = sizeof (bound);
    getsockname (udpSock, (sockaddr*) &bound, &len);
    boundPort = ntohs (bound.sin_port);
    boundHost = "0.0.0.0";

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
    std::lock_guard<std::mutex> lock (const_cast<std::mutex&> (endpointMutex));
    if (boundPort == 0)
        return {};
    return boundHost + ":" + std::to_string (boundPort);
}

void GigmaAooEngine::process (const float* const* inputs, float* const* outputs,
                              int numChannels, int numSamples)
{
    const int ch = std::min (numChannels, channels);
    const int n = std::min (numSamples, maxBlock);

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

#if GIGMA_HAS_AOO
    if (aoo && aoo->source)
    {
        const auto t = aoo_osctime_get();
        aoo->source->process (planarIn.data(), n, t);
        aoo->source->send();
    }
    if (aoo && aoo->sink)
    {
        const auto t = aoo_osctime_get();
        aoo->sink->process (planarOut.data(), n, t);
    }
#else
    // Local loopback so DAW metering still shows signal without AOO linked.
    for (int c = 0; c < ch; ++c)
        std::memcpy (channelScratchOut[(size_t) c].data(),
                     channelScratchIn[(size_t) c].data(),
                     (size_t) n * sizeof (float));
#endif

    for (int c = 0; c < ch; ++c)
    {
        if (outputs && outputs[c])
            std::memcpy (outputs[c], channelScratchOut[(size_t) c].data(), (size_t) n * sizeof (float));
    }
    for (int c = ch; c < numChannels; ++c)
        if (outputs && outputs[c])
            std::memset (outputs[c], 0, (size_t) numSamples * sizeof (float));
}

void GigmaAooEngine::networkThreadMain()
{
    std::vector<char> buf (2048);
    while (networkRunning.load())
    {
#if GIGMA_HAS_AOO
        if (aoo && aoo->source)
            aoo->source->send();
#endif
        sockaddr_in from {};
        socklen_t fromLen = sizeof (from);
        const auto n = ::recvfrom (udpSock, buf.data(), (int) buf.size(), 0,
                                   (sockaddr*) &from, &fromLen);
        if (n > 0)
        {
#if GIGMA_HAS_AOO
            if (aoo && aoo->sink)
            {
                // Reply fn / endpoint wiring filled in when peer discovery lands.
                aoo->sink->handle_message (buf.data(), (int32_t) n, &from, nullptr);
            }
#endif
        }
        else
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }
}

#if GIGMA_HAS_AOO
struct GigmaAooEngine::AooState
{
    aoo::isource::pointer source;
    aoo::isink::pointer sink;
};
#endif
