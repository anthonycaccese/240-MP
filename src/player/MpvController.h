#pragma once
#include <QObject>
#include <QProcess>
#include <QLocalSocket>
#include <QTimer>
#include <QJsonArray>
#include <QStringList>

class AppCore;

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
    explicit MpvController(const QString &appRoot, AppCore *appCore = nullptr,
                           QObject *parent = nullptr);
    ~MpvController() override;

    int position()    const { return m_position;    }
    int duration()    const { return m_duration;    }
    int playlistPos() const { return m_playlistPos; }

    Q_INVOKABLE void loadAndPlay(const QString &url, float startSeconds,
                                  int audioTrack, int subTrack,
                                  const QStringList &subFiles = {},
                                  const QStringList &subLangs = {},
                                  bool loop = false,
                                  int playlistStart = -1,
                                  float transcodeOffsetSec = 0.0f,
                                  const QString &plexToken = {},
                                  bool muteAudio = false,
                                  const QString &oscMode = {},
                                  bool shuffle = false,
                                  const QStringList &subTitles = {});
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekTo(int positionMs);
    Q_INVOKABLE void sendKey(const QString &key);

    // True only on devices whose smooth-playback decode path can't crop/zoom (the
    // Pi 3 DRM-overlay path). Settings uses this to show the "Smooth Playback"
    // toggle only where the smoothness-vs-crop trade-off actually exists.
    Q_INVOKABLE bool hasSmoothPlaybackTradeoff() const;

signals:
    void positionChanged(int ms);
    void durationChanged(int ms);
    void playlistPosChanged(int pos);
    // Emitted exactly once when mpv exits, with the reason it ended:
    //   "eof"     — file played to its natural end. (What a module does with this
    //               is its own concern.  as an example: Plex may autoplay the next episode)
    //   "stopped" — user quit/stopped before the end (also the safe default for a
    //               crash/kill with no end-file event).
    //   "failed"  — mpv exited with an error (code 2 — file could not be played;
    //               Up to the module as to when/how to use; for example Plex retries when transcoding).
    // A single signal (rather than one per reason) is deliberate: a Player view
    // connects one handler and branches on `reason`, so it can never silently drop
    // a case the way an unhandled per-reason signal would.
    void playbackEnded(int finalPositionMs, int finalDurationMs, const QString &reason);

private slots:
    void onProcessFinished();
    void tryConnectIpc();
    void onIpcReadyRead();

private:
    // Hardware video-decode profile, detected once from /proc/device-tree/model.
    enum class VideoProfile { Pi3, Pi4, PiFullKms, Generic };

    void sendCommand(const QJsonArray &args);
    void doHeadlessRestore(int pos, int dur, const QString &reason);
    bool detectHeadlessMode() const;
    VideoProfile detectVideoProfile() const;
    // Appends the profile-specific --vo/--gpu-context/--hwdec flags (honouring the
    // app-level "mpv_video_args" override) to a forming mpv argument list.
    void appendVideoArgs(QStringList &args) const;
    // App-level "smooth_playback" setting (default ON). On the Pi 3 this selects the
    // smooth zero-copy overlay path; turning it OFF restores the crop-capable scaler path.
    bool smoothPlaybackEnabled() const;
    // App-level "auto_crop" setting (default OFF). When ON, playback starts with
    // panscan=1 so video fills a CRT/4:3 screen by default (still toggleable live).
    bool autoCropEnabled() const;
    int  getActiveVt() const;
    int  findFreeVt() const;
    int  findQtDrmFd() const;
    void switchToVt(int vt);
#ifdef Q_OS_LINUX
    void saveDrmCrtcState(int fd);
    void restoreDrmCrtcState(int fd);
#endif

    AppCore      *m_appCore        = nullptr;
    VideoProfile  m_videoProfile  = VideoProfile::Generic;
    QProcess     *m_process        = nullptr;
    QLocalSocket *m_ipc            = nullptr;
    QTimer       *m_connectTimer   = nullptr;
    QTimer       *m_watchdogTimer  = nullptr;
    qint64        m_lastIpcEventMs = 0;
    QString       m_appRoot;
    QString       m_socketPath;
    QString       m_inputConfPath;
    QString       m_logFilePath;
    QString       m_subInfoPath;       // JSON map: external sub URL -> friendly name (for the OSC)
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
