#!/usr/bin/env bash
# Plugin↔plugin AOO through the broker's UDP relay (the internet path), no DAW.
#   ./scripts/test-aoo-relay.sh                                # local broker on 127.0.0.1
#   BASE=https://gigma.veritas.org.in ./scripts/test-aoo-relay.sh   # live VPS relay
# Needs third_party/aoo built (see plugin/README.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AOO="$ROOT/third_party/aoo"
OUT="$ROOT/plugin/build/aoo_relay_test"
mkdir -p "$(dirname "$OUT")"
c++ -std=c++17 -O2 -DGIGMA_HAS_AOO=1 -I"$AOO/include" -I"$AOO/deps/opus/include" \
  "$ROOT/plugin/tests/aoo_relay_test.cpp" "$ROOT/plugin/Source/GigmaAooEngine.cpp" \
  "$AOO/build/aoo/libaoo.a" "$AOO/build/deps/opus/libopus.a" -lpthread -o "$OUT"

if [[ -z "${BASE:-}" ]]; then
  PORT=18081; RPORT=17301
  BASE="http://127.0.0.1:$PORT"
  go build -o "$ROOT/bin/gigma-test" "$ROOT/cmd/gigma"
  GIGMA_ADDR=":$PORT" GIGMA_PUBLIC_BASE="$BASE" GIGMA_WEB="$ROOT/web/static" \
    GIGMA_RELAY_UDP="127.0.0.1:$RPORT" GIGMA_RELAY_PUBLIC="127.0.0.1:$RPORT" \
    "$ROOT/bin/gigma-test" >/tmp/gigma-relay-test.log 2>&1 &
  PID=$!; trap 'kill $PID 2>/dev/null || true' EXIT
  for i in $(seq 1 50); do curl -sf "$BASE/healthz" >/dev/null && break; sleep 0.1; done
fi

j() { python3 -c "import json,sys; d=json.load(sys.stdin); print(eval(sys.argv[1]))" "$1"; }
SID=$(curl -sf -X POST "$BASE/api/sessions" -d '{"title":"relay test"}' | j 'd["id"]')
CLAIM=$(curl -sf -X POST "$BASE/api/sessions/$SID/host" -d '{"label":"relay-test host"}')
HS=$(j 'd["host"]["hostId"]' <<<"$CLAIM"); HP=$(j 'd["host"]["peerId"]' <<<"$CLAIM"); RELAY=$(j 'd["relay"]' <<<"$CLAIM")
JOIN=$(curl -sf -X POST "$BASE/api/sessions/$SID/join" -d '{"name":"relay-test guest","kind":"plugin"}')
GS=$(j 'd["token"]' <<<"$JOIN"); GP=$(j 'd["guest"]["id"]' <<<"$JOIN")
test "$(j 'd["hostPeerId"]' <<<"$JOIN")" = "$HP"
# the public session view must not leak the host secret
if curl -sf "$BASE/api/sessions/$SID" | grep -q "$HS"; then echo "FAIL: hostId leaked"; exit 1; fi
echo "base=$BASE relay=$RELAY session=$SID"
"$OUT" "$RELAY" "$SID" "$HS" "$HP" "$GS" "$GP"
curl -sf "$BASE/healthz"
