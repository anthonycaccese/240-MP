#include "InputManager.h"
#include "../AppCore.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QQuickWindow>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#ifdef Q_OS_LINUX
#include <QSocketNotifier>
#include <QDir>
#include <linux/input.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace {
// Planted in nativeScanCode of synthesized events so the keyboard detector in
// eventFilter() can tell our gamepad-originated key events from real key presses.
constexpr quint32 kSyntheticScanCode = 0x240F00D;

// Extended key ids for the Consumer Control evdev path (see
// openConsumerControlDevice()) are stored in m_keyRemap as kEvdevKeyBase plus
// the raw Linux KEY_* code. Qt::Key's own values (ASCII range, or the
// "special key" range starting at 0x01000000) never reach this high, so the
// two id spaces can share one QHash with no risk of collision.
constexpr int kEvdevKeyBase = 0x02000000;

// Same idea, for a mouse button (Qt::MouseButton), some "air mouse" remotes
// wire their center/OK button as a literal left click rather than any kind of
// key, so it never generates a QKeyEvent at all. Distinct base, same QHash.
constexpr int kMouseButtonBase = 0x03000000;

// Analog stick thresholds (of ±32768). Engage above one, release below the
// other — the gap prevents flutter when the stick rests near the threshold.
constexpr Sint16 kAxisEngage  = 16384;
constexpr Sint16 kAxisRelease = 12000;

// Held-direction auto-repeat, tuned to feel like keyboard repeat in lists.
constexpr int kRepeatDelayMs    = 400;
constexpr int kRepeatIntervalMs = 100;

// Qt reports both shift keys as Qt::Key_Shift; telling them apart takes the
// platform code. Linux keymaps (eglfs/evdev, X11, Wayland) report evdev's
// KEY_RIGHTSHIFT, with or without the X11-style +8 offset; macOS reports
// kVK_RightShift in the virtual key.
bool isRightShift(const QKeyEvent *ke) {
#ifdef Q_OS_MACOS
    return ke->nativeVirtualKey() == 0x3C;   // kVK_RightShift
#else
    const quint32 sc = ke->nativeScanCode();
    return sc == 54 || sc == 62;             // KEY_RIGHTSHIFT, +8 offset
#endif
}

// True when an editable text field (e.g. a QML TextInput/TextField) currently
// holds focus. Detected via the Qt::ImEnabled input-method query — the same
// signal virtual keyboards use to decide when to appear — so it doesn't depend
// on QML class names. Used to let Right Shift type shifted characters instead
// of acting as Back while the user is editing text.
bool textInputHasFocus() {
    QObject *fo = QGuiApplication::focusObject();
    if (!fo) return false;
    QInputMethodQueryEvent query(Qt::ImEnabled);
    QCoreApplication::sendEvent(fo, &query);
    return query.value(Qt::ImEnabled).toBool();
}
}

InputManager::InputManager(const QString &dataRoot, AppCore *appCore, QObject *parent)
    : QObject(parent)
    , m_appCore(appCore)
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

    loadKeyRemap();
    if (m_appCore)
        connect(m_appCore, &AppCore::appSettingChanged, this, &InputManager::onAppSettingChanged);

#ifdef Q_OS_LINUX
    openConsumerControlDevice();
#endif

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
#ifdef Q_OS_LINUX
    if (m_consumerFd >= 0)
        ::close(m_consumerFd);
#endif
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
    // Force positional button semantics on Nintendo-type pads (default is
    // label-based there): "a" always means the SOUTH position, on every pad.
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");

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
        case SDL_CONTROLLERDEVICEADDED:   openController(e.cdevice.which);  break;
        case SDL_CONTROLLERDEVICEREMOVED: closeController(e.cdevice.which); break;
        case SDL_CONTROLLERBUTTONDOWN:
            handleButton(e.cbutton.which, e.cbutton.button, true);
            break;
        case SDL_CONTROLLERBUTTONUP:
            handleButton(e.cbutton.which, e.cbutton.button, false);
            break;
        case SDL_CONTROLLERAXISMOTION:
            handleAxis(e.caxis.which, e.caxis.axis, e.caxis.value);
            break;
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
    recomputeSuppressedDevices();
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
    m_heldActions.clear();   // no stuck "held" action if a device drops mid-press
    recomputeSuppressedDevices();   // built-in may have unplugged → unsuppress
    if (m_lastActiveController == instanceId) {
        m_lastActiveController = -1;
        updateHints();
    }
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
    m_labelOverrides.clear();
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

// Button names are POSITIONAL (Xbox reference layout): "a" is always the
// south face button regardless of what's printed on the pad. The positional
// aliases south/east/west/north and the long SDL_CONTROLLER_BUTTON_* forms
// (including SDL3-style SOUTH/EAST/…) resolve to the same buttons.
// Returns the SDL button, or -1 if the token isn't a button.
int InputManager::buttonFromToken(const QString &token) {
    QString name = token.toLower();
    name.remove(QStringLiteral("sdl_controller_button_"));
    if (name == "south")      name = QStringLiteral("a");
    else if (name == "east")  name = QStringLiteral("b");
    else if (name == "west")  name = QStringLiteral("x");
    else if (name == "north") name = QStringLiteral("y");
    const SDL_GameControllerButton button =
        SDL_GameControllerGetButtonFromString(name.toUtf8().constData());
    return button == SDL_CONTROLLER_BUTTON_INVALID ? -1 : int(button);
}

// $DATA_ROOT/input.cfg — case-insensitive, # comments, merged over defaults,
// bad lines skipped with a warning. Two line forms:
//   <input> <action>   bind a button/axis ("a", "south", "dpup", "lefty-"…)
//   label <button> <text>   override the footer label for a button
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
        line = line.simplified();   // not lowercased: label text keeps its case
        if (line.isEmpty())
            continue;

        const QStringList parts = line.split(' ');

        if (parts[0].compare(QStringLiteral("label"), Qt::CaseInsensitive) == 0) {
            if (parts.size() != 3) {
                qWarning("[input] input.cfg line %d ignored (expected \"label <button> <text>\"): %s",
                         lineNo, qPrintable(line));
                continue;
            }
            const int button = buttonFromToken(parts[1]);
            if (button < 0) {
                qWarning("[input] input.cfg line %d ignored (unknown button \"%s\")",
                         lineNo, qPrintable(parts[1]));
                continue;
            }
            m_labelOverrides[button] = parts[2];
            ++applied;
            continue;
        }

        if (parts.size() != 2) {
            qWarning("[input] input.cfg line %d ignored (expected \"<input> <action>\"): %s",
                     lineNo, qPrintable(line));
            continue;
        }

        bool actionOk = false;
        const Action action = actionFromString(parts[1].toLower(), &actionOk);
        if (!actionOk) {
            qWarning("[input] input.cfg line %d ignored (unknown action \"%s\")",
                     lineNo, qPrintable(parts[1]));
            continue;
        }

        QString input = parts[0].toLower();
        input.remove(QStringLiteral("sdl_controller_axis_"));

        // Axis bindings carry a direction suffix (lefty-, triggerright+).
        int axisSign = 0;
        if (input.endsWith('+')) { axisSign = +1; input.chop(1); }
        else if (input.endsWith('-')) { axisSign = -1; input.chop(1); }

        // Accept the enum-style trigger names alongside SDL's string names.
        if (input == "triggerleft")  input = "lefttrigger";
        if (input == "triggerright") input = "righttrigger";

        if (axisSign != 0) {
            SDL_GameControllerAxis axis =
                SDL_GameControllerGetAxisFromString(input.toUtf8().constData());
            if (axis == SDL_CONTROLLER_AXIS_INVALID) {
                qWarning("[input] input.cfg line %d ignored (unknown axis \"%s\")",
                         lineNo, qPrintable(parts[0]));
                continue;
            }
            auto pair = m_axisMap.value(axis, { Action::None, Action::None });
            (axisSign < 0 ? pair.first : pair.second) = action;
            m_axisMap[axis] = pair;
        } else {
            const int button = buttonFromToken(input);
            if (button < 0) {
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

// Keyboard/remote-button remap, config.json's app.remote_keymap.<action>,
// each an int Qt::Key value (or unset/0, meaning "no extra key for this
// action"). Set from Settings > Remote Controls (views/RemapControls.qml).
// Unlike input.cfg's gamepad rebinds, this is additive: it never removes an
// action's default key, it only adds one more physical key that also fires
// it, so a bad remap can't lock the menus out from a plain keyboard.
void InputManager::loadKeyRemap() {
    m_keyRemap.clear();
    if (!m_appCore)
        return;
    static const struct { const char *name; Action action; } kRemapActions[] = {
        { "up",     Action::Up },
        { "down",   Action::Down },
        { "left",   Action::Left },
        { "right",  Action::Right },
        { "select", Action::Select },
        { "back",   Action::Back },
    };
    for (const auto &entry : kRemapActions) {
        bool ok = false;
        const int qtKey = m_appCore->get_setting(QString(), QStringLiteral("remote_keymap.") + entry.name).toInt(&ok);
        if (ok && qtKey != 0)
            m_keyRemap[qtKey] = entry.action;
    }
}

void InputManager::onAppSettingChanged(const QString &key, const QString &value) {
    Q_UNUSED(value)
    if (key.startsWith(QStringLiteral("remote_keymap.")))
        loadKeyRemap();
}

void InputManager::setRemapCapture(bool active) {
    m_remapCapture = active;
}

#ifdef Q_OS_LINUX
// Some remote/keyboard USB combos expose their "Consumer Page" HID buttons
// (Home, Back, Menu, colored buttons, zoom, media transport…) as a *separate*
// /dev/input/eventN device from the plain keyboard interface, confirmed via
// `cat /proc/bus/input/devices` showing three sibling interfaces (Keyboard,
// Consumer Control, Mouse) off one USB composite device. Qt's EGLFS/libinput
// backend doesn't classify "Consumer Control" as a keyboard, so those button
// presses never reach QML as ordinary QKeyEvents no matter what the device is
// capable of sending. This opens that device directly (read-only) and feeds
// it into the same Action/remap system as everything else, bypassing Qt's
// input pipeline entirely for this one device.
//
// Scanned once at startup, no hotplug, this is a fixed USB dongle here, not
// something swapped mid-session. Harmless no-op on a machine with no such
// device (nothing matches the name, nothing is opened).
void InputManager::openConsumerControlDevice() {
    QDir dir(QStringLiteral("/dev/input"));
    const QStringList entries = dir.entryList(QStringList() << QStringLiteral("event*"), QDir::System);
    for (const QString &entry : entries) {
        const QString path = dir.filePath(entry);
        const int fd = ::open(path.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        char name[256] = {0};
        const bool isConsumerControl = ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0
            && QString::fromUtf8(name).contains(QStringLiteral("Consumer Control"), Qt::CaseInsensitive);
        if (isConsumerControl) {
            m_consumerFd = fd;
            m_consumerNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
            connect(m_consumerNotifier, &QSocketNotifier::activated, this, &InputManager::onConsumerControlReadable);
            qInfo("[input] Consumer Control device found: %s (%s)", name, qPrintable(path));
            return;
        }
        ::close(fd);
    }
    qInfo("[input] no Consumer Control device found, remote's extra buttons (if any) won't be remappable");
}

void InputManager::onConsumerControlReadable() {
    struct input_event ev;
    for (;;) {
        const ssize_t n = ::read(m_consumerFd, &ev, sizeof(ev));
        if (n != ssize_t(sizeof(ev))) {
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;   // drained
            // EOF or a hard error (ENODEV once the dongle is unplugged): tear
            // the notifier down, or it keeps firing on the dead fd and
            // busy-spins the event loop.
            qWarning("[input] Consumer Control device lost — disabling");
            m_consumerNotifier->setEnabled(false);
            m_consumerNotifier->deleteLater();
            m_consumerNotifier = nullptr;
            ::close(m_consumerFd);
            m_consumerFd = -1;
            return;
        }
        if (ev.type != EV_KEY || ev.value == 2)   // ignore autorepeat and non-key events
            continue;
        const int extendedId = kEvdevKeyBase + ev.code;

        if (m_remapCapture) {
            // Capture mode: report the button, never act on it. Acting is what
            // must not happen here — finishing a capture writes the binding
            // into m_keyRemap before this function resumes, so a lookup after
            // the emit would fire the action on the very press that bound it.
            if (ev.value == 1)
                emit auxButtonPressed(extendedId);
            continue;
        }

        const Action a = m_keyRemap.value(extendedId, Action::None);
        if (a == Action::None)
            continue;
        if (ev.value == 1)
            beginPress(a);
        else
            releaseAction(a);
    }
}
#endif

// Display name for a Consumer Control button (extendedId - kEvdevKeyBase is
// the raw Linux KEY_* code). Covers the codes an ordinary remote is likely to
// send; anything else falls back to "Button <code>" rather than nothing.
QString InputManager::evdevKeyName(int linuxCode) {
#ifndef Q_OS_LINUX
    // The Consumer Control evdev path only exists on Linux (see
    // openConsumerControlDevice()); nothing ever stores a code in this range
    // on other platforms, but the function still has to compile everywhere.
    return QStringLiteral("Button %1").arg(linuxCode);
#else
    switch (linuxCode) {
    case KEY_HOMEPAGE:    return QStringLiteral("Home");
    case KEY_BACK:        return QStringLiteral("Back");
    case KEY_MENU:        return QStringLiteral("Menu");
    case KEY_INFO:        return QStringLiteral("Info");
    case KEY_EXIT:        return QStringLiteral("Exit");
    case KEY_SEARCH:      return QStringLiteral("Search");
    case KEY_WWW:         return QStringLiteral("WWW");
    case KEY_MAIL:        return QStringLiteral("Mail");
    case KEY_RED:         return QStringLiteral("Red");
    case KEY_GREEN:       return QStringLiteral("Green");
    case KEY_YELLOW:      return QStringLiteral("Yellow");
    case KEY_BLUE:        return QStringLiteral("Blue");
    case KEY_ZOOMIN:      return QStringLiteral("Zoom In");
    case KEY_ZOOMOUT:     return QStringLiteral("Zoom Out");
    case KEY_ZOOMRESET:   return QStringLiteral("Zoom Reset");
    case KEY_PLAY:        return QStringLiteral("Play");
    case KEY_PLAYPAUSE:   return QStringLiteral("Play/Pause");
    case KEY_PAUSE:       return QStringLiteral("Pause");
    case KEY_STOP:        return QStringLiteral("Stop");
    case KEY_REWIND:      return QStringLiteral("Rewind");
    case KEY_FASTFORWARD: return QStringLiteral("Fast Forward");
    case KEY_NEXTSONG:    return QStringLiteral("Next");
    case KEY_PREVIOUSSONG:return QStringLiteral("Previous");
    case KEY_RECORD:      return QStringLiteral("Record");
    case KEY_VOLUMEUP:    return QStringLiteral("Volume Up");
    case KEY_VOLUMEDOWN:  return QStringLiteral("Volume Down");
    case KEY_MUTE:        return QStringLiteral("Mute");
    case KEY_CHANNELUP:   return QStringLiteral("Channel Up");
    case KEY_CHANNELDOWN: return QStringLiteral("Channel Down");
    case KEY_BUTTONCONFIG:return QStringLiteral("Tools");
    case KEY_CONFIG:      return QStringLiteral("Config");
    case KEY_SELECT:      return QStringLiteral("Select");
    case KEY_POWER:       return QStringLiteral("Power");
    case KEY_SLEEP:       return QStringLiteral("Sleep");
    default:              return QStringLiteral("Button %1").arg(linuxCode);
    }
#endif
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

// Footer labels follow the controller last touched, so swapping between e.g.
// an Xbox pad and an 8BitDo keeps the face-button labels truthful.
void InputManager::noteActiveController(SDL_JoystickID which) {
    if (which == m_lastActiveController)
        return;
    m_lastActiveController = which;
    updateHints();
}

// When Steam Input is active it presents a managed "Steam Virtual Gamepad"
// alongside the raw controllers it mirrors — on the Steam Deck that's the
// built-in "Steam Deck" pad; on a desktop running Steam it's whatever pad is
// attached. The raw device echoes each press with ~0.5s of lag, so one physical
// press registers twice. The virtual pad is the low-latency one to use (it's
// also the device Gaming Mode drives), so whenever it is present, suppress every
// other controller. With no virtual pad (Steam not running), nothing is
// suppressed and raw controllers work normally. Recomputed on connect/disconnect.
void InputManager::recomputeSuppressedDevices() {
    m_suppressedDevices.clear();
    auto isVirtual = [](SDL_GameController *gc) {
        const char *n = SDL_GameControllerName(gc);
        return n && qstrcmp(n, "Steam Virtual Gamepad") == 0;
    };
    bool hasVirtual = false;
    for (SDL_GameController *gc : std::as_const(m_controllers))
        if (isVirtual(gc)) { hasVirtual = true; break; }
    if (!hasVirtual)
        return;
    for (auto it = m_controllers.cbegin(); it != m_controllers.cend(); ++it)
        if (!isVirtual(it.value()))
            m_suppressedDevices.insert(it.key());
}

void InputManager::handleButton(SDL_JoystickID which, Uint8 button, bool pressed) {
    if (m_suppressedDevices.contains(which))
        return;
    const Action a = m_buttonMap.value(button, Action::None);
    if (a == Action::None)
        return;
    noteActiveController(which);
    if (pressed)
        pressAction(a);
    else
        releaseAction(a);
}

void InputManager::handleAxis(SDL_JoystickID which, Uint8 axis, Sint16 value) {
    if (m_suppressedDevices.contains(which))
        return;
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

    // Only a real engage/release counts as "using" this controller — idle
    // stick jitter must not steal label ownership from the pad in use.
    noteActiveController(which);
    if (old != 0)
        releaseAction(old < 0 ? it->first : it->second);
    if (now != 0)
        pressAction(now < 0 ? it->first : it->second);
}

void InputManager::pressAction(Action a) {
    if (a == Action::None)
        return;
    setLastInputDevice(QStringLiteral("gamepad"));
    beginPress(a);
}

// Deliver a press and arm the held-direction auto-repeat. Shared by gamepad
// buttons/axes and remapped consumer-control/mouse inputs — the evdev path
// filters out autorepeat (and many remotes never send it), so a held direction
// button repeats through the same timers a held d-pad does. Remapped keyboard
// keys don't come through here: the OS supplies their repeat.
void InputManager::beginPress(Action a) {
    // Drop a press for an action already held. The Steam Deck reports one
    // physical press from two SDL devices (built-in + Steam Input virtual pad),
    // which would otherwise double every navigation. Genuine repeat still comes
    // from the timers below; a real double-tap alternates held state so both taps
    // register.
    if (m_heldActions.contains(a))
        return;
    m_heldActions.insert(a);

    deliverPress(a, false);

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
    // Ignore a release for an action that isn't held — the mirror device's
    // duplicate release, or a stray release with no matching press.
    if (!m_heldActions.remove(a))
        return;
    if (m_heldDirection == a) {
        m_heldDirection = Action::None;
        m_repeatDelayTimer.stop();
        m_repeatTimer.stop();
    }
    // mpv's "keypress" command is one-shot — releases only matter for QML.
    if (windowActive())
        postKey(qtKeyForAction(a), QEvent::KeyRelease, false);
}

// While the Qt window is active, actions become posted key events into QML.
// When it isn't — fullscreen mpv owns OS focus on macOS, and a deactivated
// QQuickWindow has no activeFocusItem for key events to land on — actions go
// straight to mpv over IPC instead, mirroring what the Player views' key
// forwarding does on platforms where the window stays active (RPi/EGLFS).
// When mpv isn't running either, sendKey is a no-op, so background presses
// while the user is in another app do nothing — same as keyboard.
void InputManager::deliverPress(Action a, bool autoRepeat) {
    if (windowActive())
        postKey(qtKeyForAction(a), QEvent::KeyPress, autoRepeat);
    else
        emit mpvKeyRequested(mpvKeyForAction(a));
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
    deliverPress(m_heldDirection, true);
}

bool InputManager::windowActive() const {
    return m_window && m_window->isActive();
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

// Same key names the Player views pass to mpvController.sendKey().
QString InputManager::mpvKeyForAction(Action a) {
    switch (a) {
    case Action::Up:        return QStringLiteral("UP");
    case Action::Down:      return QStringLiteral("DOWN");
    case Action::Left:      return QStringLiteral("LEFT");
    case Action::Right:     return QStringLiteral("RIGHT");
    case Action::Select:    return QStringLiteral("ENTER");
    case Action::Back:      return QStringLiteral("ESC");
    case Action::PlayPause: return QStringLiteral("SPACE");
    case Action::None:      break;
    }
    return QString();
}

bool InputManager::isDirectional(Action a) {
    return a == Action::Up || a == Action::Down || a == Action::Left || a == Action::Right;
}

QString InputManager::keyDisplayName(int qtKey) const {
    if (qtKey == 0)
        return QString();
    if (qtKey >= kMouseButtonBase)
        return mouseButtonName(qtKey - kMouseButtonBase);
    if (qtKey >= kEvdevKeyBase)
        return evdevKeyName(qtKey - kEvdevKeyBase);
    return QKeySequence(qtKey).toString(QKeySequence::NativeText);
}

QString InputManager::mouseButtonName(int qtButton) {
    switch (Qt::MouseButton(qtButton)) {
    case Qt::LeftButton:    return QStringLiteral("Mouse: Left Click");
    case Qt::RightButton:   return QStringLiteral("Mouse: Right Click");
    case Qt::MiddleButton:  return QStringLiteral("Mouse: Middle Click");
    case Qt::BackButton:    return QStringLiteral("Mouse: Back");
    case Qt::ForwardButton: return QStringLiteral("Mouse: Forward");
    default:                return QStringLiteral("Mouse Button %1").arg(qtButton);
    }
}

// HID media keys are a separate concern from navigation actions — they always
// target mpv, never QML — so they bypass the Action enum and map straight to the
// canonical mpv key names mpv-media-keys.lua binds.
QString InputManager::mpvKeyForMediaEvent(const QKeyEvent *ke) {
    switch (ke->key()) {
    case Qt::Key_VolumeUp:              return QStringLiteral("VOLUME_UP");
    case Qt::Key_VolumeDown:            return QStringLiteral("VOLUME_DOWN");
    case Qt::Key_VolumeMute:            return QStringLiteral("MUTE");
    case Qt::Key_MediaTogglePlayPause:
    case Qt::Key_MediaPlay:
    case Qt::Key_MediaPause:            return QStringLiteral("PLAYPAUSE");
    case Qt::Key_MediaStop:             return QStringLiteral("STOP");
    case Qt::Key_MediaNext:             return QStringLiteral("NEXT");
    case Qt::Key_MediaPrevious:         return QStringLiteral("PREV");
    case Qt::Key_AudioForward:          return QStringLiteral("FORWARD");
    case Qt::Key_AudioRewind:           return QStringLiteral("REWIND");
    default:                            return QString();
    }
}

// ── Active-device tracking & footer hints ─────────────────────────────────────

bool InputManager::eventFilter(QObject *obj, QEvent *event) {
    Q_UNUSED(obj)
    const QEvent::Type type = event->type();

    // Some remotes wire their center/OK button as a literal mouse click (see
    // openConsumerControlDevice()'s comment for the sibling-devices story).
    // Qt still delivers that as an ordinary QMouseEvent regardless of what
    // libinput thinks the device is, so no raw evdev reader is needed here,
    // just the same remap table and capture signal as everything else.
    if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick
        || type == QEvent::MouseButtonRelease) {
        const auto *me = static_cast<QMouseEvent *>(event);
        // A fast second press arrives as DblClick instead of Press — for a
        // remapped button it must count as a press, or every other click
        // leaks into the UI as a real one.
        const bool isPress = type != QEvent::MouseButtonRelease;
        const int extendedId = kMouseButtonBase + int(me->button());

        if (m_remapCapture) {
            if (isPress)
                emit auxButtonPressed(extendedId);
            return true;
        }

        const Action remapped = m_keyRemap.value(extendedId, Action::None);
        if (remapped != Action::None) {
            if (isPress)
                beginPress(remapped);
            else
                releaseAction(remapped);
            return true;
        }
        return false;
    }

    if (type != QEvent::KeyPress && type != QEvent::KeyRelease)
        return false;
    const auto *ke = static_cast<QKeyEvent *>(event);
    const bool synthetic = ke->nativeScanCode() == kSyntheticScanCode;

    if (type == QEvent::KeyPress && !synthetic)
        setLastInputDevice(QStringLiteral("keyboard"));

    // Right shift acts as Back so the keyboard works one-handed: reuse the
    // gamepad Back path, which posts Escape into QML — or sends ESC to mpv
    // over IPC when fullscreen mpv holds OS focus and the window can't take
    // key events. The bare Shift event is consumed; no view binds Key_Shift.
    //
    // Known gap: during fullscreen playback on macOS the keyboard goes to
    // mpv, not us, and mpv can't bind a bare modifier — so right shift only
    // works in the player on platforms where the app keeps the keyboard
    // (RPi/EGLFS). Same asymmetry as gamepads, minus their SDL workaround.
    // While a text field is focused, let Right Shift behave as an ordinary
    // modifier so the user can type shifted characters (e.g. ':' in a server
    // URL); only repurpose it as Back when nothing editable has focus. Falling
    // through (no return) lets the bare Shift event reach QML, where it's a
    // harmless no-op until the next character key arrives with the modifier.
    if (ke->key() == Qt::Key_Shift && isRightShift(ke) && !textInputHasFocus()) {
        if (!ke->isAutoRepeat()) {
            if (type == QEvent::KeyPress)
                deliverPress(Action::Back, false);
            else
                releaseAction(Action::Back);
        }
        return true;
    }

    // Capture mode (see setRemapCapture): swallow every real key and report
    // presses through auxButtonPressed instead of letting them reach QML.
    // Going through the signal rather than key delivery is what lets an
    // already-remapped key be captured as itself — the remap branch below
    // would otherwise consume it and hand the overlay its action's default
    // key. Sits after the right-shift block so that alias still cancels the
    // overlay via its normal Back path.
    if (m_remapCapture && !synthetic) {
        if (type == QEvent::KeyPress && !ke->isAutoRepeat())
            emit auxButtonPressed(ke->key());
        return true;
    }

    // Custom remote/keyboard remap (Settings > Remote Controls): an extra
    // physical key that also fires one of the six actions, delivered the same
    // way a gamepad press is: a synthesized key event carrying that action's
    // *default* Qt key, so every existing Keys.onPressed handler sees exactly
    // what it always has. The default key itself is untouched (this table is
    // additive), so remapping can't remove the app's own keyboard controls.
    // Skipped for our own synthesized events (nativeScanCode check) so two
    // remapped keys can never feed into each other, and skipped while a text
    // field has focus so typing a remapped character still works normally
    // (the hash miss is checked first — it's cheap, while textInputHasFocus
    // is a synchronous input-method query and shouldn't run per keystroke).
    if (!synthetic) {
        const Action remapped = m_keyRemap.value(ke->key(), Action::None);
        if (remapped != Action::None && !textInputHasFocus()) {
            if (type == QEvent::KeyPress)
                deliverPress(remapped, ke->isAutoRepeat());
            else
                releaseAction(remapped);
            return true;
        }
    }

    // HID media keys drive mpv directly over IPC (sendKey no-ops when mpv isn't
    // running, so they're harmless while browsing). Volume keys repeat while
    // held; the rest fire once per press so a held key is a single seek/chapter
    // jump. On macOS during fullscreen playback mpv holds the keyboard and these
    // never reach us — mpv binds the same names natively.
    const QString mediaKey = mpvKeyForMediaEvent(ke);
    if (!mediaKey.isEmpty()) {
        if (type == QEvent::KeyPress) {
            // Volume keys repeat while held; everything else fires once per press.
            // mediaKey is the single source of truth for which key this is.
            const bool isVolume = mediaKey.startsWith(QLatin1String("VOLUME"));
            if (isVolume || !ke->isAutoRepeat())
                emit mpvKeyRequested(mediaKey);
        }
        return true;
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

// Display text for a button: user override from input.cfg wins; otherwise the
// face buttons (positional a/b/x/y) are translated to what's printed on the
// last-touched controller via its SDL type (Nintendo swaps A/B and X/Y,
// PlayStation uses shapes); everything else uses SDL's name uppercased.
QString InputManager::labelForButton(int button) const {
    const QString override_ = m_labelOverrides.value(button);
    if (!override_.isEmpty())
        return override_;

    SDL_GameController *gc = m_controllers.value(m_lastActiveController, nullptr);
    if (!gc && !m_controllers.isEmpty())
        gc = m_controllers.constBegin().value();
    const SDL_GameControllerType type =
        gc ? SDL_GameControllerGetType(gc) : SDL_CONTROLLER_TYPE_UNKNOWN;

    bool nintendo = type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO;
    bool playstation = type == SDL_CONTROLLER_TYPE_PS3
                    || type == SDL_CONTROLLER_TYPE_PS4
                    || type == SDL_CONTROLLER_TYPE_PS5;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    nintendo = nintendo
            || type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT
            || type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT
            || type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
#endif

    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:   // south
        if (nintendo)    return QStringLiteral("B");
        if (playstation) return QStringLiteral("X");
        return QStringLiteral("A");
    case SDL_CONTROLLER_BUTTON_B:   // east
        if (nintendo)    return QStringLiteral("A");
        if (playstation) return QStringLiteral("O");
        return QStringLiteral("B");
    case SDL_CONTROLLER_BUTTON_X:   // west
        if (nintendo)    return QStringLiteral("Y");
        if (playstation) return QStringLiteral("SQ");
        return QStringLiteral("X");
    case SDL_CONTROLLER_BUTTON_Y:   // north
        if (nintendo)    return QStringLiteral("X");
        if (playstation) return QStringLiteral("TR");
        return QStringLiteral("Y");
    default:
        break;
    }

    const char *name = SDL_GameControllerGetStringForButton(
        static_cast<SDL_GameControllerButton>(button));
    return name ? QString::fromLatin1(name).toUpper() : QString();
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
                    const QString label = labelForButton(b);
                    if (!label.isEmpty())
                        return "[" + label + "]";
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
