#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# Build a modern mpv from source against the host's *stock* FFmpeg.
#
# Why: 240-MP targets a current mpv (the RPi and macOS builds get one from apt /
# brew). Ubuntu 24.04's apt mpv is 0.37.0 — one release too old for the app's
# "forced subtitles only" option (`--subs-with-matching-audio=forced`, added in
# mpv 0.38.0). mpv 0.40.0 is the newest release that still builds against
# 24.04's FFmpeg 6.1 / libplacebo 6.338 (0.41 jumped to FFmpeg 8), so we pin to
# it and compile *just* mpv — no FFmpeg build, no PPA, glibc floor unchanged.
#
# Prints the path to the built mpv binary on stdout (diagnostics go to stderr):
#     MPV_BIN=$(scripts/build-mpv.sh)
#
# Env overrides:
#   MPV_TAG   mpv git tag to build   (default: v0.40.0 — bump when the FFmpeg
#             base does; anything newer than 24.04's FFmpeg 6.1 supports)
#   MPV_SRC   checkout/build dir     (default: <repo>/.mpv-build)
#
# Build deps (Ubuntu 24.04): meson ninja-build pkg-config, the FFmpeg -dev set,
# libass-dev libplacebo-dev liblua5.2-dev libva-dev, and the Wayland/X11/EGL/DRM
# dev libs — see the CI job (release.yml) for the full apt list.
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SRC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MPV_TAG="${MPV_TAG:-v0.40.0}"
MPV_SRC="${MPV_SRC:-$SRC_ROOT/.mpv-build}"

# stderr so stdout stays clean for the printed binary path (used in $(...)).
log() { printf '\033[1;35m==>\033[0m %s\n' "$*" >&2; }

if [ ! -d "$MPV_SRC/.git" ]; then
    log "Cloning mpv $MPV_TAG"
    git clone --depth 1 --branch "$MPV_TAG" \
        https://github.com/mpv-player/mpv "$MPV_SRC" >&2
fi

# libmpv off (we only ship the CLI player). Force Lua — the OSC, media-keys and
# screensaver scripts require it, but mpv's `-Dlua=enabled` probes flavors with
# required:false and would quietly disable Lua if the dev package is missing;
# naming the exact flavor (matching Ubuntu's liblua5.2-dev) makes it required and
# fails the build loudly. libass has no meson option — mpv requires it
# unconditionally, so a missing libass-dev already errors out. Everything else
# stays at meson 'auto' so VAAPI (hwdec), Wayland, X11, DRM, EGL and libplacebo
# enable from whatever dev packages are present. MPV_LUA overrides the Lua flavor
# for distros that ship a different one (e.g. luajit).
log "Configuring mpv ($MPV_TAG) against system FFmpeg"
meson setup "$MPV_SRC/build" "$MPV_SRC" \
    --prefix=/usr \
    --buildtype=release \
    -Dlibmpv=false \
    -Dcplayer=true \
    -Dlua="${MPV_LUA:-lua5.2}" \
    -Dmanpage-build=disabled \
    -Dbuild-date=false >&2

log "Building mpv"
ninja -C "$MPV_SRC/build" mpv >&2

BIN="$MPV_SRC/build/mpv"
[ -x "$BIN" ] || { echo "ERROR: mpv build did not produce $BIN" >&2; exit 1; }
log "Built $("$BIN" --version 2>/dev/null | head -1)"
echo "$BIN"
