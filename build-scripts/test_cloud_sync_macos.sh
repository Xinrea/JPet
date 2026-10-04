#!/usr/bin/env bash
set -euo pipefail
CLOUD_TEST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLOUD_TEST_DEPS="${VCPKG_INSTALLED_DIR:-${CLOUD_TEST_ROOT}/build/vcpkg_installed}/arm64-osx"
CLOUD_TEST_DIR="$(mktemp -d /tmp/jpet-cloud-tests.XXXXXX)"
trap 'rm -rf "${CLOUD_TEST_DIR}"' EXIT
c++ -std=c++17 -DCPPHTTPLIB_OPENSSL_SUPPORT -Wno-deprecated-declarations -Wno-deprecated-literal-operator \
  -I"${CLOUD_TEST_ROOT}/src" -I"${CLOUD_TEST_DEPS}/include" \
  -I"${CLOUD_TEST_ROOT}/thirdparty/CubismSdkForNative/Framework/src" \
  -I"${CLOUD_TEST_ROOT}/thirdparty/CubismSdkForNative/Core/include" \
  "${CLOUD_TEST_ROOT}/tests/cloud_sync_test.cpp" "${CLOUD_TEST_ROOT}/src/CloudGame.cpp" \
  "${CLOUD_TEST_ROOT}/src/DataManagerCloud.cpp" \
  -L"${CLOUD_TEST_DEPS}/lib" -lrocksdb -lz -lssl -lcrypto -pthread \
  -o "${CLOUD_TEST_DIR}/cloud_sync_test"
"${CLOUD_TEST_DIR}/cloud_sync_test" "${CLOUD_TEST_DIR}/data"
