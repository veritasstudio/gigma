#!/usr/bin/env bash
# Produce a statically linked (where possible) Linux amd64 binary for the
# Debian VPS at 15.235.199.44.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

OUT="${OUT:-$ROOT/bin/gigma-linux-amd64}"
mkdir -p "$(dirname "$OUT")"

export CGO_ENABLED=0
export GOOS="${GOOS:-linux}"
export GOARCH="${GOARCH:-amd64}"

echo "Building $OUT (CGO_ENABLED=0 GOOS=$GOOS GOARCH=$GOARCH)…"
go build -trimpath -ldflags="-s -w" -o "$OUT" ./cmd/gigma

echo "OK: $OUT"
file "$OUT" 2>/dev/null || ls -lh "$OUT"
go version
