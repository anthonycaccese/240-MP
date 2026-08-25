# Testing the AirPlay module on a Raspberry Pi

A Pi (or other Linux box) is required here, not optional for convenience — AirPlay
2 depends on `nqptp`, whose own docs state it supports Linux and FreeBSD only, with
no macOS build at all. That's why this can't be tested end-to-end on the dev Mac
directly; see [module-airplay-wiki.md](module-airplay-wiki.md)'s "Platform support"
section for the full explanation.

This is uncommitted work-in-progress in a local clone on the dev machine, so
step 1 is getting it onto the Pi. Pick whichever of A/B is easier; everything
after that is the same.

## 1. Get the code onto the Pi

**Option A — rsync straight from the Mac (fastest, no GitHub involved):**

```bash
# Run on the Mac, from the 240-MP checkout:
rsync -av --exclude=build --exclude=.git \
  ./ pi@<pi-hostname-or-ip>:~/240-mp/
```

**Option B — commit to a branch and pull on the Pi:**

```bash
# On the Mac:
git checkout -b airplay-module
git add -A
git commit -m "Add AirPlay module"
git push -u origin airplay-module   # needs push access / a fork

# On the Pi:
git clone -b airplay-module https://github.com/<you>/240-MP.git ~/240-mp
```

Option A is better for quick iteration since nothing needs to be committed
first; switch to B once the module is far enough along to want history and a
PR.

## 2. Install build prerequisites (one-time, on the Pi)

Assume this is a fresh install — refresh the package lists first:

```bash
sudo apt update && sudo apt-get update
```

(If `dpkg`/`apt-get` then errors with something like `files list file for
package '...' is missing final newline`, that's package-database corruption
unrelated to 240-MP, not this step — fix with
`for f in /var/lib/dpkg/info/*.list; do sudo sed -i -e '$a\' "$f"; done`
before continuing.)

Standard 240-MP Pi prerequisites — see [BUILDING.md](../BUILDING.md) for the
full list/detail if anything here is out of date:

```bash
sudo apt-get install -y \
  build-essential cmake \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-window \
  libqt6svg6 qt6-svg-dev qt6-svg-plugins qt6-wayland \
  libdrm-dev libxkbcommon-dev libssl-dev \
  libsdl2-dev \
  mpv
```

## 3. Install AirPlay's own dependencies (`nqptp` + `shairport-sync`)

This is the step that only works on Linux (nqptp has no macOS/FreeBSD-other
path) — it's the whole reason this needs a Pi rather than continuing on the
Mac. From the checkout:

```bash
cd ~/240-mp
sudo bash scripts/setup-airplay.sh
```

This builds and installs `nqptp` as an always-on systemd service (needs root
for UDP ports 319/320) and `shairport-sync` from source with
`--with-airplay-2 --with-metadata`. It takes a few minutes — it's compiling
both from source, autoreconf included. Expect it to be noticeably slower than
a laptop; a Pi 4 should still be well under 10 minutes, a Pi 3B+ longer.

Sanity-check both are actually there afterward:

```bash
systemctl status nqptp          # should be active (running)
shairport-sync -V               # should print a version string
```

⚠️ If `shairport-sync -V`'s feature string doesn't include something like
`AirPlay2` and `Metadata`, the build didn't pick up the flags — check the
script's output for a `configure` failure above the noise, most likely a
missing `-dev` package (`libplist-dev`, `libsodium-dev`, `libavahi-client-dev`,
`libgcrypt-dev`, `libavcodec-dev`/`libavformat-dev`/`libavutil-dev` for FFmpeg
AAC decode) that apt didn't have available. Compare against the flags in
[scripts/setup-airplay.sh](../scripts/setup-airplay.sh) and shairport-sync's
own `TROUBLESHOOTING.md` if apt-installed FFmpeg turns out not to support
`fltp`.

## 4. Build 240-MP

```bash
cd ~/240-mp
cmake -B build
cmake --build build
```

## 5. Run it

With a desktop (RPi OS Full):

```bash
APP_ROOT=$(pwd) ./build/240mp
```

Without a desktop (RPi OS Lite, no display server):

```bash
APP_ROOT=$(pwd) QT_QPA_PLATFORM=eglfs ./build/240mp
```

## 6. Test with a real phone

1. Open the AirPlay module from the module list.
2. On an iPhone/iPad/Mac on the **same network**, open Control Center (or an
   app's AirPlay picker) and look for **240-MP**.
3. Select it and play something (Apple Music, a podcast, anything).
4. Expect: the Now Playing screen updates with title/artist/album/art within
   a second or two of playback starting, and "Streaming from `<device name>`"
   appears under the album info.
5. Stop playback / disconnect AirPlay from the phone — the screen should
   drop back to the "Waiting for AirPlay connection…" idle state.
6. Back out of the module (`Esc`/gamepad Back) — confirm the process actually
   stops: `ps aux | grep shairport-sync` should show nothing running once
   you're back at the module list. (This was verified logically and via the
   automated test, but a real hardware confirmation is worth doing once.)

### If 240-MP doesn't show up in the AirPlay picker

- `systemctl status avahi-daemon` — mDNS discovery depends on it.
- Same subnet — AirPlay discovery doesn't cross subnets/VLANs; a guest
  Wi-Fi network is a common culprit.
- `sudo ss -ulnp | grep -E '319|320'` should show only `nqptp` bound to
  those ports — anything else there is a conflict.

### If it connects but there's no audio

- The default ALSA device may not be right for the Pi's actual output (HDMI
  vs. headphone jack vs. USB DAC). Open the AirPlay module's settings and
  pick a specific device from **Audio Output** rather than relying on
  "Default" — `aplay -L` from a terminal shows the same list the setting
  populates from, if you want to check ahead of time.

### If metadata/artwork doesn't show up but audio plays fine

- Confirm the build has metadata support: `shairport-sync -V` should list
  `Metadata`.
- Check `<DATA_ROOT>/airplay/shairport-sync.conf` — `pipe_name` there should
  match what `AirPlayBackend` is reading from (it writes this file itself
  each time the module opens, so this would only mismatch if something else
  is also writing to it).

## 7. Faster iteration without a phone (optional)

The dev tooling from `scripts/dev/` works identically on the Pi — useful if
you want to re-check the UI/backend after a code change without walking over
to grab your phone every time:

```bash
scripts/dev/run-airplay-smoke-test.sh      # ~15s automated check, no UI
scripts/dev/run-with-fake-airplay.sh       # full GUI, simulated session
```

See [scripts/dev/README.md](../scripts/dev/README.md) for what each covers
and doesn't. Once the module is confirmed working against a real phone once,
these are the faster loop for everything after.
