#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/macos-arm64"
VCPKG_DIR="${VCPKG_DIR:-${ROOT_DIR}/build/vcpkg}"
VCPKG_INSTALLED_DIR="${VCPKG_INSTALLED_DIR:-${ROOT_DIR}/build/vcpkg_installed}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
APP="${BUILD_DIR}/bin/JPet/JPet.app/Contents/MacOS/JPet"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script must be run on macOS." >&2
  exit 1
fi

if [[ "$(uname -m)" != "arm64" ]]; then
  echo "This project supports macOS arm64 only." >&2
  exit 1
fi

if ! command -v node >/dev/null || ! command -v npm >/dev/null; then
  echo "Node.js and npm are required." >&2
  exit 1
fi

if [[ ! -x "${VCPKG_DIR}/vcpkg" ]]; then
  cat >&2 <<EOF
vcpkg was not found at ${VCPKG_DIR}.
Install it first, for example:
  git clone https://github.com/microsoft/vcpkg.git ${VCPKG_DIR}
  ${VCPKG_DIR}/bootstrap-vcpkg.sh -disableMetrics
EOF
  exit 1
fi

if [[ ! -d "${ROOT_DIR}/resources/panel/node_modules" ]]; then
  echo "Installing panel dependencies..."
  (cd "${ROOT_DIR}/resources/panel" && npm ci --legacy-peer-deps --no-audit --no-fund)
fi

echo "Building settings panel..."
(cd "${ROOT_DIR}/resources/panel" && npm run build)

if [[ ! -d "${VCPKG_INSTALLED_DIR}/arm64-osx" ]]; then
  echo "Installing C++ dependencies..."
  "${VCPKG_DIR}/vcpkg" install \
    --triplet arm64-osx \
    --x-install-root="${VCPKG_INSTALLED_DIR}" \
    --disable-metrics
fi

echo "Configuring JPet (${BUILD_TYPE}, arm64)..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx \
  -DVCPKG_INSTALLED_DIR="${VCPKG_INSTALLED_DIR}" \
  -DVCPKG_MANIFEST_INSTALL=OFF

echo "Building JPet..."
cmake --build "${BUILD_DIR}" --parallel

echo "Starting JPet..."
exec "${APP}"
