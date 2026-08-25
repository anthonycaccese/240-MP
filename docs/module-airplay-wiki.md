# Module: AirPlay

> **Raspberry Pi only.** This module is not supported on macOS or on
> SteamOS/Linux x86_64. See "Platform support" below for why.

Lets a user AirPlay audio — Music, Podcasts, or any app that sends audio through the
system AirPlay picker — from an iPhone/iPad/Mac directly to a 240-MP device. The
device advertises itself as **`240-MP`** by default (renameable — see below) and
shows a Now Playing screen matching the app's retro VCR look. Audio only — this does
not support AirPlay video/screen mirroring, which is a different protocol entirely.

AirPlay 2 is supported. shairport-sync only runs while the AirPlay module screen is
open — it starts when you enter the module and stops when you back out.

### Platform support

**Raspberry Pi OS only.** AirPlay 2 needs `nqptp`, a timing daemon shairport-sync
depends on for its PTP-based clock sync — and `nqptp`'s own documentation states it
supports **Linux and FreeBSD only**, with no macOS build path at all. That's a hard
limitation of `nqptp` itself, not something 240-MP can work around.

Separately, `scripts/setup-airplay.sh` builds `shairport-sync`/`nqptp` from source
using `apt`, so it's specifically a Debian/Raspberry Pi OS script — it won't run
as-is on SteamOS (Arch-based) or other non-Debian distros, and SteamOS's immutable
root filesystem makes a source build impractical there regardless (the same problem
the NFC Reader module's PC/SC path hits).

Net result: this module targets Raspberry Pi OS specifically, out of 240-MP's three
supported platforms (macOS, Raspberry Pi OS, SteamOS/Linux x86_64 AppImage).

### Renaming the device

There's no on-screen text-entry setting for this (the settings screen is designed for
remote/gamepad navigation, not typing). Instead, edit a plain text file:

```
<data directory>/airplay/device_name.txt
```

containing just the name you want (e.g. `Living Room`). Re-open the AirPlay module to
pick it up — no app restart needed. Leave the file absent/empty to keep the default
`240-MP`.

### Settings

- **Audio Output** — which ALSA device to play through. "Default" auto-detects a
  working device; only `plughw:`-style device options are offered, since raw
  `hw:`/`dmix:`/`default` ALSA devices have been observed to crash shairport-sync
  outright on some hardware (Raspberry Pi HDMI output in particular) rather than
  failing gracefully.
- **Album Art** — off shows a generic tinted cassette-tape icon instead of
  artwork. Originally intended as a per-service logo (Spotify/Apple
  Music/Podcasts/etc.), but AirPlay's metadata protocol has no field
  identifying which app is streaming — it's source-agnostic by design, so
  there's nothing to key a per-service icon off. A generic placeholder is
  what's actually achievable.
- **Allow Screen Saver** — off (the default) blocks 240-MP's screen saver while
  this module's screen is open, matching every other screen-owning module (video
  playback, Weather's rotation). Since AirPlay is audio-only, turn this on to let
  the screen saver activate normally while music keeps playing in the background.

## To Enable

**Raspberry Pi OS only — see "Platform support" above.** `scripts/setup-airplay.sh`
will refuse to run (with an explanation) on macOS or any non-Linux system.

AirPlay requires a native build of `shairport-sync` (AirPlay 2 + metadata) and its
timing companion `nqptp`, neither of which ship as prebuilt distro packages with the
flags 240-MP needs. On a fresh Pi, refresh the package lists first:

```bash
sudo apt update && sudo apt-get update
```

(If that or the setup script itself then errors with something like `files list
file for package '...' is missing final newline`, that's package-database
corruption from the imaging process, not an AirPlay problem — fix with
`for f in /var/lib/dpkg/info/*.list; do sudo sed -i -e '$a\' "$f"; done` before
continuing. Seen on a freshly-imaged Pi 3B.)

Then run the setup script once after installing 240-MP:

```bash
sudo ./scripts/setup-airplay.sh
```

On arm64 (a real Raspberry Pi) this tries downloading a prebuilt copy of both from
the latest 240-MP release first — seconds, no compiler needed — and only falls back
to building from source (a few minutes, a dozen `-dev` packages) if that's
unavailable, e.g. no network, or a release published before this existed. Either
path installs the exact same pinned versions and ends up in the same place; nothing
else about setup changes based on which one ran. Set `AIRPLAY_FORCE_SOURCE_BUILD=1`
to always build from source.

This installs:

- **nqptp**, as an always-on systemd service (`systemctl status nqptp` to confirm) —
  it needs exclusive access to UDP ports 319/320, which requires elevated privileges,
  so it's provisioned once at install time rather than run by the app itself.
- **shairport-sync**, built from source with `--with-airplay-2 --with-metadata`. The
  240-MP app starts and stops this process itself, only while the AirPlay module
  screen is open — it does not run as a background service.
- **avahi-daemon**, for AirPlay discovery (mDNS/Bonjour), if not already present.

The script also plays a brief, audible test tone through every audio output device
it finds — AirPlay itself gives no useful feedback when sound silently fails to
reach a speaker (it can connect and show track metadata perfectly with nothing
audible), so this catches that class of problem immediately, at setup time, rather
than leaving you to debug it later through the app. On a Pi with more than one
output (e.g. two HDMI ports) it also tells you up front which one is actually live.

By default the script writes its data under `~/.local/share/240-MP/airplay`,
matching the app's own default data directory; the `DATA_ROOT` environment
variable overrides it if your 240-MP install uses a different one.

### Verifying it worked

1. Open the AirPlay module on the device.
2. On an iPhone/iPad/Mac, open Control Center (or an app's AirPlay picker) and look
   for **240-MP** in the list of AirPlay devices.
3. Start playing audio (e.g. from Apple Music) and select 240-MP as the output.
4. The Now Playing screen should update with the track title, artist, album, and
   artwork within a couple of seconds.

### Troubleshooting

- **`[AppCore] Could not write config.json: Permission denied`**: `setup-airplay.sh`
  runs as root and can end up creating `$DATA_ROOT` itself (not just its
  `airplay/` subfolder) if that's the very first thing to ever touch it —
  leaving it root-owned, so the app (running as your normal user) can't write
  next to it. Fixed in current versions of the script; if you hit this anyway,
  `sudo chown -R "$USER" ~/.local/share/240-MP` resolves it immediately.
- **240-MP doesn't show up in the AirPlay picker**: confirm `avahi-daemon` is
  running (`systemctl status avahi-daemon`) and that the device and phone are on the
  same network/subnet (AirPlay discovery is mDNS-based and doesn't cross subnets).
- **Device shows up but audio doesn't play / connection drops**: confirm `nqptp` is
  running (`systemctl status nqptp`) — AirPlay 2 timing sync depends on it having
  exclusive access to ports 319/320. `sudo ss -ulnp | grep -E '319|320'` should show
  only `nqptp` bound to those ports.
- **Audio plays but no metadata/artwork appears**: check that shairport-sync was
  built with `--with-metadata` (`shairport-sync -V` lists enabled features) and that
  the metadata pipe path in `AirPlayBackend`'s generated config
  (`<data-root>/airplay/shairport-sync.conf`) is writable.
- **No sound at all**: pick a specific output device from the module's Audio Output
  setting rather than relying on "Default" — some hardware's ALSA default device
  doesn't do the format/rate conversion AirPlay needs. Only `plughw:`-style devices
  are offered in that list for exactly this reason (see the next item).
- **"AirPlay receiver couldn't start" screen**: shairport-sync either failed to
  launch at all, or exited/crashed before ever reaching a connected session. The
  message on screen is shairport-sync's/Qt's own error text, not a generic one —
  read it first. Common causes:
  - **`shairport-sync failed to start: ... No such file or directory`** — the
    binary isn't installed or isn't on the app's `PATH`. Run
    `sudo ./scripts/setup-airplay.sh` (see "To Enable" above) if you haven't, or
    confirm `which shairport-sync` resolves for the user/session running 240-MP.
  - **`shairport-sync crashed before connecting`** — most commonly the selected
    ALSA output device rejecting the format shairport-sync requested (seen in
    testing as ALSA error `-524`/`ENOTSUPP` immediately followed by a segfault,
    on Raspberry Pi HDMI output specifically). This is why the Audio Output
    setting only offers `plughw:` devices — if you're somehow still pointed at a
    raw `hw:`/`dmix:`/`default` device (e.g. an old saved setting from before that
    filtering existed), switch to a `plughw:` one from the list.
  - For more detail than the on-screen message, run shairport-sync directly with
    the same config file 240-MP generates and full logging:
    ```bash
    shairport-sync --configfile=<data-root>/airplay/shairport-sync.conf --use-stderr -v
    ```
    (quit the 240-MP app first, so it isn't also trying to run its own instance).

## Known limitations (v1)

- Renaming the device is a text file, not a settings-screen entry — see "Renaming
  the device" above for why.
- With Album Art off, the Now Playing screen shows a generic icon rather than a
  per-streaming-service one — AirPlay's metadata protocol never identifies which
  app is casting, only track/artist/album, so there's no way to tell Spotify
  apart from Apple Music or anything else.
- No remote control (play/pause/skip) from 240-MP back to the connected
  phone — shairport-sync's own docs are explicit that remote control isn't
  available for AirPlay 2 sessions at all, only classic AirPlay.
- Only one AirPlay session is supported at a time (a shairport-sync limitation, not
  240-MP-specific).
- shairport-sync/nqptp are unofficial, reverse-engineered implementations of Apple's
  AirPlay protocol and are not licensed or endorsed by Apple.
