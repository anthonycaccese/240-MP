#include "AmbientModeBackend.h"
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
    m_mediaRoot = path;
    QDir().mkpath(path);
    qDebug("[AmbientMode] media root: %s", qPrintable(path));
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

void AmbientModeBackend::startAudio(const QString &path)
{
    stopAudio();

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
    args << path
         << QStringLiteral("--no-video")
         << QStringLiteral("--loop-playlist=inf")
         << QStringLiteral("--no-terminal")
         << QStringLiteral("--really-quiet");

    m_audioProcess = new QProcess(this);
    m_audioProcess->start(bin, args);
    qDebug("[AmbientMode] audio process started: %s", qPrintable(path));
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
