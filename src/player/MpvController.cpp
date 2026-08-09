#include "MpvController.h"
#include "../AppCore.h"
#include "../util/YtDlpLocator.h"
#include "../util/MpvLocator.h"
#include "../util/DisplayHandoff.h"
#include "../util/FontconfigOverride.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>

MpvController::MpvController(const QString &appRoot, const QString &dataRoot,
                             AppCore *appCore, DisplayHandoff *handoff,
                             QObject *parent)
    : QObject(parent)
    , m_appCore(appCore)
    , m_handoff(handoff)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
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

    m_hasMpvOscScript     = QFile::exists(m_appRoot + "/scripts/mpv-osc.lua");
    m_hasAmbientOscScript = QFile::exists(m_appRoot + "/scripts/mpv-osc-ambient.lua");
    m_hasMediaKeysScript  = QFile::exists(m_appRoot + "/scripts/mpv-media-keys.lua");

    m_ipc = new QLocalSocket(this);
    connect(m_ipc, &QLocalSocket::connected, this, [this] {
        m_connectTimer->stop();
        m_lastIpcEventMs = QDateTime::currentMSecsSinceEpoch();
        m_watchdogTimer->start();
        sendCommand({"observe_property", 1, "time-pos"});
        sendCommand({"observe_property", 2, "duration"});
        sendCommand({"observe_property", 3, "playlist-pos"});
        sendCommand({"observe_property", 4, "pause"});
    });
    connect(m_ipc, &QLocalSocket::readyRead, this, &MpvController::onIpcReadyRead);

    m_connectTimer = new QTimer(this);
    m_connectTimer->setInterval(100);
    connect(m_connectTimer, &QTimer::timeout, this, &MpvController::tryConnectIpc);

    // Watchdog: fires every 10 s; logs a warning if no IPC time-pos event has
    // arrived for 30 s while connected — strong indicator of a playback freeze.
    // Exempt while paused: time-pos is legitimately silent then (a long pause is
    // a normal state now that the screen saver runs over it), and the unpause
    // property-change event refreshes m_lastIpcEventMs so the 30 s window
    // restarts fresh on resume.
    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setInterval(10000);
    connect(m_watchdogTimer, &QTimer::timeout, this, [this] {
        if (m_ipc->state() != QLocalSocket::ConnectedState || m_paused) return;
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
    // Quitting mid-playback on a headless Pi used to leave the VT switched away
    // and DRM master dropped — a black screen or a stray text console. Restore
    // synchronously here: terminate() above has already released mpv's hold on
    // the device, and we're exiting, so there's no point deferring 200 ms.
    // A no-op if mpv wasn't holding the screen (any platform, any mode).
    if (m_handoff)
        m_handoff->releaseNow(QLatin1String(kHandoffOwner));
}

void MpvController::loadAndPlay(const QString &url, float startSeconds,
                                 int audioTrack, int subTrack,
                                 const QStringList &subFiles,
                                 const QStringList &subLangs, bool loop,
                                 int playlistStart, float transcodeOffsetSec,
                                 const QString &plexToken, bool muteAudio,
                                 const QString &oscMode, bool shuffle,
                                 const QStringList &subTitles, float imageDurationSec,
                                 bool imageContent, const QStringList &extraArgs, const QString &jellyfinToken,
                                 const QStringList &extraUrls) {
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
    m_paused      = false;
    m_lastEndFileReason.clear();
    m_pendingStartClear = false;

    // Bundled sibling first, then PATH — see util/MpvLocator.h. Shared with the
    // audio-only spawners so a bundled-mpv or user-drop-in change lands in one
    // place.
    const QString bin = mpvbin::locate();
    if (bin.isEmpty()) {
        qWarning("[MpvController] mpv not found (no bundled sibling, none on PATH)");
        QTimer::singleShot(0, this, [this]() {
            emit playbackEnded(0, 0, QStringLiteral("stopped"));
        });
        return;
    }

    const bool hasOscScript = (oscMode == "ambient") ? m_hasAmbientOscScript : m_hasMpvOscScript;
    const QString oscScript = m_appRoot + "/scripts/" + ((oscMode == "ambient") ? "mpv-osc-ambient.lua" : "mpv-osc.lua");

    // Stamp the log file so each session is identifiable when tailing over SSH.
    // Owner-only perms: mpv logs its command line (incl. auth headers) at verbose
    // level into --log-file, and it truncates rather than recreates the file — so
    // permissions set here survive the mpv session.
    {
        QFile lf(m_logFilePath);
        if (lf.open(QFile::Append | QFile::Text)) {
            lf.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            QString safeUrl = url;
            safeUrl.replace(QRegularExpression("Api[_-]?Key=[^&\\s]+", QRegularExpression::CaseInsensitiveOption), "ApiKey=REDACTED");
            safeUrl.replace(QRegularExpression("X-Plex-Token[=:][^&\\s]+"), "X-Plex-Token=REDACTED");
            safeUrl.replace(QRegularExpression("Token=\"[^\"]+\""), "Token=\"REDACTED\"");
            lf.write(QString("\n=== 240-MP session start %1 ===\n    url: %2\n\n")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                         .arg(safeUrl)
                         .toUtf8());
        }
    }

    QStringList args;
    args << url;
    // Additional playlist entries, straight after the primary url — mpv builds its
    // playlist from every non-option argument. These must be absolute paths or URLs
    // (so they can't be mistaken for flags); pass mpv options via extraArgs instead.
    args << extraUrls;
    args << QString("--input-ipc-server=%1").arg(m_socketPath)
         << QString("--log-file=%1").arg(m_logFilePath)
         << (hasOscScript ? "--osc=no" : "--osc=yes")
         << "--osd-level=0";

    if (hasOscScript)
        args << QString("--script=%1").arg(oscScript);

    // Media-key handling + themed volume bar — loaded for every mode so HID
    // media keys work anytime mpv is playing, not just inside a given module.
    if (m_hasMediaKeysScript)
        args << QString("--script=%1").arg(m_appRoot + "/scripts/mpv-media-keys.lua");

    // Screen saver Lua script — only loaded when the user has opted in via the
    // screensaver_timeout setting (a positive number of seconds; "OFF" parses
    // to 0 and disables). The timeout reaches the script via scriptOpts below.
    int screensaverTimeout = 0;
    if (m_appCore) {
        const int n = m_appCore->get_setting(QString(), "screensaver_timeout").toString().toInt();
        const QString ssScript = m_appRoot + "/scripts/mpv-screensaver.lua";
        if (n > 0 && QFile::exists(ssScript)) {
            screensaverTimeout = n;
            args << QString("--script=%1").arg(ssScript);
        }
    }

    // Still-image playback only: mpv's KMS output (--vo=drm) won't repaint the
    // primary plane between two consecutive same-size/format stills, so a photo
    // playlist freezes on the first frame while the clock advances. This script
    // nudges a render-affecting property on each playlist advance to force a
    // page-flip. Loaded only for image content, so video playback is untouched.
    if (imageContent) {
        const QString slideshowScript = m_appRoot + "/scripts/mpv-slideshow-redraw.lua";
        if (QFile::exists(slideshowScript))
            args << QString("--script=%1").arg(slideshowScript);
    }

    if (playlistStart >= 0)
        args << QString("--playlist-start=%1").arg(playlistStart);
    if (startSeconds > 0.5f) {
        args << QString("--start=%1").arg(double(startSeconds), 0, 'f', 3);
        m_pendingStartClear = true;
    }
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

    // yt-dlp hook intercepts HTTP media URLs and can break Plex/Jellyfin
    // playback with spurious 401/400 errors — disabled unless the caller
    // explicitly opts in via extraArgs (e.g. YouTube passes --ytdl=yes).
    bool ytdlEnabled = false;
    for (const QString &a : extraArgs) {
        if (a == QLatin1String("--ytdl") || a.startsWith(QLatin1String("--ytdl=")))
            ytdlEnabled = true;
    }

    QStringList scriptOpts;
    if (transcodeOffsetSec > 0.5f)
        scriptOpts << QString("transcode-offset=%1").arg(double(transcodeOffsetSec), 0, 'f', 3);
    if (screensaverTimeout > 0)
        scriptOpts << QString("screensaver_timeout=%1").arg(screensaverTimeout);
    // Tell the OSC scripts to hide their CROP button on decode paths where
    // --panscan would blank the video (Pi 3 overlay path, 1080p Playback ON).
    if (cropUnavailable())
        scriptOpts << QStringLiteral("hide-crop=1");

    // Hand the OSC a map of external sub-file URL -> friendly track name so it can show
    // the real subtitle name. mpv otherwise titles an external sub from its URL basename
    // (e.g. "Stream.srt" for Jellyfin sidecars). Purely cosmetic — it does not affect
    // which sub mpv loads or selects.
    QFile::remove(m_subInfoPath);
    if (!subTitles.isEmpty() && subTitles.size() == subFiles.size()) {
        QJsonObject info;
        for (int i = 0; i < subFiles.size(); ++i) {
            if (!subTitles[i].isEmpty())
                info.insert(subFiles[i], subTitles[i]);
        }
        QFile sf(m_subInfoPath);
        if (!info.isEmpty() && sf.open(QFile::WriteOnly | QFile::Truncate)) {
            sf.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            sf.write(QJsonDocument(info).toJson(QJsonDocument::Compact));
            sf.close();
            // Path is comma- and space-free, so it is safe in the script-opts list.
            scriptOpts << QString("subinfo-file=%1").arg(m_subInfoPath);
        }
    }
    // Point mpv's ytdl_hook at the same user-updatable yt-dlp the app resolves,
    // so both agree on one copy even when it isn't on the global PATH (the
    // SteamOS story). Merged into the single --script-opts below — a second
    // --script-opts flag would replace, not append, clobbering the entries above.
    // The comma guard mirrors the subinfo-file constraint (a path with a comma
    // would break the join); realistically never hit.
    if (ytdlEnabled) {
        const QString ytdlPath = ytdlp::locate(m_dataRoot);
        if (!ytdlPath.isEmpty() && !ytdlPath.contains(QLatin1Char(',')))
            scriptOpts << QString("ytdl_hook-ytdl_path=%1").arg(ytdlPath);
    }
    if (!scriptOpts.isEmpty())
        args << QString("--script-opts=%1").arg(scriptOpts.join(QStringLiteral(",")));

    if (loop)
        args << QStringLiteral("--loop-playlist=inf");
    if (shuffle)
        args << QStringLiteral("--shuffle");
    // How long a still image is shown before mpv advances (or EOFs back to the
    // menu). Global for the launch, so it covers every image in a mixed playlist;
    // mpv ignores it for video and animated formats.
    if (imageDurationSec > 0.0f)
        args << QString("--image-display-duration=%1").arg(double(imageDurationSec), 0, 'f', 1);
    if (muteAudio)
        args << QStringLiteral("--no-audio");
    // See ytdlEnabled above: default the hook off unless the caller opted in.
    if (!ytdlEnabled)
        args << QStringLiteral("--ytdl=no");
    args << extraArgs;
    if (!plexToken.isEmpty()) {
        args << QString("--http-header-fields=X-Plex-Token:%1").arg(plexToken);
    }
    if (!jellyfinToken.isEmpty()) {
        args << QString("--http-header-fields=Authorization:MediaBrowser Token=\"%1\"").arg(jellyfinToken);
    }

    // plex.direct certs are Let's Encrypt-signed but ffmpeg's bundled CA bundle
    // may not trust the full chain (same reason Qt needs ignoreSslErrors for these
    // hosts). Disable TLS verification only for plex.direct playback URLs.
    if (QUrl(url).host().endsWith(QStringLiteral(".plex.direct")))
        args << QStringLiteral("--tls-verify=no");

    // Auto Crop: start with panscan=1 unless the current decode path can't crop.
    // The Pi3 overlay (smooth) path blanks video under panscan, so suppress there —
    // matching the 1080p Playback trade-off. The OSC CROP button still toggles live.
    if (autoCropEnabled() && !cropUnavailable())
        args << QStringLiteral("--panscan=1");

    // Video Levels: the RGB range mpv converts YUV into. Emitted only when the
    // user has overridden it, so "Auto" leaves both mpv's own default and
    // anything in their mpv.conf untouched. Sits before appendVideoArgs so an
    // explicit --video-output-levels inside the mpv_video_args override still
    // wins (later on the command line).
    const QString outputLevels = videoOutputLevels();
    if (!outputLevels.isEmpty())
        args << QStringLiteral("--video-output-levels=%1").arg(outputLevels);

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

    m_headlessMode = DisplayHandoff::isHeadless();
    if (m_headlessMode) {
        {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("APP_ROOT", m_appRoot);
#ifdef Q_OS_LINUX
            const QString fcConf = fcoverride::write(m_appRoot + "/assets/fonts");
            if (!fcConf.isEmpty())
                env.insert("FONTCONFIG_FILE", fcConf);
#endif
            m_process->setProcessEnvironment(env);
        }

        if (m_handoff && m_handoff->isHeldBy(QLatin1String(kHandoffOwner))) {
            // loadAndPlay called while already in headless mode (e.g. rapid
            // double call from Plex Player). The hand-off already holds Qt's
            // real VT — do NOT acquire again, which would overwrite it with the
            // free VT we are currently on. The old mpv was terminated above;
            // just launch the replacement directly.
            args << QString("--input-conf=%1").arg(m_inputConfPath)
                 << "--video-sync=audio";
            appendVideoArgs(args);
            args << "--no-input-terminal";
            m_process->start(bin, args);
            m_connectTimer->start();
            return;
        }

        // First entry into headless mode: hand the display to mpv. The VT/DRM
        // ordering that makes this work lives in DisplayHandoff::acquire().
        //
        // mpv deliberately does not check savedStateValid() here — playback has
        // always proceeded even when the CRTC state couldn't be captured.
        if (m_handoff)
            m_handoff->acquire(QLatin1String(kHandoffOwner));

        args << QString("--input-conf=%1").arg(m_inputConfPath)
             << "--video-sync=audio";
        appendVideoArgs(args);
        args << "--no-input-terminal";
        m_process->start(bin, args);
        m_connectTimer->start();
    } else {
        // Desktop: X11 or Wayland compositor present.
        // Prefer X11/Xwayland for mpv — the Wayland VO stalls waiting for
        // wl_surface frame-done callbacks from labwc (the Pi compositor). But
        // only strip WAYLAND_DISPLAY when there is a DISPLAY to fall back to:
        // on a pure-Wayland session with no Xwayland DISPLAY exported to us
        // (e.g. the Steam Deck's KDE session launched from the file manager),
        // removing it would leave mpv with no output at all and it exits
        // instantly. In that case keep Wayland so mpv can open a window.
        // --no-native-fs avoids macOS Space-transition delays that can
        // prevent early OSD renders from appearing.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("APP_ROOT", m_appRoot);
        if (!qEnvironmentVariable("DISPLAY").trimmed().isEmpty())
            env.remove("WAYLAND_DISPLAY");
#ifdef Q_OS_LINUX
        const QString fcConf = fcoverride::write(m_appRoot + "/assets/fonts");
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
        safeCmd.replace(QRegularExpression("X-Plex-Token[=:][^&\\s]+"), "X-Plex-Token=REDACTED");
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

void MpvController::showOsdSkipPrompt() {
    sendCommand({"script-message", "skip-overlay-state", "1"});
    sendCommand({"keypress", "DOWN"});
}

void MpvController::clearOsdPrompt() {
    sendCommand({"script-message", "skip-overlay-state", "0"});
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
            if (event == "end-file") {
                m_lastEndFileReason = obj["reason"].toString();
            } else if (event == "playback-restart" && m_pendingStartClear) {
                // --start is a global option that mpv re-applies on *every* file load,
                // and --loop-playlist reloads its entries on wrap. Left set, a resumed
                // file would restart at the resume offset on every loop lap (and every
                // later playlist entry would start there too) instead of at the
                // beginning. mpv only reads `start` when it loads a file, and
                // playback-restart means the initial seek is already done — so clearing
                // it here leaves the current playback alone while every subsequent load
                // begins at 0. Guarded so it fires once per session, not on every seek.
                m_pendingStartClear = false;
                sendCommand({"set_property", "start", "none"});
            } else if (event == "client-message") {
                const QJsonArray args = obj["args"].toArray();
                if (args.size() > 0) {
                    const QString msg = args[0].toString();
                    if (msg == "skip-segment")
                        emit skipRequested();
                    else if (msg == "cycle-sub")
                        emit subtitleCycleRequested();
                    else if (msg == "cycle-audio")
                        emit audioCycleRequested();
                }
            }
            continue;
        }

        m_lastIpcEventMs = QDateTime::currentMSecsSinceEpoch();

        const QString     name = obj["name"].toString();
        const QJsonValue  data = obj["data"];
        if (data.isNull() || data.isUndefined()) continue; // property unavailable during shutdown
        if (name == "pause") {
            m_paused = data.toBool();
            continue;
        }
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

    if (m_headlessMode && m_handoff) {
        // DisplayHandoff defers the DRM restore and VT switch (200 ms by
        // default) because mpv's last KMS atomic commit may still be pending in
        // the vc4 driver at the moment the process exits.
        m_handoff->releaseDeferred(QLatin1String(kHandoffOwner),
                                   [this, pos, dur, reason]() {
            m_headlessMode = false;
            emit playbackEnded(pos, dur, reason);
        });
    } else {
        emit playbackEnded(pos, dur, reason);
    }
}

void MpvController::sendCommand(const QJsonArray &args) {
    if (m_ipc->state() != QLocalSocket::ConnectedState) {
        qWarning("[MpvController] IPC not connected, dropping: %s",
                 QJsonDocument(QJsonObject{{"command", args}}).toJson(QJsonDocument::Compact).constData());
        return;
    }
    QJsonObject cmd;
    cmd["command"] = args;
    m_ipc->write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n");
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
            // Pi 5 (Full KMS) and a safe fallback for unknown headless Linux for now.
            // Note: Sometimes on Pi5+composite CRTs the Pi5 reports its composite raster 
            // inconsistently (sometimes a narrow 704×432 instead of the standard 720×480i)
            // this can be accomodated for in mpv.conf via a monitorpixelaspect property or
            // in cmdline.txt/config.txt at the OS level vs hardcoding something here.
            args << "--vo=drm" << "--hwdec=auto-safe";
        }
    } else {
#if defined(Q_OS_MACOS)
        // Apple Silicon: enable VideoToolbox HW decode (mpv's default is none).
        args << "--hwdec=videotoolbox";
#elif defined(Q_OS_LINUX)
        // Desktop compositor (SteamDeck gamescope / KDE, x86_64 Intel/AMD): mpv
        // sets no hwdec by default, so decode falls back to software. This is an
        // explicit priority list rather than auto-safe because auto-safe also
        // considers Vulkan video decode, and on a host where neither NVDEC nor
        // VA-API can initialise it walks all the way down to that: on Batocera
        // with an NVIDIA GPU (no usable nvidia_drv_video.so, "Could not create
        // device" for cuda) mpv picked h264-vulkan, presented exactly one frame
        // and then deadlocked with the demuxer still buffering — a hard freeze
        // needing SIGKILL. The Vulkan *output* path is fine and stays in use; only
        // Vulkan decoding is excluded. VA-API still wins on the Deck's AMD APU and
        // on Intel/AMD, NVDEC covers NVIDIA where it actually initialises, the
        // -copy variants catch hosts whose VO interop is unavailable, and the
        // trailing "no" degrades to software instead of hanging.
        // Fully overridable via the mpv_video_args setting handled above.
        args << "--hwdec=vaapi,nvdec,vaapi-copy,nvdec-copy,no";
#endif
        // Any other desktop: leave mpv's defaults untouched.
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

QString MpvController::videoOutputLevels() const {
    // Default "Auto" → no flag at all, leaving mpv's own default (full-range RGB
    // out) and anything the user set in mpv.conf in place. Stored by Settings as
    // a string ("Auto"/"Limited"/"Full") via the list_single row, so compare on
    // the string form.
    if (!m_appCore)
        return {};
    const QString v = m_appCore->get_setting(QString(), "video_output_levels").toString();
    if (v.compare(QStringLiteral("Limited"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("limited");
    if (v.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("full");
    return {};
}

bool MpvController::cropUnavailable() const {
    // The Pi 3 smooth (overlay-plane) path is the one decode path that can't
    // crop/zoom: --panscan blanks the video there. (Ignores the mpv_video_args
    // override, same as the auto-crop gate — the setting still reflects intent.)
    return m_videoProfile == VideoProfile::Pi3 && smoothPlaybackEnabled();
}

bool MpvController::hasSmoothPlaybackTradeoff() const {
    // Only the Pi 3 overlay path sacrifices crop/zoom for smoothness. Every other
    // profile (Pi 4 copy path, Pi 5/generic --vo=drm, desktop) can already crop, so
    // the toggle would be a no-op there and is hidden.
    return m_videoProfile == VideoProfile::Pi3;
}

