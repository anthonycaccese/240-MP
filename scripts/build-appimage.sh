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

# ── 6. Stage the Wayland fallback set ─────────────────────────────────────────
# Debian/Ubuntu build SDL2 and mpv with Wayland support on, so the bundle needs
# the Wayland-facing libraries even on a machine that will only ever run X11.
# Step 7 used to delete all of them along with the driver libs, which made the
# app unlaunchable on X11-only images that ship no Wayland at all (Batocera and
# other buildroot handhelds). Each is a vendor-neutral shim rather than a driver,
# so a bundled copy is a safe substitute:
#
#   libwayland-client/-cursor/-egl  protocol shims (SDL2 and mpv link all three)
#   libva-wayland.so.2              27 KB of vaGetDisplayWl glue; the real VA
#                                   driver is still dlopened by host libva.so.2
#   libvulkan.so.1                  the Khronos *loader*, not a driver — it
#                                   dlopens the host's ICD from vulkan/icd.d, so
#                                   hardware decode stays on the host driver
#
# libvulkan earns its place for a different reason than the rest: Batocera *has*
# a libvulkan.so.1, but one built without Wayland support, so mpv died on
# "undefined symbol: vkCreateWaylandSurfaceKHR" (via libplacebo). A missing
# symbol inside a library that exists is invisible to ldd and to the build-time
# audit — only the target can reveal it. Ours exports the full WSI set.
#
# AppRun reaches for these only when the host cannot satisfy them itself.
FALLBACK_LIBS="libwayland-client.so.0 libwayland-cursor.so.0 libwayland-egl.so.1
               libva-wayland.so.2 libvulkan.so.1"
FALLBACK_DIR="$APPDIR/usr/lib/fallback"

host_lib_path() {
    local d
    for d in /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /lib64 /usr/lib64 /lib /usr/lib; do
        if [ -e "$d/$1" ]; then echo "$d/$1"; return 0; fi
    done
    return 1
}

log "Staging Wayland client fallback libraries"
mkdir -p "$FALLBACK_DIR"
# Move whatever linuxdeploy bundled (soname symlinks and versioned files alike)…
find "$APPDIR/usr/lib" -maxdepth 1 -name 'libwayland*' -print0 2>/dev/null |
    while IFS= read -r -d '' f; do mv "$f" "$FALLBACK_DIR/"; done
# …then fill the gaps. libwayland-client is on the upstream AppImage excludelist
# so linuxdeploy never bundles it, and libva-wayland is not caught by the glob
# above — both always land here, copied from the build host. (The libva-wayland
# copy linuxdeploy did put in usr/lib is removed by step 7's libva* pattern.)
for so in $FALLBACK_LIBS; do
    [ -e "$FALLBACK_DIR/$so" ] && continue
    src="$(host_lib_path "$so")" || {
        echo "ERROR: $so is in neither the AppDir nor the build host's library path." >&2
        echo "       Install it (apt-get install libwayland-client0 libwayland-cursor0" >&2
        echo "       libwayland-egl1 libva-wayland2) — an incomplete fallback set" >&2
        echo "       breaks every X11-only target." >&2
        exit 1
    }
    cp -L "$src" "$FALLBACK_DIR/$so"
done

# ── 7. Drop host-driver libraries ─────────────────────────────────────────────
# GPU/driver/display libs MUST come from the host to match the actual hardware —
# bundling them breaks VA-API decode and GL on the target (e.g. the SteamDeck's
# AMD stack). linuxdeploy excludes most of these already; belt-and-suspenders.
# -maxdepth 1 keeps usr/lib/fallback out of scope.
log "Pruning host-provided driver libraries"
PRUNED_LIBS=""
for pat in 'libva*' 'libvdpau*' 'libGL*' 'libEGL*' 'libGLX*' 'libGLdispatch*' \
           'libdrm*' 'libgbm*' 'libxcb-dri*' 'libvulkan*'; do
    # Remember what we removed: deleting a library is itself the decision that the
    # host provides it, so step 8 should not then complain about it. Deriving this
    # instead of restating the names keeps the two lists from drifting apart.
    PRUNED_LIBS="$PRUNED_LIBS $(find "$APPDIR/usr/lib" -maxdepth 1 -name "$pat" \
        -printf '%f\n' -delete 2>/dev/null || true)"
done

# ── 8. Audit the host-library contract ────────────────────────────────────────
# Every DT_NEEDED soname that is not inside the AppDir has to come from the target
# machine. That set is a promise about which distros can run this build, and it
# used to be implicit — which is how the Wayland libs above silently became
# mandatory and broke X11-only distros with no warning at build time. Make it
# explicit: anything required but neither bundled nor listed here fails the build.
#
# The list below is the upstream AppImage excludelist — libraries linuxdeploy
# refuses to bundle because they are tied to the host's kernel/driver/graphics
# stack — and step 7's prunings are added to it automatically. Add an entry here
# only when it genuinely is present on every target; otherwise bundle the library
# instead. For a one-off unblock (toolchain bump mid-release, no time to rework
# the bundle), set ALLOW_HOST_LIBS_EXTRA="soname …".
ALLOWED_HOST_LIBS="
ld-linux-x86-64.so.2 ld-linux.so.2 libanl.so.1 libasound.so.2 libBrokenLocale.so.1
libc.so.6 libcidn.so.1 libcom_err.so.2 libdl.so.2 libexpat.so.1 libfontconfig.so.1
libfreetype.so.6 libfribidi.so.0 libgcc_s.so.1 libglapi.so.0 libgmp.so.10
libgpg-error.so.0 libharfbuzz.so.0 libICE.so.6 libjack.so.0 libm.so.6 libmvec.so.1
libnss_compat.so.2 libnss_dns.so.2 libnss_files.so.2 libnss_hesiod.so.2
libnss_nis.so.2 libnss_nisplus.so.2 libpipewire-0.3.so.0 libpthread.so.0
libresolv.so.2 librt.so.1 libSM.so.6 libstdc++.so.6 libthread_db.so.1
libusb-1.0.so.0 libutil.so.1 libuuid.so.1 libz.so.1
libX11.so.6 libX11-xcb.so.1 libxcb.so.1 libxcb-dri2.so.0 libxcb-dri3.so.0
libGL.so.1 libGLX.so.0 libGLdispatch.so.0 libOpenGL.so.0 libEGL.so.1 libGLESv2.so.2
libdrm.so.2 libgbm.so.1 libvulkan.so.1 libvdpau.so.1
libva.so.2 libva-drm.so.2 libva-x11.so.2 libva-wayland.so.2
"

audit=""
if ! command -v objdump >/dev/null 2>&1; then
    echo "WARN: objdump not found (install binutils) — skipping the host-library audit" >&2
else
    log "Auditing host library requirements"
    # "soname<TAB>needing-file" for every ELF in the bundle. The same find also
    # turns up scripts and assets, so objdump is expected to fail on some inputs —
    # the trailing `|| true` is load-bearing under `set -o pipefail`, which would
    # otherwise abort the whole build on the first non-ELF file.
    needs="$(find "$APPDIR" -type f \( -name '*.so*' -o -perm -u+x \) -print0 |
        while IFS= read -r -d '' f; do
            objdump -p "$f" 2>/dev/null |
                awk -v F="${f#"$APPDIR"/}" '$1 == "NEEDED" { print $2 "\t" F }' || true
        done)"

    # "soname<TAB>bundled|host": what the bundle satisfies itself, plus what the
    # target is allowed to satisfy. Symlinks count as bundled — linuxdeploy
    # sometimes lands a soname symlink beside the versioned real file. Passed as a
    # file rather than via awk -v: mawk (Ubuntu's awk) rejects multi-line values.
    known="$(
        find "$APPDIR" \( -type f -o -type l \) -name '*.so*' -printf '%f\tbundled\n'
        printf '%s\thost\n' $ALLOWED_HOST_LIBS $PRUNED_LIBS ${ALLOW_HOST_LIBS_EXTRA:-}
    )"

    audit="$(printf '%s\n' "$needs" | awk -F'\t' '
        FNR == NR                { known[$1] = $2; next }
        NF == 0                  { next }
        known[$1] == "bundled"   { next }
        known[$1] == "host"      { host[$1] = 1; next }
                                 { bad[$1] = bad[$1] "\n    " $2 }
        END {
            for (s in host)  print "OK\t" s
            for (s in bad)   print "BAD\t" s bad[s]
        }
    ' <(printf '%s\n' "$known") -)"
fi

# Both reports are no-ops when the audit was skipped ($audit empty).
if printf '%s\n' "$audit" | grep -q '^BAD'; then
    echo "ERROR: the bundle needs libraries that are neither included in it nor allowed" >&2
    echo "       to come from the host — this AppImage would fail to start on any target" >&2
    echo "       that lacks them. Bundle them, or add them to ALLOWED_HOST_LIBS if every" >&2
    echo "       supported target really does provide them. Required by:" >&2
    printf '%s\n' "$audit" | sed -n 's/^BAD\t/  /p; /^ \{4\}/p' >&2
    exit 1
fi
printf '%s\n' "$audit" | sed -n 's/^OK\t//p' | sort | sed 's/^/  host must provide: /' >&2

# ── 9. Install the AppRun ─────────────────────────────────────────────────────
# Overwrites the symlink linuxdeploy generated. Must land after the deploy step
# and before packaging.
install -Dm755 "$SRC_ROOT/packaging/linux/AppRun" "$APPDIR/AppRun"

# ── 10. Package ───────────────────────────────────────────────────────────────
log "Packaging $OUTPUT"
ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
log "Done: $OUTPUT"
