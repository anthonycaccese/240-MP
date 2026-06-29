#include "MpvController.h"
#include "../AppCore.h"
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDateTime>
#include <QRegularExpression>
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

MpvController::MpvController(const QString &appRoot, AppCore *appCore, QObject *parent)
    : QObject(parent)
    , m_appCore(appCore)
    , m_appRoot(appRoot)
    , m_socketPath(QDir::tempPath() + "/240mp-mpv.sock")
    , m_inputConfPath(QDir::tempPath() + "/240mp-input.conf")
    , m_logFilePath(QDir::tempPath() + "/240mp-mpv.log")
    , m_subInfoPath(QDir::tempPath() + "/240mp-mpv-subinfo.json")
{
    m_videoProfile = detectVideoProfile();
    qInfo("[MpvController] video profile: %s",
          m_videoProfile == VideoProfile::Pi4       ? "Pi 4 — drm + v4l2m2m-copy"
        : m_videoProfile == VideoProfile::Pi3       ? "Pi 3 — gpu/drm + v4l2m2m (zero-copy)"
        : m_videoProfile == VideoProfile::PiFullKms ? "Pi 5 (Full KMS) — drm + auto-safe"
                                                    : "generic");

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
                                 const QStringList &subFiles,
                                 const QStringList &subLangs, bool loop,
                                 int playlistStart, float transcodeOffsetSec,
                                 const QString &plexToken, bool muteAudio,
                                 const QString &oscMode, bool shuffle,
                                 const QStringList &subTitles) {
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
        QTimer::singleShot(0, this, [this]() {
            emit playbackEnded(0, 0, QStringLiteral("stopped"));
        });
        return;
    }

    const QString oscScriptName = (oscMode == "ambient") ? "ambient-osc.lua" : "mpv-osc.lua";
    const QString oscScript = m_appRoot + "/scripts/" + oscScriptName;
    const bool hasOscScript = QFile::exists(oscScript);

    // Stamp the log file so each session is identifiable when tailing over SSH.
    {
        QFile lf(m_logFilePath);
        if (lf.open(QFile::Append | QFile::Text)) {
            QString safeUrl = url;
            safeUrl.replace(QRegularExpression("Api[_-]?Key=[^&\\s]+", QRegularExpression::CaseInsensitiveOption), "ApiKey=REDACTED");
            safeUrl.replace(QRegularExpression("X-Plex-Token:[^\\s]+"), "X-Plex-Token=REDACTED");
            safeUrl.replace(QRegularExpression("Token=\"[^\"]+\""), "Token=\"REDACTED\"");
            lf.write(QString("\n=== 240-MP session start %1 ===\n    url: %2\n\n")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                         .arg(safeUrl)
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

    // Media-key handling + themed volume bar — loaded for every mode so HID
    // media keys work anytime mpv is playing, not just inside a given module.
    const QString mediaKeysScript = m_appRoot + "/scripts/media-keys.lua";
    if (QFile::exists(mediaKeysScript))
        args << QString("--script=%1").arg(mediaKeysScript);

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
    else if (subTrack < -1)
        // subs disabled or provided via transcode
        args << QStringLiteral("--sid=no");
    else if (subTrack == -1)
        // forced subs only
        args << QStringLiteral("--subs-with-matching-audio=forced") << QStringLiteral("--subs-fallback-forced=always");
    else if (subTrack == 0) {
        // Always display subs, even if the audio and subtitle languages match
        args << QStringLiteral("--subs-with-matching-audio=yes") << QStringLiteral("--subs-fallback=yes");
        if (subFiles.isEmpty())
            // use embedded or auto-matched sub
            args << QStringLiteral("--sid=auto");
    }
    // else: external sub(s) loaded, subTrack==0 → mpv auto-selects first loaded sub
    if (!subLangs.isEmpty())
        args << QString("--slang=%1").arg(subLangs.join(QStringLiteral(",")));

    QStringList scriptOpts;
    if (transcodeOffsetSec > 0.5f)
        scriptOpts << QString("transcode-offset=%1").arg(double(transcodeOffsetSec), 0, 'f', 3);

    // Hand the OSC a map of external sub-file URL -> friendly track name so it can show
    // the real subtitle name. mpv otherwise titles an external sub from its URL basename,
    // which for Jellyfin sidecars is an opaque "Stream.srt?api_key=...". Purely cosmetic —
    // it does not affect which sub mpv loads or selects.
    QFile::remove(m_subInfoPath);
    if (!subTitles.isEmpty() && subTitles.size() == subFiles.size()) {
        QJsonObject info;
        for (int i = 0; i < subFiles.size(); ++i) {
            if (!subTitles[i].isEmpty())
                info.insert(subFiles[i], subTitles[i]);
        }
        QFile sf(m_subInfoPath);
        if (!info.isEmpty() && sf.open(QFile::WriteOnly | QFile::Truncate)) {
            sf.write(QJsonDocument(info).toJson(QJsonDocument::Compact));
            sf.close();
            // Path is comma- and space-free, so it is safe in the script-opts list.
            scriptOpts << QString("subinfo-file=%1").arg(m_subInfoPath);
        }
    }
    if (!scriptOpts.isEmpty())
        args << QString("--script-opts=%1").arg(scriptOpts.join(QStringLiteral(",")));

    if (loop)
        args << QStringLiteral("--loop-playlist=inf");
    if (shuffle)
        args << QStringLiteral("--shuffle");
    if (muteAudio)
        args << QStringLiteral("--no-audio");
    // yt-dlp hook intercepts HTTP media URLs and can break Plex/Jellyfin
    // playback with spurious 401/400 errors — disable unconditionally.
    args << QStringLiteral("--ytdl=no");
    if (!plexToken.isEmpty()) {
        args << QString("--http-header-fields=X-Plex-Token:%1").arg(plexToken);
    }

    // plex.direct certs are Let's Encrypt-signed but ffmpeg's bundled CA bundle
    // may not trust the full chain (same reason Qt needs ignoreSslErrors for these
    // hosts). Disable TLS verification only for plex.direct playback URLs.
    if (QUrl(url).host().endsWith(QStringLiteral(".plex.direct")))
        args << QStringLiteral("--tls-verify=no");

    // Auto Crop: start with panscan=1 unless the current decode path can't crop.
    // The Pi3 overlay (smooth) path blanks video under panscan, so suppress there —
    // matching the 1080p Playback trade-off. The OSC CROP button still toggles live.
    if (autoCropEnabled()) {
        const bool cropSafe = !(m_videoProfile == VideoProfile::Pi3 && smoothPlaybackEnabled());
        if (cropSafe)
            args << QStringLiteral("--panscan=1");
    }

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
                 << "--video-sync=audio";
            appendVideoArgs(args);
            args << "--no-input-terminal";
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
             << "--video-sync=audio";
        appendVideoArgs(args);
        args << "--no-input-terminal";
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
        appendVideoArgs(args);
#ifdef Q_OS_MACOS
        // mpv runs as a separate process and can't see the app-bundle font via
        // FontLoader. This will load the bundled VCR OSD Mono directly into the OSD libass
        // instance (used by the OSC scripts) so users don't need a system install.
        // macOS libass uses the coretext provider, so the Linux FONTCONFIG_FILE
        // approach doesn't apply here; --osd-fonts-dir is provider-independent.
        args << QString("--osd-fonts-dir=%1").arg(m_appRoot + "/assets/fonts");
#endif
        QString safeCmd = args.join(" ");
        // Redact all token forms in debug output
        safeCmd.replace(QRegularExpression("Api[_-]?Key=[^&\\s]+", QRegularExpression::CaseInsensitiveOption), "ApiKey=REDACTED");
        safeCmd.replace(QRegularExpression("X-Plex-Token:[^\\s]+"), "X-Plex-Token=REDACTED");
        safeCmd.replace(QRegularExpression("Token=\"[^\"]+\""), "Token=\"REDACTED\"");
        qDebug("[MpvController] desktop launch: mpv %s", qPrintable(safeCmd));
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

    // Classify why mpv exited, once, so both the headless and desktop paths emit
    // the same playbackEnded reason:
    //   exit code 2          -> "failed"  (file could not be played; up to the module as to what to do. As an example: Plex attemps a retry in this case)
    //   end-file reason "eof"-> "eof"     (natural end; up to the module as to what to do. As an example: Plex autoplays next)
    //   anything else        -> "stopped" (user quit/stop, crash, or kill; a safe default)
    QString reason;
    if (exitCode == 2)                    reason = QStringLiteral("failed");
    else if (m_lastEndFileReason == "eof") reason = QStringLiteral("eof");
    else                                   reason = QStringLiteral("stopped");

    if (m_headlessMode) {
        // Defer DRM restore and VT switch by 200 ms. mpv's last KMS atomic
        // commit may still be pending in the vc4 driver at the moment the
        // process exits. If EGLFS tries to commit before that pending flip
        // is signaled, it gets EBUSY repeatedly, drops its DRM pipeline, and
        // the kernel falls back to showing the text console on Qt's VT.
        // 200 ms is more than three VSync periods at 60 Hz — enough to clear
        // any in-flight commit without a perceptible delay for the user.
        QTimer::singleShot(200, this, [this, pos, dur, reason]() {
            doHeadlessRestore(pos, dur, reason);
        });
    } else {
        emit playbackEnded(pos, dur, reason);
    }
}

void MpvController::doHeadlessRestore(int pos, int dur, const QString &reason) {
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
    emit playbackEnded(pos, dur, reason);
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

MpvController::VideoProfile MpvController::detectVideoProfile() const {
#ifdef Q_OS_LINUX
    // The Raspberry Pi model string (e.g. "Raspberry Pi 4 Model B Rev 1.5") is
    // exposed NUL-terminated at /proc/device-tree/model. Pi 3 and Pi 4 both boot
    // Fake KMS but have different CPU budgets, so they get different decode paths;
    // Pi 5 boots Full KMS and direct-renders with --vo=drm.
    QFile f("/proc/device-tree/model");
    if (f.open(QIODevice::ReadOnly)) {
        const QString model =
            QString::fromLatin1(f.readAll()).remove(QChar('\0')).trimmed();
        if (model.startsWith("Raspberry Pi 5"))
            return VideoProfile::PiFullKms;
        if (model.startsWith("Raspberry Pi 4"))
            return VideoProfile::Pi4;
        if (model.startsWith("Raspberry Pi 3"))
            return VideoProfile::Pi3;
    }
#endif
    return VideoProfile::Generic;
}

void MpvController::appendVideoArgs(QStringList &args) const {
    // App-level "mpv_video_args" override replaces the auto-detected vo/hwdec
    // flags verbatim. Read here (not cached) so edits to config.json take effect
    // on the next playback without a rebuild — handy for per-device HW tuning.
    if (m_appCore) {
        const QString override =
            m_appCore->get_setting(QString(), "mpv_video_args").toString().trimmed();
        if (!override.isEmpty()) {
            args << override.split(' ', Qt::SkipEmptyParts);
            return;
        }
    }

    if (m_headlessMode) {
        if (m_videoProfile == VideoProfile::Pi4) {
            // Pi 4B: native --vo=drm draws on the primary plane with precise KMS
            // page-flip timing (smooth cadence). v4l2m2m-copy keeps decode on the
            // hardware block but copies frames back to RAM so they land on that
            // primary plane instead of the drmprime *overlay* plane — the overlay
            // path (vo=gpu zero-copy) decodes just as cheaply but its presentation
            // jitters into visible 24p judder. The copy + zimg downscale costs more
            // CPU (~50-70% across 4 cores) but the Pi4 has the headroom, and crop
            // (--panscan) works because frames go through the normal scaler.
            args << "--vo=drm" << "--hwdec=v4l2m2m-copy";
        } else if (m_videoProfile == VideoProfile::Pi3) {
            // Pi 3B/3B+: too weak for the copy + software-scale path above (it pegs
            // all four cores and gets choppy). Zero-copy v4l2m2m hands decoded frames
            // straight to a DRM overlay plane for the lowest possible CPU (~15%) with
            // smooth playback. The one trade-off: the overlay plane can't zoom/crop,
            // so mpv's --panscan (the OSC crop button) blanks the video on this path.
            // The "smooth_playback" setting (default ON) lets the user opt out: when
            // OFF we fall back to the crop-capable scaler path (--vo=drm) at the cost
            // of higher CPU and less smooth cadence.
            if (smoothPlaybackEnabled())
                args << "--vo=gpu" << "--gpu-context=drm" << "--hwdec=v4l2m2m";
            else
                args << "--vo=drm" << "--hwdec=v4l2m2m-copy";
        } else {
            // Pi 5 (Full KMS) and the safe fallback for unknown headless Linux.
            // --monitorpixelaspect=0.82 corrects non-square pixel aspect on
            // composite CRTs (704×432 on 4:3). Pure math, zero rendering cost.
            args << "--vo=drm" << "--hwdec=auto-safe"
                 << "--monitorpixelaspect=0.82";
        }
    } else {
#ifdef Q_OS_MACOS
        // Apple Silicon: enable VideoToolbox HW decode (mpv's default is none).
        args << "--hwdec=videotoolbox";
#endif
        // Other desktop (X11/Wayland dev): leave mpv's defaults untouched.
    }
}

bool MpvController::smoothPlaybackEnabled() const {
    // Default ON: only an explicit "Off" opts out. Stored by Settings as a string
    // ("On"/"Off") via the list_single row, so compare on the string form.
    if (!m_appCore)
        return true;
    const QVariant v = m_appCore->get_setting(QString(), "smooth_playback");
    if (!v.isValid() || v.toString().isEmpty())
        return true;
    return v.toString().compare(QStringLiteral("Off"), Qt::CaseInsensitive) != 0;
}

bool MpvController::autoCropEnabled() const {
    // Default OFF: only an explicit "On" opts in. Stored by Settings as a string
    // ("On"/"Off") via the list_single row, so compare on the string form.
    if (!m_appCore)
        return false;
    const QVariant v = m_appCore->get_setting(QString(), "auto_crop");
    return v.toString().compare(QStringLiteral("On"), Qt::CaseInsensitive) == 0;
}

bool MpvController::hasSmoothPlaybackTradeoff() const {
    // Only the Pi 3 overlay path sacrifices crop/zoom for smoothness. Every other
    // profile (Pi 4 copy path, Pi 5/generic --vo=drm, desktop) can already crop, so
    // the toggle would be a no-op there and is hidden.
    return m_videoProfile == VideoProfile::Pi3;
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
