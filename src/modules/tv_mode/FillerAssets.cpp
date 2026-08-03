#include "FillerAssets.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {
const char *kStatic    = "static.mp4";
const char *kGlitch    = "glitch.mp4";
const char *kColorbars = "colorbars.mp4";
}  // namespace

FillerAssets::FillerAssets(const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_dir(dataRoot + "/tv_assets")
{}

QString FillerAssets::pathFor(const QString &fileName) const {
    return m_dir + "/" + fileName;
}

QString FillerAssets::existing(const QString &fileName) const {
    const QString p = pathFor(fileName);
    return QFileInfo(p).isFile() ? p : QString();
}

QString FillerAssets::staticPath()    const { return existing(QString::fromLatin1(kStatic)); }
QString FillerAssets::glitchPath()    const { return existing(QString::fromLatin1(kGlitch)); }
QString FillerAssets::colorbarsPath() const { return existing(QString::fromLatin1(kColorbars)); }

void FillerAssets::ensureAsync(int width, int height) {
    if (m_proc)   // already generating
        return;

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        qWarning("[tv_mode] ffmpeg not found — channel transitions and colour bars "
                 "will be skipped (playback is unaffected)");
        return;
    }
    QDir().mkpath(m_dir);

    const QString size = QStringLiteral("%1x%2").arg(width).arg(height);

    // Analog snow. A full second is rendered even though only a fraction shows,
    // so the brief clip never reveals a loop seam. Silent on purpose: channel
    // changes shouldn't hiss.
    if (staticPath().isEmpty()) {
        m_queue.append({ pathFor(QString::fromLatin1(kStatic)), QStringList{
            "-y", "-loglevel", "error",
            "-f", "lavfi", "-i", QStringLiteral("nullsrc=s=%1:r=25:d=1").arg(size),
            "-vf", "geq=lum='random(1)*255':cb=128:cr=128,format=yuv420p",
            "-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p",
            "-an", pathFor(QString::fromLatin1(kStatic)) } });
    }

    // Digital glitch: chunky coloured macroblocks, made by scaling a tiny random
    // frame up with nearest-neighbour.
    if (glitchPath().isEmpty()) {
        m_queue.append({ pathFor(QString::fromLatin1(kGlitch)), QStringList{
            "-y", "-loglevel", "error",
            "-f", "lavfi", "-i", "nullsrc=s=96x54:r=25:d=0.6",
            "-vf", QStringLiteral("geq=r='random(1)*255':g='random(2)*255':b='random(3)*255',"
                                  "scale=%1:%2:flags=neighbor,format=yuv420p")
                       .arg(width).arg(height),
            "-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p",
            "-an", pathFor(QString::fromLatin1(kGlitch)) } });
    }

    // SMPTE bars with a 1 kHz tone, for empty channels and "no signal".
    if (colorbarsPath().isEmpty()) {
        m_queue.append({ pathFor(QString::fromLatin1(kColorbars)), QStringList{
            "-y", "-loglevel", "error",
            "-f", "lavfi", "-i", QStringLiteral("smptehdbars=s=%1:r=25:d=6").arg(size),
            "-f", "lavfi", "-i", "sine=frequency=1000:duration=6:sample_rate=48000",
            "-af", "volume=0.1",
            "-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "96k", "-shortest",
            pathFor(QString::fromLatin1(kColorbars)) } });
    }

    if (m_queue.isEmpty()) {
        emit ready();
        return;
    }
    qInfo("[tv_mode] generating %lld filler clip(s) in the background",
          static_cast<long long>(m_queue.size()));
    runNext();
}

void FillerAssets::runNext() {
    if (m_queue.isEmpty()) {
        if (m_proc) {
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        qInfo("[tv_mode] filler clips ready");
        emit ready();
        return;
    }

    const auto job = m_queue.takeFirst();
    if (!m_proc) {
        m_proc = new QProcess(this);
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus) {
                    if (code != 0) {
                        // Cosmetic assets only — log and carry on rather than
                        // letting a render failure break playback.
                        qWarning("[tv_mode] a filler clip failed to render (ffmpeg exit %d)", code);
                    }
                    runNext();
                });
    }
    m_proc->start(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")), job.second);
}
