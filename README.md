# Gigma

A modern, Zoom-simple UI around [SonoBus](https://www.sonobus.net/) for musicians who want low-latency DAW audio collaboration without the technical setup.

**Gigma** is the product name. This repository is `veritasstudio/gigma` (formerly `sonobus-wrapper`).

## Why this exists

SonoBus is excellent at peer-to-peer, low-latency audio — free, open source, and available as a DAW plugin and standalone app. The tradeoff is that getting a reliable session often means wrestling with buffer sizes, codecs, network stats, and audio-device settings — and the usual join flow (pick a group name, share a password) feels nothing like a meeting product musicians already know.

Gigma wraps that power in a friendlier product: a polished, contemporary interface that feels as easy as Zoom. The host creates a room and shares **one meeting link**; anyone who opens it joins directly. No copying group names or passwords. Sensible defaults for jamming and remote tracking, with as little jargon as possible between you and making music together.

## Design principles

- **Meeting links, not group codes** — host creates a room → gets a shareable link → guests open it and join, Zoom-style
- **As simple as Zoom** — create/join, mute, leave; advanced audio knobs stay out of the way until you need them
- **Modern aesthetics** — clean layout, readable typography, calm visual hierarchy; not a dense engineer console by default
- **UX-first** — one obvious path for first-time musicians; power options available, not required

## Goals

- **Shareable meeting links** — replace SonoBus’s group-name + password invite with a single join URL the host can send in chat, email, or calendar
- **Simple session UX** — create a room, share the link, join from the link; mute and leave are always obvious
- **Sensible defaults** — latency/quality presets for “jam”, “rehearse”, and “record monitor”
- **DAW-friendly** — work with SonoBus as a plugin or alongside a standalone session
- **Cross-platform** — target the same platforms SonoBus already supports where practical

## Status

Early scaffold. No app code yet — README and project layout only.

## Relationship to SonoBus

SonoBus is developed by Sonosaurus and released as free/open-source software. Gigma is a separate project that builds on or around SonoBus; it is not affiliated with Sonosaurus unless stated otherwise.

Under the hood, sessions may still map to SonoBus groups — the point of Gigma is that musicians never have to manage that themselves. They only see and share a meeting link.

See the official site: https://www.sonobus.net/  
Source: https://github.com/sonosaurus/sonobus

## License

This project is intended to stay GPL-compatible with SonoBus. License file TBD once the first code lands; until then treat contributions as GPL-3.0-or-later.

## Contributing

Ideas and PRs welcome once the first UI shell exists. Please keep the focus on musicians who should not need to become network engineers to play together — and on keeping the interface modern, Zoom-simple, and link-based to join.
