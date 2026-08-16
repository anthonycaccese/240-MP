#include "ScriptLauncher.h"
#include "../../util/DisplayHandoff.h"
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <array>
#include <cstdio>
#include <cstring>

#ifdef Q_OS_UNIX
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#endif

#ifdef Q_OS_LINUX
namespace {

// Steam's Gaming Mode session exports LD_PRELOAD holding BOTH copies of the
// overlay hook (ubuntu12_32/ and ubuntu12_64/gameoverlayrenderer.so) so it can
// hook games of either word size. Every child inherits it, and ld.so prints
// "wrong ELF class ... ignored" to stderr for whichever copy doesn't match.
// Harmless — but we merge stderr into the console pane, so it reads as if the
// user's script printed it. Drop only the entries we can positively identify as
// the wrong class; anything unreadable, or holding a dynamic token ($LIB,
// $ORIGIN, $PLATFORM) that only ld.so can expand, is passed through untouched.
QString sanitizedLdPreload(const QString &value)
{
    const char wantClass = (sizeof(void *) == 8) ? 2 : 1;   // ELFCLASS64 : ELFCLASS32

    QStringList kept;
    const QStringList entries = value.split(QRegularExpression(QStringLiteral("[:\\s]+")),
                                            Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        if (!entry.contains(QLatin1Char('$'))) {
            QFile f(entry);
            char hdr[5];
            if (f.open(QIODevice::ReadOnly) && f.read(hdr, 5) == 5
                && std::memcmp(hdr, "\x7f" "ELF", 4) == 0 && hdr[4] != wantClass) {
                continue;
            }
        }
        kept << entry;
    }
    return kept.join(QLatin1Char(':'));
}

} // namespace
#endif

ScriptLauncher::ScriptLauncher(const QString &appRoot, const QString &dataRoot,
                               DisplayHandoff *handoff, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot), m_handoff(handoff)
{
    m_outputTimer = new QTimer(this);
    m_outputTimer->setSingleShot(true);
    m_outputTimer->setInterval(100);
    connect(m_outputTimer, &QTimer::timeout, this, &ScriptLauncher::outputChanged);

    m_killTimer = new QTimer(this);
    m_killTimer->setSingleShot(true);
    m_killTimer->setInterval(kKillGraceMs);
    connect(m_killTimer, &QTimer::timeout, this, [this] {
        // Checked against the process GROUP, not just the script: during a group
        // drain the script itself is already gone but its children are what we are
        // actually waiting on.
        if (isBusy()) {
            qWarning("[Scripts] '%s' ignored SIGTERM — sending SIGKILL",
                     qPrintable(m_runningBasename));
            killGroup(SIGKILL);
        }
    });

    m_groupTimer = new QTimer(this);
    m_groupTimer->setInterval(kGroupPollMs);
    connect(m_groupTimer, &QTimer::timeout, this, [this] {
#ifdef Q_OS_UNIX
        if (m_pgid > 0 && ::kill(static_cast<pid_t>(-m_pgid), 0) == 0) {
            // Something the script started is still alive and may still own the
            // display. Warn once, then keep waiting — for a launcher-style script
            // (spawn the real app, exit) this drain IS the whole run, so killing
            // or timing out the group would take down the app the user is using,
            // and restoring the CRTC out from under it is no better. The drain
            // waits indefinitely by design; a genuinely wedged group is cleared
            // when the app quits (~ScriptLauncher SIGTERM→SIGKILL + releaseNow).
            if (!m_warnedDrain
                && QDateTime::currentMSecsSinceEpoch() - m_drainStartMs > kGroupWarnMs) {
                m_warnedDrain = true;
                qWarning("[Scripts] '%s' exited but its process group is still alive — "
                         "still waiting before taking the screen back",
                         qPrintable(m_runningBasename));
            }
            return;
        }
#endif
        m_groupTimer->stop();
        releaseDisplayAndReport();
    });

    m_startTimer = new QTimer(this);
    m_startTimer->setSingleShot(true);
    m_startTimer->setInterval(kStartWatchdogMs);
    connect(m_startTimer, &QTimer::timeout, this, [this] {
        // Never started and never errored. Without this the display would stay
        // handed over to a process that does not exist — a black screen with no
        // way back on a headless Pi.
        if (!m_finished && (!m_process || m_process->state() == QProcess::Starting)) {
            qWarning("[Scripts] '%s' did not start within %d ms — giving up",
                     qPrintable(m_runningBasename), kStartWatchdogMs);
            finish(-1, QStringLiteral("failed_to_start"));
        }
    });
}

ScriptLauncher::~ScriptLauncher() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        // Take the whole group down, not just the script: a bare terminate()
        // would orphan anything it spawned.
        killGroup(SIGTERM);
        if (!m_process->waitForFinished(1000)) {
            killGroup(SIGKILL);
            m_process->waitForFinished(500);
        }
    }
    // Quitting while a takeover script has the screen would otherwise leave a Pi
    // on a blank VT with DRM master dropped. Synchronous: we're exiting.
    if (m_takeoverRun && m_handoff) {
        m_handoff->releaseNow(QLatin1String(kHandoffOwner));
        m_takeoverRun = false;
    }
}

bool ScriptLauncher::isRunning() const {
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool ScriptLauncher::drainingGroup() const {
    return m_groupTimer && m_groupTimer->isActive();
}

void ScriptLauncher::resetForNewRun() {
    m_lines.clear();
    m_partial.clear();
    m_pendingCr     = false;
    m_bytes         = 0;
    m_pgid          = -1;
    m_stopRequested = false;
    m_finished      = false;
    m_lastExitCode  = 0;
    m_pendingReason.clear();
    m_takeoverRun  = false;
    m_waitForGroup  = true;
    m_downgraded    = false;
    m_drainStartMs  = 0;
    m_warnedDrain   = false;
    m_decoder.resetState();   // don't carry a half-decoded character into a new run
}

bool ScriptLauncher::start(const ScriptEntry &entry, QString *errorOut) {
    const auto fail = [&](const QString &msg) {
        qWarning("[Scripts] Cannot run '%s': %s",
                 qPrintable(entry.basename), qPrintable(msg));
        if (errorOut) *errorOut = msg;
        return false;
    };

    // isBusy(), not isRunning(): during a group drain the script itself is gone
    // but the display is still held, and acquiring again with the same owner
    // token would overwrite the saved VT with the free one we are sitting on.
    if (isBusy())
        return fail(QStringLiteral("Another script is still running"));

    // --- Pre-flight. Nothing is spawned (and from Phase 4, no display is handed
    // over) until every one of these passes.
    const QFileInfo fi(entry.path);
    if (!fi.exists())    return fail(QStringLiteral("File no longer exists"));
    if (!fi.isFile())    return fail(QStringLiteral("Not a regular file"));
    if (!fi.isReadable())return fail(QStringLiteral("File is not readable"));

    // Not requiring +x is deliberate: a script copied off a Mac, unzipped, or
    // pulled from a share loses the bit, and "not executable" is a terrible
    // error for an appliance. But when the bit IS set we exec directly, because
    // that is the only way a #!/usr/bin/env python3 shebang is honoured.
    const bool direct = fi.isExecutable();
    if (!direct && !QFileInfo::exists(QStringLiteral("/bin/sh")))
        return fail(QStringLiteral("Not executable and /bin/sh is missing"));

    resetForNewRun();
    m_runningName     = entry.meta.name;
    m_runningBasename = entry.basename;
    m_waitForGroup    = (entry.meta.wait != QLatin1String("child"));

    // --- Hand the display over, if this script wants it. Everything that could
    // fail has already been checked above: nothing may be handed over before a
    // spawn we still might refuse.
    bool takeover = entry.meta.isTakeover();
    int  vt = 0;
    if (takeover && m_handoff) {
        vt = m_handoff->acquire(QLatin1String(kHandoffOwner));
        if (vt < 0)
            return fail(QStringLiteral("The screen is in use"));

        // vt > 0 means a real hand-off happened (headless Linux). If the CRTC
        // state could not be captured we cannot put the display back afterwards,
        // so refuse the hand-off and run in console mode instead — a degraded run
        // beats a black screen with no way home. Not meaningful when vt == 0,
        // where there was nothing to save.
        if (vt > 0 && !m_handoff->savedStateValid()) {
            qWarning("[Scripts] Refusing takeover for '%s': display state could not "
                     "be saved, so it could not be restored. Running in console mode.",
                     qPrintable(entry.basename));
            m_handoff->releaseNow(QLatin1String(kHandoffOwner));
            takeover     = false;
            vt           = 0;
            m_downgraded = true;
        } else {
            m_takeoverRun = true;
        }
    }

    delete m_process;
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(fi.absolutePath());   // scripts assume their own dir

    // Only a child that will be handed a controlling terminal gets
    // ForwardedChannels: in forwarded mode Qt performs no dup2 on 0/1/2 at all,
    // so the child modifier is unambiguously the last writer regardless of Qt
    // version. Everywhere else capture the output — it is the only diagnostic a
    // user gets when a takeover script dies instantly on a headless Pi.
    const bool wantCttyRequested = takeover && entry.meta.tty && vt > 0;
    m_process->setProcessChannelMode(wantCttyRequested ? QProcess::ForwardedChannels
                                                       : QProcess::MergedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("APP_ROOT"),  m_appRoot);
    env.insert(QStringLiteral("DATA_ROOT"), m_dataRoot);
    env.insert(QStringLiteral("MP240_MODE"),
               takeover ? QStringLiteral("takeover") : QStringLiteral("console"));
    if (vt > 0)
        env.insert(QStringLiteral("MP240_VT"), QString::number(vt));
#ifdef Q_OS_LINUX
    // Strip the wrong-word-size Steam overlay hook the Gaming Mode session hands
    // down, so ld.so's "wrong ELF class ... ignored" warning stops appearing in
    // the console pane as if the script had emitted it. The matching-class entry
    // survives, so a launched app still gets the Steam overlay.
    const QString preload = env.value(QStringLiteral("LD_PRELOAD"));
    if (!preload.isEmpty()) {
        const QString cleaned = sanitizedLdPreload(preload);
        if (cleaned != preload) {
            if (cleaned.isEmpty())
                env.remove(QStringLiteral("LD_PRELOAD"));
            else
                env.insert(QStringLiteral("LD_PRELOAD"), cleaned);
        }
    }
#endif

    // NOTE: deliberately NOT removing WAYLAND_DISPLAY the way MpvController does.
    // That is an mpv-VO workaround for labwc frame-done stalls, not a general
    // truth: stripping it would break a Wayland-native child with no Xwayland.
    m_process->setProcessEnvironment(env);

#ifdef Q_OS_UNIX
    // Probe the VT from the parent, where we can actually report why it failed.
    // /dev/tty2..63 are 0620 root:tty — group WRITE only — so opening one O_RDWR
    // needs CAP_DAC_OVERRIDE or a udev rule widening the mode. If we can't, run
    // without a controlling terminal (which is how mpv already runs) rather than
    // failing the script.
    std::array<char, 16> devPath{};
    bool wantCtty = wantCttyRequested;
    if (wantCtty) {
        std::snprintf(devPath.data(), devPath.size(), "/dev/tty%d", vt);
        const int probe = ::open(devPath.data(), O_RDWR | O_NOCTTY);
        if (probe < 0) {
            qWarning("[Scripts] tty=yes requested but %s is not openable (%s) — "
                     "running without a controlling terminal. See INSTALL.md for the "
                     "udev rule.", devPath.data(), strerror(errno));
            wantCtty = false;
        } else {
            ::close(probe);
        }
    }

    // Runs between fork and exec: async-signal-safe calls only, no allocation.
    // setsid() makes the child its own session AND process-group leader, so
    // pgid == pid, killpg() reaches everything it spawns, and (in takeover mode)
    // an empty group is how we know the screen is free again.
    m_process->setChildProcessModifier([devPath, wantCtty]() {
        ::setsid();
        if (wantCtty) {
            const int fd = ::open(devPath.data(), O_RDWR | O_NOCTTY);
            if (fd >= 0) {
                ::ioctl(fd, TIOCSCTTY, 0);
                ::dup2(fd, 0);
                ::dup2(fd, 1);
                ::dup2(fd, 2);
                if (fd > 2) ::close(fd);
            }
        }
    });
#endif

    connect(m_process, &QProcess::readyRead, this, &ScriptLauncher::onReadyRead);
    connect(m_process, &QProcess::finished,  this, &ScriptLauncher::onProcessFinished);
    // errorOccurred is not redundant: on FailedToStart, finished() is NEVER
    // emitted, so without this the run would hang forever with no result.
    connect(m_process, &QProcess::errorOccurred, this, &ScriptLauncher::onErrorOccurred);
    connect(m_process, &QProcess::started, this, [this] {
        m_pgid = m_process->processId();
        emit runningChanged();
    });

    const QStringList extra = entry.meta.args.trimmed().isEmpty()
                                ? QStringList()
                                : QProcess::splitCommand(entry.meta.args);

    // The absolute path always goes first so it can never be read as a flag.
    if (direct) {
        m_process->start(fi.absoluteFilePath(), extra);
    } else {
        m_process->start(QStringLiteral("/bin/sh"),
                         QStringList{fi.absoluteFilePath()} + extra);
    }

    m_startTimer->start();

    qInfo("[Scripts] Running '%s' (%s, %s%s%s)", qPrintable(entry.basename),
          direct ? "direct" : "/bin/sh",
          takeover ? "takeover" : "console",
          vt > 0 ? qPrintable(QStringLiteral(", vt %1").arg(vt)) : "",
          extra.isEmpty() ? "" : ", with args");
    emit runningChanged();
    return true;
}

// Reached only from console mode and downgraded-takeover runs, where 240-MP kept
// the screen and a stop key is still offered. A real takeover run has no stop key
// (the child owns input; see Takeover.qml) — for those the group either exits on
// its own or is cleared at app quit.
void ScriptLauncher::requestStop() {
    if (!isBusy()) return;
    // Already stopping: do NOT re-arm the kill timer. QTimer::start() on an active
    // timer restarts it, so a repeated call (a held Back key auto-repeating, say)
    // would reset the SIGKILL grace every time and a script that ignores SIGTERM
    // would never be killed at all while the key was held.
    if (m_stopRequested) return;
    m_stopRequested = true;
    qInfo("[Scripts] Stopping '%s'%s", qPrintable(m_runningBasename),
          isRunning() ? "" : " (process group)");
    killGroup(SIGTERM);
    m_killTimer->start();
}

void ScriptLauncher::killGroup(int sig) {
#ifdef Q_OS_UNIX
    if (m_pgid > 0) {
        if (::killpg(static_cast<pid_t>(m_pgid), sig) == 0)
            return;
        // Group already gone, or we never learned the pgid — fall through.
    }
#else
    Q_UNUSED(sig)
#endif
    if (m_process && m_process->state() != QProcess::NotRunning) {
        if (sig == SIGKILL) m_process->kill();
        else                m_process->terminate();
    }
}

void ScriptLauncher::onReadyRead() {
    if (!m_process) return;
    // Through the stateful decoder, not QString::fromUtf8: a multibyte character
    // can straddle two reads, and per-chunk conversion would mangle it.
    appendOutput(m_decoder(m_process->readAll()));
}

// Applies terminal carriage-return semantics: a bare \r rewrites the current
// line rather than starting a new one, which keeps a progress bar (yt-dlp, curl,
// apt) as one updating line instead of hundreds of them. \r\n stays one newline.
void ScriptLauncher::appendOutput(const QString &chunk) {
    for (const QChar c : chunk) {
        if (c == u'\r') {
            m_pendingCr = true;      // decide once we see the next character
            continue;
        }
        if (m_pendingCr) {
            m_pendingCr = false;
            if (c != u'\n')
                m_partial.clear();   // bare \r then text: overwrite the line
        }
        if (c == u'\n') {
            pushLine();
        } else {
            m_partial += c;
            // Force-break an endless line: trimming drops only completed lines,
            // so an uncapped m_partial would grow without limit.
            if (m_partial.size() >= kMaxLineChars)
                pushLine();
        }
    }
    trimOutput();
    scheduleOutputChanged();
}

void ScriptLauncher::pushLine() {
    m_lines.append(m_partial);
    m_bytes += m_partial.size() + 1;
    m_partial.clear();
}

void ScriptLauncher::trimOutput() {
    while (m_lines.size() > kMaxLines || (m_bytes > kMaxBytes && !m_lines.isEmpty())) {
        m_bytes -= m_lines.first().size() + 1;
        m_lines.removeFirst();
    }
    if (m_bytes < 0) m_bytes = 0;
}

void ScriptLauncher::scheduleOutputChanged() {
    if (!m_outputTimer->isActive())
        m_outputTimer->start();
}

QString ScriptLauncher::outputText() const {
    QString out = m_lines.join(QLatin1Char('\n'));
    if (!m_partial.isEmpty()) {
        if (!out.isEmpty()) out += QLatin1Char('\n');
        out += m_partial;
    }
    return out;
}

void ScriptLauncher::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    QString reason;
    if (m_stopRequested)                          reason = QStringLiteral("stopped");
    else if (status == QProcess::CrashExit)        reason = QStringLiteral("failed");
    else if (exitCode == 0)                       reason = QStringLiteral("ok");
    else                                          reason = QStringLiteral("failed");
    finish(exitCode, reason);
}

void ScriptLauncher::onErrorOccurred(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        appendOutput(QStringLiteral("\n[240-MP] Failed to start: %1\n")
                        .arg(m_process ? m_process->errorString() : QString()));
        finish(-1, QStringLiteral("failed_to_start"));
        return;
    }
    // A stop we asked for arrives here as Crashed (the signal killed it), which is
    // not an error worth warning about — onProcessFinished already classifies it
    // as "stopped".
    if (m_stopRequested && error == QProcess::Crashed)
        return;

    // Other Crashed/Timedout/Read/Write errors still produce a finished() signal,
    // so they are left to onProcessFinished to classify.
    qWarning("[Scripts] '%s' process error %d: %s", qPrintable(m_runningBasename),
             static_cast<int>(error),
             qPrintable(m_process ? m_process->errorString() : QString()));
}

void ScriptLauncher::finish(int exitCode, const QString &reason) {
    if (m_finished) return;      // both signals can arrive for one run
    m_finished = true;
    m_killTimer->stop();
    m_startTimer->stop();

    // Drain whatever is still buffered: readyRead and finished are independent
    // event-loop signals, so the script's last lines may not have been read yet.
    if (m_process) {
        const QByteArray rest = m_process->readAll();
        if (!rest.isEmpty())
            appendOutput(m_decoder(rest));
    }
    if (m_pendingCr) m_pendingCr = false;
    if (!m_partial.isEmpty()) pushLine();     // flush a final unterminated line

    m_lastExitCode  = exitCode;
    m_pendingReason = reason;

    qInfo("[Scripts] '%s' finished: %s (exit %d)", qPrintable(m_runningBasename),
          qPrintable(reason), exitCode);

    m_outputTimer->stop();
    emit outputChanged();
    emit runningChanged();      // the process really is gone; say so now

    if (m_takeoverRun) {
        // Do NOT report yet: the caller pops its view on finished(), and doing
        // that while the display still belongs to the script would draw into a
        // framebuffer we don't own.
        if (m_waitForGroup) startGroupDrain();
        else                releaseDisplayAndReport();
        return;
    }

    m_pgid = -1;
    report();
}

// A launcher script that backgrounds its real work exits immediately while its
// children still hold the screen. Restoring the CRTC underneath them gives a
// black or torn display, so wait for the whole process group to empty first.
void ScriptLauncher::startGroupDrain() {
#ifdef Q_OS_UNIX
    if (m_pgid > 0 && ::kill(static_cast<pid_t>(-m_pgid), 0) == 0) {
        m_drainStartMs = QDateTime::currentMSecsSinceEpoch();
        m_warnedDrain  = false;
        m_groupTimer->start();
        qInfo("[Scripts] '%s' exited but left processes running — waiting before "
              "taking the screen back", qPrintable(m_runningBasename));
        emit runningChanged();   // isBusy() is now true again (draining)
        return;
    }
#endif
    releaseDisplayAndReport();
}

void ScriptLauncher::releaseDisplayAndReport() {
    m_pgid = -1;
    if (!m_handoff) {           // defensive: nothing to give back
        m_takeoverRun = false;
        report();
        return;
    }
    m_handoff->releaseDeferred(QLatin1String(kHandoffOwner), [this]() {
        m_takeoverRun = false;
        emit runningChanged();   // isBusy() finally false
        report();
    });
}

void ScriptLauncher::report() {
    emit finished(m_lastExitCode, m_pendingReason);
}
