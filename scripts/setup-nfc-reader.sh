#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# 240-MP NFC Reader Setup — installs and configures PC/SC for the ACR122U
#
# Run this on a fresh OS install before building 240-MP:
#   bash scripts/setup-nfc-reader.sh
#
# Or pass a username to authorize (defaults to $USER):
#   bash scripts/setup-nfc-reader.sh pi
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

# Under sudo, $USER is root — fall back to the invoking user so the polkit
# rules authorize the account that will actually run 240-MP.
AUTHORIZED_USER="${1:-${SUDO_USER:-$USER}}"

echo "==> Installing PC/SC build & runtime packages..."
sudo apt-get update -qq
sudo apt-get install -y \
    libpcsclite-dev \
    pcscd \
    pcsc-tools

echo ""
echo "==> Blacklisting pn533 kernel modules..."
# The kernel's NFC stack (pn533/pn533_usb) claims the ACR122U as soon as it
# is plugged in, which blocks PC/SC from talking to it.
sudo tee /etc/modprobe.d/blacklist-pn533.conf > /dev/null << 'EOF'
blacklist pn533
blacklist pn533_usb
EOF
sudo modprobe -r pn533_usb pn533 2>/dev/null || true

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

echo ""
echo "==> Reloading systemd and restarting pcscd..."
sudo systemctl daemon-reload
sudo systemctl enable pcscd
sudo systemctl restart pcscd

echo ""
echo "==> Verifying reader detection..."
sleep 1
if sudo pcsc_scan -n 2>&1 | head -5 | grep -qi "reader"; then
    echo "  ✓ Reader detected!"
    sudo pcsc_scan -n 2>&1 | head -5
else
    echo "  ⚠ No reader found. Check USB connection and try: sudo pcsc_scan"
fi

echo ""
echo "Done. The NFC reader module is ready."
