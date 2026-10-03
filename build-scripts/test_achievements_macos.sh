#!/usr/bin/env bash
set -euo pipefail
ACHIEVEMENT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ACHIEVEMENT_DEPENDENCIES="${VCPKG_INSTALLED_DIR:-${ACHIEVEMENT_ROOT}/build/vcpkg_installed}/arm64-osx"
ACHIEVEMENT_TEST_DIR="$(mktemp -d /tmp/jpet-achievement-tests.XXXXXX)"
c++ -std=c++17 -Wall -Wextra \
  -I"${ACHIEVEMENT_ROOT}/src" -I"${ACHIEVEMENT_DEPENDENCIES}/include" \
  "${ACHIEVEMENT_ROOT}/tests/achievements_test.cpp" \
  -o "${ACHIEVEMENT_TEST_DIR}/achievements_test"
"${ACHIEVEMENT_TEST_DIR}/achievements_test"
