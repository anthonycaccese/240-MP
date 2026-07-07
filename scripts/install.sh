#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# 240-MP installer for Raspberry Pi OS Trixie (arm64)
#
# Usage:
#   bash install.sh             # install latest release
#   bash install.sh v1.2.0      # install a specific release tag
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

REPO="anthonycaccese/240-mp"          # ← update before first release
INSTALL_DIR="/opt/240mp"
LAUNCHER="/usr/local/bin/240mp"
SYSTEMD_SERVICE="/etc/systemd/system/240mp.service"

# ── Resolve version ────────────────────────────────────────────────────────────
VERSION="${1:-latest}"
if [ "$VERSION" = "latest" ]; then
    echo "Fetching latest release tag..."
    VERSION=$(curl -fsSL \
        "https://api.github.com/repos/${REPO}/releases/latest" \
        | python3 -c "import sys, json; print(json.load(sys.stdin)['tag_name'])")
fi
echo "Installing 240-MP ${VERSION}"

TARBALL="240-MP-${VERSION}-linux-arm64.tar.gz"
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${VERSION}/${TARBALL}"

# ── Verify architecture ────────────────────────────────────────────────────────
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ]; then
    echo "Error: this installer is for arm64 (aarch64). Detected: $ARCH"
    exit 1
fi

# ── Install runtime dependencies ──────────────────────────────────────────────
echo "Installing runtime dependencies..."
sudo apt-get update -qq
sudo apt-get install -y \
    libqt6quick6 \
    libqt6qml6 \
    libqt6opengl6 \
    libqt6network6 \
    libqt6svg6 \
    qt6-svg-plugins \
    qt6-wayland \
    qml6-module-qtquick \
    qml6-module-qtquick-controls \
    qml6-module-qtquick-window \
    qml6-module-qtquick-effects \
    libsdl2-2.0-0 \
    mpv

# ── udev rule: allow tty group to open /dev/tty0 for VT switching ─────────────
echo 'KERNEL=="tty0", GROUP="tty", MODE="0620"' \
    | sudo tee /etc/udev/rules.d/99-240mp-tty.rules > /dev/null
sudo udevadm control --reload-rules
sudo udevadm trigger /dev/tty0

# ── Choose install owner ──────────────────────────────────────────────────────
# Asked up front so the extract step can hand /opt/240mp to the user the app
# will run as — the launcher applies in-app staged updates there without root.
echo ""
read -r -p "Install systemd autostart service? [y/N] " AUTOSTART_REPLY
SERVICE_USER=""
if [[ "${AUTOSTART_REPLY}" =~ ^[Yy]$ ]]; then
    read -r -p "Run service as user [default: pi]: " SERVICE_USER
    SERVICE_USER="${SERVICE_USER:-pi}"
fi
OWNER_USER="${SERVICE_USER:-$USER}"

# ── Download tarball ───────────────────────────────────────────────────────────
echo "Downloading ${TARBALL}..."
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

curl -fsSL -o "${TMP_DIR}/${TARBALL}" "${DOWNLOAD_URL}"

# ── Extract to install directory ───────────────────────────────────────────────
# Tarball structure: usr/local/bin/240mp + usr/local/share/240mp/...
# We strip the usr/local prefix and place files directly in $INSTALL_DIR.
echo "Extracting to ${INSTALL_DIR}..."
sudo mkdir -p "${INSTALL_DIR}"
sudo tar -xzf "${TMP_DIR}/${TARBALL}" \
    --strip-components=3 \
    -C "${INSTALL_DIR}"

# Owned by the running user so the launcher can swap in staged in-app updates.
sudo chown -R "${OWNER_USER}:" "${INSTALL_DIR}"

# ── Create launcher ────────────────────────────────────────────────────────────
echo "Creating launcher at ${LAUNCHER}..."
sudo tee "${LAUNCHER}" > /dev/null << 'LAUNCHER_SCRIPT'
#!/usr/bin/env bash
# 240-MP launcher — applies staged in-app updates, then auto-detects the
# display platform and execs the binary.
INSTALL_DIR="/opt/240mp"

# Tells the app this launcher knows how to apply staged updates; the in-app
# updater refuses to stage anything without it (older installs must re-run
# this installer once to pick up the launcher/240mp-stop contract).
export MP240_LAUNCHER_API=1

# ── Apply a staged in-app update ───────────────────────────────────────────────
# The app downloads the release tarball to DATA_ROOT/updates and writes
# staged.sha256 ("<sha256>  <tarball>", coreutils format). Applied here, before
# exec, so it works identically for manual runs, desktop sessions, and the
# autostart service (which relaunches through this script on exit code 11 —
# see 240mp-stop). Mirror any DATA_ROOT override into the app's environment
# (e.g. the systemd unit) or this block won't find the staging directory.
UPDATES_DIR="${DATA_ROOT:-${XDG_DATA_HOME:-$HOME/.local/share}/240-MP}/updates"

# Crash recovery: a previous apply died between the child renames below.
if [ ! -x "$INSTALL_DIR/bin/240mp" ] && [ -x "$INSTALL_DIR/.new/bin/240mp" ]; then
    for d in "$INSTALL_DIR"/.new/*; do mv -f "$d" "$INSTALL_DIR/"; done
    rm -rf "$INSTALL_DIR/.new" "$INSTALL_DIR/.old"
fi

if [ -f "$UPDATES_DIR/staged.sha256" ] && [ -w "$INSTALL_DIR" ]; then
    if ( cd "$UPDATES_DIR" && sha256sum -c --status staged.sha256 ); then
        STAGED_TARBALL=$(awk '{print $2}' "$UPDATES_DIR/staged.sha256")
        rm -rf "$INSTALL_DIR/.new" "$INSTALL_DIR/.old"
        mkdir -p "$INSTALL_DIR/.new" "$INSTALL_DIR/.old"
        # Extract fully, then swap top-level dirs (bin, share) via rename —
        # shrinks the power-loss window from the whole extraction to two mv's,
        # and drops files that no longer exist in the new release.
        if tar -xzf "$UPDATES_DIR/$STAGED_TARBALL" --strip-components=3 -C "$INSTALL_DIR/.new" \
           && [ -x "$INSTALL_DIR/.new/bin/240mp" ]; then
            for d in "$INSTALL_DIR"/.new/*; do
                name=$(basename "$d")
                [ -e "$INSTALL_DIR/$name" ] && mv "$INSTALL_DIR/$name" "$INSTALL_DIR/.old/$name"
                mv "$d" "$INSTALL_DIR/$name"
            done
            rm -rf "$INSTALL_DIR/.new" "$INSTALL_DIR/.old"
            rm -f "$UPDATES_DIR/$STAGED_TARBALL" "$UPDATES_DIR/staged.sha256" "$UPDATES_DIR/staged.json"
        else
            # Bad extract: keep the old tree; .failed stops a retry every boot.
            rm -rf "$INSTALL_DIR/.new" "$INSTALL_DIR/.old"
            mv -f "$UPDATES_DIR/staged.sha256" "$UPDATES_DIR/staged.sha256.failed" 2>/dev/null
        fi
    else
        # Corrupt/partial stage — discard; the app re-offers the update.
        rm -f "$UPDATES_DIR/staged.sha256" "$UPDATES_DIR/staged.json"
    fi
fi

if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
elif [ -n "${DISPLAY:-}" ]; then
    QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
else
    # No display server — use EGLFS for headless/kiosk mode (RPi Lite)
    QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-eglfs}"
    export QT_QPA_EGLFS_ALWAYS_SET_MODE=1
    export QT_QPA_EGLFS_KMS_ATOMIC=1

    # Point Qt EGLFS at the DRM card that has a real display pipeline. Render-
    # only nodes (v3d) have no connector dirs under /sys/class/drm and make Qt
    # fail with "drmModeGetResources failed (Operation not supported)". On
    # Pi3B+/Pi4 the display card happens to be card0 (auto-pick works), but on
    # Pi5 the v3d render node often enumerates first, so we must select the
    # right card explicitly. Prefer a connected connector; fall back to the
    # first card that has any connector at all.
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

export QT_QPA_PLATFORM
export QML2_IMPORT_PATH="/usr/lib/aarch64-linux-gnu/qt6/qml"

exec "${INSTALL_DIR}/bin/240mp" "$@"
LAUNCHER_SCRIPT

sudo chmod +x "${LAUNCHER}"

# ── Optional: systemd autostart ───────────────────────────────────────────────
if [[ "${AUTOSTART_REPLY}" =~ ^[Yy]$ ]]; then
    sudo tee "${SYSTEMD_SERVICE}" > /dev/null << UNIT
[Unit]
Description=240-MP Media Player
After=multi-user.target sound.target

[Service]
Type=simple
User=${SERVICE_USER}
SupplementaryGroups=tty video input
AmbientCapabilities=CAP_SYS_TTY_CONFIG
Environment=QT_QPA_PLATFORM=eglfs
Environment=QT_QPA_EGLFS_ALWAYS_SET_MODE=1
Environment=QT_QPA_EGLFS_KMS_ATOMIC=1
Environment=QML2_IMPORT_PATH=/usr/lib/aarch64-linux-gnu/qt6/qml
Environment=MP240_AUTOSTART=1
ExecStartPre=+-/usr/bin/systemctl stop 240mp-terminal.service
ExecStart=${LAUNCHER}
Restart=on-failure
RestartSec=5s
RestartPreventExitStatus=10
ExecStopPost=+/usr/local/bin/240mp-stop
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
UNIT

    # ExecStopPost helper: normal quit (exit 0) or a crash powers the Pi off as
    # before; exit 10 means the user chose "Exit to Terminal", so instead spawn a
    # login shell on tty1 (see views/Settings.qml). RestartPreventExitStatus=10
    # keeps Restart=on-failure from relaunching the app over that shell.
    # Exit 11 is "Apply & Restart" from the in-app updater (views/Update.qml):
    # do nothing here — it's a failure status, so Restart=on-failure relaunches
    # through the launcher, which applies the staged update before exec.
    sudo tee /usr/local/bin/240mp-stop > /dev/null << 'STOP_HELPER'
#!/usr/bin/env bash
# Called by 240mp.service ExecStopPost. systemd sets $EXIT_STATUS to the app's exit code.
if [ "${EXIT_STATUS:-}" = "10" ]; then
    systemctl start 240mp-terminal.service
elif [ "${EXIT_STATUS:-}" = "11" ]; then
    :   # in-app update restart — Restart=on-failure brings the app back up
else
    systemctl poweroff
fi
STOP_HELPER
    sudo chmod +x /usr/local/bin/240mp-stop

    # On-demand login shell for "Exit to Terminal". Not enabled (no boot race with
    # 240mp.service); getty@tty1 stays masked. Started only by 240mp-stop, and
    # stopped again by 240mp.service's ExecStartPre when the app comes back.
    sudo tee /etc/systemd/system/240mp-terminal.service > /dev/null << 'TERMINAL_UNIT'
[Unit]
Description=240-MP exit-to-terminal login shell

[Service]
Type=idle
ExecStart=-/sbin/agetty --noclear tty1 linux
StandardInput=tty
StandardOutput=tty
TTYPath=/dev/tty1
TTYReset=yes
TTYVHangup=yes
KillMode=process
Restart=no
TERMINAL_UNIT

    sudo systemctl mask getty@tty1.service autovt@.service
    sudo systemctl daemon-reload
    sudo systemctl enable 240mp.service
    echo "Service installed and enabled."
    echo "Start now with: sudo systemctl start 240mp"
fi

echo ""
echo "240-MP ${VERSION} installed successfully."
echo "Run: 240mp"
