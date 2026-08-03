#pragma once
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>

class QProcess;

// Episode durations, probed with ffprobe and cached on disk.
//
// Broadcast mode needs to know how long every episode runs so it can lay them on
// a real timeline. NostalgiaBox probes synchronously on first tune-in, which is
// fine for a handful of local files but stalls hard on a large library — at
// ~50-150 ms per file, a 500-episode channel is a minute of frozen UI.
//
// So this probes in the BACKGROUND, one file at a time, and persists results to
// $DATA_ROOT/tv_durations.json keyed by path + size + mtime (so a re-encoded file
// is re-probed). Callers get a default until the real value lands, and the
// schedule is rebuilt when probing finishes.
class DurationCache : public QObject {
    Q_OBJECT
public:
    // A typical episode, used until the real duration is known.
    static constexpr double kDefaultSeconds = 22 * 60.0;

    explicit DurationCache(const QString &dataRoot, QObject *parent = nullptr);

    // Cached duration in seconds, or kDefaultSeconds if not yet known.
    double durationFor(const QString &path) const;
    // True once every path passed to probeAsync has a real value.
    bool   isComplete() const { return m_pending.isEmpty(); }

    // Queue any paths whose duration is unknown or stale. Returns immediately.
    void probeAsync(const QStringList &paths);

signals:
    // Emitted when the queue drains and at least one new duration was learned,
    // so schedules can be rebuilt with real numbers.
    void updated();

private:
    struct Entry { qint64 size = 0; qint64 mtime = 0; double seconds = 0.0; };

    void load();
    void save() const;
    void probeNext();
    static QString keyFor(const QString &path);

    QString              m_dataRoot;
    QHash<QString, Entry> m_entries;   // absolute path -> entry
    QQueue<QString>      m_pending;
    QString              m_current;   // file being probed right now
    QProcess            *m_proc    = nullptr;
    bool                 m_learned = false;
    bool                 m_noFfprobe = false;
};
