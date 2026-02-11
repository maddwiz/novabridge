#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./scripts/ci/setup_runner_mac.sh \
#     --repo-url https://github.com/OWNER/REPO \
#     --token <REGISTRATION_TOKEN> \
#     --ue-root /Users/you/UnrealEngine \
#     [--runner-name my-mac-runner] \
#     [--labels self-hosted,macOS,ARM64,unreal]

REPO_URL=""
RUNNER_TOKEN=""
UE_ROOT=""
RUNNER_NAME="$(scutil --get ComputerName 2>/dev/null || hostname)"
RUNNER_LABELS="self-hosted,macOS,ARM64,unreal"
RUNNER_VERSION="2.327.1"
RUNNER_DIR="${HOME}/actions-runner-novabridge"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo-url) REPO_URL="$2"; shift 2 ;;
    --token) RUNNER_TOKEN="$2"; shift 2 ;;
    --ue-root) UE_ROOT="$2"; shift 2 ;;
    --runner-name) RUNNER_NAME="$2"; shift 2 ;;
    --labels) RUNNER_LABELS="$2"; shift 2 ;;
    *) echo "Unknown arg: $1" >&2; exit 64 ;;
  esac
done

[[ -n "$REPO_URL" ]] || { echo "Missing --repo-url" >&2; exit 2; }
[[ -n "$RUNNER_TOKEN" ]] || { echo "Missing --token" >&2; exit 2; }
[[ -n "$UE_ROOT" ]] || { echo "Missing --ue-root" >&2; exit 2; }

mkdir -p "$RUNNER_DIR"
cd "$RUNNER_DIR"

TAR_NAME="actions-runner-osx-arm64-${RUNNER_VERSION}.tar.gz"
if [[ ! -f "$TAR_NAME" ]]; then
  curl -fL -o "$TAR_NAME" "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/${TAR_NAME}"
fi

if [[ ! -x "./run.sh" ]]; then
  tar xzf "$TAR_NAME"
fi

if [[ -f ".runner" ]]; then
  echo "Runner already configured in $RUNNER_DIR"
else
  ./config.sh \
    --unattended \
    --url "$REPO_URL" \
    --token "$RUNNER_TOKEN" \
    --name "$RUNNER_NAME" \
    --labels "$RUNNER_LABELS" \
    --replace
fi

# Persist UE_ROOT via launchctl and runner-local env file.
launchctl setenv UE_ROOT "$UE_ROOT" || true
echo "UE_ROOT=$UE_ROOT" > .env

# Install and start as LaunchAgent service.
./svc.sh install
./svc.sh start

echo "Runner configured and started."
echo "Runner dir: $RUNNER_DIR"
echo "UE_ROOT: $UE_ROOT"
