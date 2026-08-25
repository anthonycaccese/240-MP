#!/usr/bin/env bash
# Launches the real 240-MP app (building it first if needed) with the fake
# shairport-sync from scripts/dev/fake-airplay first on PATH and a scratch,
# throwaway data/config directory — so opening the AirPlay module in the
# actual UI shows a simulated session (three looping fake tracks, with
# artwork) instead of needing a real AirPlay-2 shairport-sync build or a
# phone to test with.
#
# Usage:
#   scripts/dev/run-with-fake-airplay.sh [cmake-build-dir]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build}"

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

SCRATCH_DATA_ROOT="$(mktemp -d -t 240mp-airplay-dev)"
echo "Scratch DATA_ROOT: $SCRATCH_DATA_ROOT (config/state discarded after this run)"

if [[ -x "$BUILD_DIR/240mp.app/Contents/MacOS/240mp" ]]; then
    BIN="$BUILD_DIR/240mp.app/Contents/MacOS/240mp"
else
    BIN="$BUILD_DIR/240mp"
fi

echo "Launching 240-MP with fake AirPlay — open the AirPlay module from the module list."
PATH="$REPO_ROOT/scripts/dev/fake-airplay:$PATH" \
    APP_ROOT="$REPO_ROOT" \
    DATA_ROOT="$SCRATCH_DATA_ROOT" \
    "$BIN"
