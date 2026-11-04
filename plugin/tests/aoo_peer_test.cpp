// Two GigmaAooEngines on this machine: A addSink()s B, A plays a sine,
// B (silent input) must output A's audio. Also checks the relay ring emits
// PCM16 packets to a UDP port. Build: scripts/test-aoo-peer.sh
#include "../Source/GigmaAooEngine.h"
#include <arpa/inet.h>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

int main()
{
    const double sr = 48000; const int bs = 256, ch = 2;
    GigmaAooEngine a, b;
    a.prepare (sr, bs, ch); b.prepare (sr, bs, ch);
    auto epA = a.startNetwork (0), epB = b.startNetwork (0);
    std::printf ("A=%s B=%s\n", epA.c_str(), epB.c_str());
    if (epA.rfind ("0.0.0.0", 0) == 0) { std::puts ("FAIL: advertised 0.0.0.0"); return 1; }
    a.syncPeers ({ epB });

    // relay sink
    int rs = socket (AF_INET, SOCK_DGRAM, 0);
    sockaddr_in ra {}; ra.sin_family = AF_INET; ra.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    bind (rs, (sockaddr*) &ra, sizeof ra); socklen_t rl = sizeof ra; getsockname (rs, (sockaddr*) &ra, &rl);
    fcntl (rs, F_SETFL, O_NONBLOCK);
    a.setRelay ("127.0.0.1:" + std::to_string (ntohs (ra.sin_port)), "0123456789abcdef", "fedcba9876543210");
    a.setRelayEnabled (true);

    std::vector<std::vector<float>> inA (ch, std::vector<float> (bs)), outA = inA, inB = inA, outB = inA;
    std::vector<const float*> pinA, pinB; std::vector<float*> poutA, poutB;
    for (int c = 0; c < ch; ++c) { pinA.push_back (inA[c].data()); poutA.push_back (outA[c].data());
                                   pinB.push_back (inB[c].data()); poutB.push_back (outB[c].data()); }
    double phase = 0, peakB = 0; int relayPkts = 0;
    const int blocks = (int) (sr * 3 / bs);
    for (int k = 0; k < blocks; ++k)
    {
        for (int i = 0; i < bs; ++i) { float v = 0.5f * (float) std::sin (phase); phase += 2 * M_PI * 440 / sr;
                                       for (int c = 0; c < ch; ++c) inA[c][i] = v; }
        a.process (pinA.data(), poutA.data(), ch, bs);
        b.process (pinB.data(), poutB.data(), ch, bs);
        if (k > blocks / 2) for (int i = 0; i < bs; ++i) peakB = std::max (peakB, (double) std::fabs (outB[0][i]));
        char buf[4096]; while (recv (rs, buf, sizeof buf, 0) > 0) ++relayPkts;
        std::this_thread::sleep_for (std::chrono::microseconds ((int) (1e6 * bs / sr)));
    }
    std::printf ("peers(A)=%zu peak(B out)=%.3f relayPackets=%d\n", a.numPeers(), peakB, relayPkts);
    a.release(); b.release();
    bool ok = peakB > 0.1 && relayPkts > 100;
    std::puts (ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
