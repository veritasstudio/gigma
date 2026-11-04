# Gigma Plugin (macOS)

FX plugin (VST3 + AU) the producer loads in their DAW.

## Audio path

`GigmaAooEngine` captures DAW input in `processBlock`, sends via **AOO** (Audio over OSC — same family SonoBus uses), and mixes received remote audio to the DAW output. UDP endpoint is advertised to the Go broker as `aooEndpoint` when claiming host.

Build with AOO:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DJUCE_PATH=/path/to/JUCE \
  -DGIGMA_HAS_AOO=ON \
  -DAOO_PATH=/path/to/aoo
cmake --build build --config Release
```

Without `-DGIGMA_HAS_AOO=ON`, the engine still runs I/O (local loopback) so the plugin can be developed before AOO is linked. **Compile only on a real Mac** — Linux/Xtool cannot produce AU/VST3 for macOS here.

Use upstream AOO: https://git.iem.at/aoo/aoo (not the SonoBus-only fork).
