// One side of a cross-network relay test: run one instance as host and one as
// guest on two different networks (see scripts/test-aoo-relay-xnet.md).
// Each plays a sine at <freq> and reports how much remote audio it received.
//
//   aoo_relay_peer <relay ip:port> <session> <mySecret> <remotePeerId> <freqHz> <seconds>
#include "../Source/GigmaAooEngine.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

int main (int argc, char** argv)
{
    if (argc != 7) { std::puts ("usage: aoo_relay_peer relay session mySecret remotePeer freq seconds"); return 2; }
    const double sr = 48000, freq = std::atof (argv[5]); const int bs = 256, ch = 2, secs = std::atoi (argv[6]);
    GigmaAooEngine e;
    e.prepare (sr, bs, ch);
    e.startNetwork (0);
    e.setPeerRelay (argv[1], argv[2], argv[3]);
    e.syncPeers ({ std::string ("relay:") + argv[4] });

    std::vector<std::vector<float>> in (ch, std::vector<float> (bs)), out = in;
    std::vector<const float*> pin; std::vector<float*> pout;
    for (int c = 0; c < ch; ++c) { pin.push_back (in[c].data()); pout.push_back (out[c].data()); }
    double ph = 0, wet = 0; long nonzero = 0, total = 0;
    const int blocks = (int) (sr * secs / bs);
    auto next = std::chrono::steady_clock::now();
    for (int k = 0; k < blocks; ++k)
    {
        for (int i = 0; i < bs; ++i)
        {
            const float v = 0.3f * (float) std::sin (ph); ph += 2 * M_PI * freq / sr;
            for (int c = 0; c < ch; ++c) in[c][i] = v;
        }
        e.process (pin.data(), pout.data(), ch, bs);
        if (k > blocks / 3)
            for (int i = 0; i < bs; ++i)
            {
                const double w = std::fabs (out[0][i] - in[0][i]);
                wet = std::max (wet, w); nonzero += w > 1e-4; ++total;
            }
        if (k % (int) (sr / bs) == 0)
            std::printf ("t=%ds relayUp=%d wetPeak=%.3f\n", (int) (k * bs / sr), (int) e.peerRelayUp(), wet), std::fflush (stdout);
        next += std::chrono::microseconds ((int) (1e6 * bs / sr));
        std::this_thread::sleep_until (next);
    }
    std::printf ("FINAL relayUp=%d wetPeak=%.3f wetCoverage=%.1f%%\n", (int) e.peerRelayUp(), wet,
                 total ? 100.0 * nonzero / total : 0.0);
    e.release();
    const bool ok = wet > 0.1;
    std::puts (ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
