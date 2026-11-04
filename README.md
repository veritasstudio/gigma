# Gigma

A modern, Zoom-simple web UI around [SonoBus](https://www.sonobus.net/) for musicians who want low-latency DAW audio collaboration without the technical setup.

**Gigma** is the product name. Repository: [`veritasstudio/gigma`](https://github.com/veritasstudio/gigma).

## Product model

- Host creates a room and gets **one meeting link**
- Guests open the link and join — no SonoBus group name or password to share
- UI aims to feel as simple as Zoom: create / join / mute / leave
- Go backend will run on the Veritas Debian VPS (`15.235.199.44`)
- SonoBus is managed as an **external child process** only (launch / stop / reconnect) — Gigma does not compile-link against SonoBus

Proposed public URL (DNS not created until confirmed): `https://gigma.veritas.org.in`

## Stack

- **Go** HTTP server (`cmd/gigma`)
- Static Zoom-simple frontend in `web/static`
- Child-process manager in `internal/sonobus` (`SONOBUS_BIN`)
- In-memory sessions in `internal/session` (replace later for multi-node)

## Run locally

```bash
go build -o bin/gigma ./cmd/gigma
GIGMA_PUBLIC_BASE=http://localhost:8080 ./bin/gigma
```

Optional:

```bash
export SONOBUS_BIN=/path/to/sonobus   # if installed; otherwise API soft-fails and UI still works
export GIGMA_ADDR=:8080
```

Then open http://localhost:8080 — create a meeting and share `/m/{id}`.

## Relationship to SonoBus

SonoBus is developed by Sonosaurus (GPL-3.0, C++/JUCE). Gigma is a separate GPL-compatible wrapper project and is not affiliated with Sonosaurus unless stated otherwise. Under the hood, sessions may still map to SonoBus groups; musicians only see a meeting link.

https://www.sonobus.net/ · https://github.com/sonosaurus/sonobus

## License

GPL-compatible with SonoBus. License file TBD once packaging settles; treat contributions as GPL-3.0-or-later for now.
