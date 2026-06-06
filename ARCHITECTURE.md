# 240-MP Architecture

This is the technical reference for people working on 240-MP's code — whether you're adding a
new module or changing an existing one. If you just want to install or build the app, see
[INSTALL.md](INSTALL.md) and [BUILDING.md](BUILDING.md). If you want to contribute, start with
[CONTRIBUTING.md](CONTRIBUTING.md).

240-MP is a retro VHS-style media app built with **C++ Qt6 + QML**, targeting **Raspberry Pi 4**
and **macOS**.

---

## Philosophy

240-MP is a **browsing shell** that hands off to **purpose-built tools**.

- The app shell handles browsing, auth, and settings.
- **Modules** are self-contained media integrations (Local Files, Plex, Ambient Mode, …) that
  the shell discovers and loads at startup.
- When the user picks something to play, the shell hands off to a dedicated fullscreen tool and
  resumes when that tool exits. For video, that tool is **mpv**, launched as a subprocess by
  `MpvController`. mpv is installed separately (`apt install mpv` / `brew install mpv`) — 240-MP
  does not link against libmpv.

The guiding idea: **browse structured content, then hand off to the right tool for the job**
rather than bundling everything into one binary.

---

## Project Structure

```
240-mp/
  src/                          # C++ source
    main.cpp                    # app entry point — engine setup, context properties, registerModule calls
    AppCore.h / AppCore.cpp     # app shell: module registry, config r/w, settings routing
    modules/                    # per-module C++ backends
      local_files/
        LocalFilesBackend.h/.cpp
      plex/
        PlexBackend.h/.cpp      # reference backend implementation
      ambient_mode/
        AmbientModeBackend.h/.cpp
    player/
      MpvController.h/.cpp       # mpv subprocess controller: QProcess launch + IPC socket
  modules/                      # QML + assets per module (discovered at startup)
    plex/
      manifest.json             # module identity and settings shape
      assets/images/logo.svg
      views/
        Root.qml                # module router (required)
        ...
    local_files/
    ambient_mode/
  views/                        # app-level QML
    ModuleList.qml, Settings.qml, ModuleSettings.qml,
    MultiSelectSettings.qml, DirectoryBrowser.qml
    Components/                 # shared QML components (AppBar, qmldir)
  Main.qml                      # app root
  CMakeLists.txt
```

There are three modules today: `local_files`, `plex`, and `ambient_mode`. `plex` is the most
complete and is the recommended reference when building something new.

---

## Anatomy of a Module

A module has up to three parts:

| Part | Location | Required? |
|---|---|---|
| `manifest.json` | `modules/<name>/manifest.json` | **Yes** — read by `AppCore` at startup |
| QML views | `modules/<name>/views/` (entry point `Root.qml`) | **Yes** |
| C++ backend | `src/modules/<name>/<Name>Backend.h/.cpp` | Optional |

`AppCore` scans `modules/*/manifest.json` at startup. A module that needs **no backend**
(pure QML) requires **no C++ changes at all** — drop in the folder and it's discovered. A module
that needs a backend adds one `registerModule(...)` call in `main.cpp` (see
[AppCore](#appcore--the-app-shell)).

```
modules/<name>/
  manifest.json         # identity + settings
  assets/images/logo.svg
  views/
    Root.qml            # module router (entry point)
    Items.qml           # list view
    Detail.qml          # detail/leaf view
```

---

## manifest.json Reference

Loaded at startup by `AppCore` — the single source of truth for a module's identity and settings.
No C++ changes are needed to add or modify settings.

```json
{
  "id": "com.240mp.<name>",
  "name": "<DISPLAY NAME>",
  "icon": "assets/images/logo.svg",
  "entry_point_qml": "views/Root.qml",
  "settings": [ ... ]
}
```

### Setting types

| `type` | Description | Extra fields |
|---|---|---|
| `toggle` | ON/OFF toggle | `default: "ON"` or `"OFF"` |
| `list_single` | Single-select list | `options_source`, `options_slot`, `apply_slot` |
| `multiselect_submenu` | Multi-select list via submenu | `options_source`, `options_slot` |
| `directory_browser` | Keyboard-navigable directory picker | `default` (path string, may be empty) |
| `action` | Button that calls a backend slot | `action_slot` |

Additional fields any setting may carry:

- `key` — the config key written under `modules.<id>.<key>` in `config.json`. Supports dot-notation.
- `label` — display text in Settings.
- `requires_auth` — if `true`, the setting is only shown when the module reports an authenticated
  state via `get_module_auth_state(moduleId)`. Used by Plex to hide server/user/library settings
  until sign-in.

### Dynamic options and apply slots

- For `list_single` / `multiselect_submenu` with `"options_source": "dynamic"`, the backend slot
  named by `options_slot` must emit `dynamicOptionsReady(key, [{id, label}])`. `AppCore` re-emits
  it to QML with the module ID prepended.
- For `list_single` with `apply_slot`, that slot is called automatically (routed through
  `invoke_module_action`) when the user changes the value.

A real example (Plex) — note `requires_auth`, dynamic options, and apply slots:

```json
{
  "key": "server_machine_id",
  "label": "Server",
  "type": "list_single",
  "options_source": "dynamic",
  "options_slot": "getServers",
  "apply_slot": "applyCurrentServerSetting",
  "requires_auth": true
}
```

---

## AppCore — the App Shell

`AppCore` (`src/AppCore.h/.cpp`) is the shell. It's exposed to all QML as the context property
**`appCore`**.

**Global context properties** (available in all QML): `appCore`, `mpvController`, plus one per
module backend (`localFilesBackend`, `plexBackend`, `ambientModeBackend`, …). Backend names are
assigned by the `registerModule` call in `main.cpp`.

### Q_INVOKABLE slots used by QML

| Slot | Purpose |
|---|---|
| `scan_for_modules()` | Emits `modulesLoaded` with enabled modules |
| `get_settings()` | Returns entire `config.json` as a map |
| `get_setting(moduleId, key)` | Returns a single setting value |
| `save_setting(moduleId, key, value)` | Writes to `config.json`; supports dot-notation keys |
| `get_module_info(moduleId)` | Returns `{name, icon}` for a module |
| `get_module_settings_schema(moduleId)` | Returns the module's settings array |
| `invoke_module_action(moduleId, slotName)` | Routes to the registered backend via `QMetaObject::invokeMethod` |
| `get_module_auth_state(moduleId)` | Returns the module's auth state (for `requires_auth` settings) |
| `getCustomColorScheme()` | Returns the user's custom color scheme |
| `listDirectories(path)` / `parentDirectory(path)` / `homePath()` | Helpers for `directory_browser` |

### Signals

`modulesLoaded`, `appSettingChanged`, `moduleSettingChanged(moduleId, key, value)`,
`dynamicOptionsReady(moduleId, key, options)`, `moduleAuthStateChanged(moduleId)`.

### registerModule — wiring a backend in

Backends are wired in from `main.cpp` with a single call:

```cpp
YourBackend yourBackend(appRoot, dataRoot);   // construct with whatever args the ctor needs

appCore.registerModule("com.240mp.<name>", "yourBackend", &yourBackend, ctx);
```

`registerModule(moduleId, contextProperty, backend, ctx)` does everything: it stores the backend
for `invoke_module_action` routing, exposes it to QML under `contextProperty`, and connects the
backend's optional signals/slots **by introspection** — each is wired only if the backend actually
declares it, so there are no per-capability lambdas:

| Backend member (if declared) | Auto-connected to |
|---|---|
| signal `dynamicOptionsReady(QString, QVariant)` | re-emitted as `appCore.dynamicOptionsReady(moduleId, key, options)` |
| signal `authStateChanged()` | re-emitted as `appCore.moduleAuthStateChanged(moduleId)` |
| slot `onSettingChanged(QString, QString, QVariant)` | `appCore.moduleSettingChanged(moduleId, key, value)` |

The module ID lives in exactly one place per module — this call. Declare these members with the
exact signatures above and `registerModule` wires them with no other changes to `main.cpp`.

---

## C++ Backend Patterns

Backends are `QObject` subclasses registered via `registerModule(...)` before the engine loads.
Follow `PlexBackend` as the reference implementation.

- All HTTP via `QNetworkAccessManager` — async, on the main thread, no worker threads needed.
- Results returned to QML via signals.
- Auth/state persisted to JSON files in the data dir.
- `Q_INVOKABLE` for slots called from QML; `signals:` for callbacks to QML.
- For dynamic settings dropdowns, emit `dynamicOptionsReady(key, [{id, label}])` — auto-connected;
  `AppCore` re-emits with the module ID prepended.
- For auth-gated modules, emit `authStateChanged()` on sign-in/out — auto-connected and re-emitted
  as `moduleAuthStateChanged(moduleId)`.
- To react to your own settings changing, add a slot
  `onSettingChanged(moduleId, key, value)` — auto-connected to `moduleSettingChanged`.
- A backend resolves its own configured paths in its constructor — e.g.
  `LocalFilesBackend` / `AmbientModeBackend` read `media_directory` from `config.json`
  (defaulting to `dataRoot/media` / `dataRoot/ambient`). `main.cpp` does not touch module paths.

---

## QML View Patterns

### Root.qml — module router

Every module requires `Root.qml` as its entry point. It owns the internal nav stack and handles
exiting back to the module list.

```qml
import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})

    // must match your manifest id
    property var _moduleInfo: appCore ? appCore.get_module_info("com.240mp.<name>") : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    property var navStack: []
    property var currentParams: ({})

    function navigateTo(viewPath, params, fromState) {
        var resolved = Qt.resolvedUrl(viewPath)
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    function navigateBack() {
        if (navStack.length === 0) {
            moduleRoot.goBack()
            return
        }
        var prev = navStack.pop()
        if (!prev.source || prev.source.toString() === "") {
            moduleRoot.goBack()
            return
        }
        var restored = Object.assign({}, prev.params)
        restored.navListState = prev.listState || {}
        currentParams = restored
        internalLoader.setSource(prev.source, { "navParams": restored })
    }

    Loader {
        id: internalLoader
        anchors.fill: parent
        focus: true
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) { moduleRoot.navigateTo(path, params, listState) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    Component.onCompleted: navigateTo("Items.qml", {})
}
```

**Rules:**
- `id` is always `moduleRoot`.
- `moduleName` / `moduleIcon` always come from `appCore.get_module_info(...)` — never hardcoded.
- `goBack()` is the only signal that leaves the module — child views never emit it directly.
- `navigateBack` merges `navListState` back into params on pop so list views can restore position.
- For auth flows that need `replaceWith` (navigate without pushing to the stack), see the Plex
  module as a reference.

### Items.qml — list view

```qml
import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            goBack()
            event.accepted = true
        }
    }

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    ListView {
        id: itemList
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625

        // restore list position on back-navigate
        Component.onCompleted: {
            var restore = navListState.currentIndex !== undefined ? navListState.currentIndex : 0
            currentIndex = Math.min(restore, Math.max(0, count - 1))
            positionViewAtIndex(currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            navigateTo("Detail.qml", { item: model[currentIndex] }, { currentIndex: currentIndex })
        }
    }
}
```

### Detail.qml — leaf view

```qml
import QtQuick
import Components

FocusScope {
    id: detailRoot

    property var navParams: ({})

    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            goBack()
            event.accepted = true
        }
    }

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: navParams.item || ""
    }
}
```

**View rules:**
- Always declare `property var navParams: ({})` — the router passes params via `Loader.setSource`.
- List views also declare `property var navListState: navParams.navListState || ({})` and restore
  position in `Component.onCompleted`.
- `navigateTo` always takes 3 args: `(path, params, listState)` — pass
  `{ currentIndex: listView.currentIndex }` as listState when pushing to a detail view.
- Leaf views only need `signal goBack()` — no `navigateTo`.
- Use `root.sh` / `root.sw` for all margins and sizes — never hardcoded pixels. This keeps
  layouts responsive across CRT (240p/480i, watch overscan) and HDMI/LCD.
- Access shared state via `moduleRoot.moduleName`, `moduleRoot.moduleIcon`.
- Navigate via signals — never call router functions directly.

---

## Components

Shared QML components live in `views/Components/` (registered via `qmldir`, imported as
`import Components`).

### AppBar (`views/Components/AppBar.qml`)

| Property | Type | Description |
|---|---|---|
| `iconSource` | `url` | Module icon — use `moduleRoot.moduleIcon` |
| `title` | `string` | Module name — use `moduleRoot.moduleName` |
| `subtitle` | `string` | Optional context label (hidden when empty) |

The icon is automatically colorized to the app accent color.

---

## Config Storage

User configuration is stored in `config.json` in the app's data directory:

```json
{
  "app": { "color_scheme": "Video 1" },
  "modules": {
    "com.240mp.plex": { "enabled": true, "server_machine_id": "...", ... }
  }
}
```

Each module's settings live under `modules.<id>`. Use `save_setting` / `get_setting` (which
support dot-notation keys) rather than writing the file directly. The data directory is created on
first run and is separate from the app itself, so rebuilding never wipes user settings. For the
exact per-OS path (macOS vs Raspberry Pi OS), see [BUILDING.md](BUILDING.md#configuration).
