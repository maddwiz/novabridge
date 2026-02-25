#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="${1:-$(pwd)}"
UE_ROOT_INPUT="${2:-${UE_ROOT_MAC:-}}"
PROJECT="${REPO_ROOT}/NovaBridgeDefault/NovaBridgeDefault.uproject"
ARTIFACT_DIR="${REPO_ROOT}/artifacts/automation-mac"
LOG_PATH="${ARTIFACT_DIR}/automation.log"

mkdir -p "${ARTIFACT_DIR}"

resolve_ue_root() {
  if [[ -n "${UE_ROOT_INPUT}" ]]; then
    echo "${UE_ROOT_INPUT}"
    return
  fi

  local candidates=(
    "/Users/Shared/Epic Games/UE_5.7"
    "/Users/Shared/Epic Games/UE_5.6"
    "/Applications/UE_5.7"
    "/Applications/UE_5.6"
  )

  for candidate in "${candidates[@]}"; do
    if [[ -d "${candidate}" ]]; then
      echo "${candidate}"
      return
    fi
  done

  echo ""
}

UE_ROOT="$(resolve_ue_root)"
if [[ -z "${UE_ROOT}" ]]; then
  echo "[automation] UE root not found. Provide path as arg2 or UE_ROOT_MAC env." >&2
  exit 1
fi

if [[ ! -f "${PROJECT}" ]]; then
  echo "[automation] Missing project: ${PROJECT}" >&2
  exit 1
fi

EDITOR_BIN="${UE_ROOT}/Engine/Binaries/Mac/UnrealEditor-Cmd"
if [[ ! -x "${EDITOR_BIN}" ]]; then
  EDITOR_BIN="${UE_ROOT}/Engine/Binaries/Mac/UnrealEditor"
fi
if [[ ! -x "${EDITOR_BIN}" ]]; then
  echo "[automation] UnrealEditor executable not found under ${UE_ROOT}" >&2
  exit 1
fi

set +e
"${EDITOR_BIN}" "${PROJECT}" \
  -ExecCmds="Automation RunTests NovaBridge.Core; Quit" \
  -unattended -nop4 -nosplash -NoSound -NullRHI \
  -log="${LOG_PATH}" >/dev/null 2>&1
EDITOR_EXIT=$?
set -e

if [[ ${EDITOR_EXIT} -ne 0 ]]; then
  echo "[automation] UnrealEditor exited with code ${EDITOR_EXIT}" >&2
  tail -n 80 "${LOG_PATH}" || true
  exit ${EDITOR_EXIT}
fi

if rg -n "(Automation Test Failed|Errors: [1-9]|\bFail\b)" "${LOG_PATH}" >/dev/null 2>&1; then
  echo "[automation] Detected automation test failure markers in log ${LOG_PATH}" >&2
  tail -n 80 "${LOG_PATH}" || true
  exit 1
fi

echo "[automation] NovaBridge core automation tests passed. Log: ${LOG_PATH}"
