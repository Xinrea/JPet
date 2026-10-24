#!/usr/bin/env bash
set -euo pipefail
GAME_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME_DEPENDENCIES="${VCPKG_INSTALLED_DIR:-${GAME_ROOT}/build/vcpkg_installed}/arm64-osx"
GAME_TEST_DIR="$(mktemp -d /tmp/jpet-game-tests.XXXXXX)"
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"${GAME_ROOT}/src" -I"${GAME_DEPENDENCIES}/include" \
  "${GAME_ROOT}/tests/game_test.cpp" "${GAME_ROOT}/src/GameRules.cpp" \
  -o "${GAME_TEST_DIR}/game_test"
"${GAME_TEST_DIR}/game_test"
