<img src="https://github.com/user-attachments/assets/73c3e46f-a74a-4d96-9c4f-ae30f28378be" />

# 240-MP — Monterey Intel Edition

This repository is a fork of the original **240-MP** project by
Anthony Caccese:

https://github.com/anthonycaccese/240-MP

Its purpose is to maintain a stable Intel x86_64 build for
**macOS Monterey**, while remaining as close as possible to the
upstream project. Changes that are useful to all platforms may be
proposed back to the original repository.

## Current fork changes

- Native Intel x86_64 build targeting macOS Monterey 12.
- Portable macOS application bundle and downloadable DMG.
- Fixes the thin white border around fullscreen mpv playback by
  removing the `--no-native-fs` launch argument.
- Improved playback OSD with:
  - video codec, simplified resolution, aspect ratio and normalized
    encoded frame rate;
  - audio codec, Mono/Stereo/Surround classification and `TRACK x/y`;
  - useful audio language metadata when available;
  - subtitle format, `TRACK x/y` and useful language metadata;
  - embedded, local-file and online titles, while filtering technical
    Plex URLs and identifiers.
- The fullscreen correction was proposed upstream in Pull Request #140.

## Running the Monterey Intel release

The downloadable application is intended for:

- Intel x86_64 Macs;
- macOS Monterey 12 or later;
- systems with working `mpv` and `yt-dlp` executables.

The application bundle includes Qt and its required application
resources, but it does **not** bundle the external `mpv` player or
`yt-dlp`.

Runtime requirements:

- `mpv` is required for media playback.
- `yt-dlp` is required for YouTube playback.
- Both commands should be accessible through the shell PATH, normally
  under `/usr/local/bin` on an Intel Mac.

Verify the installation with:

    command -v mpv
    mpv --version

    command -v yt-dlp
    yt-dlp --version

## Installing Homebrew

Homebrew is a package manager for macOS. Its official website and
installation instructions are available at:

https://brew.sh/

Install Homebrew from Terminal with the official installer:

    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

On an Intel Mac, the supported Homebrew prefix is normally:

    /usr/local

After installation, verify it with:

    brew --version
    brew --prefix

## Installing mpv and yt-dlp with Homebrew

Update Homebrew and install both runtime dependencies:

    brew update
    brew install mpv yt-dlp

Verify them afterward:

    command -v mpv
    command -v yt-dlp

    mpv --version
    yt-dlp --version

On older macOS releases such as Monterey, Homebrew may need to compile
some packages locally instead of downloading prebuilt bottles. In that
case, the full Xcode application may be required in addition to the
Command Line Tools.

## Tested external executables

The release was tested with working local Intel copies of:

- mpv 0.39.0 at:

      /Applications/mpv.app/Contents/MacOS/mpv

- yt-dlp 2026.07.04 at:

      /usr/local/bin/yt-dlp

The tested mpv.app executable was exposed on the PATH with:

    sudo ln -s /Applications/mpv.app/Contents/MacOS/mpv /usr/local/bin/mpv

If `/usr/local/bin/mpv` already exists, do not recreate the symbolic
link without first checking where it points:

    ls -l /usr/local/bin/mpv

The tested yt-dlp executable could also be updated directly with:

    sudo yt-dlp -U

## Build environment used for this release

The Monterey Intel build and DMG were produced on:

- Mac architecture: Intel x86_64
- Operating system: macOS Monterey 12.7.4
- macOS build: 21H1123
- Apple Clang: 14.0.0
- Git: Apple Git 2.37.1
- CMake: 4.4.0
- Qt: 6.5.3 macOS kit
- Homebrew: Intel installation under `/usr/local`
- Homebrew version observed during development: 5.1.6
- OpenSSL: openssl@3 3.6.3
- SDL compatibility layer: sdl2-compat 2.32.70
- SDL runtime used by sdl2-compat: SDL3 3.4.12
- mpv: Intel mpv.app 0.39.0
- yt-dlp: 2026.07.04

The application was configured with:

- `CMAKE_OSX_ARCHITECTURES=x86_64`
- `CMAKE_OSX_DEPLOYMENT_TARGET=12.0`
- Qt 6.5.3 from `~/Qt/6.5.3/macos`
- Homebrew libraries from `/usr/local`

## Build dependencies

Rebuilding the application requires:

- Git
- CMake
- Apple Command Line Tools or Xcode
- Qt 6.5.3 with its macOS desktop kit
- OpenSSL 3
- SDL2 compatibility libraries
- SDL3
- `mpv` and `yt-dlp` for runtime testing

The Homebrew-provided build libraries used in the tested environment
were:

    brew install cmake openssl@3 sdl2-compat

Qt 6.5.3 was installed separately through the official Qt installer,
because newer Qt releases may require a newer macOS version and may
not run on Monterey.

## Artificial-intelligence assistance disclosure

The porting, troubleshooting, code modification, build preparation,
packaging workflow and documentation for this Monterey Intel edition
were developed by **Al Garcia with assistance from ChatGPT**.

AI assistance details:

- Service: ChatGPT by OpenAI
- Model: GPT-5.6 Thinking
- Main working dates: July 11–12, 2026
- Scope of assistance:
  - analysis of build and runtime errors;
  - generation and review of Terminal commands;
  - C++, QML and Lua modification proposals;
  - macOS bundle and DMG packaging guidance;
  - Git, GitHub fork, release and Pull Request guidance;
  - drafting and organization of technical documentation.

All commands, builds and functional changes were reviewed and tested
by the repository owner on actual hardware. AI-generated suggestions
were iteratively corrected whenever testing showed that an assumption
or proposed change was inaccurate.

This disclosure is included to document the development process
transparently and to distinguish AI assistance from human validation,
testing and project ownership.

## Fork philosophy

This fork intentionally keeps its platform-specific changes limited.

- Upstream changes should be incorporated regularly.
- Generally useful corrections should be proposed upstream.
- Monterey Intel-specific compatibility work may remain in this fork.
- Build and runtime assumptions should be documented and reproducible.

---

## Original upstream documentation

