#!/usr/bin/env bash
set -euo pipefail
CLOUD_TEST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLOUD_TEST_DEPS="${VCPKG_INSTALLED_DIR:-${CLOUD_TEST_ROOT}/build/vcpkg_installed}/arm64-osx"
CLOUD_TEST_DIR="$(mktemp -d /tmp/jpet-cloud-tests.XXXXXX)"
trap 'rm -rf "${CLOUD_TEST_DIR}"' EXIT
CLOUD_TEST_PORT="$(python3 - <<'PY'
import socket
with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0))
    print(listener.getsockname()[1])
PY
)"
cmake "-DJPET_CLOUD_URL=http://127.0.0.1:${CLOUD_TEST_PORT}" \
  "-DJPET_CLOUD_HEADER=${CLOUD_TEST_DIR}/JPetCloudConfig.hpp" \
  -P "${CLOUD_TEST_ROOT}/cmake/CloudConfig.cmake"
c++ -std=c++17 -DCPPHTTPLIB_OPENSSL_SUPPORT -Wno-deprecated-declarations -Wno-deprecated-literal-operator \
  -I"${CLOUD_TEST_DIR}" -I"${CLOUD_TEST_ROOT}/src" -I"${CLOUD_TEST_DEPS}/include" \
  -I"${CLOUD_TEST_ROOT}/thirdparty/CubismSdkForNative/Framework/src" \
  -I"${CLOUD_TEST_ROOT}/thirdparty/CubismSdkForNative/Core/include" \
  "${CLOUD_TEST_ROOT}/tests/cloud_sync_test.cpp" "${CLOUD_TEST_ROOT}/src/CloudGame.cpp" \
  "${CLOUD_TEST_ROOT}/src/DataManagerCloud.cpp" \
  -L"${CLOUD_TEST_DEPS}/lib" -lrocksdb -lz -lssl -lcrypto -pthread \
  -o "${CLOUD_TEST_DIR}/cloud_sync_test"
"${CLOUD_TEST_DIR}/cloud_sync_test" "${CLOUD_TEST_DIR}/data"
