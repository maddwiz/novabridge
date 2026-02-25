#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="${1:-$(pwd)}"
UE_ROOT_INPUT="${2:-${UE_ROOT_MAC:-}}"
PROJECT_INPUT="${3:-${NOVABRIDGE_AUTOMATION_PROJECT:-${REPO_ROOT}/NovaBridgeDefault/NovaBridgeDefault.uproject}}"
ARTIFACT_DIR="${REPO_ROOT}/artifacts/automation-mac"
LOG_PATH="${ARTIFACT_DIR}/automation.log"
RUN_STDOUT_LOG="${ARTIFACT_DIR}/automation-run.stdout.log"
BUILD_LOG="${ARTIFACT_DIR}/automation-build.log"
RUN_BUILD="${NOVABRIDGE_AUTOMATION_BUILD:-1}"
USE_SANDBOX="${NOVABRIDGE_AUTOMATION_SANDBOX:-0}"
SANDBOX_DIR="${NOVABRIDGE_AUTOMATION_SANDBOX_DIR:-/tmp/novabridge-automation-sandbox}"

mkdir -p "${ARTIFACT_DIR}"

resolve_project() {
  local raw_path="$1"
  if [[ -f "${raw_path}" ]]; then
    echo "$(cd "$(dirname "${raw_path}")" && pwd)/$(basename "${raw_path}")"
    return
  fi
  if [[ -d "${raw_path}" ]]; then
    local first_project
    first_project="$(find "${raw_path}" -maxdepth 1 -name "*.uproject" | head -n 1)"
    if [[ -n "${first_project}" ]]; then
      echo "$(cd "$(dirname "${first_project}")" && pwd)/$(basename "${first_project}")"
      return
    fi
  fi
  echo ""
}

PROJECT="$(resolve_project "${PROJECT_INPUT}")"
if [[ -z "${PROJECT}" ]]; then
  echo "[automation] Could not resolve project from: ${PROJECT_INPUT}" >&2
  exit 1
fi

PROJECT_DIR="$(cd "$(dirname "${PROJECT}")" && pwd)"
if [[ "${USE_SANDBOX}" == "1" ]]; then
  PROJECT_NAME="$(basename "${PROJECT}")"
  mkdir -p "${SANDBOX_DIR}"
  rsync -a --delete \
    --exclude "Binaries" \
    --exclude "Intermediate" \
    --exclude "Saved" \
    --exclude "DerivedDataCache" \
    --exclude "Plugins/NovaBridge" \
    "${PROJECT_DIR}/" "${SANDBOX_DIR}/"
  PROJECT="${SANDBOX_DIR}/${PROJECT_NAME}"
  PROJECT_DIR="${SANDBOX_DIR}"
fi

PLUGIN_SRC="${REPO_ROOT}/NovaBridge"
PLUGIN_DST="${PROJECT_DIR}/Plugins/NovaBridge"
if [[ -d "${PLUGIN_SRC}" ]]; then
  mkdir -p "${PROJECT_DIR}/Plugins"
  rsync -a --delete \
    --exclude "Binaries" \
    --exclude "Intermediate" \
    --exclude "Saved" \
    "${PLUGIN_SRC}/" "${PLUGIN_DST}/"
fi

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

BUILD_SCRIPT="${UE_ROOT}/Engine/Build/BatchFiles/Mac/Build.sh"
if [[ "${RUN_BUILD}" == "1" ]]; then
  if [[ ! -x "${BUILD_SCRIPT}" ]]; then
    echo "[automation] Unreal build script not found: ${BUILD_SCRIPT}" >&2
    exit 1
  fi
  echo "[automation] Building project/editor targets before running tests..."
  set +e
  "${BUILD_SCRIPT}" UnrealEditor Mac Development -Project="${PROJECT}" -WaitMutex -NoHotReloadFromIDE \
    >"${BUILD_LOG}" 2>&1
  BUILD_EXIT=$?
  set -e
  if [[ ${BUILD_EXIT} -ne 0 ]]; then
    echo "[automation] Unreal build failed with code ${BUILD_EXIT}" >&2
    tail -n 120 "${BUILD_LOG}" || true
    exit ${BUILD_EXIT}
  fi
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
  -stdout -FullStdOutLogOutput \
  -log="${LOG_PATH}" >"${RUN_STDOUT_LOG}" 2>&1
EDITOR_EXIT=$?
set -e

if [[ ${EDITOR_EXIT} -ne 0 ]]; then
  echo "[automation] UnrealEditor exited with code ${EDITOR_EXIT}" >&2
  if [[ -f "${LOG_PATH}" ]]; then
    tail -n 80 "${LOG_PATH}" || true
  fi
  if [[ -f "${RUN_STDOUT_LOG}" ]]; then
    echo "[automation] Tail of run stdout log:" >&2
    tail -n 120 "${RUN_STDOUT_LOG}" || true
  fi
  exit ${EDITOR_EXIT}
fi

if rg -n "(Automation Test Failed|Errors: [1-9]|\bFail\b)" "${LOG_PATH}" >/dev/null 2>&1; then
  echo "[automation] Detected automation test failure markers in log ${LOG_PATH}" >&2
  tail -n 80 "${LOG_PATH}" || true
  exit 1
fi

echo "[automation] NovaBridge core automation tests passed. Log: ${LOG_PATH}"
