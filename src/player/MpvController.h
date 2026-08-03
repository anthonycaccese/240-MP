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
    explicit MpvController(const QString &appRoot, const QString &dataRoot,
                           AppCore *appCore = nullptr, QObject *parent = nullptr);
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
                                  const QStringList &subTitles = {},
                                  float imageDurationSec = 0.0f,
                                  bool imageContent = false,
                                  const QStringList &extraArgs = {},
                                  const QString &jellyfinToken = {});
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekTo(int positionMs);
    Q_INVOKABLE void sendKey(const QString &key);
    Q_INVOKABLE void showOsdSkipPrompt();
    Q_INVOKABLE void clearOsdPrompt();

    // ---- TV session mode ---------------------------------------------------
    //
    // The default model above is one mpv process per playback: loadAndPlay starts
    // mpv, mpv exiting *is* the end of playback, and the module returns to a menu.
    // A channel-surfing TV needs the opposite — mpv must stay alive across every
    // episode and channel change, because relaunching it would drop the screen to
    // a console and repeat the DRM/VT hand-off on every channel press.
    //
    // Session mode launches mpv ONCE (idle, with a window forced up) and feeds it
    // files over the existing IPC socket. Nothing here links libmpv; every call
    // below is a JSON command on the same channel loadAndPlay already uses.
    //
    // Lifecycle: startSession() -> sessionLoad/sessionLoop/... -> endSession().
    // While a session is active, file ends arrive as fileEnded() and mpv exiting
    // still emits playbackEnded() exactly once (when the session itself ends).
    Q_INVOKABLE void startSession(int initialVolume = 100,
                                  const QStringList &extraArgs = {});
    Q_INVOKABLE void endSession();
    Q_INVOKABLE bool sessionActive() const { return m_sessionMode; }

    // Replace what's playing with `url`, beginning `startSeconds` in.
    Q_INVOKABLE void sessionLoad(const QString &url, double startSeconds = 0.0);
    // Play `url` on an endless loop (colour bars / "no signal"). A looping clip
    // never advances the channel — its end is deliberately ignored.
    Q_INVOKABLE void sessionLoop(const QString &url);
    // Begin decoding `url` in the background while the CURRENT file keeps playing.
    // Call sessionCommitSwitch() to cut over. This is what lets a channel change
    // happen without a frozen frame.
    Q_INVOKABLE void sessionPreload(const QString &url, double startSeconds = 0.0);
    Q_INVOKABLE void sessionCommitSwitch();
    // Show `fillerUrl` (static/glitch) cut to `fillerSeconds`, then roll straight
    // into `targetUrl`. Relies on --prefetch-playlist so the episode is ready the
    // instant the filler ends.
    Q_INVOKABLE void sessionTransition(const QString &fillerUrl, const QString &targetUrl,
                                       double startSeconds = 0.0,
                                       double fillerSeconds = 0.4);
    // Stop playback but keep the mpv session alive (standby). Unlike stop(),
    // which quits mpv entirely.
    Q_INVOKABLE void sessionStop();

    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void setMute(bool muted);
    // Draw/replace an ASS overlay in slot `overlayId` on a virtual canvas of
    // resX x resY, which mpv scales to the screen. Used for the channel banner
    // and any other on-screen readout the TV draws itself.
    Q_INVOKABLE void setOverlay(int overlayId, const QString &ass, int resX, int resY);
    Q_INVOKABLE void clearOverlay(int overlayId);

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

    void skipRequested();
    // The OSC's SUBTITLE button when the sub is burned into the stream and mpv
    // has nothing to cycle (see `sub-cycle` in scripts/mpv-osc.lua). The module
    // owns the change — typically stop, re-request the stream, relaunch.
    void subtitleCycleRequested();

    // Session mode only: the current file reached its natural end while mpv stayed
    // alive (keep-open holds it on the last frame). This is the cue to roll the
    // next episode. Deliberately separate from playbackEnded, which still means
    // "the mpv process exited" — in a session that happens once, at the very end.
    // Never emitted for a looping filler clip or during a bridge preload.
    void fileEnded();

private slots:
    void onProcessFinished();
    void tryConnectIpc();
    void onIpcReadyRead();

private:
    // Hardware video-decode profile, detected once from /proc/device-tree/model.
    enum class VideoProfile { Pi3, Pi4, PiFullKms, Generic };

    void sendCommand(const QJsonArray &args);
    // Tears down any running mpv, resets per-playback state, and resolves the mpv
    // binary. Shared by loadAndPlay and startSession. Returns an empty string when
    // mpv could not be found — callers decide how to report that.
    QString prepareLaunch();
    // Creates the QProcess and starts mpv with `args`, performing the headless
    // DRM/VT hand-off (or the desktop fullscreen path). Shared by both launch
    // modes so the framebuffer handling lives in exactly one place.
    void launchMpv(const QString &bin, QStringList &args);
    // Issues a `loadfile` IPC command with an optional per-file start offset.
    // NOTE: mpv >= 0.38 takes `loadfile <url> <flags> <index> <options>`; the index
    // slot was added in that release. RPi OS Trixie ships 0.40 and Homebrew tracks
    // current, so the 4-argument form is used. This is the ONLY place that arity is
    // stated — if this ever has to run against mpv < 0.38, drop the index here.
    void sendLoadfile(const QString &url, const QString &flags, double startSeconds);
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
    // True when the active decode path can't crop (Pi 3 overlay path with smooth
    // playback ON): --panscan blanks the video there. Gates auto-crop and tells
    // the OSC scripts to hide their CROP button.
    bool cropUnavailable() const;
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
    bool          m_paused         = false;  // mirrors mpv's pause property (watchdog exemption)
    QString       m_appRoot;
    QString       m_dataRoot;
    QString       m_socketPath;
    QString       m_inputConfPath;
    QString       m_logFilePath;
    QString       m_subInfoPath;       // JSON map: external sub URL -> friendly name (for the OSC)
    QString       m_lastEndFileReason;  // mpv end-file "reason" for the current session
    // Set when this session passed --start; cleared once mpv has applied it. See
    // onIpcReadyRead's playback-restart handling for why the option can't just stay set.
    bool          m_pendingStartClear = false;
    // True while a long-lived TV session owns mpv (see startSession).
    bool          m_sessionMode  = false;
    // Session mode: suppresses fileEnded while what's on screen is not real
    // content whose end should advance the channel — a looping filler clip, a
    // bridge preload, or a stopped/standby screen. Mirrors the same guard in
    // NostalgiaBox's player, which exists because replacing a file also produces
    // end-of-file signals for the outgoing one.
    bool          m_suppressEof  = true;
    int           m_position     = 0;
    int           m_duration     = 0;
    int           m_playlistPos  = -1;
    bool          m_headlessMode = false;
    int           m_previousVt   = -1;
    bool          m_hasMpvOscScript     = false;
    bool          m_hasAmbientOscScript = false;
    bool          m_hasMediaKeysScript  = false;
    int           m_qtDrmFd      = -1;
#ifdef Q_OS_LINUX
    DrmSavedState m_savedDrm     = {};
#endif
};
