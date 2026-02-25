#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_INPUT="${NOVABRIDGE_PROJECT:-${1:-${ROOT_DIR}/NovaBridgeDefault/NovaBridgeDefault.uproject}}"
PORT="${NOVABRIDGE_PORT:-30010}"
LAUNCH="${NOVABRIDGE_LAUNCH:-1}"

resolve_uproject() {
  local input="$1"
  if [[ -f "$input" && "$input" == *.uproject ]]; then
    echo "$input"
    return
  fi
  if [[ -d "$input" ]]; then
    local first
    first="$(find "$input" -maxdepth 1 -name '*.uproject' | head -n 1)"
    if [[ -n "$first" ]]; then
      echo "$first"
      return
    fi
  fi
  echo ""
}

UPROJECT="$(resolve_uproject "$PROJECT_INPUT")"
if [[ -z "$UPROJECT" ]]; then
  echo "[setup] Could not resolve .uproject from: $PROJECT_INPUT" >&2
  exit 2
fi

PROJECT_DIR="$(cd "$(dirname "$UPROJECT")" && pwd)"
PLUGIN_SRC="${ROOT_DIR}/NovaBridge"
PLUGIN_DST="${PROJECT_DIR}/Plugins/NovaBridge"

mkdir -p "${PROJECT_DIR}/Plugins"
rsync -a --delete \
  --exclude 'Binaries' \
  --exclude 'Intermediate' \
  --exclude 'Saved' \
  "${PLUGIN_SRC}/" "${PLUGIN_DST}/"

echo "[setup] Copied plugin to ${PLUGIN_DST}"

echo "[setup] Project: ${UPROJECT}"

echo "[setup] Suggested launch command:"
cat <<CMD
UnrealEditor "${UPROJECT}" -RenderOffScreen -nosplash -unattended -nopause -NovaBridgePort=${PORT}
CMD

if [[ "$LAUNCH" == "0" ]]; then
  echo "[setup] Skipping auto-launch (NOVABRIDGE_LAUNCH=0)."
  exit 0
fi

EDITOR_BIN=""
if [[ -n "${UE_EDITOR_BIN:-}" && -x "${UE_EDITOR_BIN}" ]]; then
  EDITOR_BIN="${UE_EDITOR_BIN}"
elif [[ -n "${UE_ROOT:-}" && -x "${UE_ROOT}/Engine/Binaries/Mac/UnrealEditor" ]]; then
  EDITOR_BIN="${UE_ROOT}/Engine/Binaries/Mac/UnrealEditor"
elif [[ -x "/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor" ]]; then
  EDITOR_BIN="/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor"
elif [[ -x "/Users/Shared/Epic Games/UE_5.6/Engine/Binaries/Mac/UnrealEditor" ]]; then
  EDITOR_BIN="/Users/Shared/Epic Games/UE_5.6/Engine/Binaries/Mac/UnrealEditor"
fi

if [[ -z "$EDITOR_BIN" ]]; then
  echo "[setup] UnrealEditor binary not found; plugin copied and project is ready." >&2
  exit 0
fi

nohup "$EDITOR_BIN" "$UPROJECT" -RenderOffScreen -nosplash -unattended -nopause -NovaBridgePort="$PORT" >/tmp/novabridge-setup-launch.log 2>&1 &

echo "[setup] UnrealEditor launched in background with NovaBridge enabled."
