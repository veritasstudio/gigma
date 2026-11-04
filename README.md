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

1. **Plugin ↔ plugin (AOO, peer-to-peer UDP, Opus).** Each plugin binds a UDP port and advertises
   `LAN-IP:port` (default-route interface, never `0.0.0.0`). Host heartbeats (every 2 s) return
   `peers` = plugin guests' endpoints; the host `addSink`s each one. A plugin guest learns the host
   endpoint from `/join` + its own heartbeat and `addSink`s the host, so audio flows both ways.
   Works on the same LAN / VPN (Tailscale etc.). Across the open internet each side needs a
   reachable UDP port (port-forward) — there's no NAT traversal / AOO relay server yet.
2. **Plugin → browser (listen-only relay).** Browsers can't speak AOO (raw UDP/OSC), so when the
   broker reports ≥1 browser listener the host plugin also sends PCM16 (native rate, stereo) to the
   broker's UDP ingest (`GIGMA_RELAY_PUBLIC`, the VPS IP — the HTTP host is behind Cloudflare,
   which doesn't proxy UDP). The broker fans it out on `GET /api/sessions/{id}/listen`
   (WebSocket); `room.html` plays it via Web Audio with ~250 ms buffer. Outbound-only from the
   producer, so no port-forwarding needed. Browser mic → producer is not implemented.

DNS: Cloudflare proxied `gigma.veritas.org.in` → `15.235.199.44`.

## Quick test (broker)

```bash
./scripts/test-join-flow.sh
```

Creates a session, claims host with an `aooEndpoint`, joins a browser guest and a plugin guest, asserts endpoints are exchanged both ways.

```bash
go run ./cmd/relaycheck -base https://gigma.veritas.org.in   # relay end to end (UDP in → WebSocket out)
./scripts/test-aoo-peer.sh                                   # two AOO engines on this Mac (needs third_party/aoo built)
```

## Build broker for Debian VPS

```bash
./scripts/build-server.sh
# → bin/gigma-linux-amd64  (CGO_ENABLED=0, linux/amd64)
```

Run on the server (behind Caddy):

```bash
GIGMA_PUBLIC_BASE=https://gigma.veritas.org.in GIGMA_ADDR=127.0.0.1:3080 \
GIGMA_RELAY_UDP=:7300 GIGMA_RELAY_PUBLIC=15.235.199.44:7300 ./gigma-linux-amd64
# ufw allow 7300/udp
```

## Plugin (Mac only)

See [`plugin/README.md`](plugin/README.md). `GigmaAooEngine` captures DAW input → AOO UDP → remote; received audio → DAW out. Enable `-DGIGMA_HAS_AOO=ON` with upstream AOO.

## License

GPL-3.0-or-later intent (compatible with SonoBus / AOO).
