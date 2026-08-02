#pragma once
#include <QObject>
#include <QEvent>
#include <QTimer>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QDateTime>
#include <QVariantMap>
#include <QFileSystemWatcher>
#include <SDL.h>

class QQuickWindow;
class QKeyEvent;
class QSocketNotifier;
class AppCore;

// Centralized gamepad input. SDL controller buttons/axes are mapped to a small
// set of named actions (up/down/left/right/select/back/play_pause), and each
// action is delivered to QML as an ordinary synthesized key event posted to the
// root window — so every existing Keys.onPressed handler (including the Player
// views that forward keys to mpv over IPC) works without gamepad-specific code.
// Defaults can be overridden per-input in $DATA_ROOT/input.cfg (live-reloaded).
//
// The same Action/synthesized-key machinery also backs keyboard/remote-button
// remapping (Settings > Remote Controls): an extra physical key can be bound to
// one of the six actions via config.json's "remote_keymap.<action>" settings,
// on top of (never replacing) that action's default key, see keyRemap handling
// in eventFilter().
class InputManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool gamepadConnected READ gamepadConnected NOTIFY gamepadConnectedChanged)
    Q_PROPERTY(QString lastInputDevice READ lastInputDevice NOTIFY lastInputDeviceChanged)
    Q_PROPERTY(QVariantMap hints READ hints NOTIFY hintsChanged)

public:
    explicit InputManager(const QString &dataRoot, AppCore *appCore, QObject *parent = nullptr);
    ~InputManager() override;

    void setTargetWindow(QQuickWindow *window);

    bool gamepadConnected() const { return !m_controllers.isEmpty(); }
    QString lastInputDevice() const { return m_lastInputDevice; }
    QVariantMap hints() const { return m_hints; }

    // Human-readable name for a Qt::Key value (e.g. "Return", "O", "F5"), for
    // the remap screen to show what's currently bound. Used from QML.
    Q_INVOKABLE QString keyDisplayName(int qtKey) const;

    // Remap-capture mode, held on by the remap screen while its "press a
    // button" overlay is up. While active, remap delivery is suspended and
    // every real input — keyboard key, Consumer Control button, mouse button —
    // is reported through auxButtonPressed() instead of acting, so the overlay
    // sees the physical input itself even when it's already bound to an action
    // (the eventFilter would otherwise consume it and synthesize that action's
    // default key). Synthesized gamepad key events keep flowing to QML so a
    // pad's Back button can still cancel the overlay.
    Q_INVOKABLE void setRemapCapture(bool active);

signals:
    void gamepadConnectedChanged();
    void lastInputDeviceChanged();
    void hintsChanged();
    // Emitted instead of posting a key event when the Qt window is inactive
    // (fullscreen mpv holds OS focus on macOS, which clears QML active focus).
    // main.cpp connects this to MpvController::sendKey.
    void mpvKeyRequested(const QString &key);
    // Capture-mode reporting (see setRemapCapture): a raw press on any of the
    // three remappable input paths — a real keyboard key (its Qt::Key), a
    // Consumer Control button (kEvdevKeyBase + Linux KEY_* code; Linux only,
    // see openConsumerControlDevice()), or a mouse button (kMouseButtonBase +
    // Qt::MouseButton). keyDisplayName() and the remote_keymap.* settings
    // accept all three id spaces alike. Only emitted while capture is active.
    void auxButtonPressed(int extendedKeyId);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void pollSdl();
    void onRepeatDelayElapsed();
    void onRepeatTick();
    void onDataDirChanged(const QString &path);
    void onAppSettingChanged(const QString &key, const QString &value);
#ifdef Q_OS_LINUX
    // Defined in the Q_OS_LINUX section of InputManager.cpp; the ifdef keeps
    // moc from emitting a metacall reference to it on other platforms.
    void onConsumerControlReadable();
#endif

private:
    enum class Action { None, Up, Down, Left, Right, Select, Back, PlayPause };

    void initSdl();
    void openController(int deviceIndex);
    void closeController(SDL_JoystickID instanceId);
    void rebuildMapping();
    void loadDefaultMapping();
    void loadUserMapping();
    void loadKeyRemap();
#ifdef Q_OS_LINUX
    void openConsumerControlDevice();
#endif
    static QString evdevKeyName(int linuxCode);
    static QString mouseButtonName(int qtButton);
    void noteActiveController(SDL_JoystickID which);
    void recomputeSuppressedDevices();
    void handleButton(SDL_JoystickID which, Uint8 button, bool pressed);
    void handleAxis(SDL_JoystickID which, Uint8 axis, Sint16 value);
    void pressAction(Action a);
    void beginPress(Action a);
    void releaseAction(Action a);
    void deliverPress(Action a, bool autoRepeat);
    void postKey(int qtKey, QEvent::Type type, bool autoRepeat);
    bool windowActive() const;
    void setLastInputDevice(const QString &device);
    void updateHints();
    QString labelForButton(int button) const;
    static int qtKeyForAction(Action a);
    static QString mpvKeyForAction(Action a);
    // Maps a HID media-key event to the canonical mpv key name mpv-media-keys.lua
    // binds, or an empty string for non-media keys.
    static QString mpvKeyForMediaEvent(const QKeyEvent *ke);
    static Action actionFromString(const QString &name, bool *ok);
    static int buttonFromToken(const QString &token);
    static bool isDirectional(Action a);

    QQuickWindow *m_window = nullptr;
    AppCore *m_appCore = nullptr;
    QString m_dataRoot;
    bool m_sdlReady = false;

    QTimer m_pollTimer;
    QTimer m_repeatDelayTimer;
    QTimer m_repeatTimer;
    QFileSystemWatcher m_watcher;
    QDateTime m_cfgLastModified;

    QHash<SDL_JoystickID, SDL_GameController*> m_controllers;
    QHash<int, Action> m_buttonMap;                  // SDL_GameControllerButton → Action
    QHash<int, QPair<Action, Action>> m_axisMap;     // SDL_GameControllerAxis → (negative, positive)
    QHash<int, int> m_axisState;                     // per-axis engaged direction: -1 / 0 / +1
    QHash<int, QString> m_labelOverrides;            // SDL button → user display label (input.cfg)
    QHash<int, Action> m_keyRemap;                   // Qt::Key or extended evdev id → Action
    SDL_JoystickID m_lastActiveController = -1;      // labels follow the pad last touched
    Action m_heldDirection = Action::None;

    // Steam Input mirror de-dup: it presents a managed "Steam Virtual Gamepad"
    // plus the raw controllers it mirrors (the Deck's built-in pad, etc.); the
    // raw device echoes each press ~0.5s later → double/late input. The virtual
    // pad is the low-latency one (and what Gaming Mode uses), so when it's
    // present we suppress the rest. Recomputed on connect/disconnect.
    QSet<SDL_JoystickID> m_suppressedDevices;

    // Actions currently held down — idempotent press/release hygiene (a device
    // can't re-fire a press it's already holding).
    QSet<Action> m_heldActions;
    bool m_remapCapture = false;                     // see setRemapCapture()

#ifdef Q_OS_LINUX
    int m_consumerFd = -1;
    QSocketNotifier *m_consumerNotifier = nullptr;
#endif

    QString m_lastInputDevice = "keyboard";
    QVariantMap m_hints;
};
