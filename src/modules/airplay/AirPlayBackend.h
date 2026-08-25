#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QString>
#include <QVariant>

class QSocketNotifier;
class QTimer;
class AppCore;

// Launches/owns shairport-sync as a QProcess for the lifetime of the AirPlay
// module screen (NowPlaying.qml calls startReceiver()/stopReceiver() from its
// Component.onCompleted/onDestruction, mirroring how WeatherBackend's
// start()/stop() are bound to Weather.qml), and republishes its metadata-pipe
// output as properties for the view.
//
// nqptp is *not* managed here: it needs root/CAP_NET_BIND_SERVICE for UDP
// ports 319/320, so it runs as an always-on systemd service installed by
// scripts/setup-airplay.sh instead — the app itself shouldn't need elevated
// privileges just to open a module.
class AirPlayBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString deviceName READ deviceName NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString album READ album NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString artworkPath READ artworkPath NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString senderName READ senderName NOTIFY nowPlayingChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStateChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)
    // Empty means no error. Set when shairport-sync fails to launch at all,
    // or exits/crashes before ever reaching a connected session — see
    // setReceiverError()'s comment for why that distinction matters.
    Q_PROPERTY(QString receiverError READ receiverError NOTIFY receiverErrorChanged)

public:
    explicit AirPlayBackend(const QString &appRoot, const QString &dataRoot,
                             AppCore *appCore, QObject *parent = nullptr);
    ~AirPlayBackend() override;

    QString deviceName() const { return m_deviceName; }
    QString trackTitle() const { return m_trackTitle; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString artworkPath() const { return m_artworkPath; }
    QString senderName() const { return m_senderName; }
    bool isConnected() const { return m_isConnected; }
    bool isPlaying() const { return m_isPlaying; }
    QString receiverError() const { return m_receiverError; }

public slots:
    // Bound to the module screen's lifetime — see NowPlaying.qml.
    Q_INVOKABLE void startReceiver();
    Q_INVOKABLE void stopReceiver();

    // manifest.json "audio_output_device" setting: options_slot / apply_slot.
    // apply_slot is invoked with no arguments (AppCore::invoke_module_action),
    // so applyAudioDeviceSetting re-reads the just-saved value itself.
    Q_INVOKABLE void getAudioDevices();
    Q_INVOKABLE void applyAudioDeviceSetting();

    // Absolute path to device_name.txt, so the idle screen can tell the user
    // exactly where to put it. There's no free-text setting type in the
    // manifest schema (WeatherBackend's weather_location.txt hit the same
    // wall — see its own comment on that), and a virtual on-screen keyboard
    // for a value set once is worse than editing one file, so this follows
    // the same plain-file convention rather than inventing new settings
    // infrastructure. Broadcast name falls back to "240-MP" when the file is
    // absent/empty.
    Q_INVOKABLE QString deviceNameFilePath() const;

    // moduleSettingChanged(moduleId, key, value) -> backend.onSettingChanged(...),
    // wired automatically by AppCore::registerModule().
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

signals:
    void nowPlayingChanged();
    void connectionStateChanged();
    void playbackStateChanged();
    void receiverErrorChanged();
    // Signature must stay exactly (QString,QVariant): AppCore::registerModule()
    // connects to it by introspecting for that normalized signature.
    void dynamicOptionsReady(const QString &key, const QVariant &options);

private slots:
    void handleProcessErrorOccurred(QProcess::ProcessError error);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QString configFilePath() const;
    QString metadataPipePath() const;
    QString airplayDataDir() const;
    void writeConfigFile();
    void openMetadataPipe();
    void closeMetadataPipe();

    // ALSA device enumeration/selection — see the .cpp for why this only
    // surfaces `plughw:` devices and why "Default" resolves to one of them
    // rather than an empty (shairport-sync's-own-default) output_device.
    QList<QPair<QString, QString>> listPlughwDevices();
    QString resolveAudioDevice(const QString &settingValue);
    QString resolveDeviceName() const;
    void resetNowPlaying();

    // Distinguishes "nobody's connected yet" (normal, no error) from
    // "the receiver never got a chance to work" (a real problem worth
    // telling the user about) — see handleProcessFinished()'s comment.
    void setReceiverError(const QString &message);

    // Metadata pipe parsing — see docs/airplay-module-plan.md §6.
    void drainMetadataPipe();
    void processMetadataBuffer();
    void handleChunk(const QByteArray &type, const QByteArray &code, const QByteArray &data);

    QString m_appRoot;
    QString m_dataRoot;
    AppCore *m_appCore = nullptr;
    QString m_selectedAudioDevice; // empty => shairport-sync default ALSA device
    QString m_deviceName = QStringLiteral("240-MP"); // resolved fresh each startReceiver()

    QProcess *m_process = nullptr;
    // Pending mid-session crash auto-restart (see handleProcessFinished()'s
    // comment) — held as a real timer, not a static QTimer::singleShot, so
    // stopReceiver() can cancel it if the user backs out of the module
    // during the delay. Otherwise shairport-sync would relaunch itself in
    // the background after the screen it belongs to has already closed.
    QTimer *m_crashRestartTimer = nullptr;

    int m_metadataFd = -1;
    QSocketNotifier *m_metadataNotifier = nullptr;
    QByteArray m_metadataBuffer;

    QString m_trackTitle;
    QString m_artist;
    QString m_album;
    QString m_artworkPath;
    QString m_senderName;
    bool m_isConnected = false;
    bool m_isPlaying = false;

    int m_artworkSequence = 0;

    QString m_receiverError;
    bool m_stoppingIntentionally = false; // true only inside stopReceiver()'s own terminate/kill
    bool m_everConnectedThisRun = false;  // reset each startReceiver(), set true on ssnc/pbeg

    // Counts consecutive crash-after-connecting restarts (see
    // handleProcessFinished()'s comment) — reset only once a session has
    // stayed connected for a while (see m_connectionUptimer), not on every
    // bare reconnect, so a receiver that reconnects but crashes again
    // within seconds every time — a real, recurring failure — still hits
    // the cap instead of resetting the counter on each reconnect and
    // respawning forever. A deliberate stopReceiver() also resets it.
    int m_crashRestartAttempts = 0;
    // Started on ssnc/pbeg; a session is only considered "healthy" (and
    // resets m_crashRestartAttempts) if it was up for at least
    // kMinHealthyUptimeMs before ending.
    QElapsedTimer m_connectionUptimer;
};
