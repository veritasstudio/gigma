#!/usr/bin/env bash
# Minimal join-flow test against a local broker:
# plugin creates session → claims host with aooEndpoint → browser guest joins.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PORT="${PORT:-18080}"
BASE="http://127.0.0.1:${PORT}"
BIN="${BIN:-$ROOT/bin/gigma-test}"

go build -o "$BIN" ./cmd/gigma
GIGMA_ADDR=":${PORT}" GIGMA_PUBLIC_BASE="$BASE" GIGMA_WEB="$ROOT/web/static" "$BIN" >/tmp/gigma-test.log 2>&1 &
PID=$!
cleanup() { kill "$PID" 2>/dev/null || true; }
trap cleanup EXIT

for i in $(seq 1 50); do
  if curl -sf "$BASE/healthz" >/dev/null; then break; fi
  sleep 0.1
done
curl -sf "$BASE/healthz" | grep -q '"ok":true'

# 1) Create session (as plugin would)
CREATE=$(curl -sf -X POST "$BASE/api/sessions" -H 'content-type: application/json' \
  -d '{"title":"Join flow test"}')
ID=$(python3 -c "import json,sys; print(json.load(sys.stdin)['id'])" <<<"$CREATE")
URL=$(python3 -c "import json,sys; print(json.load(sys.stdin)['url'])" <<<"$CREATE")
test -n "$ID"
echo "session=$ID url=$URL"

# 2) Claim host with AOO endpoint
HOST=$(curl -sf -X POST "$BASE/api/sessions/$ID/host" -H 'content-type: application/json' \
  -d '{"label":"Gigma Plugin","aooEndpoint":"203.0.113.10:14999"}')
HOST_ID=$(python3 -c "import json,sys; print(json.load(sys.stdin)['host']['hostId'])" <<<"$HOST")
test -n "$HOST_ID"
echo "hostId=$HOST_ID"

# 3) Guest opens /m/{id} then joins
curl -sf "$BASE/m/$ID" | grep -qi 'Join'
JOIN=$(curl -sf -X POST "$BASE/api/sessions/$ID/join" -H 'content-type: application/json' \
  -d '{"name":"Alex"}')
python3 - << PY
import json,sys
j=json.loads('''$JOIN''')
assert j['guest']['name']=='Alex', j
assert j['hasHost'] is True, j
assert j.get('aooEndpoint')=='203.0.113.10:14999', j
print('guest', j['guest']['id'], 'aoo', j['aooEndpoint'])
PY

# 4) Session shows guest + host
GET=$(curl -sf "$BASE/api/sessions/$ID")
python3 - << PY
import json
s=json.loads('''$GET''')
assert s['hasHost'] and len(s['guests'])==1 and s['guests'][0]['name']=='Alex'
print('OK join-flow')
PY

# 5) Plugin guest joins with its own AOO endpoint → host heartbeat lists it as a peer
PJ=$(curl -sf -X POST "$BASE/api/sessions/$ID/join" -H 'content-type: application/json' \
  -d '{"name":"Studio B","kind":"plugin","aooEndpoint":"198.51.100.7:15000"}')
GID=$(python3 -c "import json,sys; print(json.load(sys.stdin)['guest']['id'])" <<<"$PJ")
HB=$(curl -sf -X POST "$BASE/api/sessions/$ID/host/heartbeat" -H 'content-type: application/json' \
  -d "{\"hostId\":\"$HOST_ID\",\"aooEndpoint\":\"203.0.113.10:14999\"}")
GHB=$(curl -sf -X POST "$BASE/api/sessions/$ID/guests/$GID/heartbeat" -H 'content-type: application/json' \
  -d '{"aooEndpoint":"198.51.100.7:15000"}')
python3 - << PY
import json
hb=json.loads('''$HB'''); g=json.loads('''$GHB''')
assert [p['aooEndpoint'] for p in hb['peers']]==['198.51.100.7:15000'], hb
assert g['aooEndpoint']=='203.0.113.10:14999' and g['hostLive'], g
print('OK plugin-peers')
PY
