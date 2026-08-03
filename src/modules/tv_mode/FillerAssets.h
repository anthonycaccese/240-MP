#pragma once
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QVector>

class QProcess;

// The nostalgic filler clips: analog snow, a digital glitch burst, and SMPTE
// colour bars. These are what make a channel change feel like a TV rather than a
// video player, and what an empty channel shows instead of a black screen.
//
// Ported from NostalgiaBox's `static_gen.py` (MIT — see THIRD-PARTY.md), with two
// differences:
//
//   * Rendered at the composite framebuffer's own 720x480 rather than 1280x720,
//     so the Pi never scales them at playback time.
//   * Generated ASYNCHRONOUSLY. NostalgiaBox produces them from its installer;
//     240-MP has no such step, and the snow filter is slow enough on a Pi that
//     doing it inline would visibly stall the first channel change. Transitions
//     simply don't apply until the clips exist.
//
// Everything degrades gracefully: no ffmpeg, or a failed render, means playback
// continues without the effect.
class FillerAssets : public QObject {
    Q_OBJECT
public:
    explicit FillerAssets(const QString &dataRoot, QObject *parent = nullptr);

    // Queue generation of whatever is missing. Safe to call repeatedly; does
    // nothing when everything already exists or ffmpeg is unavailable.
    void ensureAsync(int width = 720, int height = 480);

    // Absolute paths, or an empty string when that clip is not (yet) present.
    QString staticPath()    const;
    QString glitchPath()    const;
    QString colorbarsPath() const;

signals:
    // Emitted once the queue drains, whether or not every clip succeeded.
    void ready();

private:
    QString pathFor(const QString &fileName) const;
    QString existing(const QString &fileName) const;
    void    runNext();

    QString  m_dir;
    QProcess *m_proc = nullptr;
    // Pending jobs: (output path, ffmpeg arguments).
    QVector<QPair<QString, QStringList>> m_queue;
};
