#include "AmbientModeBackend.h"
#include "../../util/MpvLocator.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
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
    stopAudio();

    if (paths.isEmpty())
        return;

    const QString bin = mpvbin::locate();

    if (bin.isEmpty()) {
        qWarning("[AmbientMode] mpv not found in PATH — audio will not play");
        return;
    }

    QStringList args;
    // Playlist entries first — absolute paths from scanFiles(), so they can't be
    // mistaken for flags. An .m3u/.m3u8 counts as ONE entry here: mpv expands it
    // lazily when playback reaches it, so shuffling picks whole playlists as units
    // and the tracks inside one still play in their listed order.
    args << paths
         << QStringLiteral("--no-video")
         << QStringLiteral("--loop-playlist=inf")
         << QStringLiteral("--no-terminal")
         << QStringLiteral("--really-quiet");
    // mpv shuffles once, at load — with --loop-playlist=inf the same shuffled order
    // replays on every wrap rather than being reshuffled each cycle.
    if (shuffle)
        args << QStringLiteral("--shuffle");

    m_audioProcess = new QProcess(this);
    m_audioProcess->start(bin, args);
    qDebug("[AmbientMode] audio process started: %lld entr%s%s",
           static_cast<long long>(paths.size()),
           paths.size() == 1 ? "y" : "ies",
           shuffle ? ", shuffled" : "");
}

void AmbientModeBackend::stopAudio()
{
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

void AmbientModeBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value)
{
    if (moduleId == QLatin1String("com.240mp.ambient_mode") && key == QLatin1String("media_directory"))
        setMediaRoot(value.toString());
}
