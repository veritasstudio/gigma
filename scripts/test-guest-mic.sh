#!/usr/bin/env bash
# Browser mic → host plugin, end to end, no DAW:
#   local broker + the plugin's AOO engine as host + 2 headless Chrome guests
#   (fake mic). PASS = host mixed browser-mic audio and guests heard each other.
#   ./scripts/test-guest-mic.sh                                    # local broker
#   BASE=https://gigma.veritas.org.in ./scripts/test-guest-mic.sh  # live VPS
# Needs third_party/aoo built (AOO=... to override) and python playwright.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AOO="${AOO:-$ROOT/third_party/aoo}"
CXX="${CXX:-c++}"
OUT="$ROOT/plugin/build/guest_mic_test"
mkdir -p "$(dirname "$OUT")"
$CXX -std=c++17 -O2 -DGIGMA_HAS_AOO=1 -I"$AOO/include" -I"$AOO/deps/opus/include" \
  "$ROOT/plugin/tests/guest_mic_test.cpp" "$ROOT/plugin/Source/GigmaAooEngine.cpp" \
  "$AOO/build/aoo/libaoo.a" "$AOO/build/deps/opus/libopus.a" -lpthread -o "$OUT"
"$OUT" selftest

if [[ -z "${BASE:-}" ]]; then
  PORT=18082; RPORT=17302
  BASE="http://127.0.0.1:$PORT"
  go build -o "$ROOT/bin/gigma-test" "$ROOT/cmd/gigma"
  GIGMA_ADDR="127.0.0.1:$PORT" GIGMA_PUBLIC_BASE="$BASE" GIGMA_WEB="$ROOT/web/static" \
    GIGMA_RELAY_UDP="127.0.0.1:$RPORT" GIGMA_RELAY_PUBLIC="127.0.0.1:$RPORT" \
    "$ROOT/bin/gigma-test" >/tmp/gigma-mic-test.log 2>&1 &
  PID=$!; trap 'kill $PID 2>/dev/null || true' EXIT
  for i in $(seq 1 50); do curl -sf "$BASE/healthz" >/dev/null && break; sleep 0.1; done
fi
j() { python3 -c "import json,sys; d=json.load(sys.stdin); print(eval(sys.argv[1]))" "$1"; }
SID=$(curl -sf -X POST "$BASE/api/sessions" -d '{"title":"mic test"}' | j 'd["id"]')
CLAIM=$(curl -sf -X POST "$BASE/api/sessions/$SID/host" -d '{"label":"mic-test host"}')
HS=$(j 'd["host"]["hostId"]' <<<"$CLAIM"); RELAY=$(j 'd["relay"]' <<<"$CLAIM")
echo "base=$BASE relay=$RELAY session=$SID"
"$OUT" host "$RELAY" "$SID" "$HS" 14 > /tmp/gigma-mic-host.log &
HPID=$!
sleep 1
${PYTHON:-python3} "$ROOT/scripts/guest_mic_browser.py" "$BASE" "$SID" 9 2
curl -sf "$BASE/api/sessions/$SID" | python3 -c "import json,sys; print('talkers:', json.load(sys.stdin)['talkers'])"
wait $HPID; RC=$?
tail -3 /tmp/gigma-mic-host.log
exit $RC
