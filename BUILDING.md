# Building 240-MP

If you are interested in building your own version of 240-MP and adding things to it then this page should hopefully cover what you would need to get an environment set up.  I've included details for macOS on ARM (where I primarily build) and Raspberry Pi OS.  And if you create a feature you would like to contribute back to this repo please open a PR, I'd be glad to talk through it.

## macOS (ARM)

### Prerequisites (one-time)

**Set up Build tools:**

```bash
brew install cmake
```

**Install Qt 6.*:**

- Download from [qt.io/download](https://qt.io/download) or `brew install qt@6`.
- Install to `~/Qt/`

**Install mpv (required for playback):**

```bash
brew install mpv
```

Note: 240-MP uses mpv as an external subprocess for video playback. It does not link against libmpv at build time, so mpv only needs to be on your `PATH` when running the app.

### Get the source

```bash
git clone https://github.com/anthonycaccese/240-mp.git
cd 240-mp
```

### Build

**First time, and after any CMakeLists.txt changes:**

```bash
cmake -B build -DCMAKE_PREFIX_PATH=~/Qt/6.11.0/macos . && cmake --build build
```

**For incremental builds:**

```bash
cmake --build build
```

### Run

You can either double-click `build/240mp.app` in Finder, or run from the terminal:

```bash
APP_ROOT=$(pwd) ./build/240mp.app/Contents/MacOS/240mp
```

### Configuration

On macOS all user configuration is stored at:

```
~/Library/Application Support/240-MP/
  config.json       ← app and module settings
  plex_auth.json    ← plex auth
```

This directory is created automatically on first run. It is separate from the app itself, so deleting or rebuilding the app will not wipe your settings.

## Raspberry Pi OS (arm64)

### Prerequisites (one-time)

Run on the Pi with RPi OS Trixie (Debian 13):

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-window \
  libqt6svg6 qt6-svg-dev qt6-svg-plugins qt6-wayland \
  libdrm-dev libxkbcommon-dev libssl-dev \
  mpv
```

`mpv` is the playback engine — 240-MP launches it as a subprocess. No libmpv build dependency is required.

### Get the source

```bash
git clone https://github.com/anthonycaccese/240-mp.git
cd 240-mp
```

### Build

**First time, and after any CMakeLists.txt changes:**

```bash
cmake -B build
```

**For incremental builds:**

```bash
cmake --build build
```

No `CMAKE_PREFIX_PATH` needed — Qt 6 from apt is found automatically.

### Run

**With a desktop** (RPi OS Full with a display server):

```bash
APP_ROOT=$(pwd) ./build/240mp
```

**Without a Desktop** (RPi OS Lite with no display server):

```bash
APP_ROOT=$(pwd) QT_QPA_PLATFORM=eglfs ./build/240mp
```

`eglfs` uses the KMS/DRM framebuffer directly without X11 or Wayland.

### Configuration

On Raspberry Pi OS all user configuration is stored at:

```
~/.local/share/240-MP/
  config.json      ← app and module settings
  plex_auth.json   ← plex auth
```

This directory is created automatically on first run. It is separate from the app itself, so deleting or rebuilding the app will not wipe your settings.

## GitHub Actions

### How to trigger a build

Releases are built automatically when you push a version tag:

```bash
git tag v2026.06.04
git push origin v2026.06.04
```

And you can use pre-release tags to test CI without making a public release:

```bash
git tag v1.0.0-rc1
git push origin v1.0.0-rc1
```

Tags containing `-rc`, `-beta`, or `-alpha` are published as GitHub pre-releases.

### What the workflow does

These build jobs run in parallel:

| Job | Runner | Output |
|---|---|---|
| `build-macos-arm64` | `macos-latest` (Apple Silicon) | `240-MP-<tag>-macOS-arm64.dmg` |
| `build-linux-arm64` | `ubuntu-24.04-arm` (native arm64) | `240-MP-<tag>-linux-arm64.tar.gz` |

macOS jobs: installs Qt via the Qt CDN, builds, runs `macdeployqt` to embed Qt frameworks, ad-hoc codesign, package as `.dmg`. mpv is not bundled — users install it via `brew install mpv`.

Linux arm64 job: installs Qt from apt, builds, package as `.tar.gz`. mpv is not bundled — end users install it via `apt install mpv` or by running the `install.sh` that is bundled with each release where its installed as part of the dependency list.

A final `release` job waits for all three builds, then creates a GitHub Release with all artifacts attached (including `install.sh`).

### Output

**While the workflow is running:**

Go to **Actions** → select the workflow run → each build job has an **Artifacts** section at the bottom where you can download that job's output before the release is published.

**After the workflow completes:**

Go to the repository on GitHub → **Releases** → select the release for the tag you set. All three artifacts are listed under Assets.
