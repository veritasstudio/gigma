// Browser-mic talk-back into the plugin, no DAW.
//
//   guest_mic_test selftest
//       A fake "relay" UDP socket sends GGB1 packets (Opus 48k and PCM16 16k)
//       straight to the engine; checks the voice is mixed on top of the dry
//       channel at a 44.1 kHz DAW rate and nothing else changes.
//   guest_mic_test host <relay ip:port> <session> <hostSecret> <seconds>
//       Acts as the host plugin on a real broker (keepalives on the peer relay)
//       and reports how much browser-mic audio it mixed in. Used by
//       scripts/test-guest-mic.sh with a headless Chrome guest.
#include "../Source/GigmaAooEngine.h"
#include "opus.h"
#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
struct Run { double wetPeak = 0; double coverage = 0; double dryErr = 0; };

template <typename Tick>
Run runEngine (GigmaAooEngine& e, double sr, int secs, Tick tick)
{
    const int bs = 256, ch = 2;
    std::vector<std::vector<float>> in (ch, std::vector<float> (bs)), out = in;
    std::vector<const float*> pin; std::vector<float*> pout;
    for (int c = 0; c < ch; ++c) { pin.push_back (in[c].data()); pout.push_back (out[c].data()); }
    Run r; long nz = 0, total = 0; double ph = 0;
    const int blocks = (int) (sr * secs / bs);
    auto next = std::chrono::steady_clock::now();
    for (int k = 0; k < blocks; ++k)
    {
        tick (k * bs / sr);
        for (int i = 0; i < bs; ++i)
        {
            const float v = 0.2f * (float) std::sin (ph); ph += 2 * M_PI * 220 / sr;   // "the track"
            for (int c = 0; c < ch; ++c) in[c][i] = v;
        }
        e.process (pin.data(), pout.data(), ch, bs);
        for (int i = 0; i < bs; ++i)
        {
            const double w = std::fabs (out[0][i] - in[0][i]);
            if (k > blocks / 4) { r.wetPeak = std::max (r.wetPeak, w); nz += w > 1e-3; ++total; }
            if (k < 5) r.dryErr = std::max (r.dryErr, w);   // before any voice is primed
        }
        if (k % (int) (sr / bs) == 0)
            std::printf ("t=%ds mics=%d packets=%llu wetPeak=%.3f\n", (int) (k * bs / sr), e.liveGuestMics(),
                         (unsigned long long) e.guestMicPackets(), r.wetPeak), std::fflush (stdout);
        next += std::chrono::microseconds ((int) (1e6 * bs / sr));
        std::this_thread::sleep_until (next);
    }
    r.coverage = total ? 100.0 * nz / total : 0;
    return r;
}

int selftest()
{
    int relay = socket (AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a {}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    bind (relay, (sockaddr*) &a, sizeof (a));
    socklen_t al = sizeof (a); getsockname (relay, (sockaddr*) &a, &al);
    const std::string relayHp = "127.0.0.1:" + std::to_string (ntohs (a.sin_port));

    const double sr = 44100;
    GigmaAooEngine e;
    e.prepare (sr, 256, 2);
    e.startNetwork (0);
    e.setPeerRelay (relayHp, "0123456789abcdef", "hosthosthosthost");
    sockaddr_in eng {}; eng.sin_family = AF_INET; eng.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    eng.sin_port = htons (e.port());

    int err = 0;
    OpusEncoder* enc = opus_encoder_create (48000, 1, OPUS_APPLICATION_VOIP, &err);
    uint32_t seq = 0, seq2 = 0; double ph = 0, ph2 = 0, sent = 0;
    auto tick = [&] (double t)
    {
        while (sent < t + 0.02)   // 20 ms packets, real time
        {
            unsigned char pkt[1500];
            std::memcpy (pkt, "GGB1", 4);
            // talker 3: Opus 48k, 660 Hz (drop every 10th packet to exercise PLC)
            float pcm[960];
            for (float& v : pcm) { v = 0.5f * (float) std::sin (ph); ph += 2 * M_PI * 660 / 48000; }
            const int n = opus_encode_float (enc, pcm, 960, pkt + 12, 1400);
            pkt[4] = 3; pkt[5] = 0; pkt[6] = 1; pkt[7] = 0;
            std::memcpy (pkt + 8, &seq, 4); ++seq;
            if (n > 0 && seq % 10 != 0)
                sendto (relay, pkt, 12 + n, 0, (sockaddr*) &eng, sizeof (eng));
            // talker 4: PCM16 16k, 330 Hz
            pkt[4] = 4; pkt[6] = 2;
            std::memcpy (pkt + 8, &seq2, 4); ++seq2;
            for (int i = 0; i < 320; ++i)
            {
                const auto s = (int16_t) std::lrint (8000 * std::sin (ph2)); ph2 += 2 * M_PI * 330 / 16000;
                pkt[12 + 2 * i] = (unsigned char) (s & 0xff); pkt[13 + 2 * i] = (unsigned char) ((uint16_t) s >> 8);
            }
            sendto (relay, pkt, 12 + 640, 0, (sockaddr*) &eng, sizeof (eng));
            sent += 0.02;
        }
    };
    const auto r = runEngine (e, sr, 4, tick);
    const int live = e.liveGuestMics();
    e.release();
    opus_encoder_destroy (enc);
    close (relay);
    std::printf ("FINAL mics=%d wetPeak=%.3f coverage=%.1f%% dryErr=%.5f\n", live, r.wetPeak, r.coverage, r.dryErr);
    const bool ok = live == 2 && r.wetPeak > 0.3 && r.coverage > 80 && r.dryErr < 1e-6;
    std::puts (ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int host (const char* relay, const char* session, const char* secret, int secs)
{
    GigmaAooEngine e;
    e.prepare (48000, 256, 2);
    e.startNetwork (0);
    e.setPeerRelay (relay, session, secret);
    const auto r = runEngine (e, 48000, secs, [] (double) {});
    std::printf ("FINAL packets=%llu wetPeak=%.3f coverage=%.1f%%\n",
                 (unsigned long long) e.guestMicPackets(), r.wetPeak, r.coverage);
    const bool ok = e.guestMicPackets() > 20 && r.wetPeak > 0.01;
    e.release();
    std::puts (ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc >= 2 && std::strcmp (argv[1], "selftest") == 0)
        return selftest();
    if (argc == 6 && std::strcmp (argv[1], "host") == 0)
        return host (argv[2], argv[3], argv[4], std::atoi (argv[5]));
    std::puts ("usage: guest_mic_test selftest | host <relay> <session> <hostSecret> <seconds>");
    return 2;
}
