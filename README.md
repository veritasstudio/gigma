# Gigma

Zoom-simple low-latency music collaboration: a **meeting-link broker** (Go) plus a **macOS DAW FX plugin** (JUCE + AOO).

**Repo** · [`veritasstudio/gigma`](https://github.com/veritasstudio/gigma) · https://gigma.veritas.org.in

## Architecture

| Piece | Role |
| --- | --- |
| **Go broker** (`cmd/gigma`) | Meeting links, who joins, peer endpoint exchange, and a small UDP→WebSocket relay for browser listeners. |
| **macOS plugin** (`plugin/`) | Producer loads VST3/AU in the DAW. Hosts a meeting or joins one; owns **AOO** audio I/O. |
| **Browser guests** | Open `/m/{id}`, enter name, Join, press **Listen**, **Unmute** to talk — no install. |

### Audio paths

1. **Plugin ↔ plugin over the internet (default): AOO through the Gigma UDP relay.**
   Both plugins send their AOO datagrams *out* to the VPS (`GIGMA_RELAY_PUBLIC`, UDP 7300 — the
   same port as the browser relay), wrapped in a 52-byte `GGR1` header naming the session, the
   sender's secret and the destination peer. The broker forwards each datagram to the destination's
   last-seen public `ip:port` (learned from that peer's own packets + 1 s keepalives, which also
   hold its NAT mapping open). Because nobody ever needs an inbound port, this works behind any
   NAT — home routers, symmetric NAT, mobile CGNAT — with no Tailscale and no port forwarding.
   It's TURN-style relaying, but AOO-aware and tiny (`internal/relay/peer.go`). Cost: one extra hop
   via the VPS (~44 ms RTT from Bengaluru), plus AOO's ~50 ms jitter buffer.
2. **Plugin ↔ plugin direct (LAN / VPN, opt-in).** Launch the DAW with `GIGMA_DIRECT=1` and the
   plugin `addSink`s peers' LAN `ip:port` instead (lowest latency; only works when both are on the
   same LAN/VPN or have a port forward).
3. **Plugin → browser (listen-only relay).** Browsers can't speak AOO (raw UDP/OSC), so when the
   broker reports ≥1 browser listener the host plugin also sends PCM16 (native rate, stereo) to the
   same UDP port (`GGM1` packets). The broker fans it out on `GET /api/sessions/{id}/listen`
   (WebSocket); `room.html` plays it via Web Audio with ~250 ms buffer. Outbound-only from the
   producer.
4. **Browser mic → producer + everyone (talk-back).** The same WebSocket is bidirectional. A guest
   taps **Unmute** (one permission prompt; starts muted, Zoom-style) and `talk.js` sends 20 ms mono
   frames up: **Opus 48 kHz** via WebCodecs `AudioEncoder` (~48 kbps, in-band FEC), or PCM16 16 kHz
   where the browser has no Opus encoder. The broker only fans out (`internal/relay/mic.go`):
   to every other browser on the WebSocket, and as `GGB1` UDP datagrams from the relay port to every
   plugin registered on the AOO relay (host + plugin guests) — the socket their GGR1 keepalives
   already hold open, so no new ports. The plugin decodes each talker (Opus PLC on gaps), resamples
   to the DAW rate, jitter-buffers (Quality buffer, 60–400 ms) and **adds** the voices to the insert
   output. Voices are never re-sent, so there's no loop; guests should use headphones so their mic
   doesn't pick up the producer's track. Plugin UI shows "Browser mics live: N".

   ```
   browser → broker (WS):  codec u8 (1 opus, 2 pcm16@16k) | 3 reserved | seq u32 | payload
   broker → browsers (WS): seq u32 | 0 u32 (= mic frame) | codec u8 | 0 | talker u16 | payload
   broker → plugins (UDP): "GGB1" | talker u16 | codec u8 | 0 | seq u32 | payload
   ```

The HTTP host is behind Cloudflare, which doesn't proxy UDP, so all UDP goes to the VPS IP directly.

### How endpoints are advertised

| Who | Advertised to broker | What peers use |
| --- | --- | --- |
| Host plugin | `aooEndpoint` = LAN `ip:port`; gets secret `hostId` + public `peerId` on claim | guests address it as `relay:<host peerId>` (or LAN endpoint with `GIGMA_DIRECT=1`) |
| Plugin guest | `aooEndpoint` = LAN `ip:port` on join; gets public `guest.id` + secret `token` | host addresses it as `relay:<guest id>` (heartbeat `peers[].id`, `relayed` = registered on relay now) |
| Relay | `relay` = `15.235.199.44:7300` in claim / join / heartbeat responses | where all `GGR1` / `GGM1` UDP goes |

Public endpoints are never guessed (no STUN): the relay sees each peer's real public address on
every packet. Inside AOO a relayed peer is a placeholder address in `240.0.0.0/4` (reserved,
unroutable); the engine's send hook wraps anything addressed there and unwraps relay replies, so
AOO's own protocol is untouched. Secrets (`hostId`, guest `token`) are only returned to their owner;
`GET /api/sessions/{id}` exposes the host's public view only. Topology is a star: guests ↔ host.

Relay wire format (UDP, ASCII ids are 16 chars, `0000000000000000` = keepalive / ack):

```
client → relay:  "GGR1" | session | secret | dstPeer | AOO datagram
relay → client:  "GGR1" | srcPeer | AOO datagram
```

DNS: Cloudflare proxied `gigma.veritas.org.in` → `15.235.199.44`.

## Quick test (broker)

```bash
./scripts/test-join-flow.sh
```

Creates a session, claims host with an `aooEndpoint`, joins a browser guest and a plugin guest, asserts endpoints are exchanged both ways.

```bash
go test ./...                                                # relay forwarding/auth unit test
go run ./cmd/relaycheck -base https://gigma.veritas.org.in   # browser relay end to end (UDP in → WebSocket out)
./scripts/test-aoo-peer.sh                                   # two AOO engines on this Mac, direct (needs third_party/aoo built)
./scripts/test-aoo-relay.sh                                  # two AOO engines via a local broker relay, both directions
BASE=https://gigma.veritas.org.in ./scripts/test-aoo-relay.sh   # same, through the live VPS relay
```

```bash
./scripts/test-guest-mic.sh       # browser mic → plugin: engine selftest (Opus + PCM16 @44.1k), then local
                                  # broker + host engine + 2 headless Chrome guests with fake mics
BASE=https://gigma.veritas.org.in ./scripts/test-guest-mic.sh   # same, through the live VPS
NO_OPUS=1 ./scripts/test-guest-mic.sh                            # force the PCM16 browser fallback
```

Cross-network (two machines, no DAW): build `plugin/tests/aoo_relay_peer.cpp` on each (same
compile line as `test-aoo-relay.sh`), create a session + claim host + join a plugin guest with curl,
then run `aoo_relay_peer <relay> <session> <mySecret> <otherPeerId> <freq> <secs>` on both.

## Build broker for Debian VPS

```bash
./scripts/build-server.sh
# → bin/gigma-linux-amd64  (CGO_ENABLED=0, linux/amd64)
```

Run on the server (behind Caddy):

```bash
GIGMA_PUBLIC_BASE=https://gigma.veritas.org.in GIGMA_ADDR=127.0.0.1:3080 \
GIGMA_RELAY_UDP=:7300 GIGMA_RELAY_PUBLIC=15.235.199.44:7300 ./gigma-linux-amd64
# ufw allow 7300/udp   (browser relay + plugin↔plugin AOO relay)
```

## Plugin (Mac only)

See [`plugin/README.md`](plugin/README.md). `GigmaAooEngine` captures DAW input → AOO UDP → remote; received audio → DAW out. Enable `-DGIGMA_HAS_AOO=ON` with upstream AOO.

## License

GPL-3.0-or-later intent (compatible with SonoBus / AOO).
