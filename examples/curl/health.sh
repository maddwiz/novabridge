#!/usr/bin/env bash
set -euo pipefail

PORT="${NOVABRIDGE_PORT:-30010}"
curl -s "http://localhost:${PORT}/nova/health" | jq .
