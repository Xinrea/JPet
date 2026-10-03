#!/usr/bin/env bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TASK_DEPENDENCIES="${VCPKG_INSTALLED_DIR:-${TASK_ROOT}/build/vcpkg_installed}/arm64-osx"
TASK_TEST_DIR="$(mktemp -d /tmp/jpet-task-tests.XXXXXX)"
c++ -std=c++17 -Wno-deprecated-declarations -Wno-deprecated-literal-operator \
  -I"${TASK_ROOT}/src" -I"${TASK_DEPENDENCIES}/include" \
  -I"${TASK_ROOT}/thirdparty/CubismSdkForNative/Framework/src" \
  -I"${TASK_ROOT}/thirdparty/CubismSdkForNative/Core/include" \
  "${TASK_ROOT}/tests/task_queue_test.cpp" \
  "${TASK_ROOT}/src/DataManagerTasks.cpp" "${TASK_ROOT}/src/GameTask.cpp" \
  -L"${TASK_DEPENDENCIES}/lib" -lrocksdb -lz -pthread \
  -o "${TASK_TEST_DIR}/task_queue_test"
if [[ "${1:-}" == "preview" ]]; then
  exec "${TASK_TEST_DIR}/task_queue_test" preview "${TASK_TEST_DIR}/preview" "${TASK_ROOT}/resources/panel/dist"
fi
for scenario in settle restart fifo duplicates failure oneoff blocked capacity overflow bottle corrupt; do
  TASK_CASE_DIR="${TASK_TEST_DIR}/${scenario}"
  if [[ "${scenario}" == "restart" ]]; then TASK_CASE_DIR="${TASK_TEST_DIR}/settle"; fi
  "${TASK_TEST_DIR}/task_queue_test" "${scenario}" "${TASK_CASE_DIR}"
done
