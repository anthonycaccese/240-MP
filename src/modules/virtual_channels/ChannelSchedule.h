#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

namespace vchan {

enum class SlotKind   { Programme, Commercial, Bump, Intro, Outro, Filler, Unknown };

inline bool leadsProgramme(SlotKind k) { return k == SlotKind::Intro; }

enum class SlotSource { Local, Plex, Jellyfin, Emby };

inline bool isServerSource(SlotSource s) { return s != SlotSource::Local; }

struct Slot {
    qint64     start = 0;
    qint64     dur   = 0;
    SlotKind   kind  = SlotKind::Unknown;
    SlotSource src   = SlotSource::Local;
    QString    ref;
    QString    partKey;
    QString    title;
    QString    series;
    QString    ep;
    QString    desc;
    QString    art;

    qint64 end() const { return start + dur; }
};

enum class Status {
    Ok,
    NoSchedule,
    Malformed,
    ClockUnsane,
    BeforeStart,
    PastHorizon,
    OffAirGap
};

inline constexpr qint64 kRunWindowMs   = 30LL * 60LL * 1000LL;
inline constexpr int    kMaxRunItems   = 20;

inline constexpr qint64 kMinSaneEpochMs = 1600000000000LL;

class ChannelSchedule {
public:
    ChannelSchedule() = default;

    static ChannelSchedule fromJson(const QByteArray &json,
                                    QString *error = nullptr,
                                    int *droppedSlots = nullptr);

    static ChannelSchedule load(const QString &path,
                                QString *error = nullptr,
                                int *droppedSlots = nullptr);

    bool    isValid()     const { return m_valid; }
    int     channel()     const { return m_channel; }
    quint32 seed()        const { return m_seed; }
    qint64  generatedAt() const { return m_generatedAt; }
    qint64  horizonEnd()  const { return m_horizonEnd; }
    qint64  rotation()    const { return m_rotation; }
    const QHash<QString, QString> &marks() const { return m_marks; }
    const QString &mark()                  const { return m_mark; }

    const QVector<Slot> &slotList() const { return m_slots; }
    int   slotCount()               const { return m_slots.size(); }
    bool  isEmpty()                 const { return m_slots.isEmpty(); }

    int indexAt(qint64 nowMs) const;

    Status statusAt(qint64 nowMs) const;

    qint64 offsetInto(int index, qint64 nowMs) const;

    QVector<int> runWindow(int startIndex,
                           qint64 windowMs = kRunWindowMs,
                           int    maxItems = kMaxRunItems) const;

    struct Block {
        qint64  start = 0;
        qint64  dur   = 0;
        int     slotIndex = -1;
        QString title;
        QString series;
        QString ep;
        int     count = 1;

        qint64 end() const { return start + dur; }
    };

    QVector<Block> programmeBlocks(qint64 fromMs, qint64 toMs,
                                   qint64 minDurMs = 0) const;

    bool blockAt(qint64 nowMs, Block *out = nullptr) const;

private:
    bool          m_valid       = false;
    int           m_channel     = -1;
    quint32       m_seed        = 0;
    qint64        m_generatedAt = 0;
    qint64        m_horizonEnd  = 0;
    qint64        m_rotation    = 0;
    QHash<QString, QString> m_marks;
    QString       m_mark;
    QVector<Slot> m_slots;
};

bool clockLooksSane(qint64 nowMs, qint64 generatedAt);

SlotKind   slotKindFromString(const QString &s);
QString    slotKindToString(SlotKind k);
SlotSource slotSourceFromString(const QString &s);
QString    slotSourceToString(SlotSource s);
}
