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

---

## Key facts to keep in mind

- **Modules are discovered from `modules/*/manifest.json`** at startup — a pure-QML module needs no C++ changes. A module with a backend adds **one** `registerModule(...)` call in `src/main.cpp`; that call is the single place the module ID is stated. (Details: [ARCHITECTURE.md → AppCore](ARCHITECTURE.md#appcore--the-app-shell).)
- **`registerModule` wires optional backend signals/slots by introspection** (`dynamicOptionsReady`, `authStateChanged`, `onSettingChanged`) — declare them with the exact signatures and no `main.cpp` changes are needed.
- **Every module's QML entry point is `Root.qml`** (the router). Views are `FocusScope`s that pass state via `navParams` and communicate via `navigateTo` / `goBack` signals. Size everything with `root.sh` / `root.sw`, never hardcoded pixels.
- **`PlexBackend` is the reference implementation** for backends.
- **Config** is `config.json` in the data dir; module settings live under `modules.<id>`. Use `save_setting` / `get_setting` (dot-notation supported), not direct file writes.
- **Gamepad input is centralized in `src/input/InputManager`** (SDL2) and arrives in QML as ordinary synthesized key events — never add gamepad-specific handling to views; if a view handles the right keys it handles gamepads. Footer hint labels bind to `root.hints.*` (adapts keyboard↔gamepad), never hardcoded `[ESC]`/`[ENTER]` strings and never `inputManager.hints.*` directly — context-property bindings throw TypeErrors when the view Loader tears down; id-resolved `root.*` is teardown-safe. (Details: [ARCHITECTURE.md → Input](ARCHITECTURE.md#input-inputmanager).)
