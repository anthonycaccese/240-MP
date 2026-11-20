#pragma once
#include <QHash>
#include <QString>

namespace vchan {

class DurationProbe {
public:
    explicit DurationProbe(const QString &dataRoot);

    qint64 durationMs(const QString &absPath);

    bool isUsable() const { return !m_ffprobe.isEmpty() || !m_mpv.isEmpty(); }

    QString proberName() const;

    void save() const;
    void load();

    int  cachedCount() const { return m_cache.size(); }
    void clearCache() { m_cache.clear(); }

private:
    struct Entry {
        qint64 durMs = 0;
        qint64 size  = 0;
        qint64 mtime = 0;
    };

    QString m_dataRoot;
    QString m_ffprobe;
    QString m_mpv;
    QHash<QString, Entry> m_cache;
    mutable bool m_dirty = false;

    QString cachePath() const;
    qint64  probeWithFfprobe(const QString &absPath) const;
    qint64  probeWithMpv(const QString &absPath) const;
};
}
