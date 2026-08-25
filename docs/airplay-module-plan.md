# 240-MP — AirPlay Module Plan
**Version:** 0.2.0
**Repo:** https://github.com/anthonycaccese/240-MP
**Goal:** Let a user AirPlay audio (e.g. Apple Music) from an iPhone/iPad/Mac directly to a 240-MP device, with a Now Playing screen matching the retro VCR look of the other modules.
**Status:** Decisions locked in — ready to hand to Claude Code for implementation.

> **Discovered during implementation, not anticipated in the original plan below:**
> this module is **Raspberry Pi only**. `nqptp` — required for AirPlay 2 — supports
> Linux and FreeBSD only per its own documentation, with no macOS build path at all.
> See [module-airplay-wiki.md](module-airplay-wiki.md)'s "Platform support" section.

---

## Decisions (locked)

| Decision | Choice |
|---|---|
| AirPlay version | **AirPlay 2** from the start (not classic-only) |
| shairport-sync lifecycle | **Only runs while the AirPlay module screen is open** — starts on entry, stops on exit |
| Broadcast device name | Fixed: **`240-MP`** (no user-facing text setting needed for v1) |

Two implementation-level calls made to support those decisions (not asked about, but worth stating explicitly so Claude Code knows the reasoning):

- **nqptp runs as an always-on systemd service**, installed by a setup script — separate from the "only while screen open" app-controlled process. Reason: nqptp needs exclusive access to low-numbered ports 319/320, which typically requires root/`CAP_NET_BIND_SERVICE`. The 240-MP app itself shouldn't need elevated privileges just to open a module, so nqptp is provisioned once at install time (like NFC Reader's setup script) and left running in the background — it's idle/cheap when no AirPlay session is active. **shairport-sync itself** is what the app starts/stops with the module screen.
- **No ALSA conflict with mpv**: because AirPlay only runs while its own screen is open, and mpv only runs during video hand-off, the two never need the audio device at the same time. This is a nice side effect of the "only while open" choice, not a change you need to weigh in on.

---

## 1. How this fits the existing architecture

240-MP's philosophy: the shell browses, then hands off to a purpose-built external tool, and surfaces state back to QML via signals.

| Existing pattern | AirPlay module |
|---|---|
| `MpvController` launches `mpv` as a `QProcess`, controls it over a Unix socket, republishes `time-pos`/`duration` as signals | `AirPlayBackend` launches `shairport-sync` as a `QProcess` when the module opens, reads its metadata pipe, republishes track/artist/album/art/connection-state as signals |
| Local Files / Plex: browse a list → play an item → hand off | AirPlay: nothing to browse — the *phone* initiates. The module is a passive receiver + status display |
| Closest existing module shape | **Ambient Mode** / **Weather** — single live view, no `Items.qml` → `Detail.qml` flow |

Unlike mpv, shairport-sync does **not** need the fullscreen framebuffer / DRM-VT hand-off — it's audio-only, so the QML view stays on screen the whole time showing live status.

---

## 2. Dependencies

| Dependency | Purpose | Install notes |
|---|---|---|
| `shairport-sync` (built with `--with-airplay-2` and `--with-metadata`) | AirPlay 2 audio receiver | Distro `apt` packages are typically classic-only — build from source. See §5. |
| `nqptp` | Timing/sync companion, required for AirPlay 2 | Needs exclusive access to UDP ports 319/320. Runs as its own always-on systemd service (see Decisions above). |
| `avahi-daemon` | mDNS/Bonjour advertisement | Usually already present on Raspberry Pi OS; confirm on the SteamOS/Linux x86_64 target too. |
| FFmpeg (with an AAC decoder supporting `fltp`) | Required for AirPlay 2 buffered-audio formats | Confirm the target OS's FFmpeg build supports it — shairport-sync's own `TROUBLESHOOTING.md` has a check for this. |
| ALSA output device | Playback | Already required for mpv, so should be present. |

Per 240-MP's existing convention (YouTube needs `yt-dlp`, NFC Reader has `setup-nfc-reader.sh`), AirPlay should ship a **`scripts/setup-airplay.sh`** and a wiki "To Enable" page rather than bundling the build into the main app build.

---

## 3. manifest.json (final)

```json
{
  "id": "com.240mp.airplay",
  "name": "AirPlay",
  "icon": "assets/images/logo.svg",
  "entry_point_qml": "views/Root.qml",
  "settings": [
    {
      "key": "audio_output_device",
      "label": "Audio Output",
      "type": "list_single",
      "options_source": "dynamic",
      "options_slot": "getAudioDevices",
      "apply_slot": "applyAudioDeviceSetting"
    }
  ]
}
```

No device-name setting needed — it's fixed to `240-MP` in the shairport-sync config (see §5).

---

## 4. File/task checklist for Claude Code

Work through in this order — each step should leave the app in a buildable state.

### Step 1 — Module skeleton (no backend yet)
- [ ] `modules/airplay/manifest.json` (above)
- [ ] `modules/airplay/assets/images/logo.svg` — single-color `#ffffff` AirPlay glyph, matching the existing icon convention (colorized at runtime by `AppBar`)
- [ ] `modules/airplay/views/Root.qml` — copy the standard router pattern from another module (e.g. Ambient Mode), `id: moduleRoot`, navigates to `NowPlaying.qml` on `Component.onCompleted`
- [ ] `modules/airplay/views/NowPlaying.qml` — placeholder view with `AppBar` (iconSource/title from `moduleRoot`) and static "Waiting for AirPlay connection…" text
- [ ] Confirm the module shows up in the module list and opens/closes cleanly with **no backend registered yet** — this validates the pure-QML discovery path before adding C++.

### Step 2 — Backend process lifecycle (no metadata yet)
- [ ] `src/modules/airplay/AirPlayBackend.h/.cpp` — `QObject` subclass, constructed with `(appRoot, dataRoot)` like other backends
- [ ] `Q_INVOKABLE void startReceiver()` — launches `shairport-sync` as a `QProcess` with the flags in §5
- [ ] `Q_INVOKABLE void stopReceiver()` — terminates the `QProcess` cleanly (`terminate()` then `kill()` after a timeout)
- [ ] Destructor calls `stopReceiver()` as a safety net (app quit / crash shouldn't leave shairport-sync orphaned)
- [ ] `getAudioDevices()` — enumerates ALSA output devices (`aplay -L` or `/proc/asound/`), emits `dynamicOptionsReady("audio_output_device", [...])`
- [ ] `onSettingChanged(moduleId, key, value)` slot — if `audio_output_device` changes while connected, restart the receiver with the new device
- [ ] Wire into `main.cpp`: `AirPlayBackend airplayBackend(appRoot, dataRoot); appCore.registerModule("com.240mp.airplay", "airplayBackend", &airplayBackend, ctx);`
- [ ] Add new source files to `CMakeLists.txt`
- [ ] In `NowPlaying.qml`: `Component.onCompleted: airplayBackend.startReceiver()`, `Component.onDestruction: airplayBackend.stopReceiver()`
- [ ] **Test:** open the module, confirm shairport-sync process starts (`ps aux | grep shairport`) and the Pi appears as `240-MP` in an iPhone's AirPlay picker; back out of the module, confirm the process stops.

### Step 3 — Metadata pipe + Now Playing state
- [ ] Backend creates/opens the metadata FIFO shairport-sync writes to (`--metadata-pipename=<dataRoot>/airplay/metadata.pipe`)
- [ ] Async read loop (`QFile` + `QSocketNotifier`, or a `QLocalSocket`-style approach consistent with `MpvController`'s IPC handling) parses the tagged-chunk protocol (see §6)
- [ ] Expose `Q_PROPERTY`s: `trackTitle`, `artist`, `album`, `artworkPath`, `isConnected`, `isPlaying`, `senderName` (the connecting device's name, if available — nice touch for "Streaming from Anthony's iPhone")
- [ ] Emit a signal (e.g. `nowPlayingChanged()`) on any metadata update; connect in QML
- [ ] Album art: decode base64 payload, write to a temp file in `dataRoot`, expose path via `artworkPath` for QML `Image`
- [ ] **Test:** play a track from Apple Music to the Pi, confirm title/artist/album/art update live in the app.

### Step 4 — Now Playing UI polish
- [ ] Idle state: `240-MP` name shown prominently (device name to look for in Control Center), waiting animation/message matching the app's retro aesthetic
- [ ] Connected state: album art, title, artist, album, optionally `senderName`
- [ ] Use `root.sh`/`root.sw` for all sizing (per existing view rules), no hardcoded pixels
- [ ] Match color-scheme theming used by other modules (icon auto-colorized via `AppBar`)

### Step 5 — Setup script + docs
- [ ] `scripts/setup-airplay.sh` — installs build deps, builds `nqptp` and `shairport-sync` (with `--with-airplay-2 --with-metadata`), installs `nqptp` as an enabled systemd service, verifies FFmpeg `fltp` support, writes the shairport-sync config template used by the backend
- [ ] Wiki page draft: `Module: AirPlay`, with a "To Enable" section mirroring NFC Reader's/YouTube's format
- [ ] Update root `README.md` module list/count and `INSTALL.md` if it references per-module setup

---

## 5. shairport-sync invocation

Config file (`<dataRoot>/airplay/shairport-sync.conf`, written/managed by the backend or setup script):

```
general = {
  name = "240-MP";
};
alsa = {
  output_device = "<selected ALSA device>";
};
metadata = {
  enabled = "yes";
  include_cover_art = "yes";
  pipe_name = "<dataRoot>/airplay/metadata.pipe";
};
```

`QProcess` launch (roughly):

```
shairport-sync --configfile=<dataRoot>/airplay/shairport-sync.conf
```

Build flags for the setup script:

```
./configure --with-alsa --with-avahi --with-ssl=openssl \
  --with-systemd --with-metadata --with-airplay-2 \
  --sysconfdir=<install path>
```

nqptp, installed separately as its own systemd service (not app-managed):

```
git clone https://github.com/mikebrady/nqptp.git && cd nqptp
autoreconf -fi && ./configure --with-systemd-startup
make && sudo make install
sudo systemctl enable --now nqptp
```

---

## 6. Metadata pipe format (for Step 3)

> **Corrected post-implementation** (this section's `pfls` guess was wrong —
> see below) against the real wire format: `metadata/pipe.c`'s format string
> (`<item><type>%x</type><code>%x</code><length>%u</length>`, `type`/`code`
> hex-encoded, unpadded — safe to treat as always 8 hex digits since every
> real tag is 4 printable-ASCII characters, none of which hex-encode with a
> leading zero nibble) and the code table in
> [`shairport-sync-metadata-reader.c`](https://github.com/mikebrady/shairport-sync-metadata-reader/blob/master/shairport-sync-metadata-reader.c).

shairport-sync's metadata pipe emits a stream of tagged chunks, each carrying a 4-character `type` and 4-character `code`, plus a base64-encoded `data` payload.

| type | code | Meaning |
|---|---|---|
| `core` | `minm` | Track title |
| `core` | `asar` | Artist |
| `core` | `asal` | Album |
| `ssnc` | `PICT` | Album artwork (binary, base64-encoded) |
| `ssnc` | `pbeg` | Playback/session begin (client connected) |
| `ssnc` | `pend` | Playback/session end (client disconnected) |
| `ssnc` | `paus` | Paused — **not** `pfls` as originally guessed here; that code doesn't exist |
| `ssnc` | `prsm` / `pres` | Resumed (classic AirPlay / AirPlay 2 respectively — both handled the same) |
| `ssnc` | `snam` | Connecting device's name (e.g. "Anthony's iPhone") — this is `senderName`, left as a TODO in the original plan below |

The backend should treat `pbeg` as `isConnected = true` and `pend` as `isConnected = false` (drives the idle ↔ connected UI state), and update the relevant `Q_PROPERTY` whenever `core`/`minm`, `asar`, `asal`, `PICT`, or `snam` arrive.

---

## 7. Risks / caveats

- shairport-sync/nqptp are reverse-engineered, unofficial implementations of Apple's protocol — not licensed by Apple.
- AirPlay 2 build has heavier requirements (FFmpeg `fltp` AAC decode, ports 319/320 free) — confirm on a Pi 3B+ specifically, since it's the lower end of 240-MP's supported hardware; community reports suggest even a Pi Zero W works, so this is likely fine.
- Only one AirPlay 2 shairport-sync instance can run per system — not a concern for 240-MP's single-purpose use.
- Metadata pipe field set above is not exhaustive/guaranteed exact — validate against shairport-sync's current docs during Step 3.

---

## Changelog

| Version | Date | Change |
|---|---|---|
| 0.1.0 | 2026-08-23 | Initial plan drafted, feasibility confirmed, open decisions identified. |
| 0.2.0 | 2026-08-23 | Decisions locked (AirPlay 2, screen-lifecycle-bound, fixed device name). Added concrete manifest, backend design, shairport-sync/nqptp invocation details, metadata pipe format, and an ordered implementation checklist for Claude Code. |
