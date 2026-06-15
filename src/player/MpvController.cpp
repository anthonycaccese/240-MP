#include "MpvController.h"
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDateTime>
#include <QDebug>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/vt.h>
#include <string>
// DRM master ioctls (also provided by xf86drm.h, but define as fallback).
#ifndef DRM_IOCTL_SET_MASTER
#define DRM_IOCTL_SET_MASTER   _IO('d', 0x1e)
#define DRM_IOCTL_DROP_MASTER  _IO('d', 0x1f)
#endif

// Write a fontconfig override so the mpv subprocess's libass can find custom
// fonts without needing them installed system-wide.
static QString writeFontconfigOverride(const QString &fontsDir) {
    const QString path = QDir::tempPath() + "/240mp-fonts.conf";
    QFile f(path);
    if (!f.open(QFile::WriteOnly | QFile::Text))
        return {};
    f.write(QString(
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
        "<fontconfig>\n"
        "  <dir>%1</dir>\n"
        "  <include ignore_missing=\"yes\">/etc/fonts/fonts.conf</include>\n"
        "</fontconfig>\n"
    ).arg(fontsDir).toUtf8());
    return path;
}
#endif

MpvController::MpvController(const QString &appRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_socketPath(QDir::tempPath() + "/240mp-mpv.sock")
    , m_inputConfPath(QDir::tempPath() + "/240mp-input.conf")
    , m_logFilePath(QDir::tempPath() + "/240mp-mpv.log")
{
    QFile f(m_inputConfPath);
    if (f.open(QFile::WriteOnly | QFile::Text)) {
        f.write("ESC quit\n");
        f.write("BS quit\n");
        f.write("ENTER cycle pause\n");
        f.close();
    }

    m_ipc = new QLocalSocket(this);
    connect(m_ipc, &QLocalSocket::connected, this, [this] {
        m_connectTimer->stop();
        m_lastIpcEventMs = QDateTime::currentMSecsSinceEpoch();
        m_watchdogTimer->start();
        sendCommand({"observe_property", 1, "time-pos"});
        sendCommand({"observe_property", 2, "duration"});
        sendCommand({"observe_property", 3, "playlist-pos"});
    });
    connect(m_ipc, &QLocalSocket::readyRead, this, &MpvController::onIpcReadyRead);

    m_connectTimer = new QTimer(this);
    m_connectTimer->setInterval(100);
    connect(m_connectTimer, &QTimer::timeout, this, &MpvController::tryConnectIpc);

    // Watchdog: fires every 10 s; logs a warning if no IPC time-pos event has
    // arrived for 30 s while connected — strong indicator of a playback freeze.
    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setInterval(10000);
    connect(m_watchdogTimer, &QTimer::timeout, this, [this] {
        if (m_ipc->state() != QLocalSocket::ConnectedState) return;
        qint64 silenceMs = QDateTime::currentMSecsSinceEpoch() - m_lastIpcEventMs;
        if (silenceMs > 30000) {
            qWarning("[MpvController] WATCHDOG: no IPC time-pos event for %lld s — possible freeze",
                     silenceMs / 1000);
        }
    });
}

MpvController::~MpvController() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        m_process->waitForFinished(2000);
    }
}

void MpvController::loadAndPlay(const QString &url, float startSeconds,
                                 int audioTrack, int subTrack,
                                 const QStringList &subFiles, bool loop,
                                 int playlistStart, float transcodeOffsetSec,
                                 const QString &plexToken, bool muteAudio,
                                 const QString &oscMode, bool shuffle) {
    if (m_process) {
        m_process->disconnect();
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            m_process->waitForFinished(1000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_watchdogTimer->stop();
    m_ipc->abort();
    QFile::remove(m_socketPath);
    m_position    = 0;
    m_duration    = 0;
    m_playlistPos = -1;
    m_lastEndFileReason.clear();

#ifdef Q_OS_MACOS
    // .app bundles launched via double-click get a minimal PATH that excludes
    // Homebrew. Prepend known install locations so findExecutable works.
    {
        const QStringList extraPaths = { "/opt/homebrew/bin", "/usr/local/bin" };
        const QStringList currentPath = qEnvironmentVariable("PATH").split(":");
        for (const QString &p : extraPaths) {
            if (!currentPath.contains(p))
                qputenv("PATH", (p + ":" + qEnvironmentVariable("PATH")).toUtf8());
        }
    }
#endif
    const QString bin = QStandardPaths::findExecutable("mpv");
    if (bin.isEmpty()) {
        qWarning("[MpvController] mpv not found in PATH");
        QTimer::singleShot(0, this, [this]() { emit playbackFinished(0, 0); });
        return;
    }

    const QString oscScriptName = (oscMode == "ambient") ? "ambient-osc.lua" : "mpv-osc.lua";
    const QString oscScript = m_appRoot + "/scripts/" + oscScriptName;
    const bool hasOscScript = QFile::exists(oscScript);

    // Stamp the log file so each session is identifiable when tailing over SSH.
    {
        QFile lf(m_logFilePath);
        if (lf.open(QFile::Append | QFile::Text)) {
            lf.write(QString("\n=== 240-MP session start %1 ===\n    url: %2\n\n")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                         .arg(url)
                         .toUtf8());
        }
    }

    QStringList args;
    args << url
         << QString("--input-ipc-server=%1").arg(m_socketPath)
         << QString("--log-file=%1").arg(m_logFilePath)
         << (hasOscScript ? "--osc=no" : "--osc=yes")
         << "--osd-level=0";

    if (hasOscScript)
        args << QString("--script=%1").arg(oscScript);

    if (playlistStart >= 0)
        args << QString("--playlist-start=%1").arg(playlistStart);
    if (startSeconds > 0.5f)
        args << QString("--start=%1").arg(double(startSeconds), 0, 'f', 3);
    if (audioTrack > 0)
        args << QString("--aid=%1").arg(audioTrack);
    for (const QString &sf : subFiles)
        args << QString("--sub-file=%1").arg(sf);
    if (subTrack > 0)
        args << QString("--sid=%1").arg(subTrack);
    else if (subFiles.isEmpty() || subTrack < 0)
        args << QStringLiteral("--sid=no");
    // else: external sub(s) loaded, subTrack==0 → mpv auto-selects first loaded sub

    if (transcodeOffsetSec > 0.5f)
        args << QString("--script-opts=transcode-offset=%1").arg(double(transcodeOffsetSec), 0, 'f', 3);

    if (loop)
        args << QStringLiteral("--loop-playlist=inf");
    if (shuffle)
        args << QStringLiteral("--shuffle");
    if (muteAudio)
        args << QStringLiteral("--no-audio");
    if (!plexToken.isEmpty()) {
        args << QString("--http-header-fields=X-Plex-Token:%1").arg(plexToken);
        // Plex URLs are direct file paths — yt-dlp hook is not needed and causes
        // spurious 401 errors when mpv encounters a non-2xx response from PMS.
        args << QStringLiteral("--ytdl=no");
    }

    // plex.direct certs are Let's Encrypt-signed but ffmpeg's bundled CA bundle
    // may not trust the full chain (same reason Qt needs ignoreSslErrors for these
    // hosts). Disable TLS verification only for plex.direct playback URLs.
    if (QUrl(url).host().endsWith(QStringLiteral(".plex.direct")))
        args << QStringLiteral("--tls-verify=no");

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MpvController::onProcessFinished);
    connect(m_process, &QProcess::readyRead, this, [this]() {
        const QByteArray out = m_process->readAll();
        if (!out.isEmpty())
            qWarning("[mpv] %s", out.trimmed().constData());
    });

    m_headlessMode = detectHeadlessMode();
    if (m_headlessMode) {
        {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("APP_ROOT", m_appRoot);
#ifdef Q_OS_LINUX
            const QString fcConf = writeFontconfigOverride(m_appRoot + "/assets/fonts");
            if (!fcConf.isEmpty())
                env.insert("FONTCONFIG_FILE", fcConf);
#endif
            m_process->setProcessEnvironment(env);
        }

        if (m_previousVt > 0) {
            // loadAndPlay called while already in headless mode (e.g. rapid
            // double call from Plex Player). m_previousVt already holds Qt's
            // real VT — do NOT overwrite it with the current free VT. The old
            // mpv was terminated above; just launch the replacement directly.
            args << QString("--input-conf=%1").arg(m_inputConfPath)
                 << "--video-sync=audio"
                 << "--vo=drm" << "--hwdec=auto-safe" << "--no-input-terminal";
            m_process->start(bin, args);
            m_connectTimer->start();
            return;
        }

        // First entry into headless mode.
        //
        // On kernels 5.8+, drmSetMaster() returns EACCES for non-root if any
        // other process holds DRM master — even after a VT switch, because Qt
        // EGLFS runs in VT_AUTO mode and never calls drmDropMaster() itself.
        //
        // Fix: switch to a free VT first (suspends Qt's render thread), then
        // drop Qt's DRM master so mpv can acquire it cleanly.

        m_previousVt = getActiveVt();
        m_qtDrmFd    = -1;

#ifdef Q_OS_LINUX
        // Switch VT first — suspends Qt's render thread via the kernel's VT
        // switch signal before DRM master is dropped, eliminating the race
        // that causes "Failed to commit atomic request" log noise.
        switchToVt(findFreeVt());

        m_qtDrmFd = findQtDrmFd();
        if (m_qtDrmFd < 0) {
            qWarning("[MpvController] Could not find Qt DRM fd");
        } else {
            qDebug("[MpvController] DRM master dropped (fd %d)", m_qtDrmFd);
            // Save the current CRTC state so we can restore it exactly after
            // mpv exits. mpv's atomic cleanup disables the CRTC (CRTC_ACTIVE=0);
            // without this restore, Qt EGLFS gets EINVAL on its next page flip.
            saveDrmCrtcState(m_qtDrmFd);
        }
#endif

        args << QString("--input-conf=%1").arg(m_inputConfPath)
             << "--video-sync=audio"
             << "--vo=drm" << "--hwdec=auto-safe" << "--no-input-terminal";
        m_process->start(bin, args);
        m_connectTimer->start();
    } else {
        // Desktop: X11 or Wayland compositor present.
        // Remove WAYLAND_DISPLAY so mpv uses X11/Xwayland — the Wayland VO
        // stalls waiting for wl_surface frame-done callbacks from labwc.
        // --no-native-fs avoids macOS Space-transition delays that can
        // prevent early OSD renders from appearing.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("APP_ROOT", m_appRoot);
        env.remove("WAYLAND_DISPLAY");
#ifdef Q_OS_LINUX
        const QString fcConf = writeFontconfigOverride(m_appRoot + "/assets/fonts");
        if (!fcConf.isEmpty())
            env.insert("FONTCONFIG_FILE", fcConf);
#endif
        m_process->setProcessEnvironment(env);
        args << QString("--input-conf=%1").arg(m_inputConfPath)
             << "--video-sync=audio"
             << "--fullscreen" << "--no-native-fs";
#ifdef Q_OS_MACOS
        // mpv runs as a separate process and can't see the app-bundle font via
        // FontLoader. This will load the bundled VCR OSD Mono directly into the OSD libass
        // instance (used by the OSC scripts) so users don't need a system install.
        // macOS libass uses the coretext provider, so the Linux FONTCONFIG_FILE
        // approach doesn't apply here; --osd-fonts-dir is provider-independent.
        args << QString("--osd-fonts-dir=%1").arg(m_appRoot + "/assets/fonts");
#endif
        qDebug("[MpvController] desktop launch: mpv %s", qPrintable(args.join(" ")));
        m_process->start(bin, args);
        m_connectTimer->start();
    }
}

void MpvController::stop() {
    if (m_ipc->state() == QLocalSocket::ConnectedState) {
        sendCommand({"quit"});
    } else if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
    }
}

void MpvController::seekTo(int positionMs) {
    sendCommand({"seek", positionMs / 1000.0, "absolute+exact"});
}

void MpvController::sendKey(const QString &key) {
    sendCommand({"keypress", key});
}

void MpvController::tryConnectIpc() {
    if (m_ipc->state() == QLocalSocket::ConnectedState ||
        m_ipc->state() == QLocalSocket::ConnectingState)
        return;
    m_ipc->connectToServer(m_socketPath);
}

void MpvController::onIpcReadyRead() {
    while (m_ipc->canReadLine()) {
        const QByteArray line = m_ipc->readLine().trimmed();
        const QJsonObject obj = QJsonDocument::fromJson(line).object();
        if (obj.isEmpty()) continue;
        const QString event = obj["event"].toString();
        // property-change is the hot path (fires many times per second), so test
        // it first; only other events pay for the end-file check below.
        if (event != "property-change") {
            // mpv reports why playback ended: "eof" (played to the end),
            // "quit"/"stop" (user exited), "error", etc. Remember the last one
            // so onProcessFinished can distinguish a natural finish from a quit.
            if (event == "end-file")
                m_lastEndFileReason = obj["reason"].toString();
            continue;
        }

        m_lastIpcEventMs = QDateTime::currentMSecsSinceEpoch();

        const QString     name = obj["name"].toString();
        const QJsonValue  data = obj["data"];
        if (data.isNull() || data.isUndefined()) continue; // property unavailable during shutdown
        const double val = data.toDouble();
        if (name == "time-pos") {
            m_position = int(val * 1000.0);
            emit positionChanged(m_position);
        } else if (name == "duration") {
            m_duration = int(val * 1000.0);
            emit durationChanged(m_duration);
        } else if (name == "playlist-pos") {
            m_playlistPos = int(val);
            emit playlistPosChanged(m_playlistPos);
        }
    }
}

void MpvController::onProcessFinished() {
    int exitCode = m_process ? m_process->exitCode() : -1;
    if (m_process) {
        const QByteArray remaining = m_process->readAll();
        if (!remaining.isEmpty())
            qWarning("[mpv] %s", remaining.trimmed().constData());
    }
    if (exitCode != 0)
        qWarning("[MpvController] mpv exited with code %d", exitCode);
    m_connectTimer->stop();
    m_watchdogTimer->stop();
    // Drain any buffered-but-unread IPC data before tearing the socket down.
    // readyRead and QProcess::finished are independent event-loop signals with
    // no ordering guarantee, so mpv's final "end-file" event may still be sitting
    // in the socket buffer here. Flushing it now ensures m_lastEndFileReason is
    // accurate, so a natural EOF reliably triggers autoplay-next.
    if (m_ipc->state() == QLocalSocket::ConnectedState)
        onIpcReadyRead();
    m_ipc->abort();
    QFile::remove(m_socketPath);
    const int pos = m_position;
    const int dur = m_duration;
    m_position = 0;
    m_duration = 0;

    // mpv reports reason "eof" only when the file played to its natural end.
    // Any other reason (quit/stop/error) or a missing end-file event (crash/kill)
    // is treated as a non-natural exit — the safe default that never auto-advances.
    const bool naturalEof = (m_lastEndFileReason == "eof");

    if (m_headlessMode) {
        // Defer DRM restore and VT switch by 200 ms. mpv's last KMS atomic
        // commit may still be pending in the vc4 driver at the moment the
        // process exits. If EGLFS tries to commit before that pending flip
        // is signaled, it gets EBUSY repeatedly, drops its DRM pipeline, and
        // the kernel falls back to showing the text console on Qt's VT.
        // 200 ms is more than three VSync periods at 60 Hz — enough to clear
        // any in-flight commit without a perceptible delay for the user.
        QTimer::singleShot(200, this, [this, pos, dur, naturalEof]() {
            doHeadlessRestore(pos, dur, naturalEof);
        });
    } else {
        if (exitCode == 2)
            emit playbackFailed();
        else if (naturalEof)
            emit playbackFinishedNaturally(pos, dur);
        else
            emit playbackFinished(pos, dur);
    }
}

void MpvController::doHeadlessRestore(int pos, int dur, bool naturalEof) {
#ifdef Q_OS_LINUX
    if (m_qtDrmFd >= 0) {
        if (::ioctl(m_qtDrmFd, DRM_IOCTL_SET_MASTER, 0) < 0) {
            qWarning("[MpvController] drmSetMaster failed: %s", strerror(errno));
        } else {
            qDebug("[MpvController] DRM master restored (fd %d)", m_qtDrmFd);
            // Restore CRTC to its pre-mpv state using legacy drmModeSetCrtc.
            // This re-enables the CRTC with the original mode and Qt's last
            // framebuffer, so EGLFS's first atomic page flip succeeds instead
            // of getting EINVAL from a disabled CRTC.
            restoreDrmCrtcState(m_qtDrmFd);
        }
        m_qtDrmFd = -1;
    }
#endif
    if (m_previousVt > 0) {
        qDebug("[MpvController] Switching back to VT %d", m_previousVt);
        int prevVt = m_previousVt;
        m_previousVt = -1;
        switchToVt(prevVt);
    }
    m_headlessMode = false;
    if (naturalEof)
        emit playbackFinishedNaturally(pos, dur);
    else
        emit playbackFinished(pos, dur);
}

void MpvController::sendCommand(const QJsonArray &args) {
    if (m_ipc->state() != QLocalSocket::ConnectedState) return;
    QJsonObject cmd;
    cmd["command"] = args;
    m_ipc->write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n");
}

bool MpvController::detectHeadlessMode() const {
#ifdef Q_OS_LINUX
    return qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty();
#else
    return false;
#endif
}

int MpvController::getActiveVt() const {
#ifdef Q_OS_LINUX
    QFile f("/sys/class/tty/tty0/active");
    if (!f.open(QIODevice::ReadOnly)) return -1;
    const QString name = QString::fromLatin1(f.readAll()).trimmed();
    bool ok;
    int n = name.mid(3).toInt(&ok);
    return ok ? n : -1;
#else
    return -1;
#endif
}

int MpvController::findFreeVt() const {
#ifdef Q_OS_LINUX
    int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd < 0) return 7;
    int n = -1;
    ::ioctl(fd, VT_OPENQRY, &n);
    ::close(fd);
    return (n > 0) ? n : 7;
#else
    return -1;
#endif
}

void MpvController::switchToVt(int vt) {
#ifdef Q_OS_LINUX
    int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd < 0) {
        qWarning("[MpvController] switchToVt %d: open /dev/tty0 failed: %s", vt, strerror(errno));
        return;
    }
    if (::ioctl(fd, VT_ACTIVATE, vt) < 0)
        qWarning("[MpvController] VT_ACTIVATE %d failed: %s", vt, strerror(errno));
    if (::ioctl(fd, VT_WAITACTIVE, vt) < 0)
        qWarning("[MpvController] VT_WAITACTIVE %d failed: %s", vt, strerror(errno));
    ::close(fd);
#else
    Q_UNUSED(vt)
#endif
}

int MpvController::findQtDrmFd() const {
#ifdef Q_OS_LINUX
    // Scan the process's open file descriptors for Qt's DRM primary card
    // device. DRM primary nodes have major=226, minor 0-63 (card0, card1…).
    // We try DRM_IOCTL_DROP_MASTER on each candidate — it succeeds only on
    // the fd that currently holds DRM master, which tells us it's Qt's fd.
    QDir fdDir("/proc/self/fd");
    const QStringList entries = fdDir.entryList(QDir::Files | QDir::System);
    for (const QString &entry : entries) {
        bool ok;
        int fd = entry.toInt(&ok);
        if (!ok) continue;
        struct stat st;
        if (::fstat(fd, &st) < 0) continue;
        if (!S_ISCHR(st.st_mode)) continue;
        if (major(st.st_rdev) != 226) continue;   // not a DRM device
        if (minor(st.st_rdev) >= 64) continue;    // render node, not primary card
        // Found a DRM primary fd — try to drop master; if it works, this is it.
        if (::ioctl(fd, DRM_IOCTL_DROP_MASTER, 0) == 0)
            return fd;
    }
    return -1;
#else
    return -1;
#endif
}

#ifdef Q_OS_LINUX
void MpvController::saveDrmCrtcState(int fd) {
    m_savedDrm = {};

    drmModeResPtr res = drmModeGetResources(fd);
    if (!res) {
        qWarning("[MpvController] saveDrmCrtcState: drmModeGetResources failed");
        return;
    }

    for (int i = 0; i < res->count_crtcs && !m_savedDrm.valid; ++i) {
        drmModeCrtcPtr crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (!crtc) continue;

        if (crtc->mode_valid) {
            m_savedDrm.crtcId = crtc->crtc_id;
            m_savedDrm.fbId   = crtc->buffer_id;
            m_savedDrm.x      = crtc->x;
            m_savedDrm.y      = crtc->y;
            m_savedDrm.mode   = crtc->mode;

            // Find the connector whose encoder is driving this CRTC
            for (int j = 0; j < res->count_connectors; ++j) {
                drmModeConnectorPtr conn = drmModeGetConnector(fd, res->connectors[j]);
                if (!conn) continue;
                if (conn->encoder_id) {
                    drmModeEncoderPtr enc = drmModeGetEncoder(fd, conn->encoder_id);
                    if (enc) {
                        if (enc->crtc_id == m_savedDrm.crtcId) {
                            m_savedDrm.connectorId = conn->connector_id;
                            m_savedDrm.valid = true;
                        }
                        drmModeFreeEncoder(enc);
                    }
                }
                drmModeFreeConnector(conn);
                if (m_savedDrm.valid) break;
            }
        }
        drmModeFreeCrtc(crtc);
    }
    drmModeFreeResources(res);

    if (m_savedDrm.valid)
        qDebug("[MpvController] Saved CRTC %u connector %u mode %dx%d@%d",
               m_savedDrm.crtcId, m_savedDrm.connectorId,
               m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay,
               m_savedDrm.mode.vrefresh);
    else
        qWarning("[MpvController] Could not save CRTC state");
}

void MpvController::restoreDrmCrtcState(int fd) {
    if (!m_savedDrm.valid) return;

    int ret = drmModeSetCrtc(fd,
                              m_savedDrm.crtcId,
                              m_savedDrm.fbId,
                              m_savedDrm.x, m_savedDrm.y,
                              &m_savedDrm.connectorId, 1,
                              &m_savedDrm.mode);
    if (ret < 0)
        qWarning("[MpvController] drmModeSetCrtc restore failed: %s", strerror(errno));
    else
        qDebug("[MpvController] CRTC restored (mode %dx%d@%d)",
               m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay,
               m_savedDrm.mode.vrefresh);

    m_savedDrm.valid = false;
}
#endif
