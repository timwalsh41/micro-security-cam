#!/usr/bin/env bash
# End-to-end regression suite against real hardware. Connection
# settings come from the environment only (CAM_HOST / CAM_PASSWORD; no
# address or credential is baked into this repository).
#
# The firmware must be flashed before running: build from the tip of
# the feature branch and flash manually, then run this suite.
#
# Usage:
#   script/test-e2e.sh                     # whole suite
#   script/test-e2e.sh -k photo            # subset
#   script/test-e2e.sh --collect-only      # dry run
set -euo pipefail
cd "$(dirname "$0")/.."
exec uv run pytest test/e2e --e2e-required "$@"
