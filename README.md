# Gigma

A modern, Zoom-simple web UI around [SonoBus](https://www.sonobus.net/) for musicians who want low-latency DAW audio collaboration without the technical setup.

**Gigma** · [`veritasstudio/gigma`](https://github.com/veritasstudio/gigma) · https://gigma.veritas.org.in

## Product decisions

- **No login or password** — sessions are open; anyone with the meeting link can join
- **Join page is minimal** — name field + Join button only
- **Meeting links** — host creates a room → shares one URL → guests open it (not SonoBus group name + password)
- **Go web app** — backend serves the UI and APIs; frontend is simple static HTML/CSS/JS
- **SonoBus as child process** — launched/stopped on the same Debian VPS (`15.235.199.44`) alongside other Veritas sites; never linked at compile time (`SONOBUS_BIN`)
- **DNS** — Cloudflare A record `gigma.veritas.org.in` → `15.235.199.44` (proxied)

## Run locally

```bash
go build -o bin/gigma ./cmd/gigma
GIGMA_PUBLIC_BASE=http://localhost:8080 ./bin/gigma
```

Optional: `SONOBUS_BIN=/path/to/sonobus`, `GIGMA_ADDR=:8080`

## Relationship to SonoBus

SonoBus is Sonosaurus software (GPL-3.0, C++/JUCE). Gigma is a separate GPL-compatible wrapper and is not affiliated with Sonosaurus unless stated otherwise.

https://www.sonobus.net/ · https://github.com/sonosaurus/sonobus

## License

Treat contributions as GPL-3.0-or-later until a license file is added.
