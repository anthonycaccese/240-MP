#!/usr/bin/env bash
# Builds and runs the AirPlay module's standalone smoke test — a ~15 second,
# no-hardware, no-QML check that AirPlayBackend's process lifecycle and
# metadata-pipe parsing actually work, using scripts/dev/fake-airplay in
# place of the real (heavy-to-build) shairport-sync binary.
#
# Usage:
#   scripts/dev/run-airplay-smoke-test.sh [cmake-build-dir]
#
# Defaults to ./build. Configures it with -DAIRPLAY_BUILD_TESTS=ON if needed
# (harmless to re-run on an existing build dir — it just adds one target).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build}"

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DAIRPLAY_BUILD_TESTS=ON >/dev/null
cmake --build "$BUILD_DIR" --target airplay_smoke_test -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

echo "──────────────────────────────────────────────────────────────"
PATH="$REPO_ROOT/scripts/dev/fake-airplay:$PATH" "$BUILD_DIR/airplay_smoke_test"
