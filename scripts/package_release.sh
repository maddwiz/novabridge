#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION_RAW="${1:-1.0.1}"
VERSION="${VERSION_RAW#v}"
VERSION_TAG="v${VERSION}"
DIST_DIR="${ROOT_DIR}/dist"
PKG_NAME="NovaBridge-${VERSION_TAG}"
PKG_DIR="${DIST_DIR}/${PKG_NAME}"
ZIP_PATH="${DIST_DIR}/${PKG_NAME}.zip"

BUILD_WHEEL="${NOVABRIDGE_BUILD_WHEEL:-1}"
BUILD_DOCKER="${NOVABRIDGE_BUILD_DOCKER:-0}"
DOCKER_IMAGE="${NOVABRIDGE_DOCKER_IMAGE:-ghcr.io/maddwiz/novabridge}"

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
    --exclude 'artifacts' \
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
copy_tree "${ROOT_DIR}/scripts" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/demo" "${PKG_DIR}/"
copy_tree "${ROOT_DIR}/site" "${PKG_DIR}/"

mkdir -p "${PKG_DIR}/docs"
for doc in API.md SETUP_LINUX.md SETUP_MAC.md SETUP_WINDOWS.md SMOKE_TEST_CHECKLIST.md MACOS_SMOKE_TEST.md; do
  if [[ -f "${ROOT_DIR}/docs/${doc}" ]]; then
    cp "${ROOT_DIR}/docs/${doc}" "${PKG_DIR}/docs/${doc}"
  fi
done

cp "${ROOT_DIR}/README.md" "${PKG_DIR}/README.md"
cp "${ROOT_DIR}/QUICK_START.md" "${PKG_DIR}/QUICK_START.md"
cp "${ROOT_DIR}/.gitignore" "${PKG_DIR}/.gitignore.example"
for root_doc in INSTALL.md BuyerGuide.md CHANGELOG.md SUPPORT.md EULA.txt; do
  if [[ -f "${ROOT_DIR}/${root_doc}" ]]; then
    cp "${ROOT_DIR}/${root_doc}" "${PKG_DIR}/${root_doc}"
  fi
done
if [[ -f "${ROOT_DIR}/LICENSE" ]]; then
  cp "${ROOT_DIR}/LICENSE" "${PKG_DIR}/LICENSE"
fi

(
  cd "${DIST_DIR}"
  zip -r "${ZIP_PATH}" "${PKG_NAME}" >/dev/null
)

echo "Created package: ${ZIP_PATH}"

if [[ "${BUILD_WHEEL}" != "0" ]]; then
  if python3 -m build --help >/dev/null 2>&1; then
    mkdir -p "${DIST_DIR}/python-sdk"
    python3 -m build --wheel --outdir "${DIST_DIR}/python-sdk" "${ROOT_DIR}/python-sdk"
    echo "Created python wheel(s): ${DIST_DIR}/python-sdk"
  else
    echo "Skipping python wheel build (python3 -m build not available)." >&2
  fi
fi

if [[ "${BUILD_DOCKER}" != "0" ]]; then
  if ! command -v docker >/dev/null 2>&1; then
    echo "Docker build requested but docker is not installed." >&2
    exit 2
  fi

  docker build \
    -t "${DOCKER_IMAGE}:${VERSION}" \
    -t "${DOCKER_IMAGE}:latest" \
    "${ROOT_DIR}"
  echo "Built docker images: ${DOCKER_IMAGE}:${VERSION}, ${DOCKER_IMAGE}:latest"
fi
