#include "AirPlayBackend.h"
#include "../../AppCore.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTextStream>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr int kKillTimeoutMs = 3000;
constexpr int kMetadataReadChunk = 65536;
const char *kModuleId = "com.240mp.airplay";
// How many times in a row to auto-restart after a mid-session crash before
// giving up and surfacing a persistent error instead — see
// handleProcessFinished()'s comment.
constexpr int kMaxCrashRestartAttempts = 3;
constexpr int kCrashRestartDelayMs = 1000;
// A session shorter than this doesn't count as "healthy" for resetting the
// crash-restart counter — see m_connectionUptimer's comment.
constexpr qint64 kMinHealthyUptimeMs = 30000;
}

AirPlayBackend::AirPlayBackend(const QString &appRoot, const QString &dataRoot,
                               AppCore *appCore, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_appCore(appCore)
{
    QDir().mkpath(airplayDataDir());
}

AirPlayBackend::~AirPlayBackend()
{
    // Safety net: app quit / crash shouldn't leave shairport-sync orphaned.
    stopReceiver();
}

QString AirPlayBackend::airplayDataDir() const
{
    return m_dataRoot + "/airplay";
}

QString AirPlayBackend::configFilePath() const
{
    return airplayDataDir() + "/shairport-sync.conf";
}

QString AirPlayBackend::metadataPipePath() const
{
    return airplayDataDir() + "/metadata.pipe";
}

QString AirPlayBackend::deviceNameFilePath() const
{
    return airplayDataDir() + "/device_name.txt";
}

// Re-read fresh on every startReceiver() rather than cached for the process
// lifetime, so editing the file and reopening the module is enough to pick
// up a rename — no restart of the app required.
QString AirPlayBackend::resolveDeviceName() const
{
    QFile f(deviceNameFilePath());
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString name = QString::fromUtf8(f.readAll()).trimmed();
        // Strip characters that would break out of the config file's quoted
        // string rather than bother with proper escaping for a display name.
        // trimmed() only strips leading/trailing whitespace, so an embedded
        // (not just trailing) newline in the file — e.g. someone pasted in a
        // multi-line value by mistake — would otherwise survive into a
        // libconfig quoted string, which can't contain a literal line break.
        name.remove(QLatin1Char('"'));
        name.remove(QLatin1Char('\\'));
        name.replace(QLatin1Char('\n'), QLatin1Char(' '));
        name.replace(QLatin1Char('\r'), QLatin1Char(' '));
        if (!name.isEmpty()) {
            return name;
        }
    }
    return QStringLiteral("240-MP");
}

void AirPlayBackend::writeConfigFile()
{
    QDir().mkpath(airplayDataDir());

    QFile f(configFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "AirPlayBackend: failed to write config file" << configFilePath();
        return;
    }

    QTextStream out(&f);
    out << "general = {\n";
    out << "  name = \"" << m_deviceName << "\";\n";
    out << "};\n";
    out << "alsa = {\n";
    if (!m_selectedAudioDevice.isEmpty()) {
        out << "  output_device = \"" << m_selectedAudioDevice << "\";\n";
    }
    out << "};\n";
    out << "metadata = {\n";
    out << "  enabled = \"yes\";\n";
    out << "  include_cover_art = \"yes\";\n";
    out << "  pipe_name = \"" << metadataPipePath() << "\";\n";
    out << "};\n";
}

void AirPlayBackend::startReceiver()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        return; // already running
    }

    m_deviceName = resolveDeviceName();
    resetNowPlaying(); // emits nowPlayingChanged(), covering the name resolved just above
    m_everConnectedThisRun = false;
    setReceiverError(QString()); // clear any stale error from a previous attempt

    if (m_appCore) {
        m_selectedAudioDevice = resolveAudioDevice(m_appCore->get_setting(kModuleId, "audio_output_device").toString());
    }
    writeConfigFile();

    // The FIFO must exist before shairport-sync opens it for writing.
    QByteArray pipePathUtf8 = metadataPipePath().toUtf8();
    ::unlink(pipePathUtf8.constData());
    if (::mkfifo(pipePathUtf8.constData(), 0666) != 0) {
        qWarning() << "AirPlayBackend: mkfifo failed for" << metadataPipePath();
    }

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::errorOccurred, this, &AirPlayBackend::handleProcessErrorOccurred);
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this,
                &AirPlayBackend::handleProcessFinished);
    }

    m_process->setProgram(QStringLiteral("shairport-sync"));
    m_process->setArguments({QStringLiteral("--configfile=") + configFilePath()});
    m_process->start();

    if (!m_process->waitForStarted(3000)) {
        qWarning() << "AirPlayBackend: shairport-sync failed to start:" << m_process->errorString();
        setReceiverError(QStringLiteral("shairport-sync failed to start: %1").arg(m_process->errorString()));
        return;
    }

    openMetadataPipe();
}

void AirPlayBackend::stopReceiver()
{
    // Cancel a pending mid-session-crash auto-restart, if one is queued —
    // see handleProcessFinished()'s comment. Without this, backing out of
    // the module right after a crash (but within the restart delay) would
    // still relaunch shairport-sync a moment later, in the background,
    // after the screen it belongs to has already closed.
    if (m_crashRestartTimer) {
        m_crashRestartTimer->stop();
    }

    // Stop the writer before tearing down the reader, not the other way
    // around: closing our end of the metadata FIFO first, while
    // shairport-sync might still be about to write to it, risks it seeing a
    // broken pipe on its next write instead of the clean exit terminate()
    // asks for.
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_stoppingIntentionally = true;
        m_process->terminate();
        if (!m_process->waitForFinished(kKillTimeoutMs)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        m_stoppingIntentionally = false;
    }

    closeMetadataPipe();
    resetNowPlaying();
    setReceiverError(QString()); // a deliberate stop is a clean slate, not a failure
    m_crashRestartAttempts = 0;
    m_connectionUptimer.invalidate();
}

void AirPlayBackend::resetNowPlaying()
{
    // Delete the session's artwork now rather than leaving it — the "delete
    // the previous one" logic in handleChunk() only fires when a *new* PICT
    // arrives to replace it, so a session that ends without another one
    // starting would otherwise leave the file behind forever: one orphaned
    // image per completed session, unbounded over the life of the install.
    if (!m_artworkPath.isEmpty()) {
        QFile::remove(m_artworkPath);
    }

    m_trackTitle.clear();
    m_artist.clear();
    m_album.clear();
    m_artworkPath.clear();
    m_senderName.clear();
    m_isConnected = false;
    m_isPlaying = false;
    emit nowPlayingChanged();
    emit connectionStateChanged();
    emit playbackStateChanged();
}

void AirPlayBackend::setReceiverError(const QString &message)
{
    if (m_receiverError == message) {
        return;
    }
    m_receiverError = message;
    emit receiverErrorChanged();
}

void AirPlayBackend::handleProcessErrorOccurred(QProcess::ProcessError error)
{
    qWarning() << "AirPlayBackend: shairport-sync process error" << error
               << (m_process ? m_process->errorString() : QString());
    if (error == QProcess::FailedToStart) {
        setReceiverError(QStringLiteral("shairport-sync failed to start: %1")
                              .arg(m_process ? m_process->errorString() : QString()));
    }
}

void AirPlayBackend::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // A session ending normally (someone disconnected, or the user backed
    // out of the module) always passes through here too — that's not an
    // error. What's worth surfacing is the receiver dying before it ever
    // managed a single successful connection *and* nobody asked it to stop:
    // a real problem (bad config, ALSA rejecting the format, a crash — see
    // the shutdown-ordering fix in stopReceiver() for one already found this
    // way), not just "nobody's connected yet".
    if (!m_stoppingIntentionally && !m_everConnectedThisRun) {
        setReceiverError(exitStatus == QProcess::CrashExit
            ? QStringLiteral("shairport-sync crashed before connecting (exit code %1)").arg(exitCode)
            : QStringLiteral("shairport-sync exited before connecting (exit code %1)").arg(exitCode));
    }
    // A crash (or unexpected exit) *after* already connecting once is a
    // different case entirely from the branch above: nothing else in the
    // app ever calls startReceiver() again on its own, so without this the
    // module would just sit on the idle "waiting for connection" screen
    // forever — looking normal — even though the receiver is actually dead.
    // Restart automatically, the way a real receiver should recover from a
    // transient crash without the user having to notice or back out and
    // reopen the module. Capped so a genuine crash loop still gives up
    // instead of respawning forever.
    const bool shouldAutoRestart = !m_stoppingIntentionally && m_everConnectedThisRun;
    // A session that stayed up a while before dying is treated as healthy —
    // reset the counter so an occasional one-off crash always gets a fresh
    // set of retries. A session that dies again within seconds of each
    // reconnect (a real, recurring problem, not a fluke) does *not* reset
    // it, or this cap would never actually bite: every reconnect would wipe
    // the counter right back to 0 before it could accumulate.
    if (shouldAutoRestart && m_connectionUptimer.isValid()
        && m_connectionUptimer.elapsed() >= kMinHealthyUptimeMs) {
        m_crashRestartAttempts = 0;
    }
    closeMetadataPipe();
    resetNowPlaying();
    if (shouldAutoRestart) {
        if (++m_crashRestartAttempts <= kMaxCrashRestartAttempts) {
            if (!m_crashRestartTimer) {
                m_crashRestartTimer = new QTimer(this);
                m_crashRestartTimer->setSingleShot(true);
                connect(m_crashRestartTimer, &QTimer::timeout, this, &AirPlayBackend::startReceiver);
            }
            m_crashRestartTimer->start(kCrashRestartDelayMs);
        } else {
            setReceiverError(QStringLiteral(
                "shairport-sync keeps crashing mid-session — gave up after %1 restart attempts. "
                "Please check the wiki for troubleshooting steps.")
                    .arg(m_crashRestartAttempts - 1));
        }
    }
}

// --- Metadata pipe -------------------------------------------------------------------

void AirPlayBackend::openMetadataPipe()
{
    if (m_metadataFd >= 0) {
        return;
    }

    QByteArray pipePathUtf8 = metadataPipePath().toUtf8();
    // O_NONBLOCK so opening a FIFO with no writer yet doesn't block the UI
    // thread; shairport-sync opens it for writing shortly after it starts.
    m_metadataFd = ::open(pipePathUtf8.constData(), O_RDONLY | O_NONBLOCK);
    if (m_metadataFd < 0) {
        qWarning() << "AirPlayBackend: failed to open metadata pipe" << metadataPipePath();
        return;
    }

    m_metadataNotifier = new QSocketNotifier(m_metadataFd, QSocketNotifier::Read, this);
    connect(m_metadataNotifier, &QSocketNotifier::activated, this, [this](int fd) {
        Q_UNUSED(fd);
        drainMetadataPipe();
    });
}

void AirPlayBackend::closeMetadataPipe()
{
    if (m_metadataNotifier) {
        m_metadataNotifier->setEnabled(false);
        m_metadataNotifier->deleteLater();
        m_metadataNotifier = nullptr;
    }
    if (m_metadataFd >= 0) {
        ::close(m_metadataFd);
        m_metadataFd = -1;
    }
    m_metadataBuffer.clear();
}

void AirPlayBackend::drainMetadataPipe()
{
    if (m_metadataFd < 0) {
        return;
    }

    char buf[kMetadataReadChunk];
    for (;;) {
        ssize_t n = ::read(m_metadataFd, buf, sizeof(buf));
        if (n > 0) {
            m_metadataBuffer.append(buf, static_cast<int>(n));
            continue;
        }
        // n == 0: writer closed its end (shairport-sync restarted/exited). Keep
        // the fd open and let the next activation re-arm; n < 0 with EAGAIN just
        // means "no more data right now" for a non-blocking read.
        break;
    }

    processMetadataBuffer();
}

// Parses shairport-sync's metadata-pipe "tagged chunk" protocol:
//   <item><type>HEXHEXHEXHEX</type><code>HEXHEXHEXHEX</code><length>N</length>
//   [<data encoding="base64">\nBASE64\n</data>]</item>
// `type`/`code` are 4-character ASCII tags (e.g. "core"/"minm"), hex-encoded.
// TODO: validate against shairport-sync's current metadata docs — this was
// written from the plan's summary (docs/airplay-module-plan.md §6), not a
// live capture of the protocol.
void AirPlayBackend::processMetadataBuffer()
{
    static const QRegularExpression itemRe(
        QStringLiteral("<item>\\s*<type>([0-9a-fA-F]{8})</type>\\s*<code>([0-9a-fA-F]{8})</code>"
                        "\\s*<length>(\\d+)</length>"
                        "(?:\\s*<data encoding=\"base64\">\\r?\\n?(.*?)\\r?\\n?</data>)?"
                        "\\s*</item>"),
        QRegularExpression::DotMatchesEverythingOption);

    int consumedUpTo = 0;
    QRegularExpressionMatchIterator it = itemRe.globalMatch(QString::fromLatin1(m_metadataBuffer));
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QByteArray type = QByteArray::fromHex(m.captured(1).toLatin1());
        QByteArray code = QByteArray::fromHex(m.captured(2).toLatin1());
        QByteArray data = QByteArray::fromBase64(m.captured(4).toLatin1());
        handleChunk(type, code, data);
        consumedUpTo = m.capturedEnd(0);
    }

    if (consumedUpTo > 0) {
        m_metadataBuffer.remove(0, consumedUpTo);
    }
    // Keep the buffer from growing unbounded if we ever see malformed input
    // with no matchable </item> for a long stretch.
    if (m_metadataBuffer.size() > 4 * 1024 * 1024) {
        qWarning() << "AirPlayBackend: metadata buffer overflow, dropping" << m_metadataBuffer.size() << "bytes";
        m_metadataBuffer.clear();
    }
}

void AirPlayBackend::handleChunk(const QByteArray &type, const QByteArray &code, const QByteArray &data)
{
    const QString text = QString::fromUtf8(data);

    if (type == "core" && code == "minm") {
        m_trackTitle = text;
        emit nowPlayingChanged();
    } else if (type == "core" && code == "asar") {
        m_artist = text;
        emit nowPlayingChanged();
    } else if (type == "core" && code == "asal") {
        m_album = text;
        emit nowPlayingChanged();
    } else if (type == "ssnc" && code == "PICT") {
        if (!data.isEmpty()) {
            // Sniff magic bytes rather than trusting a fixed extension.
            QString ext = data.startsWith("\x89PNG") ? QStringLiteral("png") : QStringLiteral("jpg");
            const QString path = QStringLiteral("%1/artwork-%2.%3")
                                      .arg(airplayDataDir())
                                      .arg(++m_artworkSequence)
                                      .arg(ext);
            QFile artFile(path);
            if (artFile.open(QIODevice::WriteOnly)) {
                artFile.write(data);
                artFile.close();
                const QString previous = m_artworkPath;
                m_artworkPath = path;
                emit nowPlayingChanged();
                if (!previous.isEmpty() && previous != path) {
                    QFile::remove(previous);
                }
            }
        }
    } else if (type == "ssnc" && code == "pbeg") {
        m_everConnectedThisRun = true;
        m_connectionUptimer.start(); // see handleProcessFinished()'s comment
        m_isConnected = true;
        emit connectionStateChanged();
    } else if (type == "ssnc" && code == "pend") {
        resetNowPlaying();
    } else if (type == "ssnc" && (code == "prsm" || code == "pres")) {
        // prsm: classic AirPlay resume. pres: the AirPlay 2 equivalent —
        // shairport-sync's own reader script documents them as distinct
        // codes for the two protocol versions, so both are handled the same
        // way here rather than assuming only one will ever arrive.
        m_isPlaying = true;
        emit playbackStateChanged();
    } else if (type == "ssnc" && code == "paus") {
        m_isPlaying = false;
        emit playbackStateChanged();
    } else if (type == "ssnc" && code == "snam") {
        // The connecting device's name (e.g. "Anthony's iPhone") — confirmed
        // against shairport-sync-metadata-reader's own code table, replacing
        // the placeholder guess this module shipped with initially.
        m_senderName = text;
        emit nowPlayingChanged();
    }
    // Other chunk types (client model/cmod, DACP/active-remote ids, volume,
    // progress, format info, etc.) are ignored for v1 — not needed for the
    // Now Playing screen.
}

// --- Audio device setting --------------------------------------------------------------

// Runs `aplay -L` and returns only the `plughw:` entries, as (id, label)
// pairs. `aplay -L` also lists `hw:`, `dmix:`, `sysdefault:`, and a bare
// `default`/`sysdefault`/`null` — deliberately excluded here, because on
// real hardware (confirmed on a Pi 4's HDMI/vc4hdmi output) those can fail
// ALSA's hw_params negotiation with error -524 (ENOTSUPP), which
// shairport-sync responds to by segfaulting rather than failing cleanly.
// `plughw:` adds the `plug` conversion layer that avoids the negotiation
// failure in the first place, and every card that shows up in `aplay -L`
// has a `plughw:` entry, so nothing is actually lost by hiding the rest —
// only the crash-prone paths to the same hardware.
QList<QPair<QString, QString>> AirPlayBackend::listPlughwDevices()
{
    QList<QPair<QString, QString>> devices;

    QProcess probe;
    probe.start(QStringLiteral("aplay"), {QStringLiteral("-L")});
    if (!probe.waitForFinished(3000)) {
        qWarning() << "AirPlayBackend: `aplay -L` timed out enumerating audio devices";
        return devices;
    }

    const QStringList lines = QString::fromUtf8(probe.readAllStandardOutput()).split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (line.isEmpty() || line.at(0).isSpace() || !line.startsWith(QLatin1String("plughw:"))) {
            continue; // not a device-id line, or a device we deliberately don't offer
        }
        // The next line, if indented, is aplay's human-readable description
        // of this device (e.g. "vc4-hdmi-0, MAI PCM i2s-hifi-0") — nicer to
        // show in the settings list than the raw ALSA id.
        QString label = line;
        if (i + 1 < lines.size() && !lines.at(i + 1).isEmpty() && lines.at(i + 1).at(0).isSpace()) {
            label = lines.at(i + 1).trimmed();
        }
        devices.append({line, label});
    }
    return devices;
}

// "Default" (the manifest's default, and what a fresh install has) means
// auto-detect — resolved here to the first plughw: device found rather than
// left as an empty output_device for shairport-sync to pick its own ALSA
// default. That bare ALSA "default" PCM is exactly what crashed on the
// hardware this was tested against, so leaving it unresolved would mean a
// fresh install's out-of-the-box experience is a crash on first real
// playback. Only falls through to empty (shairport-sync's own default) if
// no cards were found at all — better than nothing on a system aplay can't
// enumerate.
QString AirPlayBackend::resolveAudioDevice(const QString &settingValue)
{
    if (!settingValue.isEmpty() && settingValue != QLatin1String("Default")) {
        return settingValue;
    }
    const auto devices = listPlughwDevices();
    return devices.isEmpty() ? QString() : devices.first().first;
}

void AirPlayBackend::getAudioDevices()
{
    QVariantList options;
    options.append(QVariantMap{{"id", "Default"}, {"label", "Default (auto-detect)"}});
    for (const auto &device : listPlughwDevices()) {
        options.append(QVariantMap{{"id", device.first}, {"label", device.second}});
    }
    emit dynamicOptionsReady(QStringLiteral("audio_output_device"), options);
}

void AirPlayBackend::applyAudioDeviceSetting()
{
    if (!m_appCore) {
        return;
    }
    const QString device = resolveAudioDevice(m_appCore->get_setting(kModuleId, "audio_output_device").toString());
    if (m_selectedAudioDevice == device) {
        return;
    }
    m_selectedAudioDevice = device;

    if (m_process && m_process->state() != QProcess::NotRunning) {
        stopReceiver();
        startReceiver();
    }
}

void AirPlayBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value)
{
    Q_UNUSED(value);
    if (moduleId != QLatin1String(kModuleId)) {
        return;
    }
    if (key == QLatin1String("audio_output_device")) {
        applyAudioDeviceSetting();
    }
}
