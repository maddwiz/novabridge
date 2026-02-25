#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)"

SMOKE_ROOT="${NOVABRIDGE_SMOKE_ROOT:-/tmp/novabridge-smoke-${RUN_ID}}"
ARTIFACT_DIR="${NOVABRIDGE_ARTIFACT_DIR:-${SMOKE_ROOT}/artifacts/executeplan-smoke}"
PROJECT_PATH="${NOVABRIDGE_PROJECT:-${ROOT_DIR}/NovaBridgeDefault/NovaBridgeDefault.uproject}"

SYNC_PLUGIN="${NOVABRIDGE_SYNC_PLUGIN:-1}"
RUN_BUILD="${NOVABRIDGE_BUILD:-1}"

UE_BUILD_SCRIPT="${UE_MAC_BUILD_SCRIPT:-/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh}"
UE_EDITOR_BIN="${UE_EDITOR_BIN:-/Users/Shared/Epic Games/UE_5.6/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"

EDITOR_PORT="${NOVABRIDGE_EDITOR_PORT:-31210}"
EDITOR_EVENTS_PORT="${NOVABRIDGE_EDITOR_EVENTS_PORT:-31212}"
RUNTIME_PORT="${NOVABRIDGE_RUNTIME_PORT:-31220}"
RUNTIME_EVENTS_PORT="${NOVABRIDGE_RUNTIME_EVENTS_PORT:-31222}"

EDITOR_HEALTH_URL="http://127.0.0.1:${EDITOR_PORT}/nova/health"
EDITOR_EXECUTE_URL="http://127.0.0.1:${EDITOR_PORT}/nova/executePlan"
RUNTIME_PAIR_URL="http://127.0.0.1:${RUNTIME_PORT}/nova/runtime/pair"
RUNTIME_EXECUTE_URL="http://127.0.0.1:${RUNTIME_PORT}/nova/executePlan"

EDITOR_LOG="${ARTIFACT_DIR}/editor.log"
EDITOR_HEALTH_JSON="${ARTIFACT_DIR}/editor-health.json"
EDITOR_EXECUTE_JSON="${ARTIFACT_DIR}/editor-execute.json"
EDITOR_SUMMARY_JSON="${ARTIFACT_DIR}/editor-execute-summary.json"
RUNTIME_LOG="${ARTIFACT_DIR}/runtime.log"
RUNTIME_PAIR_JSON="${ARTIFACT_DIR}/runtime-pair.json"
RUNTIME_EXECUTE_JSON="${ARTIFACT_DIR}/runtime-execute.json"
RUNTIME_SUMMARY_JSON="${ARTIFACT_DIR}/runtime-execute-summary.json"
RUN_SUMMARY_JSON="${ARTIFACT_DIR}/run-summary.json"
BUILD_LOG="${ARTIFACT_DIR}/build.log"

EDITOR_PID=""
RUNTIME_PID=""

log() {
	printf '[mac-executeplan-smoke] %s\n' "$1"
}

require_cmd() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing required command: $1" >&2
		exit 1
	fi
}

cleanup_pid() {
	local pid="$1"
	if [[ -z "${pid}" ]]; then
		return
	fi
	if kill -0 "${pid}" 2>/dev/null; then
		kill "${pid}" 2>/dev/null || true
		for _ in $(seq 1 20); do
			if ! kill -0 "${pid}" 2>/dev/null; then
				break
			fi
			sleep 0.1
		done
		if kill -0 "${pid}" 2>/dev/null; then
			kill -9 "${pid}" 2>/dev/null || true
		fi
	fi
	wait "${pid}" 2>/dev/null || true
}

cleanup() {
	cleanup_pid "${EDITOR_PID}"
	cleanup_pid "${RUNTIME_PID}"
}
trap cleanup EXIT

wait_for_health() {
	local url="$1"
	local out_json="$2"
	local pid="$3"
	local attempts="${4:-180}"
	local ready=0

	for _ in $(seq 1 "${attempts}"); do
		if ! kill -0 "${pid}" 2>/dev/null; then
			return 2
		fi
		if curl -fsS "${url}" > "${out_json}" 2>/dev/null; then
			ready=1
			break
		fi
		sleep 1
	done

	if [[ "${ready}" -ne 1 ]]; then
		return 1
	fi
	return 0
}

require_cmd curl
require_cmd jq
require_cmd rsync
require_cmd sed

if [[ ! -f "${PROJECT_PATH}" ]]; then
	echo "Project not found: ${PROJECT_PATH}" >&2
	exit 1
fi

mkdir -p "${ARTIFACT_DIR}"

if [[ "${SYNC_PLUGIN}" == "1" ]]; then
	PLUGIN_SRC="${ROOT_DIR}/NovaBridge/"
	PROJECT_DIR="$(cd "$(dirname "${PROJECT_PATH}")" && pwd)"
	PLUGIN_DST="${PROJECT_DIR}/Plugins/NovaBridge/"
	if [[ ! -d "${PLUGIN_SRC}" ]]; then
		echo "Plugin source not found: ${PLUGIN_SRC}" >&2
		exit 1
	fi
	log "Syncing plugin into project: ${PLUGIN_DST}"
	mkdir -p "$(dirname "${PLUGIN_DST}")"
	rsync -a --delete "${PLUGIN_SRC}" "${PLUGIN_DST}"
fi

if [[ "${RUN_BUILD}" == "1" ]]; then
	if [[ ! -x "${UE_BUILD_SCRIPT}" ]]; then
		echo "UE build script not executable: ${UE_BUILD_SCRIPT}" >&2
		exit 1
	fi
	log "Building project (UnrealEditor Mac Development)"
	"${UE_BUILD_SCRIPT}" UnrealEditor Mac Development -Project="${PROJECT_PATH}" -WaitMutex -NoHotReloadFromIDE | tee "${BUILD_LOG}"
fi

if [[ ! -x "${UE_EDITOR_BIN}" ]]; then
	echo "UE editor binary not executable: ${UE_EDITOR_BIN}" >&2
	exit 1
fi

EDITOR_LABEL="ExecutePlanEditorSmoke_${RUN_ID}"
RUNTIME_LABEL="ExecutePlanRuntimeSmoke_${RUN_ID}"

EDITOR_PLAN_JSON="${ARTIFACT_DIR}/editor-plan.json"
RUNTIME_PLAN_JSON="${ARTIFACT_DIR}/runtime-plan.json"
PAIR_REQUEST_JSON="${ARTIFACT_DIR}/runtime-pair-request.json"

cat > "${EDITOR_PLAN_JSON}" <<EOF
{
  "plan_id": "executeplan-editor-${RUN_ID}",
  "steps": [
    {
      "action": "spawn",
      "params": {
        "type": "PointLight",
        "label": "${EDITOR_LABEL}",
        "transform": { "location": [0, 25, 310] }
      }
    },
    {
      "action": "delete",
      "params": { "name": "${EDITOR_LABEL}" }
    }
  ]
}
EOF

cat > "${RUNTIME_PLAN_JSON}" <<EOF
{
  "plan_id": "executeplan-runtime-${RUN_ID}",
  "steps": [
    {
      "action": "spawn",
      "params": {
        "type": "PointLight",
        "label": "${RUNTIME_LABEL}",
        "transform": { "location": [20, 25, 310] }
      }
    },
    {
      "action": "delete",
      "params": { "name": "${RUNTIME_LABEL}" }
    }
  ]
}
EOF

log "Launching editor smoke (port ${EDITOR_PORT})"
"${UE_EDITOR_BIN}" "${PROJECT_PATH}" \
	-RenderOffScreen -nosplash -nosound -unattended -nopause \
	-NovaBridgePort="${EDITOR_PORT}" \
	-NovaBridgeEventsPort="${EDITOR_EVENTS_PORT}" \
	-stdout -FullStdOutLogOutput -log > "${EDITOR_LOG}" 2>&1 &
EDITOR_PID="$!"

if ! wait_for_health "${EDITOR_HEALTH_URL}" "${EDITOR_HEALTH_JSON}" "${EDITOR_PID}" 240; then
	echo "Editor health check failed: ${EDITOR_HEALTH_URL}" >&2
	tail -n 120 "${EDITOR_LOG}" >&2 || true
	exit 1
fi

curl -fsS -X POST "${EDITOR_EXECUTE_URL}" \
	-H "Content-Type: application/json" \
	--data @"${EDITOR_PLAN_JSON}" > "${EDITOR_EXECUTE_JSON}"

jq '{status, plan_id, step_count, success_count, error_count}' "${EDITOR_EXECUTE_JSON}" > "${EDITOR_SUMMARY_JSON}"
jq -e '.status == "ok" and .success_count == 2 and .error_count == 0' "${EDITOR_SUMMARY_JSON}" >/dev/null

cleanup_pid "${EDITOR_PID}"
EDITOR_PID=""

log "Launching runtime smoke (port ${RUNTIME_PORT})"
"${UE_EDITOR_BIN}" "${PROJECT_PATH}" \
	-game -RenderOffScreen -nosplash -nosound -unattended -nopause \
	-NovaBridgeRuntime=1 \
	-NovaBridgeRuntimePort="${RUNTIME_PORT}" \
	-NovaBridgeRuntimeEventsPort="${RUNTIME_EVENTS_PORT}" \
	-stdout -FullStdOutLogOutput -log > "${RUNTIME_LOG}" 2>&1 &
RUNTIME_PID="$!"

PAIR_CODE=""
for _ in $(seq 1 240); do
	PAIR_CODE="$(sed -nE 's/.*Runtime pairing code: ([0-9]{6}).*/\1/p' "${RUNTIME_LOG}" | tail -n 1)"
	if [[ -n "${PAIR_CODE}" ]]; then
		break
	fi
	sleep 1
done

if [[ -z "${PAIR_CODE}" ]]; then
	echo "Runtime pairing code not found in ${RUNTIME_LOG}" >&2
	tail -n 120 "${RUNTIME_LOG}" >&2 || true
	exit 1
fi

cat > "${PAIR_REQUEST_JSON}" <<EOF
{ "code": "${PAIR_CODE}" }
EOF

PAIR_OK=0
for _ in $(seq 1 60); do
	if curl -fsS -X POST "${RUNTIME_PAIR_URL}" \
		-H "Content-Type: application/json" \
		--data @"${PAIR_REQUEST_JSON}" > "${RUNTIME_PAIR_JSON}"; then
		PAIR_OK=1
		break
	fi
	sleep 1
done

if [[ "${PAIR_OK}" -ne 1 ]]; then
	echo "Runtime pairing request failed: ${RUNTIME_PAIR_URL}" >&2
	tail -n 120 "${RUNTIME_LOG}" >&2 || true
	exit 1
fi

RUNTIME_TOKEN="$(jq -r '.token // empty' "${RUNTIME_PAIR_JSON}")"
if [[ -z "${RUNTIME_TOKEN}" ]]; then
	echo "Runtime pair response missing token: ${RUNTIME_PAIR_JSON}" >&2
	cat "${RUNTIME_PAIR_JSON}" >&2
	exit 1
fi

curl -fsS -X POST "${RUNTIME_EXECUTE_URL}" \
	-H "Content-Type: application/json" \
	-H "X-NovaBridge-Token: ${RUNTIME_TOKEN}" \
	--data @"${RUNTIME_PLAN_JSON}" > "${RUNTIME_EXECUTE_JSON}"

jq '{status, plan_id, step_count, success_count, error_count}' "${RUNTIME_EXECUTE_JSON}" > "${RUNTIME_SUMMARY_JSON}"
jq -e '.status == "ok" and .success_count == 2 and .error_count == 0' "${RUNTIME_SUMMARY_JSON}" >/dev/null

cleanup_pid "${RUNTIME_PID}"
RUNTIME_PID=""

jq -n \
	--arg status "ok" \
	--arg timestamp_utc "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
	--arg project "${PROJECT_PATH}" \
	--arg artifact_dir "${ARTIFACT_DIR}" \
	--arg editor_port "${EDITOR_PORT}" \
	--arg runtime_port "${RUNTIME_PORT}" \
	--arg editor_events_port "${EDITOR_EVENTS_PORT}" \
	--arg runtime_events_port "${RUNTIME_EVENTS_PORT}" \
	--slurpfile editor "${EDITOR_SUMMARY_JSON}" \
	--slurpfile runtime "${RUNTIME_SUMMARY_JSON}" \
	'{
		status: $status,
		timestamp_utc: $timestamp_utc,
		project: $project,
		artifact_dir: $artifact_dir,
		editor: {
			port: ($editor_port | tonumber),
			events_port: ($editor_events_port | tonumber),
			summary: $editor[0]
		},
		runtime: {
			port: ($runtime_port | tonumber),
			events_port: ($runtime_events_port | tonumber),
			summary: $runtime[0]
		}
	}' > "${RUN_SUMMARY_JSON}"

log "Smoke run completed successfully."
log "Artifacts: ${ARTIFACT_DIR}"
cat "${RUN_SUMMARY_JSON}"
