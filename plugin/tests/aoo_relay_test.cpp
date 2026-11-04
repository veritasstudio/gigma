// Two GigmaAooEngines talk ONLY through the Gigma UDP relay ("relay:<peerId>"),
// both directions: host A plays 440 Hz, guest B plays 660 Hz; each must hear
// the other. Run via scripts/test-aoo-relay.sh (local broker or the live VPS).
//
//   aoo_relay_test <relay ip:port> <session> <hostSecret> <hostPeer> <guestSecret> <guestPeer>
#include "../Source/GigmaAooEngine.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

int main (int argc, char** argv)
{
    if (argc != 7) { std::puts ("usage: aoo_relay_test relay session hostSecret hostPeer guestSecret guestPeer"); return 2; }
    const std::string relay = argv[1], sid = argv[2], hs = argv[3], hp = argv[4], gs = argv[5], gp = argv[6];
    const double sr = 48000; const int bs = 256, ch = 2;
    GigmaAooEngine a, b;
    a.prepare (sr, bs, ch); b.prepare (sr, bs, ch);
    a.startNetwork (0); b.startNetwork (0);
    a.setPeerRelay (relay, sid, hs);
    b.setPeerRelay (relay, sid, gs);
    a.syncPeers ({ "relay:" + gp });
    b.syncPeers ({ "relay:" + hp });

    std::vector<std::vector<float>> inA (ch, std::vector<float> (bs)), outA = inA, inB = inA, outB = inA;
    std::vector<const float*> pinA, pinB; std::vector<float*> poutA, poutB;
    for (int c = 0; c < ch; ++c) { pinA.push_back (inA[c].data()); poutA.push_back (outA[c].data());
                                   pinB.push_back (inB[c].data()); poutB.push_back (outB[c].data()); }
    double phA = 0, phB = 0, wetAtB = 0, wetAtA = 0;
    const int blocks = (int) (sr * 5 / bs);
    for (int k = 0; k < blocks; ++k)
    {
        for (int i = 0; i < bs; ++i)
        {
            float va = 0.5f * (float) std::sin (phA), vb = 0.3f * (float) std::sin (phB);
            phA += 2 * M_PI * 440 / sr; phB += 2 * M_PI * 660 / sr;
            for (int c = 0; c < ch; ++c) { inA[c][i] = va; inB[c][i] = vb; }
        }
        a.process (pinA.data(), poutA.data(), ch, bs);
        b.process (pinB.data(), poutB.data(), ch, bs);
        if (k > blocks / 2)
            for (int i = 0; i < bs; ++i)
            {   // output = dry + wet; subtract dry to see what came over the network
                wetAtB = std::max (wetAtB, (double) std::fabs (outB[0][i] - inB[0][i]));
                wetAtA = std::max (wetAtA, (double) std::fabs (outA[0][i] - inA[0][i]));
            }
        std::this_thread::sleep_for (std::chrono::microseconds ((int) (1e6 * bs / sr)));
    }
    std::printf ("relayUp A=%d B=%d  wet A->B peak=%.3f  wet B->A peak=%.3f\n",
                 (int) a.peerRelayUp(), (int) b.peerRelayUp(), wetAtB, wetAtA);
    a.release(); b.release();
    const bool ok = wetAtB > 0.2 && wetAtA > 0.1;
    std::puts (ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
