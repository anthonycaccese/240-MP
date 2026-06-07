# Contributing to 240-MP

Thank you for considering contributing to 240-MP!  I originally built this as a personal project for watching video content on a CRT but I'm also stoked to see what ideas you might want to add.  So with that in mind I'll try to make contributing to this project as easy and transparent as possible.  If you have any questions on the below please create a post in [Discussions > Q&A](https://github.com/anthonycaccese/240-MP/discussions/categories/q-a).

## Non-code contributions

The most useful community contributions are often not code, items like the following are super helpful...

- Documentation improvements where a step was unclear
- Hardware validation reports for different Pi models and CRT outputs
- Logs from failures along with step by step ways to replicate
- Photos or screenshots of working setups

## Getting started

1. **Set up an environment** — follow [BUILDING.md](BUILDING.md) to build and run on macOS (ARM) or Raspberry Pi OS.
2. **Understand the codebase** — read [ARCHITECTURE.md](ARCHITECTURE.md). It's the technical reference for how the shell, modules, and views fit together.
3. **Discuss first if it's big** — for anything beyond a small fix, a quick post in [Discussions > Q&A](https://github.com/anthonycaccese/240-MP/discussions/categories/q-a) can help make sure it fits the project's direction before you invest time.
4. **Branch and open a PR** — fork, work on a branch, and open a pull request against `main` with a clear description (see the [AI use](#note-on-ai-use) note below for what to disclose).

## Submitting code

### Principles to keep in mind

1. **Baseline on remote control as an input device**: All experiences should be built so they can be interacted with via up/down/left/right enter and esc/backspace.  More complex inputs should be avoided so that users can navigate via a simple usb remote.
2. **Lay out screens for 240p/480i on a CRT**: Design layouts and size elements to display well on a CRT TV.  Consider overscan when placing elements on screen.  If you leverage the `root.sh` and `root.sw` vars for sizing you'll get responsive display for LCD tvs out of the box.
3. **Keep modules self contained**:  If your module just relies on QML then you can simply add your module in a `/modules/[module name]` directory with a `manifest.json` and 240-MP will pick it up for display.  If your module requires a backend then you'll also need to register it in `/src/main.cpp`.  But other than that please keep all of your module source in a `/src/modules/[module name]` folder.  See [Anatomy of a Module](ARCHITECTURE.md#anatomy-of-a-module) for the full layout.
4. **No tracking or analytics**: Do not include any mechanisms for tracking or reporting usage to an external source that you maintain.  A module should only ever write details to the local 240-MP configuration directory.  If a module relies on connecting to a 3rd party API (example: the Plex module) then it should only communicate with that API directly.
5. **Browse & Hand-off**: Think of 240-MP and its modules as a way to browse structured content (either on a filesystem or via an API response) and to hand-off to a purpose built tool for an action (like how it relies on MPV for video playback which is purpose built for that ask).  The approach is to leverage existing, purpose built applications that exist on a system and not bundle everything into 240-MP.

### Understanding the codebase

240-MP is a **browsing shell** plus a set of **self-contained modules**. The shell (`AppCore`) discovers modules at startup, exposes settings, and routes actions; each module owns its own QML views and, optionally, a C++ backend. When the user plays something, the shell hands off to a purpose-built tool (mpv for video).

For the full reference — module anatomy, `manifest.json` setting types, `AppCore` / `registerModule`, backend patterns, and the QML view/navigation contract — see **[ARCHITECTURE.md](ARCHITECTURE.md)**.

### Adding a new module

A module is a folder under `modules/` with a `manifest.json` and QML views, plus an optional C++ backend. At a high level:

1. **Create `modules/[name]/`** with a `manifest.json` (identity + settings) and `assets/images/logo.svg`. See the [manifest.json reference](ARCHITECTURE.md#manifestjson-reference).
2. **Add QML views** under `modules/[name]/views/`, with `Root.qml` as the entry point (the module router) plus list/detail views. Follow the [QML view patterns](ARCHITECTURE.md#qml-view-patterns).
3. **(Optional) Add a C++ backend** under `src/modules/[name]/`, add the `.cpp` to `CMakeLists.txt`, and register it with one `registerModule(...)` call in `src/main.cpp`. See [AppCore → registerModule](ARCHITECTURE.md#registermodule--wiring-a-backend-in).

A pure-QML module needs **no C++ changes** — the shell discovers it from its manifest. `PlexBackend` is the most complete backend and the best one to study.

### Changing an existing module

- **Don't break saved settings.** Users' choices live in `config.json` keyed by setting `key`. Keep existing keys stable (or migrate carefully) so an update doesn't reset someone's configuration.
- **Preserve the manifest contract.** If you add a setting, follow the existing [setting types](ARCHITECTURE.md#setting-types); if it needs dynamic options, wire up the `dynamicOptionsReady` / `apply_slot` pattern rather than inventing a new mechanism.
- **Follow the existing view and navigation patterns** in that module — the `navigateTo` / `goBack` contract and `navListState` position restoration. Don't introduce a different nav style.
- **Reuse shared `Components`** (e.g. `AppBar`) instead of re-implementing them.
- **Keep it self-contained** — module source stays under `modules/[name]` and `src/modules/[name]`.

### Use a Consistent Coding Style

- Please follow the same style as the source you are editing.
- If you are contributing new code, keep the style consistent with other similar works.
- Parameterize as much as possible, try to avoid hard coded values whenever you can.
- **C++**: backends are `QObject` subclasses — use `Q_INVOKABLE` for slots QML calls and `signals:` for callbacks to QML, and persist state as JSON in the data directory (see [C++ Backend Patterns](ARCHITECTURE.md#c-backend-patterns)).
- **QML**: views are `FocusScope`s that declare `navParams` and communicate via the `navigateTo` / `goBack` signals — never call router functions directly (see [QML View Patterns](ARCHITECTURE.md#qml-view-patterns)).

### Testing your change

Sorry I've not made time yet to work on automated tests so for now testing is manual:

- **Build and run** on at least one target (macOS ARM or Raspberry Pi). See [BUILDING.md](BUILDING.md#run).
- **Navigate with a remote/keyboard only** and confirm every screen in your change is reachable and exitable.
- **Check the layout** reads correctly on a CRT (mind overscan) and, ideally, over HDMI/LCD too.
- **Confirm settings persist** across an app restart, and that existing settings still load.
- If you can only test on one platform, please indicate that in your PR.

### Note on AI Use

- I used (and will continue to use) AI tools when building 240-MP so leveraging AI tools for development is very much allowed. With that in mind, contributors are expected to own and understand the code they submit and any communication in a PR (including code, code comments, and GitHub comments) must come from a human contributor, not an AI agent acting autonomously.
- Pull requests should include a detailed description that outlines the scope of AI involvement (e.g. which parts were AI-generated and what human testing or review was performed prior to submission). PRs that omit this disclosure may be closed without review.

### Best-practices checklist

Before opening a PR, please check your change against these:

- [ ] **Remote-only navigation** works end to end — up/down/left/right, enter, and esc/backspace. No mouse or complex input required.
- [ ] **Sized and positioned elements with `root.sh` / `root.sw`**, no hardcoded pixel sizes, and laid out with CRT overscan in mind.
- [ ] **Avoided hardcoded values** where a parameter or existing variable would do — parameterize as much as possible.
- [ ] **No tracking or analytics.** The only network calls are direct to a third-party API the module integrates with.
- [ ] **Only writes to the local data directory** (`config.json` and module state files) — nothing outside it.
- [ ] **Browse & hand-off** — heavy lifting (like playback) is handed to a purpose-built tool, not bundled in.

## License

By contributing, you agree your contributions are licensed under GPL (please see [LICENSE](LICENSE)).
