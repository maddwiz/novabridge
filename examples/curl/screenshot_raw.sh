#!/usr/bin/env bash
set -euo pipefail

PORT="${NOVABRIDGE_PORT:-30010}"
OUT="${1:-viewport.png}"

curl -s "http://localhost:${PORT}/nova/viewport/screenshot?format=raw&width=1280&height=720" -o "${OUT}"
echo "Saved ${OUT}"
