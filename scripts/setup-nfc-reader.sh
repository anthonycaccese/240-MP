#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# 240-MP NFC Reader Setup — grants access to supported NFC readers on Linux
#
# Two independent paths, because the two reader families need entirely
# different things from the OS:
#
#   PN532 USB  — a PN532 behind a CH340/CP210x/FTDI USB-serial bridge. Needs no
#                packages and no daemon; the kernel already has the driver. All
#                this script does is grant the user access to the device node,
#                which works even on an immutable distro like SteamOS.
#
#   PC/SC      — the ACR122U and other CCID readers. Needs the pcscd daemon, a
#                CCID driver, and the kernel's own NFC stack kept out of the
#                way. Skipped on immutable distros, where it cannot be set up.
#
# Run it from a checkout:
#   bash scripts/setup-nfc-reader.sh
#
# Or standalone, without cloning:
#   bash <(curl -fsSL https://github.com/anthonycaccese/240-mp/releases/latest/download/setup-nfc-reader.sh)
#
# Pass a username to authorize (defaults to the invoking user):
#   bash scripts/setup-nfc-reader.sh pi
#
# Env overrides:
#   SKIP_PCSC=1   PN532 only, even on a distro where PC/SC could be installed
#   SKIP_PN532=1  PC/SC only
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

# Under sudo, $USER is root — fall back to the invoking user so the polkit
# rules and group membership apply to the account that will actually run 240-MP.
AUTHORIZED_USER="${1:-${SUDO_USER:-$USER}}"

UDEV_RULE=/etc/udev/rules.d/99-240mp-nfc.rules
NEEDS_RELOGIN=0

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "This script configures Linux only."
    echo "On macOS both reader types work with no setup: PC/SC ships with the OS,"
    echo "and PN532 USB modules appear as /dev/cu.* with the built-in drivers."
    exit 0
fi

# ── Detect the distro family ─────────────────────────────────────────────────
# The repo otherwise solves portability by bundling (the AppImage), but a script
# that installs system packages has no such option.
DISTRO_ID=""
DISTRO_LIKE=""
DISTRO_NAME="unknown"
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    DISTRO_ID="${ID:-}"
    DISTRO_LIKE="${ID_LIKE:-}"
    DISTRO_NAME="${PRETTY_NAME:-${ID:-unknown}}"
fi

PKG=""
case " $DISTRO_ID $DISTRO_LIKE " in
    *" debian "*|*" ubuntu "*|*" raspbian "*) PKG=apt ;;
    *" arch "*|*" archlinux "*)               PKG=pacman ;;
    *" fedora "*|*" rhel "*|*" centos "*)     PKG=dnf ;;
    *" suse "*|*" opensuse "*)                PKG=zypper ;;
esac
# ID_LIKE is often missing; fall back to whichever package manager is present.
if [[ -z "$PKG" ]]; then
    if   command -v apt-get >/dev/null 2>&1; then PKG=apt
    elif command -v pacman  >/dev/null 2>&1; then PKG=pacman
    elif command -v dnf     >/dev/null 2>&1; then PKG=dnf
    elif command -v zypper  >/dev/null 2>&1; then PKG=zypper
    fi
fi

# ── Detect an immutable root ─────────────────────────────────────────────────
# SteamOS and the ostree-based images (Bazzite, Silverblue) have a read-only
# rootfs, so installing pcscd is out. /etc stays writable on all of them, which
# is why the PN532 udev rule below still works.
IMMUTABLE=0
if [[ "$DISTRO_ID" == "steamos" ]] || command -v steamos-readonly >/dev/null 2>&1; then
    IMMUTABLE=1
elif [[ -f /run/ostree-booted ]] || command -v rpm-ostree >/dev/null 2>&1; then
    IMMUTABLE=1
fi

echo "==> Detected: ${DISTRO_NAME}"
echo "    package manager: ${PKG:-none}   immutable root: $([[ $IMMUTABLE == 1 ]] && echo yes || echo no)"
echo "    authorizing user: ${AUTHORIZED_USER}"
echo ""

# ── PN532 USB: udev rule only, no packages ───────────────────────────────────
if [[ "${SKIP_PN532:-0}" != "1" ]]; then
    echo "==> Installing udev rule for PN532 USB readers..."

    # The serial device group differs by distro: Debian uses dialout, Arch and
    # Fedora use uucp. Pick whichever this system actually has.
    SERIAL_GROUP=""
    for candidate in dialout uucp; do
        if getent group "$candidate" >/dev/null 2>&1; then
            SERIAL_GROUP="$candidate"
            break
        fi
    done

    # Both TAG+="uaccess" and a group are set, because neither covers every case:
    # uaccess grants an ACL to the user logged in at the seat (a SteamOS or Pi
    # desktop session), but 240-MP can also run headless from 240mp.service,
    # which has no seat and so gets no ACL — that case needs the group.
    {
        echo '# 240-MP: PN532 USB NFC readers (USB-serial bridge chips).'
        echo '# uaccess covers a desktop session; the group covers headless/systemd runs.'
        # vid:pid pairs of the USB-serial bridges PN532 modules are built on.
        # Kept in sync with kAllowedVidPids in src/modules/nfc_reader/SerialPort.cpp.
        for idpair in 1a86:7523 1a86:5523 1a86:55d4 10c4:ea60 \
                      0403:6001 0403:6015 067b:2303; do
            printf 'SUBSYSTEM=="tty", ATTRS{idVendor}=="%s", ATTRS{idProduct}=="%s", MODE="0660"%s, TAG+="uaccess"\n' \
                "${idpair%%:*}" "${idpair##*:}" "${SERIAL_GROUP:+, GROUP=\"$SERIAL_GROUP\"}"
        done
    } | sudo tee "$UDEV_RULE" > /dev/null
    echo "  ✓ wrote ${UDEV_RULE}"

    if [[ -n "$SERIAL_GROUP" ]]; then
        if id -nG "$AUTHORIZED_USER" 2>/dev/null | tr ' ' '\n' | grep -qx "$SERIAL_GROUP"; then
            echo "  ✓ ${AUTHORIZED_USER} is already in ${SERIAL_GROUP}"
        else
            sudo usermod -aG "$SERIAL_GROUP" "$AUTHORIZED_USER"
            echo "  ✓ added ${AUTHORIZED_USER} to ${SERIAL_GROUP}"
            NEEDS_RELOGIN=1
        fi
    else
        echo "  ⚠ no dialout or uucp group on this system - relying on uaccess alone"
    fi

    sudo udevadm control --reload-rules
    sudo udevadm trigger --subsystem-match=tty
    echo ""
fi

# ── PC/SC: daemon, CCID driver, and getting the kernel out of the way ────────
if [[ "${SKIP_PCSC:-0}" == "1" ]]; then
    : # explicitly skipped
elif [[ $IMMUTABLE == 1 ]]; then
    echo "==> Skipping PC/SC setup (immutable root filesystem)."
    echo "    pcscd and its CCID driver cannot be installed here, so PC/SC readers"
    echo "    like the ACR122U are not supported on this OS. Use a PN532 USB"
    echo "    reader instead - it is already set up by the step above."
    echo ""
elif [[ -z "$PKG" ]]; then
    echo "==> Skipping PC/SC setup (no supported package manager found)."
    echo "    Install pcscd and a CCID driver by hand if you need a PC/SC reader."
    echo ""
else
    echo "==> Installing PC/SC packages..."
    case "$PKG" in
        apt)
            sudo apt-get update -qq
            sudo apt-get install -y pcscd pcsc-tools libpcsclite1
            ;;
        pacman)
            sudo pacman -S --needed --noconfirm pcsclite ccid pcsc-tools
            ;;
        dnf)
            sudo dnf install -y pcsc-lite pcsc-lite-ccid pcsc-tools
            ;;
        zypper)
            sudo zypper --non-interactive install pcsc-lite pcsc-ccid pcsc-tools
            ;;
    esac
    echo "  (building 240-MP from source additionally needs the -dev/-devel package)"

    echo ""
    echo "==> Blacklisting pn533 kernel modules..."
    # The kernel's own NFC stack (pn533/pn533_usb) claims the ACR122U as soon as
    # it is plugged in, which blocks PC/SC from talking to it.
    sudo tee /etc/modprobe.d/blacklist-pn533.conf > /dev/null << 'EOF'
blacklist pn533
blacklist pn533_usb
EOF
    sudo modprobe -r pn533_usb pn533 2>/dev/null || true

    # The next two steps are Debian packaging specifics, not upstream pcsc-lite:
    # the PrivateUsers hardening and the org.debian.pcsc-lite.* polkit actions
    # only exist there, so applying them elsewhere would be inert at best.
    if [[ "$PKG" == "apt" ]]; then
        echo ""
        echo "==> Creating systemd override for pcscd (disable PrivateUsers)..."
        # PrivateUsers=identity isolates UID namespaces so non-root clients can't
        # connect. We disable it so user processes can use PC/SC.
        sudo mkdir -p /etc/systemd/system/pcscd.service.d
        sudo tee /etc/systemd/system/pcscd.service.d/override.conf > /dev/null << 'EOF'
[Service]
PrivateUsers=no
UMask=0022
EOF

        echo ""
        echo "==> Creating polkit rule to authorize ${AUTHORIZED_USER} for PC/SC..."
        sudo mkdir -p /etc/polkit-1/rules.d
        sudo tee /etc/polkit-1/rules.d/99-pcsc.rules > /dev/null << RULES
polkit.addRule(function(action, subject) {
    if (action.id == "org.debian.pcsc-lite.access_pcsc" &&
        subject.user == "${AUTHORIZED_USER}") {
        return polkit.Result.YES;
    }
});
polkit.addRule(function(action, subject) {
    if (action.id == "org.debian.pcsc-lite.access_card" &&
        subject.user == "${AUTHORIZED_USER}") {
        return polkit.Result.YES;
    }
});
RULES
    fi

    echo ""
    echo "==> Reloading systemd and restarting pcscd..."
    sudo systemctl daemon-reload
    sudo systemctl enable pcscd
    sudo systemctl restart pcscd
    echo ""
fi

# ── Report what is actually visible now ──────────────────────────────────────
echo "==> Checking for readers..."
FOUND=0

if [[ "${SKIP_PN532:-0}" != "1" ]]; then
    SERIAL_FOUND=0
    for dev in /dev/ttyUSB* /dev/ttyACM*; do
        [[ -e "$dev" ]] || continue
        SERIAL_FOUND=1
        if [[ -r "$dev" && -w "$dev" ]]; then
            echo "  ✓ ${dev} (readable/writable)"
            FOUND=1
        else
            echo "  ⚠ ${dev} found but not accessible as $(id -un) yet"
        fi
    done
    [[ $SERIAL_FOUND == 0 ]] && echo "  · no USB-serial devices present (plug in a PN532 USB reader)"
fi

if command -v pcsc_scan >/dev/null 2>&1; then
    sleep 1
    if sudo pcsc_scan -n 2>&1 | head -5 | grep -qi "reader"; then
        echo "  ✓ PC/SC reader detected:"
        sudo pcsc_scan -n 2>&1 | head -5 | sed 's/^/    /'
        FOUND=1
    else
        echo "  · no PC/SC reader found"
    fi
fi

echo ""
if [[ $NEEDS_RELOGIN == 1 ]]; then
    echo "Log out and back in (or reboot) for the new group membership to apply."
fi
if [[ $IMMUTABLE == 1 ]]; then
    echo "Note: this OS keeps /etc writable across updates, so the udev rule normally"
    echo "survives. A major OS release may reset it - re-run this script if the reader"
    echo "stops being detected after an update."
fi
if [[ $FOUND == 0 ]]; then
    echo "No reader is plugged in yet. Connect one and re-run the check with:"
    echo "  ls -l /dev/ttyUSB* /dev/ttyACM*    # PN532 USB"
    echo "  pcsc_scan                          # PC/SC"
fi
echo "Done. The NFC reader module is ready."
