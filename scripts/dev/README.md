# AirPlay module dev tooling

Ways to test the AirPlay module without a real AirPlay-2 `shairport-sync` build
(heavy — see `scripts/setup-airplay.sh`) or an actual phone in the room.

## `fake-airplay/shairport-sync`

A bash script standing in for the real binary. `AirPlayBackend` launches
whatever `shairport-sync` resolves to on `PATH` — this script doesn't speak
AirPlay at all, it just opens the metadata pipe `AirPlayBackend` already
created and writes shairport-sync's tagged-chunk protocol to it, looping
through a simulated "phone connects, three tracks play, phone disconnects"
session until killed. Must stay named exactly `shairport-sync` (PATH lookup
is by that bare name) and first on `PATH`.

## `run-airplay-smoke-test.sh`

```bash
scripts/dev/run-airplay-smoke-test.sh
```

Builds and runs `tests/airplay/smoke_test.cpp` — a small QML-free executable
(`-DAIRPLAY_BUILD_TESTS=ON`, off by default) that drives the real
`AirPlayBackend` class directly against the fake binary above: starts it,
asserts `isConnected`/`trackTitle`/`artist`/`album`/`artworkPath` update
correctly as fake metadata arrives, then stops it and asserts a clean
shutdown with no process errors. Takes about 15 seconds (mostly the fake
script's built-in per-track delay), exits 0/1. Good for catching regressions
in the process lifecycle or metadata parser without touching QML or a phone.

## `run-with-fake-airplay.sh`

```bash
scripts/dev/run-with-fake-airplay.sh
```

Builds and launches the real app (a scratch, throwaway `DATA_ROOT` so it
doesn't touch your normal config) with the fake binary first on `PATH`. Open
the AirPlay module from the module list and you'll see the simulated session
— live title/artist/album/art updates — instead of "waiting for
connection" forever. Useful for eyeballing `NowPlaying.qml`'s layout/states
without any real AirPlay hardware.

## What these do and don't cover

Covered: process start/stop lifecycle, metadata-pipe parsing (title, artist,
album, artwork decode-and-write, connect/disconnect), the Now Playing UI's
idle and connected states, module registration/manifest loading.

Not covered — needs the real binary and/or real hardware: actual AirPlay 2
protocol negotiation and audio playback, `nqptp` timing behavior, and the
`audio_output_device` setting's effect on real ALSA output.

Escape-key back-navigation was checked visually (via computer-use driving the
real GUI against `run-with-fake-airplay.sh`) and *looked* broken from
NowPlaying — but the identical `[ESC]:BACK` path also failed to navigate back
from the pre-existing, unmodified `Local Files` module in the same session,
while Up/Down/Enter worked fine everywhere. That rules out the AirPlay code:
it's a synthetic-input quirk of driving this app's true-fullscreen window
via computer-use in this sandbox, not a module defect. Still worth a real
keyboard/gamepad check once this runs somewhere normal.
