#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# Build a self-contained Linux x86_64 AppImage of 240-MP.
#
# Produces a single portable file that runs on immutable distros (SteamOS /
# SteamDeck) and modern Intel/AMD desktops with no apt/pacman step — Qt, SDL2 and
# mpv are all bundled inside. Runnable locally and from CI (release.yml).
#
# Usage:
#   scripts/build-appimage.sh [--configure]
#
#   --configure   Run cmake configure+build first (Release). Omit if you already
#                 built into $BUILD_DIR yourself.
#
# Env overrides (all optional):
#   BUILD_DIR      cmake build tree                     (default: build)
#   APPDIR         staging AppDir                        (default: AppDir)
#   VERSION        version baked into the app (APP_VERSION) (default: git describe)
#   MPV_BIN        mpv to bundle                          (default: $(command -v mpv))
#   QMAKE          qmake for linuxdeploy-plugin-qt        (default: auto-detect)
#   CMAKE_PREFIX_PATH  passed through to cmake --configure (find Qt)
#
# Requires: cmake, a C++ toolchain, mpv (to bundle), and network access on first
# run to fetch linuxdeploy + linuxdeploy-plugin-qt + appimagetool (cached under
# .appimage-tools/). On CI these can be pre-provisioned on PATH.
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SRC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SRC_ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
APPDIR="${APPDIR:-AppDir}"
TOOLS_DIR="${TOOLS_DIR:-$SRC_ROOT/.appimage-tools}"
VERSION="${VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}"
# Deliberately version-less: the AppImage self-updates in place (swaps over
# $APPIMAGE), so a version in the filename would drift from the actual app
# version after the first update and could break the Steam/.desktop shortcut
# path. The real version is baked in (APP_VERSION) and shown in-app; the release
# tag records it on GitHub. (dmg/tarball keep their version — they're installers,
# not the installed-in-place runtime.)
OUTPUT="240-MP-linux-x86_64.AppImage"

CONFIGURE=0
[ "${1:-}" = "--configure" ] && CONFIGURE=1

# Run the linuxdeploy/appimagetool AppImages by extracting them rather than
# mounting via FUSE — avoids a libfuse2 dependency on the build host (and CI).
export APPIMAGE_EXTRACT_AND_RUN="${APPIMAGE_EXTRACT_AND_RUN:-1}"

# Diagnostics go to stderr so they never pollute a function's captured stdout
# (fetch_tool below is used in command substitution).
log() { printf '\033[1;36m==>\033[0m %s\n' "$*" >&2; }

# ── 1. Fetch the AppImage tooling (cached) ────────────────────────────────────
# linuxdeploy bundles the app's libraries and patches rpaths; the qt plugin adds
# the Qt runtime + QML modules + plugins; appimagetool packs the finished AppDir.
fetch_tool() {
    local name="$1" url="$2" dest="$TOOLS_DIR/$1"
    if command -v "$name" >/dev/null 2>&1; then echo "$(command -v "$name")"; return; fi
    if [ ! -x "$dest" ]; then
        mkdir -p "$TOOLS_DIR"
        log "Downloading $name"
        curl -fL# "$url" -o "$dest"
        chmod +x "$dest"
    fi
    echo "$dest"
}

LD_BASE="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous"
LDQT_BASE="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous"
AT_BASE="https://github.com/AppImage/appimagetool/releases/download/continuous"

LINUXDEPLOY="$(fetch_tool linuxdeploy-x86_64.AppImage       "$LD_BASE/linuxdeploy-x86_64.AppImage")"
fetch_tool linuxdeploy-plugin-qt-x86_64.AppImage "$LDQT_BASE/linuxdeploy-plugin-qt-x86_64.AppImage" >/dev/null
APPIMAGETOOL="$(fetch_tool appimagetool-x86_64.AppImage     "$AT_BASE/appimagetool-x86_64.AppImage")"
# linuxdeploy discovers plugins by name on PATH.
export PATH="$TOOLS_DIR:$PATH"

# ── 2. Configure/build (optional) ─────────────────────────────────────────────
if [ "$CONFIGURE" = "1" ]; then
    log "Configuring + building ($BUILD_DIR)"
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DAPP_VERSION="$VERSION" \
        ${CMAKE_PREFIX_PATH:+-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"} .
    cmake --build "$BUILD_DIR" --parallel
fi

# ── 3. Install into the AppDir (FHS layout) ───────────────────────────────────
# The Linux install() rules put the binary at usr/bin/240mp and QML/assets at
# usr/share/240mp — exactly what resolveAppRoot() discovers via ../share/240mp,
# so APP_ROOT does not need to be set at runtime.
log "Installing into $APPDIR"
rm -rf "$APPDIR"
DESTDIR="$SRC_ROOT/$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

# ── 4. Bundle mpv ─────────────────────────────────────────────────────────────
# Stock SteamOS has no system mpv, so ship our own next to the app binary
# (usr/bin/mpv beside usr/bin/240mp). MpvController resolves mpv as a sibling of
# its own executable first (linuxdeploy's AppRun does NOT add usr/bin to PATH,
# so findExecutable alone would miss it). linuxdeploy pulls in mpv's own libs.
# MPV_BIN should point at a modern mpv (>= 0.38); CI builds one with
# scripts/build-mpv.sh because distro packages are often too old. Falls back to
# the system mpv for local builds on a distro that already ships a recent one.
MPV_BIN="${MPV_BIN:-$(command -v mpv || true)}"
[ -x "$MPV_BIN" ] || { echo "ERROR: mpv not found — set MPV_BIN (e.g. \$(scripts/build-mpv.sh)) or install mpv >= 0.38" >&2; exit 1; }
install -Dm755 "$MPV_BIN" "$APPDIR/usr/bin/mpv"

# ── 5. Deploy Qt + libraries (no packaging yet) ───────────────────────────────
# QML_SOURCES_PATHS lets the qt plugin scan our .qml for imports and bundle the
# right QML modules (QtQuick, Controls, Effects, Svg…). EXTRA_QT_PLUGINS forces
# ones not always auto-detected: tls (OpenSSL backend — Plex/Jellyfin HTTPS),
# svg/imageformats (SVG icons), iconengines.
export QML_SOURCES_PATHS="$SRC_ROOT"
export EXTRA_QT_PLUGINS="tls;svg;imageformats;iconengines"
if [ -z "${QMAKE:-}" ]; then
    # Prefer qmake on PATH; fall back to the Qt pointed at by CMAKE_PREFIX_PATH
    # (Qt online-installer layouts don't put qmake on PATH).
    QMAKE="$(command -v qmake6 || command -v qmake || true)"
    if [ -z "$QMAKE" ] && [ -n "${CMAKE_PREFIX_PATH:-}" ] && [ -x "${CMAKE_PREFIX_PATH%%;*}/bin/qmake" ]; then
        QMAKE="${CMAKE_PREFIX_PATH%%;*}/bin/qmake"
    fi
fi
[ -n "$QMAKE" ] && export QMAKE || echo "WARN: qmake not found — linuxdeploy-plugin-qt may fail to locate Qt" >&2
log "Deploying Qt + libraries"
"$LINUXDEPLOY" --appdir "$APPDIR" --plugin qt \
    --executable "$APPDIR/usr/bin/240mp" \
    --executable "$APPDIR/usr/bin/mpv" \
    --desktop-file "$SRC_ROOT/packaging/linux/240-mp.desktop" \
    --icon-file "$SRC_ROOT/packaging/linux/240-mp.png"

# ── 6. Drop host-driver libraries ─────────────────────────────────────────────
# GPU/driver/display libs MUST come from the host to match the actual hardware —
# bundling them breaks VA-API decode and GL on the target (e.g. the SteamDeck's
# AMD stack). linuxdeploy excludes most of these already; belt-and-suspenders.
log "Pruning host-provided driver libraries"
for pat in 'libva*' 'libvdpau*' 'libGL*' 'libEGL*' 'libGLX*' 'libGLdispatch*' \
           'libdrm*' 'libgbm*' 'libwayland*' 'libxcb-dri*' 'libvulkan*'; do
    find "$APPDIR/usr/lib" -maxdepth 1 -name "$pat" -delete 2>/dev/null || true
done

# ── 7. Package ────────────────────────────────────────────────────────────────
log "Packaging $OUTPUT"
ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
log "Done: $OUTPUT"
