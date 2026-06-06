# 240-MP Development Guidelines

240-MP is a retro VHS-style media app built with C++ Qt6 + QML, targeting Raspberry Pi 4 and macOS. Modules are self-contained media integrations (Plex, Local Files, etc.) that the app shell loads at startup.

**Playback engine**: 240-MP launches **mpv** as a subprocess for video playback. mpv must be installed separately (`apt install mpv` on RPi/Debian, `brew install mpv` on macOS). The app handles all browsing, auth, and settings; when a video is selected it hands off to mpv fullscreen via `MpvController`, then resumes when mpv exits.

---

## Project Structure

```
240-mp/
  src/                          # C++ source
    main.cpp                    # app entry point — engine setup, context properties
    AppCore.h / AppCore.cpp     # app shell: module registry, config r/w, settings routing
    modules/                    # per module source files
      local_files/
        LocalFilesBackend.h/.cpp
      plex/
        PlexBackend.h/.cpp      # reference backend implementation
    player/
      MpvController.h/.cpp      # mpv subprocess controller: QProcess launch + IPC socket
  modules/                      # QML + assets per module
    plex/
      manifest.json             # module identity and settings shape
      assets/images/logo.svg
      views/
        Root.qml                # module router (required)
        ...
  views/                        # app-level QML (ModuleList, Settings, Components/)
  Main.qml                      # app root
  CMakeLists.txt
```

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

---

## Adding a New Module

A module has two parts: a **C++ backend** (optional) and **QML views** (required).

### 1. Create the module folder

```
modules/[YOUR_MODULE_NAME]/
  manifest.json         # required — read by AppCore at startup
  assets/images/logo.svg
  views/
    Root.qml            # required — module router
    Items.qml           # list view (entry point)
    Detail.qml          # detail/leaf view
```

AppCore scans `modules/*/manifest.json` at startup — no C++ changes needed to register the module or its settings.

### 2. Create a C++ backend (if needed)

Add `src/modules/[YOUR_MODULE_NAME]/[YOUR_MODULE_NAME].h/.cpp` — a `QObject` subclass with `Q_INVOKABLE` slots and signals. Add the `.cpp` to `CMakeLists.txt`.

### 3. Wire into main.cpp

```cpp
YourBackend yourBackend(appRoot);
appCore.registerBackend("com.240mp.[YOUR_MODULE_NAME]", &yourBackend);
engine.rootContext()->setContextProperty("yourBackend", &yourBackend);

// If the backend emits dynamicOptionsReady, forward it to AppCore:
QObject::connect(&yourBackend, &YourBackend::dynamicOptionsReady, &appCore, [&appCore](const QString &key, const QVariant &opts) {
    emit appCore.dynamicOptionsReady("com.240mp.[YOUR_MODULE_NAME]", key, opts);
});
```

---

## AppCore — App Shell

**Global context properties** (available in all QML): `appCore`, `localFilesBackend`, `plexBackend`, `mpvController`.

Context property name: **`appCore`**.

**Q_INVOKABLE slots used by QML:**

| Slot | Purpose |
|---|---|
| `scan_for_modules()` | Emits `modulesLoaded` with enabled modules |
| `get_settings()` | Returns entire `config.json` as a map |
| `save_setting(moduleId, key, value)` | Writes to `config.json`; supports dot-notation keys |
| `get_module_info(moduleId)` | Returns `{name, icon}` for a module |
| `get_module_settings_schema(moduleId)` | Returns the module's settings array |
| `invoke_module_action(moduleId, slotName)` | Routes to the registered backend via `QMetaObject::invokeMethod` |

**Signals:** `modulesLoaded`, `appSettingChanged`, `dynamicOptionsReady(moduleId, key, options)`.

Config is stored at `{APP_ROOT}/config.json`:
```json
{
  "app": { "color_scheme": "Video 1" },
  "modules": {
    "com.240mp.plex": { "enabled": true, "server_machine_id": "...", ... }
  }
}
```

---

## C++ Backend

Backends are `QObject` subclasses registered as QML context properties before the engine loads.

**Patterns to follow** (see `PlexBackend` as a reference implementation):
- All HTTP via `QNetworkAccessManager` — async, main thread, no worker threads needed
- Results returned to QML via signals
- Auth/state persisted to JSON files in `APP_ROOT`
- `Q_INVOKABLE` for slots called from QML; `signals:` for callbacks to QML
- For dynamic settings dropdowns, emit `dynamicOptionsReady(key, [{id, label}])` — AppCore forwards it with the moduleId prepended

---

## manifest.json

Loaded at startup by `AppCore` — the single source of truth for a module's identity and settings. No C++ changes needed when adding or modifying settings.

```json
{
  "id": "com.240mp.[YOUR_MODULE_NAME]",
  "name": "[YOUR MODULE NAME]",
  "icon": "assets/images/logo.svg",
  "entry_point_qml": "views/Root.qml",
  "settings": [...]
}
```

### Setting types

| `type` | Description | Extra fields |
|---|---|---|
| `toggle` | ON/OFF toggle | `default: "ON"` or `"OFF"` |
| `list_single` | Single-select list | `options_source`, `options_slot`, `apply_slot` |
| `multiselect_submenu` | Multi-select list via submenu | `options_source`, `options_slot` |
| `action` | Button that calls a backend slot | `action_slot` |

For `list_single` / `multiselect_submenu` with `"options_source": "dynamic"`, the backend slot named by `options_slot` must emit `dynamicOptionsReady(key, [{id, label}])`.

For `list_single` with `apply_slot`, that slot is called automatically when the user changes the value (routed via `invoke_module_action`).

---

## Root.qml — Module Router

Every module requires `Root.qml` as its entry point. It owns the internal nav stack and handles exiting back to the module list.

```qml
import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})

    // must match your manifest id
    property var _moduleInfo: appCore ? appCore.get_module_info("com.240mp.[YOUR_MODULE_NAME]") : ({})
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

**Key rules:**
- `id` is always `moduleRoot`
- `moduleName` / `moduleIcon` always sourced from `appCore.get_module_info(...)` — never hardcoded
- `goBack()` is the only signal that leaves the module — child views never emit it directly
- `navigateBack` merges `navListState` back into params on pop so list views can restore position
- For auth flows that need `replaceWith` (navigate without pushing to stack), see the Plex module as reference

---

## Child Views

### List view (Items.qml)

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
        // ...
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

### Detail / leaf view (Detail.qml)

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

**Key rules:**
- Always declare `property var navParams: ({})` — the router passes params via `Loader.setSource`
- List views also declare `property var navListState: navParams.navListState || ({})` and restore position in `Component.onCompleted`
- `navigateTo` always takes 3 args: `(path, params, listState)` — pass `{ currentIndex: listView.currentIndex }` as listState when pushing to a detail view
- Leaf views only need `signal goBack()` — no `navigateTo`
- Use `root.sh` / `root.sw` for all margins and sizes (never hardcoded pixels)
- Access shared state via `moduleRoot.moduleName`, `moduleRoot.moduleIcon`
- Navigate via signals — never call router functions directly

---

## Components (WIP)

### AppBar

Defined in `views/Components/AppBar.qml`.

| Property | Type | Description |
|---|---|---|
| `iconSource` | `url` | Module icon — use `moduleRoot.moduleIcon` |
| `title` | `string` | Module name — use `moduleRoot.moduleName` |
| `subtitle` | `string` | Optional context label (hidden when empty) |

The icon is automatically colorized to the app accent color.
