#include "AmbientModeBackend.h"
#include "../../linux_audio_utils.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>

static const QStringList kVideoExts = {
    "mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv", "flv", "f4v", "mpg", "mpeg", "vob"
};
static const QStringList kAudioExts = {
    "mp3", "wav", "flac", "m4a", "m3u", "ogg", "aac", "m3u8"
};

AmbientModeBackend::AmbientModeBackend(const QString &dataRoot, QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot), m_mediaRoot(dataRoot + "/ambient")
{
    // Resolve the configured media directory (falls back to the dataRoot/ambient default).
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonObject cfg = QJsonDocument::fromJson(f.readAll()).object();
        QString dir = cfg["modules"].toObject()["com.240mp.ambient_mode"].toObject()
                          ["media_directory"].toString();
        if (!dir.isEmpty())
            setMediaRoot(dir);
    }
}

AmbientModeBackend::~AmbientModeBackend()
{
    stopAudio();
}

QString AmbientModeBackend::mediaRoot() const
{
    return m_mediaRoot;
}

void AmbientModeBackend::setMediaRoot(const QString &path)
{
    // An empty (reset) setting means back to the dataRoot/ambient default.
    m_mediaRoot = path.isEmpty() ? m_dataRoot + "/ambient" : path;
    QDir().mkpath(m_mediaRoot);
    qDebug("[AmbientMode] media root: %s", qPrintable(m_mediaRoot));
}

QVariantList AmbientModeBackend::scanFiles(const QStringList &extensions) const
{
    QVariantList result;
    QDir dir(m_mediaRoot);
    if (!dir.exists())
        return result;
    for (const QString &name : dir.entryList(QDir::Files, QDir::Name)) {
        if (!extensions.contains(QFileInfo(name).suffix().toLower()))
            continue;
        QVariantMap item;
        item["name"] = name;
        item["path"] = dir.absoluteFilePath(name);
        result.append(item);
    }
    return result;
}

QVariantList AmbientModeBackend::getVideoFiles() const
{
    return scanFiles(kVideoExts);
}

QVariantList AmbientModeBackend::getAudioFiles() const
{
    return scanFiles(kAudioExts);
}

void AmbientModeBackend::startAudio(const QStringList &paths, bool shuffle)
{
    m_audioStopRequested = true; // covers the stopAudio() call below
    stopAudio();

    if (paths.isEmpty())
        return;

    m_audioPaths   = paths;
    m_audioShuffle = shuffle;

#ifdef Q_OS_MACOS
    {
        const QStringList extraPaths = { "/opt/homebrew/bin", "/usr/local/bin" };
        const QStringList current = qEnvironmentVariable("PATH").split(":");
        for (const QString &p : extraPaths) {
            if (!current.contains(p))
                qputenv("PATH", (p + ":" + qEnvironmentVariable("PATH")).toUtf8());
        }
    }
#endif

    const QString bin = QStandardPaths::findExecutable("mpv");
    if (bin.isEmpty()) {
        qWarning("[AmbientMode] mpv not found in PATH — audio will not play");
        return;
    }

    QStringList args;
    args << paths
         << QStringLiteral("--no-video")
         << QStringLiteral("--loop-playlist=inf");
    if (shuffle)
        args << QStringLiteral("--shuffle");
#ifdef Q_OS_LINUX
    {
        const QString audioDevice = detectAlsaAudioDevice();
        if (!audioDevice.isEmpty())
            args << QStringLiteral("--audio-device=%1").arg(audioDevice);
    }
#endif
    args << QStringLiteral("--no-terminal")
         << QStringLiteral("--really-quiet");

    m_audioProcess = new QProcess(this);
    connect(m_audioProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AmbientModeBackend::onAudioProcessFinished);
    m_audioProcess->start(bin, args);
    m_audioStopRequested = false;
    qDebug("[AmbientMode] audio process started: %d track(s), shuffle=%d", paths.size(), shuffle);
}

void AmbientModeBackend::stopAudio()
{
    m_audioStopRequested = true;
    m_audioRespawnCount  = 0;
    if (!m_audioProcess)
        return;
    if (m_audioProcess->state() != QProcess::NotRunning) {
        m_audioProcess->terminate();
        m_audioProcess->waitForFinished(1000);
    }
    m_audioProcess->deleteLater();
    m_audioProcess = nullptr;
    qDebug("[AmbientMode] audio process stopped");
}

// The companion audio track has no video fallback to lean on, so if its mpv
// process dies on its own (an ALSA hiccup, a transient device stall), the
// screen keeps playing but goes silent with nothing to notice or recover.
// Mirrors NfcReaderBackend's bounded-respawn watchdog for the same class of
// "external process wedged/died mid-session" failure.
static constexpr int kMaxAudioRespawns = 5;

void AmbientModeBackend::onAudioProcessFinished()
{
    if (m_audioStopRequested || m_audioPaths.isEmpty())
        return;

    if (m_audioRespawnCount >= kMaxAudioRespawns) {
        qWarning("[AmbientMode] audio process keeps dying, giving up until next playback session");
        return;
    }
    m_audioRespawnCount++;
    qWarning("[AmbientMode] audio process exited unexpectedly, restarting (attempt %d/%d)",
              m_audioRespawnCount, kMaxAudioRespawns);
    const QStringList paths = m_audioPaths;
    const bool shuffle = m_audioShuffle;
    QTimer::singleShot(1000, this, [this, paths, shuffle]() { startAudio(paths, shuffle); });
}

void AmbientModeBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value)
{
    if (moduleId == QLatin1String("com.240mp.ambient_mode") && key == QLatin1String("media_directory"))
        setMediaRoot(value.toString());
}
