# Gigma

Zoom-simple low-latency music collaboration: a **meeting-link broker** (Go) plus a **macOS DAW FX plugin** (JUCE + AOO).

**Repo** · [`veritasstudio/gigma`](https://github.com/veritasstudio/gigma) · https://gigma.veritas.org.in

## Architecture

| Piece | Role |
| --- | --- |
| **Go broker** (`cmd/gigma`) | Meeting links + who joins. Not the audio path. |
| **macOS plugin** (`plugin/`) | Producer loads VST3/AU in the DAW. Creates link via broker, owns **AOO** audio I/O. |
| **Browser guests** | Open `/m/{id}`, enter name, Join — no SonoBus install. |

DNS: Cloudflare proxied `gigma.veritas.org.in` → `15.235.199.44`.

## Quick test (broker)

```bash
./scripts/test-join-flow.sh
```

Creates a session, claims host with an `aooEndpoint`, joins a guest named Alex, asserts the guest sees the host endpoint.

## Build broker for Debian VPS

```bash
./scripts/build-server.sh
# → bin/gigma-linux-amd64  (CGO_ENABLED=0, linux/amd64)
```

Run on the server (behind Caddy):

```bash
GIGMA_PUBLIC_BASE=https://gigma.veritas.org.in GIGMA_ADDR=127.0.0.1:3080 ./gigma-linux-amd64
```

## Plugin (Mac only)

See [`plugin/README.md`](plugin/README.md). `GigmaAooEngine` captures DAW input → AOO UDP → remote; received audio → DAW out. Enable `-DGIGMA_HAS_AOO=ON` with upstream AOO.

## License

GPL-3.0-or-later intent (compatible with SonoBus / AOO).
