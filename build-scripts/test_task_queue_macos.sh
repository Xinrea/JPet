#!/usr/bin/env bash
set -euo pipefail
TASK_TEST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Task queues now run in the cloud. The suite covers pause/resume and settlement.
cd "${TASK_TEST_ROOT}/cloud"
export WRANGLER_SEND_METRICS=false
npm test -- test/game.test.ts test/worker.test.ts
