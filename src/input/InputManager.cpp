#include "InputManager.h"

#include <QCoreApplication>
#include <QQuickWindow>
#include <QKeyEvent>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {
// Planted in nativeScanCode of synthesized events so the keyboard detector in
// eventFilter() can tell our gamepad-originated key events from real key presses.
constexpr quint32 kSyntheticScanCode = 0x240F00D;

// Analog stick thresholds (of ±32768). Engage above one, release below the
// other — the gap prevents flutter when the stick rests near the threshold.
constexpr Sint16 kAxisEngage  = 16384;
constexpr Sint16 kAxisRelease = 12000;

// Held-direction auto-repeat, tuned to feel like keyboard repeat in lists.
constexpr int kRepeatDelayMs    = 400;
constexpr int kRepeatIntervalMs = 100;
}

InputManager::InputManager(const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
{
    m_repeatDelayTimer.setSingleShot(true);
    m_repeatDelayTimer.setInterval(kRepeatDelayMs);
    m_repeatTimer.setInterval(kRepeatIntervalMs);
    connect(&m_pollTimer,        &QTimer::timeout, this, &InputManager::pollSdl);
    connect(&m_repeatDelayTimer, &QTimer::timeout, this, &InputManager::onRepeatDelayElapsed);
    connect(&m_repeatTimer,      &QTimer::timeout, this, &InputManager::onRepeatTick);

    rebuildMapping();
    initSdl();

    // Watch the data dir (not the file) so input.cfg can appear later and so
    // replace-on-save editors are caught; mtime check filters unrelated writes
    // (e.g. config.json saves land in the same dir).
    m_cfgLastModified = QFileInfo(m_dataRoot + "/input.cfg").lastModified();
    m_watcher.addPath(m_dataRoot);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &InputManager::onDataDirChanged);

    // App-wide filter: any real key press marks the keyboard as the active device.
    QCoreApplication::instance()->installEventFilter(this);
}

InputManager::~InputManager() {
    for (SDL_GameController *gc : std::as_const(m_controllers))
        SDL_GameControllerClose(gc);
    m_controllers.clear();
    if (m_sdlReady)
        SDL_Quit();
}

void InputManager::setTargetWindow(QQuickWindow *window) {
    m_window = window;
}

// ── SDL lifecycle ─────────────────────────────────────────────────────────────

void InputManager::initSdl() {
    // Keep receiving controller events while another window (mpv fullscreen)
    // has OS focus, and don't let SDL steal SIGINT/SIGTERM from Qt.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    // Game-controller subsystem only: no video, so this works headless (EGLFS).
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        qWarning("[input] SDL init failed: %s — gamepad support disabled", SDL_GetError());
        return;
    }
    m_sdlReady = true;

    const QString dbPath = m_dataRoot + "/gamecontrollerdb.txt";
    if (QFile::exists(dbPath)) {
        int added = SDL_GameControllerAddMappingsFromFile(dbPath.toUtf8().constData());
        if (added >= 0)
            qInfo("[input] loaded %d controller mappings from gamecontrollerdb.txt", added);
        else
            qWarning("[input] could not parse gamecontrollerdb.txt: %s", SDL_GetError());
    }

    // SDL emits CONTROLLERDEVICEADDED for already-connected pads on init,
    // so the poll loop handles initial enumeration and hotplug identically.
    m_pollTimer.start(16);
    qInfo("[input] SDL game-controller subsystem ready");
}

void InputManager::pollSdl() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_CONTROLLERDEVICEADDED:   openController(e.cdevice.which);          break;
        case SDL_CONTROLLERDEVICEREMOVED: closeController(e.cdevice.which);         break;
        case SDL_CONTROLLERBUTTONDOWN:    handleButton(e.cbutton.button, true);     break;
        case SDL_CONTROLLERBUTTONUP:      handleButton(e.cbutton.button, false);    break;
        case SDL_CONTROLLERAXISMOTION:    handleAxis(e.caxis.axis, e.caxis.value);  break;
        default: break;
        }
    }
}

void InputManager::openController(int deviceIndex) {
    SDL_GameController *gc = SDL_GameControllerOpen(deviceIndex);
    if (!gc) {
        qWarning("[input] could not open controller %d: %s", deviceIndex, SDL_GetError());
        return;
    }
    SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    m_controllers.insert(id, gc);
    qInfo("[input] controller added: %s", SDL_GameControllerName(gc));
    emit gamepadConnectedChanged();
}

void InputManager::closeController(SDL_JoystickID instanceId) {
    SDL_GameController *gc = m_controllers.take(instanceId);
    if (!gc)
        return;
    qInfo("[input] controller removed: %s", SDL_GameControllerName(gc));
    SDL_GameControllerClose(gc);

    // Don't leave a direction repeating (or an axis latched) after unplug.
    if (m_heldDirection != Action::None)
        releaseAction(m_heldDirection);
    m_axisState.clear();
    emit gamepadConnectedChanged();
}

// ── Mapping ───────────────────────────────────────────────────────────────────

void InputManager::rebuildMapping() {
    loadDefaultMapping();
    loadUserMapping();
    updateHints();
}

void InputManager::loadDefaultMapping() {
    m_buttonMap.clear();
    m_axisMap.clear();
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_UP]       = Action::Up;
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_DOWN]     = Action::Down;
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_LEFT]     = Action::Left;
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_RIGHT]    = Action::Right;
    m_buttonMap[SDL_CONTROLLER_BUTTON_A]             = Action::Select;
    m_buttonMap[SDL_CONTROLLER_BUTTON_B]             = Action::Back;
    m_buttonMap[SDL_CONTROLLER_BUTTON_BACK]          = Action::Back;
    m_buttonMap[SDL_CONTROLLER_BUTTON_START]         = Action::PlayPause;
    m_buttonMap[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]  = Action::Left;
    m_buttonMap[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = Action::Right;
    m_axisMap[SDL_CONTROLLER_AXIS_LEFTX] = { Action::Left, Action::Right };
    m_axisMap[SDL_CONTROLLER_AXIS_LEFTY] = { Action::Up,   Action::Down  };
}

InputManager::Action InputManager::actionFromString(const QString &name, bool *ok) {
    *ok = true;
    if (name == "up")         return Action::Up;
    if (name == "down")       return Action::Down;
    if (name == "left")       return Action::Left;
    if (name == "right")      return Action::Right;
    if (name == "select")     return Action::Select;
    if (name == "back")       return Action::Back;
    if (name == "play_pause" || name == "playpause") return Action::PlayPause;
    if (name == "none")       return Action::None;
    *ok = false;
    return Action::None;
}

// $DATA_ROOT/input.cfg — one "<input> <action>" per line, # comments, case-
// insensitive. Inputs are SDL names ("a", "dpup", "lefty-"…) with the long
// SDL_CONTROLLER_BUTTON_*/AXIS_* prefixes optional; axes take a +/- suffix.
// Lines merge over the defaults; bad lines are skipped with a warning.
void InputManager::loadUserMapping() {
    QFile f(m_dataRoot + "/input.cfg");
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("[input] could not read input.cfg: %s", qPrintable(f.errorString()));
        return;
    }

    QTextStream in(&f);
    int lineNo = 0, applied = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();
        ++lineNo;
        int hash = line.indexOf('#');
        if (hash >= 0)
            line.truncate(hash);
        line = line.simplified().toLower();
        if (line.isEmpty())
            continue;

        const QStringList parts = line.split(' ');
        if (parts.size() != 2) {
            qWarning("[input] input.cfg line %d ignored (expected \"<input> <action>\"): %s",
                     lineNo, qPrintable(line));
            continue;
        }

        bool actionOk = false;
        const Action action = actionFromString(parts[1], &actionOk);
        if (!actionOk) {
            qWarning("[input] input.cfg line %d ignored (unknown action \"%s\")",
                     lineNo, qPrintable(parts[1]));
            continue;
        }

        QString input = parts[0];
        input.remove(QStringLiteral("sdl_controller_button_"));
        input.remove(QStringLiteral("sdl_controller_axis_"));

        // Axis bindings carry a direction suffix (lefty-, triggerright+).
        int axisSign = 0;
        if (input.endsWith('+')) { axisSign = +1; input.chop(1); }
        else if (input.endsWith('-')) { axisSign = -1; input.chop(1); }

        // Accept the enum-style trigger names alongside SDL's string names.
        if (input == "triggerleft")  input = "lefttrigger";
        if (input == "triggerright") input = "righttrigger";

        const QByteArray inputUtf8 = input.toUtf8();
        if (axisSign != 0) {
            SDL_GameControllerAxis axis = SDL_GameControllerGetAxisFromString(inputUtf8.constData());
            if (axis == SDL_CONTROLLER_AXIS_INVALID) {
                qWarning("[input] input.cfg line %d ignored (unknown axis \"%s\")",
                         lineNo, qPrintable(parts[0]));
                continue;
            }
            auto pair = m_axisMap.value(axis, { Action::None, Action::None });
            (axisSign < 0 ? pair.first : pair.second) = action;
            m_axisMap[axis] = pair;
        } else {
            SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(inputUtf8.constData());
            if (button == SDL_CONTROLLER_BUTTON_INVALID) {
                qWarning("[input] input.cfg line %d ignored (unknown input \"%s\")",
                         lineNo, qPrintable(parts[0]));
                continue;
            }
            m_buttonMap[button] = action;
        }
        ++applied;
    }
    qInfo("[input] input.cfg: applied %d binding(s)", applied);
}

void InputManager::onDataDirChanged(const QString &) {
    const QFileInfo cfg(m_dataRoot + "/input.cfg");
    const QDateTime modified = cfg.exists() ? cfg.lastModified() : QDateTime();
    if (modified == m_cfgLastModified)
        return;
    m_cfgLastModified = modified;
    qInfo("[input] input.cfg changed — reloading mapping");
    rebuildMapping();
}

// ── Input → action → synthesized key event ───────────────────────────────────

void InputManager::handleButton(Uint8 button, bool pressed) {
    const Action a = m_buttonMap.value(button, Action::None);
    if (a == Action::None)
        return;
    if (pressed)
        pressAction(a);
    else
        releaseAction(a);
}

void InputManager::handleAxis(Uint8 axis, Sint16 value) {
    const auto it = m_axisMap.constFind(axis);
    if (it == m_axisMap.constEnd())
        return;

    const int old = m_axisState.value(axis, 0);
    int now = old;
    if (old == 0) {
        if (value >= kAxisEngage)       now = +1;
        else if (value <= -kAxisEngage) now = -1;
    } else if (old > 0) {
        if (value < kAxisRelease)       now = (value <= -kAxisEngage) ? -1 : 0;
    } else {
        if (value > -kAxisRelease)      now = (value >= kAxisEngage) ? +1 : 0;
    }
    if (now == old)
        return;
    m_axisState[axis] = now;

    if (old != 0)
        releaseAction(old < 0 ? it->first : it->second);
    if (now != 0)
        pressAction(now < 0 ? it->first : it->second);
}

void InputManager::pressAction(Action a) {
    if (a == Action::None)
        return;
    setLastInputDevice(QStringLiteral("gamepad"));
    postKey(qtKeyForAction(a), QEvent::KeyPress, false);

    if (isDirectional(a)) {
        // Most recent direction wins the repeat slot.
        m_heldDirection = a;
        m_repeatTimer.stop();
        m_repeatDelayTimer.start();
    }
}

void InputManager::releaseAction(Action a) {
    if (a == Action::None)
        return;
    if (m_heldDirection == a) {
        m_heldDirection = Action::None;
        m_repeatDelayTimer.stop();
        m_repeatTimer.stop();
    }
    postKey(qtKeyForAction(a), QEvent::KeyRelease, false);
}

void InputManager::onRepeatDelayElapsed() {
    if (m_heldDirection == Action::None)
        return;
    onRepeatTick();
    m_repeatTimer.start();
}

void InputManager::onRepeatTick() {
    if (m_heldDirection == Action::None)
        return;
    postKey(qtKeyForAction(m_heldDirection), QEvent::KeyPress, true);
}

// Post to the root QQuickWindow, not QGuiApplication::focusWindow(): Qt Quick
// delivers posted key events to the window's activeFocusItem even when the
// window has no OS-level focus, which is exactly the state during fullscreen
// mpv playback on macOS.
void InputManager::postKey(int qtKey, QEvent::Type type, bool autoRepeat) {
    if (!m_window)
        return;
    QCoreApplication::postEvent(
        m_window,
        new QKeyEvent(type, qtKey, Qt::NoModifier,
                      kSyntheticScanCode, 0, 0, QString(), autoRepeat));
}

int InputManager::qtKeyForAction(Action a) {
    switch (a) {
    case Action::Up:        return Qt::Key_Up;
    case Action::Down:      return Qt::Key_Down;
    case Action::Left:      return Qt::Key_Left;
    case Action::Right:     return Qt::Key_Right;
    case Action::Select:    return Qt::Key_Return;
    case Action::Back:      return Qt::Key_Escape;
    case Action::PlayPause: return Qt::Key_Space;
    case Action::None:      break;
    }
    return 0;
}

bool InputManager::isDirectional(Action a) {
    return a == Action::Up || a == Action::Down || a == Action::Left || a == Action::Right;
}

// ── Active-device tracking & footer hints ─────────────────────────────────────

bool InputManager::eventFilter(QObject *obj, QEvent *event) {
    Q_UNUSED(obj)
    if (event->type() == QEvent::KeyPress) {
        const auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->nativeScanCode() != kSyntheticScanCode)
            setLastInputDevice(QStringLiteral("keyboard"));
    }
    return false;
}

void InputManager::setLastInputDevice(const QString &device) {
    if (m_lastInputDevice == device)
        return;
    m_lastInputDevice = device;
    emit lastInputDeviceChanged();
    updateHints();
}

// hints drives the footer labels in every view. Keyboard values are the exact
// strings the footers used before this existed; gamepad values come from a
// reverse lookup of the active mapping (enum order puts face buttons first).
// Directional glyphs stay — they're d-pad-true on a controller.
void InputManager::updateHints() {
    QVariantMap h;
    h["navigate"]   = QStringLiteral("[▲▼]");
    h["change"]     = QStringLiteral("[◄►]");
    h["browse"]     = QStringLiteral("[►]");
    h["back"]       = QStringLiteral("[ESC]");
    h["select"]     = QStringLiteral("[ENTER]");
    h["play_pause"] = QStringLiteral("[SPACE]");

    if (m_lastInputDevice == QStringLiteral("gamepad")) {
        const auto buttonLabel = [this](Action a) -> QString {
            for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
                if (m_buttonMap.value(b, Action::None) == a) {
                    const char *name = SDL_GameControllerGetStringForButton(
                        static_cast<SDL_GameControllerButton>(b));
                    if (name)
                        return "[" + QString::fromLatin1(name).toUpper() + "]";
                }
            }
            return QString();  // unbound → keep keyboard label
        };
        const QString back = buttonLabel(Action::Back);
        const QString select = buttonLabel(Action::Select);
        const QString playPause = buttonLabel(Action::PlayPause);
        if (!back.isEmpty())      h["back"]       = back;
        if (!select.isEmpty())    h["select"]     = select;
        if (!playPause.isEmpty()) h["play_pause"] = playPause;
    }

    if (h != m_hints) {
        m_hints = h;
        emit hintsChanged();
    }
}
