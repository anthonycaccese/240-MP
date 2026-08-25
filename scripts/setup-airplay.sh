#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# 240-MP AirPlay Setup — installs shairport-sync (AirPlay 2) + nqptp
#
# Tries a prebuilt binary release first (built by .github/workflows/release.yml
# on every tagged 240-MP release, for arm64 Raspberry Pi OS specifically); falls
# back to building both from source if none is available — different CPU arch,
# no network, or the release simply predates this pipeline. Distro packages for
# shairport-sync are typically classic-AirPlay-only, so a source build always
# uses --with-airplay-2 --with-metadata regardless of which path ran.
#
# nqptp needs exclusive access to UDP ports 319/320 — which typically requires
# root/CAP_NET_BIND_SERVICE — so it's installed once here as an always-on
# systemd service rather than something the app itself manages. shairport-sync
# is NOT installed as a service either way: the 240-MP app starts/stops it
# itself as a subprocess, only while the AirPlay module screen is open.
#
# Run it from a checkout:
#   sudo bash scripts/setup-airplay.sh
#
# Or standalone, without cloning:
#   curl -fsSL https://github.com/anthonycaccese/240-mp/releases/latest/download/setup-airplay.sh | sudo bash
#
# Env overrides:
#   DATA_ROOT                  defaults to ~/.local/share/240-MP for the
#                               invoking user, matching the app's own default
#                               (QStandardPaths::AppDataLocation)
#   AIRPLAY_FORCE_SOURCE_BUILD set to 1 to always build from source, skipping
#                               the prebuilt-binary download entirely — for
#                               testing a new version pin before it's published
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "This script configures Linux only (builds nqptp/shairport-sync + a systemd service)."
    echo "On macOS, build shairport-sync yourself with --with-airplay-2 --with-metadata and"
    echo "point AirPlayBackend at it; there is no equivalent of nqptp's port requirement to"
    echo "work around there since macOS AirPlay development is not a supported 240-MP target."
    exit 0
fi

if [[ $EUID -ne 0 ]]; then
    echo "This script installs system packages and a systemd service; re-run with sudo." >&2
    exit 1
fi

# Under sudo, $HOME is root's — resolve the invoking user's home so the data
# root written below matches where the app itself will actually look. Only
# needed when DATA_ROOT isn't already overridden: if getent can't find the
# invoking user (no SUDO_USER and an unresolvable $USER — unusual, but seen
# in minimal/container environments), fail loudly rather than silently
# falling back to an empty INVOKING_HOME, which would put DATA_ROOT at the
# filesystem root ("/.local/share/240-MP") instead of erroring out.
INVOKING_USER="${SUDO_USER:-$USER}"
if [[ -z "${DATA_ROOT:-}" ]]; then
    INVOKING_HOME="$(getent passwd "$INVOKING_USER" | cut -d: -f6)"
    if [[ -z "$INVOKING_HOME" ]]; then
        echo "Could not resolve a home directory for user '$INVOKING_USER' (getent passwd found nothing)." >&2
        echo "Set DATA_ROOT explicitly and re-run, e.g.:" >&2
        echo "  sudo DATA_ROOT=/home/pi/.local/share/240-MP ./scripts/setup-airplay.sh" >&2
        exit 1
    fi
    DATA_ROOT="$INVOKING_HOME/.local/share/240-MP"
fi

# Pinned to the exact tags this module has actually been built and tested
# against (real hardware, real AirPlay session) — not each repo's moving
# default branch. `mikebrady/shairport-sync` in particular carries newer
# `v2.x`-style tags whose compatibility with this module has never been
# checked; drifting onto them silently on every fresh setup would be a
# regression risk with no upside. Bump deliberately, re-test, then update
# both this pin and CI's matching one in .github/workflows/release.yml.
NQPTP_VERSION="1.2.8"
SHAIRPORT_SYNC_VERSION="5.2.2"

AIRPLAY_DEPS_ASSET="240-MP-airplay-deps-linux-arm64.tar.gz"
AIRPLAY_DEPS_URL="https://github.com/anthonycaccese/240-mp/releases/latest/download/${AIRPLAY_DEPS_ASSET}"

# Downloads and installs the prebuilt nqptp/shairport-sync binaries CI
# produces for arm64 (see .github/workflows/release.yml's airplay-deps job).
# Returns 1 — never fatal, `set -e` and all — on anything that means "no
# prebuilt binary available here", so the caller falls back to a source build.
install_prebuilt_airplay_deps() {
    if [[ "${AIRPLAY_FORCE_SOURCE_BUILD:-}" == "1" ]]; then
        return 1
    fi
    case "$(uname -m)" in
        aarch64|arm64) ;;
        *) return 1 ;; # only published for arm64 (the Raspberry Pi target)
    esac

    local tmp
    tmp="$(mktemp -d)"

    if ! curl -fsSL "$AIRPLAY_DEPS_URL" -o "$tmp/deps.tar.gz" 2>/dev/null; then
        rm -rf "$tmp"
        return 1 # no release published yet, or no network — fine, not fatal
    fi
    if ! tar -xzf "$tmp/deps.tar.gz" -C "$tmp" 2>/dev/null; then
        echo "WARNING: downloaded airplay-deps archive was corrupt — falling back to source build." >&2
        rm -rf "$tmp"
        return 1
    fi
    if [[ ! -x "$tmp/nqptp" || ! -x "$tmp/shairport-sync" || ! -f "$tmp/nqptp.service" ]]; then
        echo "WARNING: airplay-deps archive is missing expected files — falling back to source build." >&2
        rm -rf "$tmp"
        return 1
    fi

    install -m 0755 "$tmp/nqptp" /usr/local/bin/nqptp
    install -m 0755 "$tmp/shairport-sync" /usr/local/bin/shairport-sync
    mkdir -p /usr/local/lib/systemd/system
    install -m 0644 "$tmp/nqptp.service" /usr/local/lib/systemd/system/nqptp.service
    systemctl daemon-reload
    rm -rf "$tmp"
    return 0
}

# Builds both from source — the always-correct fallback. Slower (a few
# minutes: autoreconf + compile + a dozen -dev packages) and only reachable
# when no prebuilt binary matched, but it's what makes the prebuilt path safe
# to add at all: nothing regresses for anyone it doesn't help yet.
build_airplay_deps_from_source() {
    echo "==> Installing build dependencies"
    apt-get update
    apt-get install -y --no-install-recommends \
        build-essential git autoconf automake libtool pkg-config \
        libpopt-dev libconfig-dev libasound2-dev libavahi-client-dev \
        libssl-dev libsoxr-dev libplist-dev libplist-utils libsodium-dev \
        libavcodec-dev libavformat-dev libavutil-dev libgcrypt-dev xxd

    local build_dir
    build_dir="$(mktemp -d)"
    trap 'rm -rf "$build_dir"' RETURN

    echo "==> Building nqptp $NQPTP_VERSION (timing companion, required for AirPlay 2)"
    git clone --depth 1 --branch "$NQPTP_VERSION" https://github.com/mikebrady/nqptp.git "$build_dir/nqptp"
    (
        cd "$build_dir/nqptp"
        autoreconf -fi
        ./configure --with-systemd-startup
        make -j"$(nproc)"
        make install
    )

    echo "==> Building shairport-sync $SHAIRPORT_SYNC_VERSION (AirPlay 2 + metadata)"
    git clone --depth 1 --branch "$SHAIRPORT_SYNC_VERSION" https://github.com/mikebrady/shairport-sync.git "$build_dir/shairport-sync"
    (
        cd "$build_dir/shairport-sync"
        autoreconf -fi
        ./configure --with-alsa --with-avahi --with-ssl=openssl \
            --with-metadata --with-airplay-2
        make -j"$(nproc)"
        make install
    )
}

if install_prebuilt_airplay_deps; then
    echo "==> Installed prebuilt nqptp $NQPTP_VERSION and shairport-sync $SHAIRPORT_SYNC_VERSION"
else
    echo "==> No prebuilt binaries available for this system — building from source instead"
    build_airplay_deps_from_source
fi

echo "==> Installing avahi-daemon (AirPlay discovery) and alsa-utils (audio self-test)"
apt-get update
apt-get install -y --no-install-recommends avahi-daemon alsa-utils
systemctl enable --now avahi-daemon || echo "WARNING: could not enable avahi-daemon; AirPlay discovery may not work" >&2
systemctl enable --now nqptp

echo "==> Checking FFmpeg AAC support (required for AirPlay 2 buffered audio)"
if command -v ffmpeg >/dev/null 2>&1; then
    # Captured to a variable rather than piped straight into `grep -q`: with
    # `set -o pipefail`, grep -q's habit of exiting (and closing its end of
    # the pipe) the instant it finds a match races ffmpeg's still-writing
    # process into a SIGPIPE — ffmpeg then exits nonzero *despite* the match
    # being real, and pipefail propagates that as a false-positive failure.
    ffmpeg_decoders="$(ffmpeg -hide_banner -decoders 2>/dev/null || true)"
    if ! grep -qi 'aac' <<< "$ffmpeg_decoders"; then
        echo "WARNING: system ffmpeg does not report an AAC decoder — AirPlay 2 buffered" >&2
        echo "         audio formats may fail. See shairport-sync's TROUBLESHOOTING.md." >&2
    fi
else
    echo "WARNING: ffmpeg not found on PATH — install it and re-check AAC support." >&2
fi

echo "==> Testing audio output devices"
# Plays a brief, audible test tone through every plughw: device ALSA can see —
# the same device family the app's Audio Output setting offers (see
# AirPlayBackend::listPlughwDevices()'s comment for why only plughw:, not raw
# hw:/dmix:/default). This exists because AirPlay itself gives no useful
# feedback when audio silently fails: it can connect and show track metadata
# perfectly with zero sound reaching a speaker, and the only way to know
# *before* trying AirPlay from a phone is to actually play something. On a
# multi-output Pi (e.g. two HDMI ports) this also tells you up front which
# one is actually live, rather than guessing through the app's settings list.
if command -v speaker-test >/dev/null 2>&1 && command -v aplay >/dev/null 2>&1; then
    device_list="$(aplay -L 2>/dev/null | grep '^plughw:' || true)"
    if [[ -z "$device_list" ]]; then
        echo "WARNING: no plughw: audio devices found — aplay -L reports nothing usable." >&2
        echo "         Audio hardware may not be configured; the Audio Output setting will" >&2
        echo "         have nothing to offer until this is resolved." >&2
    else
        while IFS= read -r device; do
            echo "    Testing $device (you should hear a brief tone)..."
            if timeout 5 speaker-test -D "$device" -c2 -twav -l1 >/dev/null 2>&1; then
                echo "    -> OK: $device produced audio without error"
            else
                echo "    -> FAILED: $device did not play cleanly — this device will likely" >&2
                echo "       fail the same way in the AirPlay module. If it's HDMI output and" >&2
                echo "       this is the only device, check that the display/receiver on that" >&2
                echo "       port is actually powered on and selected as its active input —" >&2
                echo "       some hardware won't accept audio-only HDMI otherwise." >&2
            fi
        done <<< "$device_list"
    fi
else
    echo "WARNING: speaker-test/aplay not found (usually part of alsa-utils) — skipping" >&2
    echo "         the audio self-test. Install alsa-utils to enable it next run." >&2
fi

echo "==> Writing shairport-sync config template"
mkdir -p "$DATA_ROOT/airplay"
cat > "$DATA_ROOT/airplay/shairport-sync.conf" <<EOF
general = {
  name = "240-MP";
};
alsa = {
};
metadata = {
  enabled = "yes";
  include_cover_art = "yes";
  pipe_name = "$DATA_ROOT/airplay/metadata.pipe";
};
EOF
# The whole data root, not just airplay/: running as root, the `mkdir -p`
# above creates $DATA_ROOT itself (and its own parents) if this is the very
# first time anything has touched it — e.g. setup-airplay.sh run before the
# app has ever been launched once as the normal user. Chowning only the
# subfolder left $DATA_ROOT root-owned in that case, so the app (running
# unprivileged) couldn't write config.json right next to it — caught on a
# fresh Pi 3B install.
chown -R "$INVOKING_USER" "$DATA_ROOT" 2>/dev/null || true
echo "    -> $DATA_ROOT/airplay/shairport-sync.conf"
echo "    (AirPlayBackend overwrites this file on every module open, based on the app's"
echo "     Audio Output setting; this is just a starting template.)"

echo "==> Done. nqptp is running as a system service; shairport-sync will be started"
echo "    on demand by the 240-MP app when the AirPlay module is opened."
