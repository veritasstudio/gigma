# Gigma

Zoom-simple low-latency music collaboration: a **meeting-link broker** (Go) plus a **macOS DAW FX plugin** (JUCE + AOO).

**Repo** · [`veritasstudio/gigma`](https://github.com/veritasstudio/gigma) · https://gigma.veritas.org.in

## Architecture

| Piece | Role |
| --- | --- |
| **Go broker** (`cmd/gigma`) | Meeting links, who joins, peer endpoint exchange, and a small UDP→WebSocket relay for browser listeners. |
| **macOS plugin** (`plugin/`) | Producer loads VST3/AU in the DAW. Hosts a meeting or joins one; owns **AOO** audio I/O. |
| **Browser guests** | Open `/m/{id}`, enter name, Join, press **Listen** — no install. |

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
   producer. Browser mic → producer is not implemented.

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
