#pragma once
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ShuffleBag.h"

// The channel model: a folder of episodes that knows what to play when you tune
// in, and what to play when an episode ends.
//
// Ported from NostalgiaBox's `channel.py` (MIT — see THIRD-PARTY.md). The
// "broadcast" tune-in mode is deliberately not ported yet; it needs per-episode
// durations from ffprobe and a duration cache to avoid stalling startup.

// What to hand mpv: a file, and how many seconds into it to begin.
struct PlayRequest {
    QString path;
    double  start = 0.0;
    bool    isValid() const { return !path.isEmpty(); }
};

struct ChannelConfig {
    int         number = 0;
    QString     name;
    QString     path;
    QStringList exclude;          // case-insensitive glob patterns
    QSet<int>   excludeSeasons;   // season numbers detected from the path
    // Folder of commercials for this channel specifically. Empty means fall back
    // to <commercials_directory>/<name>, then to the shared pool.
    QString     commercials;
    QString     bumpers;
    QString     idents;
};

// Best-effort season number from a path or filename (S06E01, "Season 6", 6x01).
// Returns -1 when nothing matches.
int detectSeason(const QString &text);

// Every playable episode under `root`, sorted stably, minus anything excluded.
QStringList scanEpisodes(const QString &root,
                         const QStringList &extensions,
                         bool recursive,
                         const QStringList &exclude,
                         const QSet<int> &excludeSeasons);

// The file extensions treated as "an episode".
QStringList defaultVideoExtensions();

class Channel {
public:
    // `tuneInMode` is "random" or "resume". Start offsets put the viewer a few
    // seconds into the episode so it feels like the show was already running.
    // `order` is "sequential" (airs in sorted order, wrapping at the end — the
    // default, and what a real channel airing a series does) or "random" (a
    // shuffle bag: every episode once, then reshuffle).
    Channel(const ChannelConfig &config,
            const QStringList &episodes,
            const QString &tuneInMode,
            double startOffsetMin,
            double startOffsetMax,
            const QString &order = QStringLiteral("sequential"));

    int     number() const { return m_config.number; }
    QString name()   const { return m_config.name;   }
    QString path()   const { return m_config.path;   }
    QString commercialsDir() const { return m_config.commercials; }
    QString bumpersDir()     const { return m_config.bumpers; }
    QString identsDir()      const { return m_config.idents; }
    // Switchable at runtime, from the guide's options row.
    void    setOrder(const QString &order);
    QString order() const { return m_order; }
    int     episodeCount() const { return m_episodes.size(); }
    bool    isEmpty()      const { return m_episodes.isEmpty(); }

    // What to play the instant a viewer switches to this channel.
    PlayRequest tuneIn();
    // What to play when the current episode ends naturally.
    PlayRequest advance();
    // -- broadcast mode ---------------------------------------------------
    // Lay this channel's episodes on a continuous timeline so it can report what
    // "would be airing" at any wall-clock moment — the illusion that the station
    // kept broadcasting while nobody was watching.
    //
    // The epoch is FIXED (not "now"), so the schedule is stable across restarts:
    // the channel has genuinely been running since that instant rather than
    // restarting whenever the box boots.
    void        buildSchedule(const QVector<double> &durations, double epoch);
    bool        hasSchedule() const { return !m_schedule.isEmpty(); }
    PlayRequest scheduledAt(double when);

    // Record where the viewer left off, for the "resume" tune-in mode.
    void remember(const QString &path, double position);

private:
    PlayRequest nextInOrder();
    // The next `count` episodes this channel will air, without consuming them —
    // what a TV guide needs to fill its time slots.
    public:
    QStringList upcoming(int count) const;
    QStringList episodes() const { return m_episodes; }
    // Jump the rotation to a specific episode (a guide selection).
    PlayRequest playSpecific(const QString &path);
    private:

    ChannelConfig m_config;
    QStringList   m_episodes;
    QString       m_tuneInMode;
    double        m_startOffsetMin = 0.0;
    double        m_startOffsetMax = 0.0;
    QString       m_order;
    int           m_seqIndex = 0;
    // Index of the episode most recently aired, so switching out of shuffle
    // can pick the series back up instead of restarting it.
    int           m_lastPlayedIndex = -1;
    ShuffleBag    m_bag;
    // Parallel arrays: the running order and how long each entry airs.
    QStringList   m_schedule;
    QVector<double> m_scheduleDurations;
    double        m_scheduleEpoch = 0.0;
    double        m_scheduleCycle = 0.0;
    QString       m_resumePath;
    double        m_resumePosition = 0.0;
};

// An ordered set of channels with remote-style navigation.
class ChannelLineup {
public:
    ChannelLineup() = default;
    explicit ChannelLineup(const QVector<Channel> &channels);

    int  size()    const { return m_channels.size(); }
    bool isEmpty() const { return m_channels.isEmpty(); }

    Channel       *current();
    const Channel *current() const;
    int  currentIndex() const { return m_index; }

    Channel *up();
    Channel *down();
    Channel *selectNumber(int number);
    Channel *selectIndex(int index);
    bool     hasNumber(int number) const;

    const QVector<Channel> &channels() const { return m_channels; }
    QVector<Channel> &mutableChannels() { return m_channels; }

private:
    QVector<Channel> m_channels;   // kept sorted by channel number, like a tuner
    int              m_index = 0;
};
