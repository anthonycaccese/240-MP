#pragma once
#include <QObject>
#include <QEvent>
#include <QTimer>
#include <QHash>
#include <QPair>
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

signals:
    void gamepadConnectedChanged();
    void lastInputDeviceChanged();
    void hintsChanged();
    // Emitted instead of posting a key event when the Qt window is inactive
    // (fullscreen mpv holds OS focus on macOS, which clears QML active focus).
    // main.cpp connects this to MpvController::sendKey.
    void mpvKeyRequested(const QString &key);
    // A raw press on the auxiliary "Consumer Control" input device (Linux
    // only, see openConsumerControlDevice()) that Qt's own key event delivery
    // never sees, e.g. a remote's Home/Back/Menu/colored/zoom buttons. Carries
    // an opaque extended key id (keyDisplayName() and the remote_keymap.*
    // settings both accept it the same as a real Qt::Key). The remap capture
    // screen listens for this alongside Keys.onPressed so those buttons are
    // bindable too.
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
    void handleButton(SDL_JoystickID which, Uint8 button, bool pressed);
    void handleAxis(SDL_JoystickID which, Uint8 axis, Sint16 value);
    void pressAction(Action a);
    void releaseAction(Action a);
    void deliverPress(Action a, bool autoRepeat);
    void postKey(int qtKey, QEvent::Type type, bool autoRepeat);
    bool windowActive() const;
    void setLastInputDevice(const QString &device);
    void updateHints();
    QString labelForButton(int button) const;
    static int qtKeyForAction(Action a);
    static QString mpvKeyForAction(Action a);
    // Maps a HID media-key event to the canonical mpv key name media-keys.lua
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

#ifdef Q_OS_LINUX
    int m_consumerFd = -1;
    QSocketNotifier *m_consumerNotifier = nullptr;
#endif

    QString m_lastInputDevice = "keyboard";
    QVariantMap m_hints;
};
