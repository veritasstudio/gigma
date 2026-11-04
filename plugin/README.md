# Gigma Plugin (macOS)

FX plugin (VST3 + AU) the producer loads in their DAW.

## Audio path

`GigmaAooEngine` captures DAW input in `processBlock`, sends via **AOO** (Audio over OSC — same family SonoBus uses), and mixes received remote audio to the DAW output. UDP endpoint is advertised to the Go broker as `aooEndpoint` when claiming host.

In the plugin window: **New meeting (host)** creates a link (share it), or paste someone's link and
**Join as plugin**. Status shows `Internet: connected via Gigma relay` (plugin↔plugin audio goes
through the VPS relay so it works across any NAT), plugin peers, and browser listeners. Output =
your dry input + whatever remote plugins send you. Launch the DAW with `GIGMA_DIRECT=1` to use
LAN/VPN direct UDP instead of the relay.

Build with AOO (deps live in `third_party/`, git-ignored):

```bash
git clone https://github.com/juce-framework/JUCE third_party/JUCE
git clone https://git.iem.at/aoo/aoo third_party/aoo   # AOO 2.x
cmake -S third_party/aoo -B third_party/aoo/build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release -DAOO_BUILD_SHARED_LIBRARY=OFF -DAOO_LOCAL_OPUS=ON \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON && cmake --build third_party/aoo/build -j8
cmake -S plugin -B plugin/build -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=$PWD/third_party/JUCE \
  -DGIGMA_HAS_AOO=ON -DAOO_PATH=$PWD/third_party/aoo
cmake --build plugin/build --config Release -j8   # copies to ~/Library/Audio/Plug-Ins
auval -v aufx Gigm Vrts
./scripts/test-aoo-peer.sh                        # A→B over AOO on this machine + relay packets
```

Without `-DGIGMA_HAS_AOO=ON`, the engine still runs I/O (local loopback) so the plugin can be developed before AOO is linked. **Compile only on a real Mac** — Linux/Xtool cannot produce AU/VST3 for macOS here.

Use upstream AOO: https://git.iem.at/aoo/aoo (not the SonoBus-only fork).
