# Gigma Plugin (macOS)

FX plugin the **producer** loads in their DAW. Formats: **VST3** and **AU**.

## Role

1. On open / “New meeting”, call the Go broker at `https://gigma.veritas.org.in` to create a session and claim host.
2. Show / copy the shareable meeting link.
3. Act as the **SonoBus-class audio endpoint** on the producer machine (full DAW I/O via the plugin graph).
4. Remote guests open the link in a browser — no separate SonoBus install for them. Audio path is plugin↔guests (direct or via relay later); the Go app only coordinates sessions/links.

## Build (macOS)

Requires Xcode, CMake ≥ 3.22, and a JUCE 7/8 checkout (GPL-compatible usage).

```bash
cd plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=/path/to/JUCE
cmake --build build --config Release
```

Artefacts land under `build/GigmaPlugin_artefacts/`.

This folder is a scaffold: processor/editor stubs + HTTP session client headers. Wire real low-latency network audio next (reuse patterns from SonoBus / AOO / Opus under GPL).
