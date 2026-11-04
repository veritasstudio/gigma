# SonoBus Wrapper

A modern, Zoom-simple UI around [SonoBus](https://www.sonobus.net/) for musicians who want low-latency DAW audio collaboration without the technical setup.

## Why this exists

SonoBus is excellent at peer-to-peer, low-latency audio — free, open source, and available as a DAW plugin and standalone app. The tradeoff is that getting a reliable session often means wrestling with buffer sizes, codecs, network stats, and audio-device settings.

This project wraps that power in a friendlier product: a polished, contemporary interface that feels as easy as Zoom — join or create a session in a few clicks, clear controls, sensible defaults for jamming and remote tracking, and as little jargon as possible between you and making music together.

## Design principles

- **As simple as Zoom** — create/join a room, mute, leave; advanced audio knobs stay out of the way until you need them
- **Modern aesthetics** — clean layout, readable typography, calm visual hierarchy; not a dense engineer console by default
- **UX-first** — one obvious path for first-time musicians; power options available, not required

## Goals

- **Simple session UX** — create or join a room with a name and optional password, Zoom-style
- **Sensible defaults** — latency/quality presets for “jam”, “rehearse”, and “record monitor”
- **DAW-friendly** — work with SonoBus as a plugin or alongside a standalone session
- **Cross-platform** — target the same platforms SonoBus already supports where practical

## Status

Early scaffold. No app code yet — README and project layout only.

## Relationship to SonoBus

SonoBus is developed by Sonosaurus and released as free/open-source software. This wrapper is a separate project that builds on or around SonoBus; it is not affiliated with Sonosaurus unless stated otherwise.

See the official site: https://www.sonobus.net/  
Source: https://github.com/sonosaurus/sonobus

## License

This project is intended to stay GPL-compatible with SonoBus. License file TBD once the first code lands; until then treat contributions as GPL-3.0-or-later.

## Contributing

Ideas and PRs welcome once the first UI shell exists. Please keep the focus on musicians who should not need to become network engineers to play together — and on keeping the interface modern and Zoom-simple.
