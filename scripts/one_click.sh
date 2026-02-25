#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${NOVABRIDGE_ENV_FILE:-${ROOT_DIR}/novabridge.env}"
PROJECT_INPUT="${NOVABRIDGE_PROJECT:-${1:-${ROOT_DIR}/NovaBridgeDefault/NovaBridgeDefault.uproject}}"
PORT="${NOVABRIDGE_PORT:-30010}"
ASSISTANT_PORT="${NOVABRIDGE_ASSISTANT_PORT:-30016}"
SKIP_ASSISTANT="${NOVABRIDGE_SKIP_ASSISTANT:-0}"
OPEN_STUDIO="${NOVABRIDGE_OPEN_STUDIO:-1}"

if [[ -f "$ENV_FILE" ]]; then
  echo "[one-click] Loading env from ${ENV_FILE}"
  set -a
  # shellcheck disable=SC1090
  source "$ENV_FILE"
  set +a
fi

PROJECT_INPUT="${NOVABRIDGE_PROJECT:-$PROJECT_INPUT}"
PORT="${NOVABRIDGE_PORT:-$PORT}"
ASSISTANT_PORT="${NOVABRIDGE_ASSISTANT_PORT:-$ASSISTANT_PORT}"
SKIP_ASSISTANT="${NOVABRIDGE_SKIP_ASSISTANT:-$SKIP_ASSISTANT}"
OPEN_STUDIO="${NOVABRIDGE_OPEN_STUDIO:-$OPEN_STUDIO}"

echo "[one-click] Bootstrapping NovaBridge plugin + UE project..."
NOVABRIDGE_PORT="$PORT" "${ROOT_DIR}/scripts/setup.sh" "$PROJECT_INPUT"

if [[ "$SKIP_ASSISTANT" != "1" ]]; then
  if command -v node >/dev/null 2>&1; then
    if pgrep -f "assistant-server/server.js" >/dev/null 2>&1; then
      echo "[one-click] assistant-server already running."
    else
      echo "[one-click] Starting assistant-server on port ${ASSISTANT_PORT}..."
      nohup node "${ROOT_DIR}/assistant-server/server.js" >/tmp/novabridge-assistant.log 2>&1 &
    fi
  else
    echo "[one-click] Node.js not found; skipping assistant-server."
  fi
fi

if [[ "$OPEN_STUDIO" == "1" ]]; then
  STUDIO_URL="http://127.0.0.1:${ASSISTANT_PORT}/nova/studio"
  echo "[one-click] Opening Studio: ${STUDIO_URL}"
  if command -v open >/dev/null 2>&1; then
    open "$STUDIO_URL" >/dev/null 2>&1 || true
  elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$STUDIO_URL" >/dev/null 2>&1 || true
  fi
fi

echo "[one-click] Done."
