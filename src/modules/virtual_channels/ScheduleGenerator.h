#pragma once
#include "ChannelSchedule.h"

#include <QByteArray>
#include <QString>
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
};

struct BreakPack {
    QString            name;
    QVector<MediaItem> intros;
    QVector<MediaItem> outros;
};

enum class Ordering { Sequential, Shuffle };

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
    Ordering order = Ordering::Sequential;

    int gridMinutes = 0;

    int adsPerBreak = 0;

    qint64  rotation = 0;

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

inline constexpr qint64 kMinFillerMs = 1000;

inline constexpr int kMaxSlotsPerChannel = 20000;
}
