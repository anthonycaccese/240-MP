# 240-MP Development Guidelines

240-MP is a retro VHS-style media app built with C++ Qt6 + QML, targeting Raspberry Pi 4 and macOS. Modules are self-contained media integrations (Plex, Local Files, Ambient Mode, etc.) that the app shell discovers and loads at startup.

**Playback engine**: 240-MP launches **mpv** as a subprocess for video playback. mpv must be installed separately (`apt install mpv` on RPi/Debian, `brew install mpv` on macOS). The app handles all browsing, auth, and settings; when a video is selected it hands off to mpv fullscreen via `MpvController`, then resumes when mpv exits.

---

## Build & Run (macOS ARM)

```bash
# First time / after CMakeLists.txt changes:
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/macos . && cmake --build build

# Incremental (code changes only):
cmake --build build

# Run:
APP_ROOT=$(pwd) ./build/240mp
```

For the full build/install story on both targets (macOS and Raspberry Pi OS), CI, and config paths, see **[BUILDING.md](BUILDING.md)** and **[INSTALL.md](INSTALL.md)**.

---

## Where things live

This file stays intentionally lean. The detailed documentation is single-sourced elsewhere — read the relevant doc before working in an area rather than relying on memory:

| If you need… | Read |
|---|---|
| Architecture, module anatomy, `manifest.json` setting types, `AppCore` / `registerModule`, C++ backend patterns, input/gamepad handling (`InputManager`), QML view/navigation patterns, Components, config shape | **[ARCHITECTURE.md](ARCHITECTURE.md)** |
| How to contribute, project principles, best-practices checklist, adding/changing a module, testing, coding style | **[CONTRIBUTING.md](CONTRIBUTING.md)** |
| Building & running on macOS / Raspberry Pi, CI/release workflow, per-OS config/data directory paths | **[BUILDING.md](BUILDING.md)** |
| End-user install (Raspberry Pi imaging, `config.txt`, macOS DMG) | **[INSTALL.md](INSTALL.md)** |
| Virtual Channels: schedules, pools, exclusions, movie slots, the local `series/`+`movies/` layout | **[ARCHITECTURE.md → Virtual Channels](ARCHITECTURE.md#virtual-channels-scheduled-tv)** |

---

## Key facts to keep in mind

- **Modules are discovered from `modules/*/manifest.json`** at startup — a pure-QML module needs no C++ changes. A module with a backend adds **one** `registerModule(...)` call in `src/main.cpp`; that call is the single place the module ID is stated. (Details: [ARCHITECTURE.md → AppCore](ARCHITECTURE.md#appcore--the-app-shell).)
- **`registerModule` wires optional backend signals/slots by introspection** (`dynamicOptionsReady`, `authStateChanged`, `onSettingChanged`) — declare them with the exact signatures and no `main.cpp` changes are needed.
- **Every module's QML entry point is `Root.qml`** (the router). Views are `FocusScope`s that pass state via `navParams` and communicate via `navigateTo` / `goBack` signals. Size everything with `root.sh` / `root.sw`, never hardcoded pixels.
- **`PlexBackend` is the reference implementation** for backends.
- **A virtual channel is a timeline that is built, not played live.** Editing a pool only writes `channels.json`; nothing airs differently until that channel is rebuilt, which is why every editing screen carries a REBUILD row. Local files, Plex, Jellyfin and Emby are all browsed and stored in one shape — ask the backend what a source supports (e.g. `source_supports_playlists()`) rather than hardcoding it in a view. Exclusions live in the channel's source block, not on the entry. (Details: [ARCHITECTURE.md → Virtual Channels](ARCHITECTURE.md#virtual-channels-scheduled-tv).)
- **The channels module has a C++ test suite** — `vchan-tests`, run with `ctest --test-dir build-tests`. Anything that writes to `channels.json` needs a test that writes, against a deliberately broken channel as well as a healthy one: a save that refuses the whole list because of one unresolvable row locks the viewer out of that screen. (Build steps: [CONTRIBUTING.md → Testing](CONTRIBUTING.md#testing-your-change).)
- **Config** is `config.json` in the data dir; module settings live under `modules.<id>`. Use `save_setting` / `get_setting` (dot-notation supported), not direct file writes.
- **An NFC card can hand off to another module** (a Plex guid on a card plays through the Plex module). The NFC backend routes by URI scheme; the receiving module carries a `CardPlay.qml` that resolves and then `replaceWith`s its Player. A module reached this way must bypass its own auth/user gate — a card plays as whoever is already active and must never prompt a profile switch. (Details: [ARCHITECTURE.md → Card Hand-off](ARCHITECTURE.md#card-hand-off-nfc--a-module).)
- **Gamepad input is centralized in `src/input/InputManager`** (SDL2) and arrives in QML as ordinary synthesized key events — never add gamepad-specific handling to views; if a view handles the right keys it handles gamepads. Footer hint labels bind to `root.hints.*` (adapts keyboard↔gamepad), never hardcoded `[ESC]`/`[ENTER]` strings and never `inputManager.hints.*` directly — context-property bindings throw TypeErrors when the view Loader tears down; id-resolved `root.*` is teardown-safe. (Details: [ARCHITECTURE.md → Input](ARCHITECTURE.md#input-inputmanager).)
