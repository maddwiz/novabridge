#!/usr/bin/env bash
set -euo pipefail

PORT="${NOVABRIDGE_PORT:-30010}"
HOST="${NOVABRIDGE_HOST:-127.0.0.1}"
BASE="http://${HOST}:${PORT}/nova"
API_KEY="${NOVABRIDGE_API_KEY:-}"

HDR_ARGS=()
if [[ -n "${API_KEY}" ]]; then
  HDR_ARGS+=(-H "X-API-Key: ${API_KEY}")
fi

tmp_obj="$(mktemp /tmp/novabridge-golden-XXXXXX.obj)"
cat > "${tmp_obj}" <<'OBJ'
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
OBJ

echo "[1/6] health"
curl -fsS "${HDR_ARGS[@]}" "${BASE}/health" | jq .

echo "[2/6] spawn"
curl -fsS "${HDR_ARGS[@]}" -X POST "${BASE}/scene/spawn" \
  -H "Content-Type: application/json" \
  -d '{"class":"PointLight","label":"GoldenPathLight","x":0,"y":0,"z":250}' | jq .

echo "[3/6] import obj"
curl -fsS "${HDR_ARGS[@]}" -X POST "${BASE}/asset/import" \
  -H "Content-Type: application/json" \
  -d "{\"file_path\":\"${tmp_obj}\",\"asset_name\":\"GoldenPathMesh\",\"destination\":\"/Game\",\"scale\":100}" | jq .

echo "[4/6] screenshot"
curl -fsS "${HDR_ARGS[@]}" "${BASE}/viewport/screenshot?format=raw&width=960&height=540" -o /tmp/novabridge-golden.png
file /tmp/novabridge-golden.png

echo "[5/6] cleanup actor"
curl -fsS "${HDR_ARGS[@]}" -X POST "${BASE}/scene/delete" \
  -H "Content-Type: application/json" \
  -d '{"name":"GoldenPathLight"}' | jq .

echo "[6/6] done"
echo "Screenshot: /tmp/novabridge-golden.png"

