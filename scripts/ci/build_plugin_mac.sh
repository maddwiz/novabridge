#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="${1:-$(pwd)}"
OUTPUT_ROOT="${2:-${REPO_ROOT}/artifacts-mac}"
UE_ROOT_OVERRIDE="${3:-}"

if [[ -n "${UE_ROOT_OVERRIDE}" ]]; then
  UE_ROOT="${UE_ROOT_OVERRIDE}"
elif [[ -n "${UE_ROOT:-}" ]]; then
  UE_ROOT="${UE_ROOT}"
else
  echo "ERROR: UE root not set. Pass arg3 or set UE_ROOT on the runner." >&2
  exit 2
fi

RUNUAT="${UE_ROOT}/Engine/Build/BatchFiles/RunUAT.sh"
PLUGIN_FILE="${REPO_ROOT}/NovaBridge/NovaBridge.uplugin"
PACKAGE_DIR="${OUTPUT_ROOT}/NovaBridge-Mac"
ZIP_PATH="${OUTPUT_ROOT}/NovaBridge-Mac.zip"
MANIFEST="${OUTPUT_ROOT}/manifest.txt"

[[ -x "${RUNUAT}" ]] || { echo "ERROR: RunUAT not found: ${RUNUAT}" >&2; exit 3; }
[[ -f "${PLUGIN_FILE}" ]] || { echo "ERROR: Plugin descriptor not found: ${PLUGIN_FILE}" >&2; exit 4; }

rm -rf "${PACKAGE_DIR}" "${ZIP_PATH}"
mkdir -p "${OUTPUT_ROOT}"

"${RUNUAT}" BuildPlugin \
  -Plugin="${PLUGIN_FILE}" \
  -Package="${PACKAGE_DIR}" \
  -TargetPlatforms=Mac \
  -Rocket

(
  cd "${OUTPUT_ROOT}"
  /usr/bin/zip -r "${ZIP_PATH}" "$(basename "${PACKAGE_DIR}")" >/dev/null
)

{
  echo "platform=Mac"
  echo "ue_root=${UE_ROOT}"
  echo "plugin=${PLUGIN_FILE}"
  echo "package_dir=${PACKAGE_DIR}"
  echo "zip=${ZIP_PATH}"
  date -u '+built_at_utc=%Y-%m-%dT%H:%M:%SZ'
} > "${MANIFEST}"

echo "Built: ${ZIP_PATH}"
