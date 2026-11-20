#include "DurationProbe.h"

#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace vchan {

namespace {

constexpr int kProbeTimeoutMs = 15000;

constexpr qint64 kMinPlausibleMs = 100;
constexpr qint64 kMaxPlausibleMs = 24LL * 3600 * 1000;
}

DurationProbe::DurationProbe(const QString &dataRoot) : m_dataRoot(dataRoot) {
    m_ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    m_mpv     = QStandardPaths::findExecutable(QStringLiteral("mpv"));
    load();
}

QString DurationProbe::proberName() const {
    if (!m_ffprobe.isEmpty()) return QStringLiteral("ffprobe");
    if (!m_mpv.isEmpty())     return QStringLiteral("mpv");
    return QStringLiteral("none");
}

QString DurationProbe::cachePath() const {
    return m_dataRoot + QStringLiteral("/channels/duration-cache.json");
}

qint64 DurationProbe::durationMs(const QString &absPath) {
    const QFileInfo fi(absPath);
    if (!fi.isFile()) return 0;

    const qint64 size  = fi.size();
    const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();

    const auto it = m_cache.constFind(absPath);
    if (it != m_cache.constEnd() && it->size == size && it->mtime == mtime)
        return it->durMs;

    qint64 ms = 0;
    if (!m_ffprobe.isEmpty()) ms = probeWithFfprobe(absPath);
    if (ms <= 0 && !m_mpv.isEmpty()) ms = probeWithMpv(absPath);

    if (ms < kMinPlausibleMs || ms > kMaxPlausibleMs)
        ms = 0;

    Entry e;
    e.durMs = ms;
    e.size  = size;
    e.mtime = mtime;
    m_cache.insert(absPath, e);
    m_dirty = true;
    return ms;
}

qint64 DurationProbe::probeWithFfprobe(const QString &absPath) const {
    QProcess p;
    p.start(m_ffprobe, {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
        QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"),
        absPath
    });
    if (!p.waitForFinished(kProbeTimeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return 0;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return 0;

    bool ok = false;
    const double secs = QString::fromUtf8(p.readAllStandardOutput()).trimmed().toDouble(&ok);
    return ok ? qint64(secs * 1000.0) : 0;
}

qint64 DurationProbe::probeWithMpv(const QString &absPath) const {
    QProcess p;
    p.start(m_mpv, {
        QStringLiteral("--no-config"),
        QStringLiteral("--vo=null"),
        QStringLiteral("--ao=null"),
        QStringLiteral("--frames=0"),
        QStringLiteral("--no-terminal"),
        QStringLiteral("--print-text=DURATION=${=duration}"),
        absPath
    });
    if (!p.waitForFinished(kProbeTimeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return 0;
    }

    const QString out = QString::fromUtf8(p.readAllStandardOutput())
                      + QString::fromUtf8(p.readAllStandardError());
    const int at = out.indexOf(QLatin1String("DURATION="));
    if (at < 0) return 0;

    QString num = out.mid(at + 9).section(QLatin1Char('\n'), 0, 0).trimmed();
    bool ok = false;
    const double secs = num.toDouble(&ok);
    return ok ? qint64(secs * 1000.0) : 0;
}

// ---------------------------------------------------------------------------
// Cache persistence
// ---------------------------------------------------------------------------

void DurationProbe::load() {
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.durMs = qint64(o.value(QLatin1String("dur")).toDouble(0));
        e.size  = qint64(o.value(QLatin1String("size")).toDouble(0));
        e.mtime = qint64(o.value(QLatin1String("mtime")).toDouble(0));
        if (e.durMs > 0 && e.size > 0)
            m_cache.insert(it.key(), e);
    }
    m_dirty = false;
}

void DurationProbe::save() const {
    if (!m_dirty) return;

    QDir().mkpath(QFileInfo(cachePath()).absolutePath());

    QJsonObject root;
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
        if (it->durMs <= 0) continue;
        QJsonObject o;
        o["dur"]   = double(it->durMs);
        o["size"]  = double(it->size);
        o["mtime"] = double(it->mtime);
        root.insert(it.key(), o);
    }

    QSaveFile f(cachePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (f.commit())
        m_dirty = false;
}
}
