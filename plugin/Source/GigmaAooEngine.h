#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/**
 * DAW-side AOO (Audio over OSC) endpoint, following the SonoBus pattern:
 * capture from processBlock → aoo source → UDP; UDP → aoo sink → processBlock out.
 *
 * Compile with -DGIGMA_HAS_AOO=1 and link libaoo + Opus for the real path.
 * Without that flag, prepare/process still run a local ring so the plugin
 * exercises I/O wiring on any build host.
 */
class GigmaAooEngine
{
public:
    GigmaAooEngine();
    ~GigmaAooEngine();

    bool prepare (double sampleRate, int maxBlockSize, int numChannels);
    void release();

    /** Bind UDP + start network thread. Returns "host:port" for the broker. */
    std::string startNetwork (int preferredPort = 0);

    void stopNetwork();

    /** Push non-interleaved (or planar) input from the DAW; pull remote mix into output. */
    void process (const float* const* inputs, float* const* outputs,
                  int numChannels, int numSamples);

    bool isNetworkRunning() const { return networkRunning.load(); }
    std::string endpoint() const;

private:
    void networkThreadMain();

    double sampleRate = 48000.0;
    int maxBlock = 512;
    int channels = 2;

    std::atomic<bool> networkRunning { false };
    std::thread netThread;
    int udpSock = -1;
    uint16_t boundPort = 0;
    std::string boundHost;

    // Scratch / deinterleave
    std::vector<std::vector<float>> channelScratchIn;
    std::vector<std::vector<float>> channelScratchOut;
    std::vector<const float*> planarIn;
    std::vector<float*> planarOut;

    std::mutex endpointMutex;

#if GIGMA_HAS_AOO
    struct AooState;
    std::unique_ptr<AooState> aoo;
#endif
};
