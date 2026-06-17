#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# Run a locally-built 240-MP binary with the same display environment the
# installed launcher sets up. Use this to test a dev build on real hardware —
# notably the Pi 5, whose EGLFS display-card selection lives in that launcher and
# is otherwise missing when you run ./build/240mp directly.
#
# Usage (from anywhere):
#   scripts/run-local.sh                       # runs ./build/240mp from the repo
#   MP240_BIN=/path/to/240mp scripts/run-local.sh
#
# Headless (RPi Lite / EGLFS): run from the Pi's console VT, not over SSH, and
# stop the autostart service first so it isn't holding the display:
#   sudo systemctl stop 240mp
#
# NOTE: the display-platform + KMS-card detection below is kept in sync with the
# launcher heredoc in scripts/install.sh — change both together.
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

# Repo root = parent of this script's dir, so APP_ROOT resolves assets/modules.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN="${MP240_BIN:-${REPO_ROOT}/build/240mp}"

if [ ! -x "$BIN" ]; then
    echo "Error: binary not found or not executable at: $BIN" >&2
    echo "Build it first (cmake --build build) or set MP240_BIN." >&2
    exit 1
fi

export APP_ROOT="${APP_ROOT:-${REPO_ROOT}}"

if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
elif [ -n "${DISPLAY:-}" ]; then
    export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
else
    # No display server — EGLFS for headless/kiosk mode (RPi Lite).
    export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-eglfs}"
    export QT_QPA_EGLFS_ALWAYS_SET_MODE=1
    export QT_QPA_EGLFS_KMS_ATOMIC=1

    # Point Qt EGLFS at the DRM card that has a real display pipeline. Render-only
    # nodes (v3d) have no connector dirs under /sys/class/drm and make Qt fail. On
    # Pi3B+/Pi4 the display card is card0 (auto-pick works); on Pi5 the v3d render
    # node often enumerates first, so we select the right card explicitly. Prefer a
    # connected connector; fall back to the first card with any connector.
    KMS_CARD=""
    for s in /sys/class/drm/card*-*/status; do
        [ -e "$s" ] || continue
        if [ "$(cat "$s")" = "connected" ]; then
            n=$(basename "$(dirname "$s")"); KMS_CARD="${n%%-*}"; break
        fi
    done
    if [ -z "$KMS_CARD" ]; then
        for d in /sys/class/drm/card*-*; do
            [ -e "$d" ] || continue
            n=$(basename "$d"); KMS_CARD="${n%%-*}"; break
        done
    fi
    if [ -n "$KMS_CARD" ] && [ -e "/dev/dri/$KMS_CARD" ]; then
        KMS_CONF="${XDG_RUNTIME_DIR:-/tmp}/240mp-kms.json"
        printf '{ "device": "/dev/dri/%s" }\n' "$KMS_CARD" > "$KMS_CONF"
        export QT_QPA_EGLFS_KMS_CONFIG="$KMS_CONF"
    fi
fi

# System Qt6 QML modules (matches the installed launcher); only if present.
if [ -d /usr/lib/aarch64-linux-gnu/qt6/qml ]; then
    export QML2_IMPORT_PATH="${QML2_IMPORT_PATH:-/usr/lib/aarch64-linux-gnu/qt6/qml}"
fi

exec "$BIN" "$@"
