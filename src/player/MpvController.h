#pragma once
#include <QObject>
#include <QProcess>
#include <QLocalSocket>
#include <QTimer>
#include <QJsonArray>

#ifdef Q_OS_LINUX
#include <xf86drm.h>
#include <xf86drmMode.h>

struct DrmSavedState {
    uint32_t crtcId      = 0;
    uint32_t connectorId = 0;
    uint32_t fbId        = 0;
    int      x           = 0;
    int      y           = 0;
    drmModeModeInfo mode = {};
    bool     valid       = false;
};
#endif

class MpvController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int position    READ position    NOTIFY positionChanged)
    Q_PROPERTY(int duration    READ duration    NOTIFY durationChanged)
    Q_PROPERTY(int playlistPos READ playlistPos NOTIFY playlistPosChanged)

public:
    explicit MpvController(const QString &appRoot, QObject *parent = nullptr);
    ~MpvController() override;

    int position()    const { return m_position;    }
    int duration()    const { return m_duration;    }
    int playlistPos() const { return m_playlistPos; }

    Q_INVOKABLE void loadAndPlay(const QString &url, float startSeconds,
                                  int audioTrack, int subTrack,
                                  const QStringList &subFiles = {},
                                  bool loop = false,
                                  int playlistStart = -1,
                                  float transcodeOffsetSec = 0.0f,
                                  const QString &plexToken = {},
                                  bool muteAudio = false,
                                  const QString &oscMode = {},
                                  bool shuffle = false);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekTo(int positionMs);
    Q_INVOKABLE void sendKey(const QString &key);

signals:
    void positionChanged(int ms);
    void durationChanged(int ms);
    void playlistPosChanged(int pos);
    // Emitted when mpv exits because the user quit/stopped playback before the end.
    void playbackFinished(int finalPositionMs, int finalDurationMs);
    // Emitted when mpv exits because the file played to its natural end (mpv's
    // end-file event reported reason "eof"). Used to trigger autoplay-next.
    void playbackFinishedNaturally(int finalPositionMs, int finalDurationMs);
    // Emitted when mpv exits with an error (code 2 — file could not be played).
    // Player.qml uses this to retry with transcoding.
    void playbackFailed();

private slots:
    void onProcessFinished();
    void tryConnectIpc();
    void onIpcReadyRead();

private:
    void sendCommand(const QJsonArray &args);
    void doHeadlessRestore(int pos, int dur, bool naturalEof);
    bool detectHeadlessMode() const;
    int  getActiveVt() const;
    int  findFreeVt() const;
    int  findQtDrmFd() const;
    void switchToVt(int vt);
#ifdef Q_OS_LINUX
    void saveDrmCrtcState(int fd);
    void restoreDrmCrtcState(int fd);
#endif

    QProcess     *m_process        = nullptr;
    QLocalSocket *m_ipc            = nullptr;
    QTimer       *m_connectTimer   = nullptr;
    QTimer       *m_watchdogTimer  = nullptr;
    qint64        m_lastIpcEventMs = 0;
    QString       m_appRoot;
    QString       m_socketPath;
    QString       m_inputConfPath;
    QString       m_logFilePath;
    QString       m_lastEndFileReason;  // mpv end-file "reason" for the current session
    int           m_position     = 0;
    int           m_duration     = 0;
    int           m_playlistPos  = -1;
    bool          m_headlessMode = false;
    int           m_previousVt   = -1;
    int           m_qtDrmFd      = -1;
#ifdef Q_OS_LINUX
    DrmSavedState m_savedDrm     = {};
#endif
};
