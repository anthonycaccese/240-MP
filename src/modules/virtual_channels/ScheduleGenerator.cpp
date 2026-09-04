#include "ScheduleGenerator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QDateTime>
#include <QHash>
#include <QTimeZone>

#include <random>

namespace vchan {

namespace {

Slot toSlot(const MediaItem &m, qint64 startMs, SlotKind kind) {
    Slot s;
    s.start   = startMs;
    s.dur     = m.durMs;
    s.kind    = kind;
    s.src     = m.src;
    s.ref     = m.ref;
    s.partKey = m.partKey;
    s.title   = m.title;
    s.series  = m.series;
    s.ep      = m.ep;
    s.desc    = m.desc;
    s.art     = m.art;
    return s;
}

QVector<MediaItem> usableOnly(const QVector<MediaItem> &in) {
    QVector<MediaItem> out;
    out.reserve(in.size());
    for (const MediaItem &m : in)
        if (m.durMs > 0 && !m.ref.trimmed().isEmpty())
            out.push_back(m);
    return out;
}
}

int minuteOfDayFromString(const QString &hhmm) {
    const QStringList parts = hhmm.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 2) return -1;
    bool okH = false, okM = false;
    const int h = parts[0].toInt(&okH);
    const int m = parts[1].toInt(&okM);
    if (!okH || !okM || h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

int dayOfWeekFromString(const QString &name) {
    static const QStringList kDays = {
        QStringLiteral("monday"), QStringLiteral("tuesday"), QStringLiteral("wednesday"),
        QStringLiteral("thursday"), QStringLiteral("friday"), QStringLiteral("saturday"),
        QStringLiteral("sunday")
    };
    const QString v = name.trimmed().toLower();
    if (v.isEmpty()) return 0;
    for (int i = 0; i < kDays.size(); ++i) {
        if (kDays[i] == v || (v.size() >= 2 && kDays[i].startsWith(v)))
            return i + 1;
    }
    return 0;
}

bool Appointment::isValid() const {
    if (minuteOfDay < 0 || minuteOfDay > 1439) return false;
    for (const MediaItem &m : pool)
        if (m.durMs > 0 && !m.ref.trimmed().isEmpty()) return true;
    return false;
}

bool Appointment::airsOn(int qtDayOfWeek) const {
    if (days.isEmpty()) return true;
    return days.contains(qtDayOfWeek);
}

qint64 airedAtMs(const QString &isoDate, const QVariant &yearValue) {
    const QDate exact = QDate::fromString(isoDate.left(10), Qt::ISODate);
    if (exact.isValid())
        return QDateTime(exact, QTime(0, 0), QTimeZone::utc()).toMSecsSinceEpoch();
    bool ok = false;
    const int year = yearValue.toInt(&ok);
    if (ok && year > 1800 && year < 2200)
        return QDateTime(QDate(year, 1, 1), QTime(0, 0), QTimeZone::utc()).toMSecsSinceEpoch();
    return 0;
}

Ordering orderingFromString(const QString &s) {
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("shuffle"))     return Ordering::Shuffle;
    if (v == QLatin1String("interleaved")) return Ordering::Interleaved;
    // "sequential" is what channels written before this said. It sorted by
    // series title, which nobody chose on purpose, so it reads as broadcast.
    return Ordering::Broadcast;
}

QString orderingToString(Ordering o) {
    switch (o) {
    case Ordering::Shuffle:     return QStringLiteral("shuffle");
    case Ordering::Interleaved: return QStringLiteral("interleaved");
    case Ordering::Broadcast:   break;
    }
    return QStringLiteral("broadcast");
}

bool ChannelDef::isPlayable() const {
    for (const MediaItem &m : programmes)
        if (m.durMs > 0 && !m.ref.trimmed().isEmpty())
            return true;
    return false;
}

QVector<Slot> generateSlots(const ChannelDef &def, qint64 startMs,
                            qint64 *endRotation) {
    QVector<Slot> out;

    const QVector<MediaItem> programmes  = usableOnly(def.programmes);
    if (programmes.isEmpty() || startMs <= 0 || def.horizonHours <= 0)
        return out;

    const QVector<MediaItem> intros      = usableOnly(def.intros);
    const QVector<MediaItem> outros      = usableOnly(def.outros);
    const QVector<MediaItem> commercials = usableOnly(def.commercials);
    const QVector<MediaItem> bumps       = usableOnly(def.bumps);

    QVector<QVector<MediaItem>> packIntros, packOutros;
    packIntros.reserve(def.packs.size());
    packOutros.reserve(def.packs.size());
    for (const BreakPack &p : def.packs) {
        packIntros.append(usableOnly(p.intros));
        packOutros.append(usableOnly(p.outros));
    }

    const auto introsFor = [&](const MediaItem &m) -> const QVector<MediaItem> & {
        if (m.pack >= 0 && m.pack < packIntros.size() && !packIntros[m.pack].isEmpty())
            return packIntros[m.pack];
        return intros;
    };
    const auto outrosFor = [&](const MediaItem &m) -> const QVector<MediaItem> & {
        if (m.pack >= 0 && m.pack < packOutros.size() && !packOutros[m.pack].isEmpty())
            return packOutros[m.pack];
        return outros;
    };

    QVector<QPair<MediaItem, SlotKind>> breakPool;
    for (const MediaItem &m : commercials) breakPool.append({m, SlotKind::Commercial});
    for (const MediaItem &m : bumps)       breakPool.append({m, SlotKind::Bump});

    QVector<QPair<MediaItem, SlotKind>> padPool = breakPool;
    std::sort(padPool.begin(), padPool.end(),
              [](const QPair<MediaItem, SlotKind> &a, const QPair<MediaItem, SlotKind> &b) {
                  return a.first.durMs < b.first.durMs;
              });

    std::mt19937 rng(def.seed);

    const int progCount = programmes.size();
    qint64 rotation = qMax<qint64>(0, def.rotation);
    qint64 loadedPass = -1;
    QVector<int> order(progCount);

    QVector<QVector<int>> groups;
    QVector<QString> groupKeys;
    {
        QHash<QString, int> byName;
        for (int i = 0; i < progCount; ++i) {
            const QString name = programmes[i].series.trimmed();
            const QString key = name.isEmpty() ? unnamedSeriesKey() : name.toLower();
            const auto it = byName.constFind(key);
            if (it == byName.constEnd()) {
                byName.insert(key, int(groups.size()));
                groups.append(QVector<int>{ i });
                groupKeys.append(key);
            } else {
                groups[it.value()].append(i);
            }
        }
    }

    // Broadcast order: when it first aired. Anything the library could not date
    // sorts after everything dated, alphabetically, so an undatable show lands
    // in one predictable place instead of scattered through the day.
    const auto airedBefore = [&programmes](int lhs, int rhs) {
        const MediaItem &a = programmes[lhs];
        const MediaItem &b = programmes[rhs];
        // Zero is the only "unknown": anything else is a date, negative ones
        // included. Testing for a positive treated every programme first aired
        // before 1970 as undated, which is most of the classic television this
        // is for -- Star Trek and The Twilight Zone sorted alphabetically among
        // the things nothing was known about.
        if ((a.airMs != 0) != (b.airMs != 0)) return a.airMs != 0;
        if (a.airMs != 0 && a.airMs != b.airMs) return a.airMs < b.airMs;
        const int byName = a.series.localeAwareCompare(b.series);
        if (byName != 0) return byName < 0;
        if (a.seasonNo  != b.seasonNo)  return a.seasonNo  < b.seasonNo;
        if (a.episodeNo != b.episodeNo) return a.episodeNo < b.episodeNo;
        return a.ref < b.ref;
    };

    const auto shuffleInto = [&](QVector<int> &into, qint64 pass) {
        std::mt19937 rng(def.seed ^ static_cast<quint32>(pass * 2654435761ull));
        const auto shuffle = [&rng](QVector<int> &v) {
            for (int i = int(v.size()) - 1; i > 0; --i) {
                std::uniform_int_distribution<int> d(0, i);
                std::swap(v[i], v[d(rng)]);
            }
        };

        QVector<QVector<int>> dealt = groups;
        for (QVector<int> &one : dealt) shuffle(one);

        QVector<int> turn(groups.size());
        for (int i = 0; i < turn.size(); ++i) turn[i] = i;
        shuffle(turn);

        into.clear();
        into.reserve(progCount);
        QVector<int> taken(dealt.size(), 0);
        for (bool any = true; any; ) {
            any = false;
            for (const int t : turn) {
                if (taken[t] >= dealt[t].size()) continue;
                into.append(dealt[t][taken[t]++]);
                any = true;
            }
        }
    };

    // Interleaved gives each series a turn, each in its own broadcast order, so
    // a channel of shows from different decades takes turns instead of airing
    // one until it runs out. Sorted once: the turn order never changes.
    QVector<int> resume(groups.size(), 0);
    QVector<int> taken(groups.size(), 0);
    qint64 placedHere = 0;

    // Broadcast runs one timeline for the whole channel, so it keeps one place.
    // Held as a ref rather than an offset: adding an episode anywhere in the
    // pool shifts every offset after it, but the mark still names the same
    // programme, so the channel carries on from where it actually was.
    QVector<int> broadcastOrder(progCount);
    int resumeAired = 0;
    if (def.order == Ordering::Broadcast) {
        for (int i = 0; i < progCount; ++i) broadcastOrder[i] = i;
        std::sort(broadcastOrder.begin(), broadcastOrder.end(), airedBefore);
        // A schedule written before marks existed carries only the count, which
        // says the same thing as long as the pool has not changed since.
        resumeAired = int(rotation % progCount);
        if (!def.mark.isEmpty()) {
            for (int k = 0; k < progCount; ++k) {
                if (programmes[broadcastOrder[k]].ref == def.mark) { resumeAired = k + 1; break; }
            }
        }
    }
    if (def.order == Ordering::Interleaved) {
        for (int g = 0; g < groups.size(); ++g) {
            QVector<int> &one = groups[g];
            std::sort(one.begin(), one.end(), airedBefore);

            // Resume after the episode this series last aired. A mark for an
            // episode no longer in the library reads as absent, so a series
            // whose files moved restarts rather than refusing to air.
            const QString mark = def.marks.value(groupKeys[g]);
            if (mark.isEmpty()) continue;
            for (int k = 0; k < one.size(); ++k) {
                if (programmes[one[k]].ref == mark) { resume[g] = k + 1; break; }
            }
        }
    }

    const auto loadPass = [&](qint64 pass) {
        for (int i = 0; i < progCount; ++i) order[i] = i;
        shuffleInto(order, pass);

        if (progCount > 1 && pass > 0) {
            QVector<int> previous(progCount);
            shuffleInto(previous, pass - 1);
            if (order[0] == previous[progCount - 1])
                std::swap(order[0], order[1]);
        }
    };

    const auto advanceProgramme = [&]() {
        if (def.order == Ordering::Interleaved)
            ++taken[int(rotation % groups.size())];
        ++placedHere;
        ++rotation;
    };

    const auto peekProgramme = [&]() -> const MediaItem & {
        if (def.order == Ordering::Interleaved) {
            // One episode of each series in turn, then round again. Each series
            // holds its own place and wraps on its own, so a short run repeats
            // sooner rather than leaving a long one playing by itself, and the
            // series drift out of step instead of restarting together.
            const int s = int(rotation % groups.size());
            const QVector<int> &turn = groups[s];
            return programmes[turn[(resume[s] + taken[s]) % turn.size()]];
        }
        if (def.order == Ordering::Broadcast)
            return programmes[broadcastOrder[int((resumeAired + placedHere) % progCount)]];

        const qint64 pass = rotation / progCount;
        if (pass != loadedPass) { loadedPass = pass; loadPass(pass); }
        return programmes[order[rotation % progCount]];
    };

    const qint64 horizonEnd = startMs + qint64(def.horizonHours * 3600.0 * 1000.0);
    const auto pick = [&rng](int n) {
        std::uniform_int_distribution<int> d(0, n - 1);
        return d(rng);
    };

    struct Anchor { qint64 start; MediaItem item; QString name; };
    QVector<Anchor> anchors;

    for (const Appointment &appt : def.appointments) {
        if (!appt.isValid()) continue;
        const QVector<MediaItem> pool = usableOnly(appt.pool);
        if (pool.isEmpty()) continue;

        QDate day = QDateTime::fromMSecsSinceEpoch(startMs).date();
        const QDate lastDay = QDateTime::fromMSecsSinceEpoch(horizonEnd).date();
        for (; day <= lastDay; day = day.addDays(1)) {
            if (!appt.airsOn(day.dayOfWeek())) continue;
            const QDateTime when(day, QTime(appt.minuteOfDay / 60, appt.minuteOfDay % 60));
            const qint64 at = when.toMSecsSinceEpoch();
            if (at < startMs || at >= horizonEnd) continue;
            anchors.push_back({ at, pool[pick(pool.size())], appt.name });
        }
    }

    std::sort(anchors.begin(), anchors.end(),
              [](const Anchor &a, const Anchor &b) { return a.start < b.start; });

    {
        QVector<Anchor> kept;
        qint64 freeFrom = startMs;
        for (const Anchor &a : anchors) {
            if (a.start < freeFrom) {
                // Two slots wanting the same part of the day is something the
                // viewer set up and can fix, so it is said rather than dropped
                // in silence.
                qWarning("[VirtualChannels] slot \"%s\" at %s is dropped: the one before it "
                         "is still running then",
                         qPrintable(a.name),
                         qPrintable(QDateTime::fromMSecsSinceEpoch(a.start)
                                        .toString(QStringLiteral("ddd HH:mm"))));
                continue;
            }
            kept.push_back(a);
            freeFrom = a.start + a.item.durMs;
        }
        anchors = kept;
    }

    qint64 t = startMs;

    QString lastBreakRef;

    const auto place = [&](const MediaItem &m, SlotKind k) {
        out.push_back(toSlot(m, t, k));
        t += m.durMs;
        lastBreakRef = (k == SlotKind::Programme) ? QString() : m.ref;
    };

    const auto chooseUnrepeated = [&](const QVector<int> &candidates,
                                      const QVector<QPair<MediaItem, SlotKind>> &pool) -> int {
        if (candidates.isEmpty()) return -1;
        QVector<int> fresh;
        fresh.reserve(candidates.size());
        for (const int i : candidates)
            if (pool[i].first.ref != lastBreakRef) fresh.append(i);
        const QVector<int> &from = fresh.isEmpty() ? candidates : fresh;
        return from[pick(from.size())];
    };

    const auto placeFiller = [&](qint64 durMs) {
        if (durMs <= 0) return;
        if (durMs < kMinFillerMs && !out.isEmpty()) {
            out.back().dur += durMs;
            t += durMs;
            return;
        }
        Slot f;
        f.start = t;
        f.dur   = durMs;
        f.kind  = SlotKind::Filler;
        f.src   = SlotSource::Local;
        f.title = def.name;
        out.push_back(f);
        t += durMs;
    };

    const qint64 gridMs = qint64(def.gridMinutes) * 60000LL;
    const auto markAtOrBefore = [gridMs](qint64 ms) -> qint64 {
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
        const QDateTime midnight(dt.date(), QTime(0, 0));
        const qint64 since = midnight.msecsTo(dt);
        if (since < 0) return ms;
        return midnight.addMSecs((since / gridMs) * gridMs).toMSecsSinceEpoch();
    };
    const auto markAtOrAfter = [gridMs](qint64 ms) -> qint64 {
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
        const QDateTime midnight(dt.date(), QTime(0, 0));
        const qint64 since = midnight.msecsTo(dt);
        if (since < 0) return ms;
        const qint64 units = (since + gridMs - 1) / gridMs;
        return midnight.addMSecs(units * gridMs).toMSecsSinceEpoch();
    };

    const auto packUntil = [&](qint64 limit) {
        bool progress = true;
        while (progress && t < limit && out.size() < kMaxSlotsPerChannel) {
            progress = false;
            const qint64 room = limit - t;

            QVector<int> exact;
            for (int i = 0; i < padPool.size(); ++i)
                if (padPool[i].first.durMs == room) exact.append(i);

            int chosen = -1;
            if (!exact.isEmpty()) {
                chosen = chooseUnrepeated(exact, padPool);
            } else {
                QVector<int> fits;
                for (int i = 0; i < padPool.size(); ++i)
                    if (padPool[i].first.durMs <= room) fits.append(i);
                chosen = chooseUnrepeated(fits, padPool);
            }
            if (chosen < 0) break;
            place(padPool[chosen].first, padPool[chosen].second);
            progress = true;
        }
    };

    const auto fillUntil = [&](qint64 limit, bool padRemainder) {
        while (t < limit && out.size() < kMaxSlotsPerChannel) {
            const MediaItem &prog = peekProgramme();
            const QVector<MediaItem> &myIntros = introsFor(prog);
            const MediaItem *intro = myIntros.isEmpty() ? nullptr
                                                        : &myIntros[pick(myIntros.size())];
            qint64 need = prog.durMs + (intro ? intro->durMs : 0);
            if (t + need > limit) break;

            advanceProgramme();
            if (intro) place(*intro, SlotKind::Intro);
            place(prog, SlotKind::Programme);

            const QVector<MediaItem> &myOutros = outrosFor(prog);
            if (!myOutros.isEmpty()) {
                const MediaItem &o = myOutros[pick(myOutros.size())];
                if (t + o.durMs <= limit) place(o, SlotKind::Outro);
            }
            for (int i = 0; i < def.adsPerBreak && !breakPool.isEmpty(); ++i) {
                QVector<int> fits;
                for (int j = 0; j < breakPool.size(); ++j)
                    if (t + breakPool[j].first.durMs <= limit) fits.append(j);
                const int chosen = chooseUnrepeated(fits, breakPool);
                if (chosen < 0) break;
                place(breakPool[chosen].first, breakPool[chosen].second);
            }
        }

        if (!padRemainder) return;

        bool progress = true;
        while (progress && t < limit && out.size() < kMaxSlotsPerChannel) {
            progress = false;
            QVector<int> fits;
            for (int i = 0; i < padPool.size(); ++i)
                if (t + padPool[i].first.durMs <= limit) fits.append(i);
            const int chosen = chooseUnrepeated(fits, padPool);
            if (chosen < 0) break;
            place(padPool[chosen].first, padPool[chosen].second);
            progress = true;
        }

        // Whatever break content could not cover, the card holds — the same as
        // the grid path. A free-run channel with a movie slot and nothing short
        // enough to pad with was otherwise left as a hole, and a hole is dead
        // air: the tuner reports the channel off between the two.
        if (t < limit) placeFiller(limit - t);
    };

    const auto fillGrid = [&](qint64 limit) {
        while (t < limit && out.size() < kMaxSlotsPerChannel) {
            const qint64 blockStart = markAtOrAfter(t);

            if (blockStart > t) {
                if (blockStart >= limit) {
                    packUntil(limit);
                    if (t < limit) placeFiller(limit - t);
                    return;
                }
                packUntil(blockStart);
                if (t < blockStart) placeFiller(blockStart - t);
                continue;
            }
            if (t >= limit) return;

            const MediaItem &prog = peekProgramme();

            const qint64 blocks   = (prog.durMs + gridMs - 1) / gridMs;
            const qint64 blockEnd = blockStart + blocks * gridMs;

            if (blockEnd > limit) {
                packUntil(limit);
                if (t < limit) placeFiller(limit - t);
                return;
            }

            const MediaItem closing = prog;
            advanceProgramme();
            place(closing, SlotKind::Programme);
            const QVector<MediaItem> &myOutros = outrosFor(closing);
            if (!myOutros.isEmpty()) {
                const MediaItem &o = myOutros[pick(myOutros.size())];
                if (t + o.durMs <= blockEnd) place(o, SlotKind::Outro);
            }

            const QVector<MediaItem> &nextIntros = introsFor(peekProgramme());
            const MediaItem *nextIntro = nextIntros.isEmpty()
                                             ? nullptr
                                             : &nextIntros[pick(nextIntros.size())];
            qint64 packTo = blockEnd;
            if (nextIntro && blockEnd - nextIntro->durMs > t)
                packTo = blockEnd - nextIntro->durMs;
            else
                nextIntro = nullptr;

            packUntil(packTo);
            if (t < packTo) placeFiller(packTo - t);
            if (nextIntro) place(*nextIntro, SlotKind::Intro);

            if (t < blockEnd) t = blockEnd;
        }
    };

    const auto fill = [&](qint64 limit, bool padRemainder) {
        if (def.gridMinutes > 0) fillGrid(limit);
        else                     fillUntil(limit, padRemainder);
    };

    if (def.gridMinutes > 0) t = markAtOrBefore(startMs);

    for (const Anchor &a : anchors) {
        if (out.size() >= kMaxSlotsPerChannel) break;
        if (a.start > t) fill(a.start, /*padRemainder*/ true);
        if (t > a.start) continue;

        t = a.start;
        place(a.item, SlotKind::Programme);
    }
    fill(horizonEnd, /*padRemainder*/ false);

    if (endRotation) *endRotation = rotation;
    return out;
}

QByteArray serializeSchedule(const ChannelDef &def,
                             qint64 generatedAt,
                             const QVector<Slot> &placed) {
    QJsonArray arr;
    for (const Slot &s : placed) {
        QJsonObject o;
        o["start"] = double(s.start);
        o["dur"]   = double(s.dur);
        o["kind"]  = slotKindToString(s.kind);
        o["src"]   = slotSourceToString(s.src);
        o["ref"]   = s.ref;
        if (!s.partKey.isEmpty()) o["part_key"] = s.partKey;
        if (!s.title.isEmpty())   o["title"]    = s.title;
        if (!s.series.isEmpty())  o["series"]   = s.series;
        if (!s.ep.isEmpty())      o["ep"]       = s.ep;
        if (!s.desc.isEmpty())    o["desc"]     = s.desc;
        if (!s.art.isEmpty())     o["art"]      = s.art;
        arr.append(o);
    }

    QJsonObject root;
    root["channel"]      = def.number;
    root["seed"]         = double(def.seed);
    root["generated_at"] = double(generatedAt);
    root["horizon_end"]  = double(placed.isEmpty() ? generatedAt : placed.last().end());
    root["rotation"]     = double(def.rotation);
    if (!def.marks.isEmpty()) {
        QJsonObject marks;
        for (auto it = def.marks.constBegin(); it != def.marks.constEnd(); ++it)
            marks.insert(it.key(), it.value());
        root["marks"] = marks;
    }
    if (!def.mark.isEmpty()) root["mark"] = def.mark;
    root["slots"]        = arr;

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
}
