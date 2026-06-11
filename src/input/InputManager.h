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

// Centralized gamepad input. SDL controller buttons/axes are mapped to a small
// set of named actions (up/down/left/right/select/back/play_pause), and each
// action is delivered to QML as an ordinary synthesized key event posted to the
// root window — so every existing Keys.onPressed handler (including the Player
// views that forward keys to mpv over IPC) works without gamepad-specific code.
// Defaults can be overridden per-input in $DATA_ROOT/input.cfg (live-reloaded).
class InputManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool gamepadConnected READ gamepadConnected NOTIFY gamepadConnectedChanged)
    Q_PROPERTY(QString lastInputDevice READ lastInputDevice NOTIFY lastInputDeviceChanged)
    Q_PROPERTY(QVariantMap hints READ hints NOTIFY hintsChanged)

public:
    explicit InputManager(const QString &dataRoot, QObject *parent = nullptr);
    ~InputManager() override;

    void setTargetWindow(QQuickWindow *window);

    bool gamepadConnected() const { return !m_controllers.isEmpty(); }
    QString lastInputDevice() const { return m_lastInputDevice; }
    QVariantMap hints() const { return m_hints; }

signals:
    void gamepadConnectedChanged();
    void lastInputDeviceChanged();
    void hintsChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void pollSdl();
    void onRepeatDelayElapsed();
    void onRepeatTick();
    void onDataDirChanged(const QString &path);

private:
    enum class Action { None, Up, Down, Left, Right, Select, Back, PlayPause };

    void initSdl();
    void openController(int deviceIndex);
    void closeController(SDL_JoystickID instanceId);
    void rebuildMapping();
    void loadDefaultMapping();
    void loadUserMapping();
    void handleButton(Uint8 button, bool pressed);
    void handleAxis(Uint8 axis, Sint16 value);
    void pressAction(Action a);
    void releaseAction(Action a);
    void postKey(int qtKey, QEvent::Type type, bool autoRepeat);
    void setLastInputDevice(const QString &device);
    void updateHints();
    static int qtKeyForAction(Action a);
    static Action actionFromString(const QString &name, bool *ok);
    static bool isDirectional(Action a);

    QQuickWindow *m_window = nullptr;
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
    Action m_heldDirection = Action::None;

    QString m_lastInputDevice = "keyboard";
    QVariantMap m_hints;
};
