#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-v1.0.0}"
DIST_DIR="${ROOT_DIR}/dist"
PKG_NAME="NovaBridge-${VERSION}"
PKG_DIR="${DIST_DIR}/${PKG_NAME}"
ZIP_PATH="${DIST_DIR}/${PKG_NAME}.zip"

rm -rf "${PKG_DIR}" "${ZIP_PATH}"
mkdir -p "${PKG_DIR}" "${DIST_DIR}"

copy_tree() {
  local src="$1"
  local dst="$2"
  rsync -a \
    --exclude '.git' \
    --exclude 'node_modules' \
    --exclude '__pycache__' \
    --exclude '*.pyc' \
    --exclude '*.log' \
    --exclude 'Binaries' \
    --exclude 'Intermediate' \
    --exclude 'Saved' \
    --exclude 'Content/Developers' \
    --exclude 'Content/Collections' \
    --exclude 'DerivedDataCache' \
    "${src}" "${dst}"
}

copy_tree "${ROOT_DIR}/NovaBridge" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/NovaBridgeDemo" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/NovaBridgeDefault" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/python-sdk" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/mcp-server" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/blender" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/extensions" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/examples" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/docs" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/scripts" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/demo" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/site" "${PKG_DIR}/"

cp "${ROOT_DIR}/README.md" "${PKG_DIR}/README.md"
cp "${ROOT_DIR}/QUICK_START.md" "${PKG_DIR}/QUICK_START.md"
cp "${ROOT_DIR}/.gitignore" "${PKG_DIR}/.gitignore.example"
if [[ -f "${ROOT_DIR}/LICENSE" ]]; then
  cp "${ROOT_DIR}/LICENSE" "${PKG_DIR}/LICENSE"
fi

(
  cd "${DIST_DIR}"
  zip -r "${ZIP_PATH}" "${PKG_NAME}" >/dev/null
)

echo "Created package: ${ZIP_PATH}"
