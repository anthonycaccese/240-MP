#include "DurationCache.h"

#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

DurationCache::DurationCache(const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
{
    load();
}

QString DurationCache::keyFor(const QString &path) { return path; }

void DurationCache::load() {
    QFile f(m_dataRoot + "/tv_durations.json");
    if (!f.open(QFile::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.size    = qint64(o.value("size").toDouble());
        e.mtime   = qint64(o.value("mtime").toDouble());
        e.seconds = o.value("seconds").toDouble();
        if (e.seconds > 0.0)
            m_entries.insert(it.key(), e);
    }
    qInfo("[tv_mode] duration cache: %lld entr(ies)",
          static_cast<long long>(m_entries.size()));
}

void DurationCache::save() const {
    QJsonObject root;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        QJsonObject o;
        o.insert("size",    double(it.value().size));
        o.insert("mtime",   double(it.value().mtime));
        o.insert("seconds", it.value().seconds);
        root.insert(it.key(), o);
    }
    QFile f(m_dataRoot + "/tv_durations.json");
    if (f.open(QFile::WriteOnly | QFile::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

double DurationCache::durationFor(const QString &path) const {
    const auto it = m_entries.constFind(keyFor(path));
    if (it != m_entries.constEnd() && it.value().seconds > 0.0)
        return it.value().seconds;
    return kDefaultSeconds;
}

void DurationCache::probeAsync(const QStringList &paths) {
    if (m_noFfprobe)
        return;
    if (QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty()) {
        m_noFfprobe = true;
        qWarning("[tv_mode] ffprobe not found — broadcast mode will assume %.0f-minute "
                 "episodes (install ffmpeg for real schedules)", kDefaultSeconds / 60.0);
        return;
    }

    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (!fi.isFile())
            continue;
        const auto it = m_entries.constFind(keyFor(p));
        // Re-probe when the file changed underneath us (re-encode, replacement).
        if (it != m_entries.constEnd()
            && it.value().size == fi.size()
            && it.value().mtime == fi.lastModified().toSecsSinceEpoch())
            continue;
        if (!m_pending.contains(p))
            m_pending.enqueue(p);
    }
    if (!m_pending.isEmpty() && !m_proc) {
        qInfo("[tv_mode] probing %lld episode duration(s) in the background",
              static_cast<long long>(m_pending.size()));
        probeNext();
    }
}

void DurationCache::probeNext() {
    if (m_pending.isEmpty()) {
        if (m_proc) {
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        if (m_learned) {
            m_learned = false;
            save();
            qInfo("[tv_mode] duration probing complete");
            emit updated();
        }
        return;
    }

    const QString path = m_pending.dequeue();
    if (!m_proc) {
        m_proc = new QProcess(this);
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus) {
                    if (code == 0) {
                        const double secs = m_proc->readAllStandardOutput()
                                                .trimmed().toDouble();
                        if (secs > 0.0 && !m_current.isEmpty()) {
                            const QFileInfo fi(m_current);
                            Entry e;
                            e.size    = fi.size();
                            e.mtime   = fi.lastModified().toSecsSinceEpoch();
                            e.seconds = secs;
                            m_entries.insert(keyFor(m_current), e);
                            m_learned = true;
                        }
                    }
                    probeNext();
                });
    }
    m_current = path;
    m_proc->start(QStandardPaths::findExecutable(QStringLiteral("ffprobe")),
                  { "-v", "error", "-show_entries", "format=duration",
                    "-of", "default=noprint_wrappers=1:nokey=1", path });
}
