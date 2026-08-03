#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>

class QProcess;
class QQuickWindow;

// HDMI-CEC input: drive 240-MP with the TV's own remote.
//
// Most TVs can forward remote presses to attached HDMI devices over CEC
// (Samsung "Anynet+", LG "SimpLink", Sony "BRAVIA Sync"). libCEC's `cec-client`
// prints a line like `key pressed: up (1)` for each one; this class spawns it and
// turns those lines into ordinary Qt key events, so every view handles a TV
// remote without knowing it exists — the same contract InputManager gives
// gamepads.
//
// Ported in spirit from NostalgiaBox's `input/cec.py` (MIT — see THIRD-PARTY.md),
// but as a QProcess rather than a linked library, matching how 240-MP already
// treats mpv and ffmpeg: no libcec build dependency, and a missing cec-utils
// simply means the feature stays off.
//
// REQUIRES HDMI. CEC travels on the HDMI cable's CEC pin, so this does nothing
// on a composite/CRT setup — where `enable_tvout=1` disables HDMI outright.
class CecInput : public QObject {
    Q_OBJECT
public:
    explicit CecInput(const QString &dataRoot, QObject *parent = nullptr);
    ~CecInput() override;

    // Where synthesized key events are posted. Same reasoning as InputManager:
    // posting to the root window reaches the QML activeFocusItem even when the
    // window has no OS focus.
    void setTargetWindow(QQuickWindow *window);

    // Starts cec-client if it is present and the feature is enabled. Safe to call
    // when neither is true — it just logs and does nothing.
    void start();
    void stop();

    bool isRunning() const;

signals:
    // Emitted instead of posting a key when the Qt window is inactive (fullscreen
    // mpv holding OS focus). main.cpp wires this to MpvController::sendKey.
    void mpvKeyRequested(const QString &key);

private slots:
    void onReadyRead();
    void onFinished();

private:
    // CEC user-control name (as cec-client prints it) -> Qt key. 0 = ignore.
    static int qtKeyForCecKey(const QString &name);
    // The mpv key name for a Qt key, for the window-inactive path.
    static QString mpvKeyForQtKey(int qtKey);
    void handleLine(const QString &line);
    void deliver(int qtKey);
    bool enabledInConfig() const;

    QString       m_dataRoot;
    QProcess     *m_proc   = nullptr;
    QQuickWindow *m_window = nullptr;
    QByteArray    m_buffer;
};
