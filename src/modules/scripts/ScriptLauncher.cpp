#include "ScriptLauncher.h"
#include <QFileInfo>
#include <QDir>
#include <QProcessEnvironment>
#include <QTimer>
#include <QDebug>

#ifdef Q_OS_UNIX
#include <unistd.h>
#include <signal.h>
#endif

ScriptLauncher::ScriptLauncher(const QString &appRoot, const QString &dataRoot,
                               QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot)
{
    m_outputTimer = new QTimer(this);
    m_outputTimer->setSingleShot(true);
    m_outputTimer->setInterval(100);
    connect(m_outputTimer, &QTimer::timeout, this, &ScriptLauncher::outputChanged);

    m_killTimer = new QTimer(this);
    m_killTimer->setSingleShot(true);
    m_killTimer->setInterval(kKillGraceMs);
    connect(m_killTimer, &QTimer::timeout, this, [this] {
        if (isRunning()) {
            qWarning("[Scripts] '%s' ignored SIGTERM — sending SIGKILL",
                     qPrintable(m_runningBasename));
            killGroup(SIGKILL);
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
}

bool ScriptLauncher::isRunning() const {
    return m_process && m_process->state() != QProcess::NotRunning;
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
}

bool ScriptLauncher::start(const ScriptEntry &entry, QString *errorOut) {
    const auto fail = [&](const QString &msg) {
        qWarning("[Scripts] Cannot run '%s': %s",
                 qPrintable(entry.basename), qPrintable(msg));
        if (errorOut) *errorOut = msg;
        return false;
    };

    if (isRunning())
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

    delete m_process;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setWorkingDirectory(fi.absolutePath());   // scripts assume their own dir

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("APP_ROOT"),  m_appRoot);
    env.insert(QStringLiteral("DATA_ROOT"), m_dataRoot);
    env.insert(QStringLiteral("MP240_MODE"), QStringLiteral("console"));
    m_process->setProcessEnvironment(env);

#ifdef Q_OS_UNIX
    // New session => the child is its own process-group leader, so pgid ==
    // its pid and killpg() reaches everything it spawns. Only async-signal-safe
    // calls are allowed here (this runs between fork and exec).
    m_process->setChildProcessModifier([]() { ::setsid(); });
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

    qInfo("[Scripts] Running '%s' (%s%s)", qPrintable(entry.basename),
          direct ? "direct" : "/bin/sh",
          extra.isEmpty() ? "" : ", with args");
    emit runningChanged();
    return true;
}

void ScriptLauncher::requestStop() {
    if (!isRunning()) return;
    m_stopRequested = true;
    qInfo("[Scripts] Stopping '%s'", qPrintable(m_runningBasename));
    killGroup(SIGTERM);
    m_killTimer->start();
}

void ScriptLauncher::forceStop() {
    if (!isRunning()) return;
    m_stopRequested = true;
    qWarning("[Scripts] Force-stopping '%s'", qPrintable(m_runningBasename));
    killGroup(SIGKILL);
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
    appendOutput(QString::fromUtf8(m_process->readAll()));
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
        if (c == u'\n') pushLine();
        else            m_partial += c;
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

    // Drain whatever is still buffered: readyRead and finished are independent
    // event-loop signals, so the script's last lines may not have been read yet.
    if (m_process) {
        const QByteArray rest = m_process->readAll();
        if (!rest.isEmpty())
            appendOutput(QString::fromUtf8(rest));
    }
    if (m_pendingCr) m_pendingCr = false;
    if (!m_partial.isEmpty()) pushLine();     // flush a final unterminated line

    m_lastExitCode = exitCode;
    m_pgid         = -1;

    qInfo("[Scripts] '%s' finished: %s (exit %d)", qPrintable(m_runningBasename),
          qPrintable(reason), exitCode);

    m_outputTimer->stop();
    emit outputChanged();
    emit runningChanged();
    emit finished(exitCode, reason);
}
