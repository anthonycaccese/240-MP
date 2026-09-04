#include "ChannelSchedule.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>

namespace vchan {

namespace {

constexpr qint64 kClockSkewToleranceMs = 5LL * 60LL * 1000LL;
}

// ---------------------------------------------------------------------------
// Enum <-> string
//
// Unknown/Local are the fallbacks rather than a parse failure: an unrecognised
// kind from a newer generator should still play as an ordinary slot, not take
// the channel off air.
// ---------------------------------------------------------------------------

SlotKind slotKindFromString(const QString &s) {
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("programme") || v == QLatin1String("program")) return SlotKind::Programme;
    if (v == QLatin1String("commercial"))                                 return SlotKind::Commercial;
    if (v == QLatin1String("bump"))                                       return SlotKind::Bump;
    if (v == QLatin1String("intro"))                                      return SlotKind::Intro;
    if (v == QLatin1String("outro"))                                      return SlotKind::Outro;
    if (v == QLatin1String("filler"))                                     return SlotKind::Filler;
    return SlotKind::Unknown;
}

QString slotKindToString(SlotKind k) {
    switch (k) {
    case SlotKind::Programme:  return QStringLiteral("programme");
    case SlotKind::Commercial: return QStringLiteral("commercial");
    case SlotKind::Bump:       return QStringLiteral("bump");
    case SlotKind::Intro:      return QStringLiteral("intro");
    case SlotKind::Outro:      return QStringLiteral("outro");
    case SlotKind::Filler:     return QStringLiteral("filler");
    case SlotKind::Unknown:    break;
    }
    return QStringLiteral("unknown");
}

SlotSource slotSourceFromString(const QString &s) {
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("plex"))     return SlotSource::Plex;
    if (v == QLatin1String("jellyfin")) return SlotSource::Jellyfin;
    if (v == QLatin1String("emby"))     return SlotSource::Emby;
    return SlotSource::Local;
}

QString slotSourceToString(SlotSource s) {
    switch (s) {
    case SlotSource::Plex:     return QStringLiteral("plex");
    case SlotSource::Jellyfin: return QStringLiteral("jellyfin");
    case SlotSource::Emby:     return QStringLiteral("emby");
    case SlotSource::Local:    break;
    }
    return QStringLiteral("local");
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

ChannelSchedule ChannelSchedule::fromJson(const QByteArray &json,
                                          QString *error,
                                          int *droppedSlots) {
    if (droppedSlots) *droppedSlots = 0;

    const auto fail = [&](const QString &why) {
        if (error) *error = why;
        return ChannelSchedule{};
    };

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError)
        return fail(QStringLiteral("json parse error at offset %1: %2")
                        .arg(perr.offset).arg(perr.errorString()));
    if (!doc.isObject())
        return fail(QStringLiteral("top level is not an object"));

    const QJsonObject root = doc.object();

    ChannelSchedule s;
    s.m_channel = root.value(QLatin1String("channel")).toInt(-1);
    if (s.m_channel < 0)
        return fail(QStringLiteral("missing or invalid \"channel\""));

    s.m_seed        = static_cast<quint32>(root.value(QLatin1String("seed")).toDouble(0));
    s.m_generatedAt = static_cast<qint64>(root.value(QLatin1String("generated_at")).toDouble(0));
    s.m_horizonEnd  = static_cast<qint64>(root.value(QLatin1String("horizon_end")).toDouble(0));
    s.m_rotation    = static_cast<qint64>(root.value(QLatin1String("rotation")).toDouble(0));
    s.m_mark        = root.value(QLatin1String("mark")).toString();

    const QJsonObject marks = root.value(QLatin1String("marks")).toObject();
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
        const QString ref = it.value().toString();
        if (!ref.isEmpty()) s.m_marks.insert(it.key(), ref);
    }

    const QJsonValue slotsVal = root.value(QLatin1String("slots"));
    if (!slotsVal.isArray())
        return fail(QStringLiteral("missing or invalid \"slots\" array"));

    const QJsonArray arr = slotsVal.toArray();
    s.m_slots.reserve(arr.size());

    int drops = 0;
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) { ++drops; continue; }
        const QJsonObject o = v.toObject();

        Slot slot;
        slot.start = static_cast<qint64>(o.value(QLatin1String("start")).toDouble(0));
        slot.dur   = static_cast<qint64>(o.value(QLatin1String("dur")).toDouble(0));

        if (slot.start <= 0 || slot.dur <= 0) { ++drops; continue; }

        slot.ref  = o.value(QLatin1String("ref")).toString().trimmed();
        slot.kind = slotKindFromString(o.value(QLatin1String("kind")).toString());

        if (slot.ref.isEmpty() && slot.kind != SlotKind::Filler) { ++drops; continue; }
        slot.src     = slotSourceFromString(o.value(QLatin1String("src")).toString());
        slot.partKey = o.value(QLatin1String("part_key")).toString();
        slot.title   = o.value(QLatin1String("title")).toString();
        slot.series  = o.value(QLatin1String("series")).toString();
        slot.ep      = o.value(QLatin1String("ep")).toString();
        slot.desc    = o.value(QLatin1String("desc")).toString();
        slot.art     = o.value(QLatin1String("art")).toString();

        s.m_slots.push_back(slot);
    }

    std::sort(s.m_slots.begin(), s.m_slots.end(),
              [](const Slot &a, const Slot &b) { return a.start < b.start; });

    if (!s.m_slots.isEmpty()) {
        QVector<Slot> kept;
        kept.reserve(s.m_slots.size());
        kept.push_back(s.m_slots.first());
        for (int i = 1; i < s.m_slots.size(); ++i) {
            if (s.m_slots[i].start < kept.last().end()) { ++drops; continue; }
            kept.push_back(s.m_slots[i]);
        }
        s.m_slots = std::move(kept);
    }

    if (droppedSlots) *droppedSlots = drops;

    if (s.m_slots.isEmpty())
        return fail(QStringLiteral("no usable slots (%1 dropped)").arg(drops));

    s.m_horizonEnd = s.m_slots.last().end();

    s.m_valid = true;
    if (error) error->clear();
    return s;
}

ChannelSchedule ChannelSchedule::load(const QString &path,
                                      QString *error,
                                      int *droppedSlots) {
    if (droppedSlots) *droppedSlots = 0;

    QFile f(path);
    if (!f.exists()) {
        if (error) *error = QStringLiteral("no schedule file at %1").arg(path);
        return {};
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return {};
    }
    return fromJson(f.readAll(), error, droppedSlots);
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

int ChannelSchedule::indexAt(qint64 nowMs) const {
    if (m_slots.isEmpty()) return -1;

    const auto it = std::upper_bound(
        m_slots.cbegin(), m_slots.cend(), nowMs,
        [](qint64 t, const Slot &s) { return t < s.start; });

    if (it == m_slots.cbegin()) return -1;

    const int idx = static_cast<int>(it - m_slots.cbegin()) - 1;
    return nowMs < m_slots[idx].end() ? idx : -1;
}

Status ChannelSchedule::statusAt(qint64 nowMs) const {
    if (!m_valid || m_slots.isEmpty())            return Status::NoSchedule;
    if (!clockLooksSane(nowMs, m_generatedAt))    return Status::ClockUnsane;
    if (nowMs <  m_slots.first().start)           return Status::BeforeStart;
    if (nowMs >= m_slots.last().end())            return Status::PastHorizon;
    return indexAt(nowMs) >= 0 ? Status::Ok : Status::OffAirGap;
}

qint64 ChannelSchedule::offsetInto(int index, qint64 nowMs) const {
    if (index < 0 || index >= m_slots.size()) return 0;
    const Slot &s = m_slots[index];
    const qint64 off = nowMs - s.start;
    if (off <= 0)      return 0;
    if (off >= s.dur)  return s.dur - 1;
    return off;
}

QVector<int> ChannelSchedule::runWindow(int startIndex,
                                        qint64 windowMs,
                                        int maxItems) const {
    QVector<int> out;
    if (startIndex < 0 || startIndex >= m_slots.size()) return out;
    if (maxItems < 1) maxItems = 1;

    qint64 accumulated = 0;
    for (int i = startIndex; i < m_slots.size() && out.size() < maxItems; ++i) {
        if (i > startIndex && m_slots[i].start != m_slots[i - 1].end()) break;

        out.push_back(i);
        accumulated += m_slots[i].dur;
        if (accumulated >= windowMs) break;
    }
    return out;
}

QVector<ChannelSchedule::Block> ChannelSchedule::programmeBlocks(qint64 fromMs,
                                                                 qint64 toMs,
                                                                 qint64 minDurMs) const {
    QVector<Block> blocks;
    if (m_slots.isEmpty() || toMs <= fromMs) return blocks;

    QVector<int> anchors;
    for (int i = 0; i < m_slots.size(); ++i)
        if (m_slots[i].kind == SlotKind::Programme)
            anchors.push_back(i);

    if (anchors.isEmpty()) return blocks;

    for (int a = 0; a < anchors.size(); ++a) {
        const int p = anchors[a];

        int startIdx = p;
        const int floorIdx = (a > 0) ? anchors[a - 1] + 1 : 0;
        while (startIdx > floorIdx && leadsProgramme(m_slots[startIdx - 1].kind))
            --startIdx;

        Block b;
        b.start     = m_slots[startIdx].start;
        b.slotIndex = p;
        b.title     = m_slots[p].title;
        b.series    = m_slots[p].series;
        b.ep        = m_slots[p].ep;

        if (a + 1 < anchors.size()) {
            int nextStart = anchors[a + 1];
            const int nextFloor = p + 1;
            while (nextStart > nextFloor && leadsProgramme(m_slots[nextStart - 1].kind))
                --nextStart;
            b.dur = m_slots[nextStart].start - b.start;
        } else {
            b.dur = m_slots.last().end() - b.start;
        }

        if (b.dur <= 0) continue;
        if (b.end() <= fromMs) continue;
        if (b.start >= toMs)  break;
        blocks.push_back(b);
    }

    if (minDurMs <= 0) return blocks;

    QVector<Block> merged;
    merged.reserve(blocks.size());
    for (const Block &b : std::as_const(blocks)) {
        if (!merged.isEmpty() && b.dur < minDurMs && merged.last().dur < minDurMs
            && merged.last().end() == b.start) {
            Block &into = merged.last();
            into.dur += b.dur;
            into.count += 1;
            if (into.series.compare(b.series, Qt::CaseInsensitive) != 0) into.series.clear();
            into.ep.clear();
            continue;
        }
        merged.push_back(b);
    }
    return merged;
}

bool ChannelSchedule::blockAt(qint64 nowMs, Block *out) const {
    const QVector<Block> b = programmeBlocks(nowMs, nowMs + 1);
    if (b.isEmpty()) return false;
    if (out) *out = b.first();
    return true;
}

bool clockLooksSane(qint64 nowMs, qint64 generatedAt) {
    if (nowMs < kMinSaneEpochMs) return false;

    if (generatedAt > 0 && nowMs + kClockSkewToleranceMs < generatedAt) return false;

    return true;
}
}
