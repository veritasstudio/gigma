#!/usr/bin/env bash
# Plugin↔plugin AOO smoke test (no DAW): needs third_party/aoo built (see plugin/README.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AOO="$ROOT/third_party/aoo"
OUT="$ROOT/plugin/build/aoo_peer_test"
mkdir -p "$(dirname "$OUT")"
c++ -std=c++17 -O2 -DGIGMA_HAS_AOO=1 -I"$AOO/include" -I"$AOO/deps/opus/include" \
  "$ROOT/plugin/tests/aoo_peer_test.cpp" "$ROOT/plugin/Source/GigmaAooEngine.cpp" \
  "$AOO/build/aoo/libaoo.a" "$AOO/build/deps/opus/libopus.a" -lpthread -o "$OUT"
"$OUT"
