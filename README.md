# Gigma

Zoom-simple low-latency music collaboration: a **meeting-link broker** plus a **macOS DAW FX plugin**.

**Product** · [`veritasstudio/gigma`](https://github.com/veritasstudio/gigma) · https://gigma.veritas.org.in

## Architecture (current direction)

| Piece | Location | Role |
| --- | --- | --- |
| **Go web app** | `cmd/gigma`, `web/static`, `internal/*` | Brokers meeting links and who joins. **Not** the audio path. Open sessions (no login); join page = name + Join. |
| **macOS plugin** | `plugin/` | Producer loads **Gigma** (VST3 / AU) in the DAW. Creates/claims a session via the Go API, shows a shareable link, and is the **audio endpoint** on the producer machine (full DAW I/O). Guests open the link in a browser — no separate SonoBus install for them. |

Audio path: **plugin ↔ guests** (direct or relay later). Go only does link/session coordination. DNS already live: Cloudflare proxied `gigma.veritas.org.in` → `15.235.199.44`.

## Repo layout

```
cmd/gigma/           Go broker entrypoint
internal/session/    In-memory sessions
internal/pluginreg/  Producer plugin host claims / heartbeats
web/static/          Minimal meeting UI (name + Join)
plugin/              macOS VST3 + AU FX scaffold (JUCE)
  CMakeLists.txt
  Source/PluginProcessor.*
  Source/PluginEditor.*
  Source/GigmaSessionClient.*
```

## Go broker

```bash
go build -o bin/gigma ./cmd/gigma
GIGMA_PUBLIC_BASE=https://gigma.veritas.org.in ./bin/gigma
```

Useful APIs: `POST /api/sessions`, `POST /api/sessions/{id}/host`, `GET /m/{id}`.

## macOS plugin

See [`plugin/README.md`](plugin/README.md). Scaffold compiles against a local JUCE tree; network audio (SonoBus-class) is the next implementation step.

## Relationship to SonoBus

SonoBus (Sonosaurus, GPL-3.0, C++/JUCE) remains the reference for peer audio. Gigma’s plugin is a separate GPL-compatible product that may reuse similar techniques; guests should not need a standalone SonoBus install.

## License

Treat contributions as GPL-3.0-or-later until a license file is added.
