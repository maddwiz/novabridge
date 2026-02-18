#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UE_EDITOR_BIN="${UE_EDITOR_BIN:-$(command -v UnrealEditor || true)}"
PROJECT_FILE="${PROJECT_FILE:-${ROOT_DIR}/NovaBridgeDefault/NovaBridgeDefault.uproject}"
SERVICE_NAME="${SERVICE_NAME:-nova-ue5-editor.service}"
SERVICE_PATH="${HOME}/.config/systemd/user/${SERVICE_NAME}"
PORT="${NOVABRIDGE_PORT:-30010}"

if [[ -z "${UE_EDITOR_BIN}" || ! -x "${UE_EDITOR_BIN}" ]]; then
  echo "Could not locate UnrealEditor binary."
  echo "Set UE_EDITOR_BIN to your UE editor executable path."
  exit 1
fi

mkdir -p "${HOME}/.config/systemd/user"

cat > "${SERVICE_PATH}" <<SERVICE
[Unit]
Description=UE5 Editor with NovaBridge (Vulkan GPU Rendering)
After=network.target

[Service]
Type=simple
WorkingDirectory=$(dirname "${UE_EDITOR_BIN}")
LimitSTACK=infinity
ExecStart=${UE_EDITOR_BIN} ${PROJECT_FILE} -vulkan -RenderOffScreen -nosplash -nosound -unattended -nopause -NovaBridgePort=${PORT}
Restart=on-failure
RestartSec=30
StandardOutput=append:${HOME}/ue5-editor.log
StandardError=append:${HOME}/ue5-editor.log
Environment=HOME=${HOME}
Environment=DISPLAY=:1
Environment=VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json
Environment=UE_USE_SYSTEM_DOTNET=1
Environment=DOTNET_ROLL_FORWARD=LatestMajor
TimeoutStartSec=300
OOMScoreAdjust=-500

[Install]
WantedBy=default.target
SERVICE

systemctl --user daemon-reload
systemctl --user enable "${SERVICE_NAME}"
systemctl --user restart "${SERVICE_NAME}"

echo "Installed and started ${SERVICE_NAME}"
echo "Service file: ${SERVICE_PATH}"
