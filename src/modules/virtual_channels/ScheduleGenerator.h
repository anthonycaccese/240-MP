#pragma once
#include "ChannelSchedule.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVector>

namespace vchan {

struct MediaItem {
    QString    ref;
    SlotSource src = SlotSource::Local;
    QString    partKey;
    qint64     durMs = 0;
    QString    title;
    QString    series;
    QString    ep;
    QString    desc;
    QString    art;
    int        pack = -1;
    // When this first aired, as epoch ms; 0 when nothing could be learned.
    // Sources fall back from an episode's own date to its season's or show's
    // year, so "unknown" means the library really has nothing.
    qint64     airMs = 0;
    // The numbers behind `ep`. Sorting on the string put episode 100 before
    // episode 99, and a show numbered by year (Young Indiana Jones) not in any
    // sensible place at all.
    int        seasonNo  = -1;
    int        episodeNo = -1;
};

struct BreakPack {
    QString            name;
    QVector<MediaItem> intros;
    QVector<MediaItem> outros;
};

// How a channel lays its programmes out. Broadcast replaced an ordering that
// sorted by series title, which put a 1978 show before a 1953 one. AsListed is
// Broadcast without the sort: it airs the pool in the order the pool arrived,
// which is what a playlist means. A film channel is given it by the backend
// rather than the viewer -- the sources screen has no ordering row.
enum class Ordering { Broadcast, Shuffle, Interleaved, AsListed };

struct Appointment {
    QString name;
    QVector<int> days;
    int minuteOfDay = -1;
    QVector<MediaItem> pool;

    bool isValid() const;
    bool airsOn(int qtDayOfWeek) const;
};

Ordering orderingFromString(const QString &s);
QString  orderingToString(Ordering o);

struct ChannelDef {
    int     number = -1;
    QString name;
    quint32 seed = 1;
    double  horizonHours = 24.0;
    Ordering order = Ordering::Broadcast;

    int gridMinutes = 0;

    int adsPerBreak = 0;

    qint64  rotation = 0;

    // Where each series had got to, as the ref of the last episode it aired,
    // keyed by lowercased series name. Interleaved resumes every series from
    // its own mark, so editing the pool cannot shuffle the others' places.
    QHash<QString, QString> marks;

    // The same, for the one timeline Broadcast runs: the ref of the last
    // programme aired, whichever series it belonged to.
    QString mark;

    QVector<MediaItem> programmes;
    QVector<Appointment> appointments;
    QVector<MediaItem> intros;
    QVector<MediaItem> outros;
    QVector<MediaItem> commercials;
    QVector<MediaItem> bumps;
    QVector<BreakPack> packs;

    bool isPlayable() const;
};

QVector<Slot> generateSlots(const ChannelDef &def, qint64 startMs,
                            qint64 *endRotation = nullptr);

int minuteOfDayFromString(const QString &hhmm);

int dayOfWeekFromString(const QString &name);

QByteArray serializeSchedule(const ChannelDef &def,
                             qint64 generatedAt,
                             const QVector<Slot> &placed);

// The key an item with no series name of its own is grouped and marked under.
// The generator groups on it and the backend writes marks against it, so it
// lives here rather than in either: a silent disagreement would lose the place
// of everything unnamed, which on a real channel means the films.
inline QString unnamedSeriesKey() { return QStringLiteral("\x1f"); }

// When something first aired, as epoch ms, from whatever a library knows: an
// episode's own date where there is one, otherwise its season's or show's year,
// which is enough to keep it among its contemporaries. Zero means the library
// had nothing and the ordering falls back to alphabetical.
qint64 airedAtMs(const QString &isoDate, const QVariant &yearValue);

inline constexpr qint64 kMinFillerMs = 1000;

inline constexpr int kMaxSlotsPerChannel = 20000;
}
