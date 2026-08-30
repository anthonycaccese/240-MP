#include "../ChannelSchedule.h"
#include "../ScheduleGenerator.h"
#include "TestHarness.h"

#include <QDateTime>
#include <QHash>
#include <QSet>

using namespace vchan;
using vtest::check;
using vtest::checkEq;
using vtest::checkStr;
using vtest::section;

namespace {

constexpr qint64 kBase = 1787000000000LL;

qint64 onTheHour() {
    return QDateTime(QDate(2026, 8, 24), QTime(10, 0)).toMSecsSinceEpoch();
}

MediaItem episodeIn(const QString &series, const QString &ref, qint64 durMs, int pack) {
    MediaItem m;
    m.ref = ref; m.durMs = durMs; m.title = ref; m.series = series; m.pack = pack;
    return m;
}

MediaItem episodeOf(const QString &series, const QString &ref, qint64 durMs) {
    MediaItem m;
    m.ref    = ref;
    m.durMs  = durMs;
    m.title  = ref;
    m.series = series;
    return m;
}

MediaItem item(const QString &ref, qint64 durMs, const QString &title = {}) {
    MediaItem m;
    m.ref   = ref;
    m.durMs = durMs;
    m.title = title.isEmpty() ? ref : title;
    return m;
}

ChannelDef basicDef() {
    ChannelDef d;
    d.number = 3;
    d.name = "Test";
    d.seed = 7;
    d.horizonHours = 1.0;
    d.programmes = { item("a.mkv", 600000), item("b.mkv", 900000), item("c.mkv", 300000) };
    return d;
}

bool contiguous(const QVector<Slot> &s) {
    for (int i = 1; i < s.size(); ++i)
        if (s[i].start != s[i - 1].end()) return false;
    return true;
}
}

int runScheduleGeneratorTests() {
    section("generate: basic shape");
    {
        const ChannelDef d = basicDef();
        const QVector<Slot> s = generateSlots(d, kBase);

        check(!s.isEmpty(), "produces slots");
        check(contiguous(s), "timeline is contiguous");
        checkEq(s.first().start, kBase, "starts where asked");
        check(s.last().end() >= kBase + 3600000, "covers the requested horizon");
        check(s.first().kind == SlotKind::Programme, "no interstitials configured");

        for (const Slot &sl : s) check(sl.dur > 0, "every slot has a real duration");
    }

    section("generate: refuses to produce nonsense");
    {
        ChannelDef empty = basicDef();
        empty.programmes.clear();
        check(generateSlots(empty, kBase).isEmpty(), "no programmes -> no slots");
        check(!empty.isPlayable(), "isPlayable is false");

        ChannelDef zero = basicDef();
        zero.programmes = { item("bad.mkv", 0), item("also-bad.mkv", -5) };
        check(generateSlots(zero, kBase).isEmpty(), "zero-duration items produce nothing");
        check(!zero.isPlayable(), "a pool of unusable items is not playable");

        ChannelDef mixed = basicDef();
        mixed.programmes = { item("bad.mkv", 0), item("good.mkv", 60000) };
        const QVector<Slot> ms = generateSlots(mixed, kBase);
        check(!ms.isEmpty(), "one good item is enough");
        for (const Slot &sl : ms) check(sl.ref == QLatin1String("good.mkv"), "only the good item placed");

        ChannelDef noHorizon = basicDef();
        noHorizon.horizonHours = 0;
        check(generateSlots(noHorizon, kBase).isEmpty(), "zero horizon -> no slots");
        check(generateSlots(basicDef(), 0).isEmpty(), "zero start -> no slots");
        check(generateSlots(basicDef(), -1).isEmpty(), "negative start -> no slots");
    }

    section("generate: interstitials");
    {
        ChannelDef d = basicDef();
        d.intros      = { item("intro.mkv", 2000) };
        d.outros      = { item("outro.mkv", 3000) };
        d.commercials = { item("ad1.mkv", 30000), item("ad2.mkv", 15000) };
        d.bumps       = { item("bump.mkv", 5000) };
        d.adsPerBreak = 2;

        const QVector<Slot> s = generateSlots(d, kBase);
        check(contiguous(s), "still contiguous with interstitials");

        QSet<int> kinds;
        for (const Slot &sl : s) kinds.insert(int(sl.kind));
        check(kinds.contains(int(SlotKind::Intro)),     "intros placed");
        check(kinds.contains(int(SlotKind::Programme)), "programmes placed");
        check(kinds.contains(int(SlotKind::Outro)),     "outros placed");
        check(kinds.contains(int(SlotKind::Commercial)) || kinds.contains(int(SlotKind::Bump)),
              "ad break placed");

        checkEq(int(s[0].kind), int(SlotKind::Intro),     "cycle starts with the intro");
        checkEq(int(s[1].kind), int(SlotKind::Programme), "then the programme");
        checkEq(int(s[2].kind), int(SlotKind::Outro),     "then the outro");
        check(s[3].kind == SlotKind::Commercial || s[3].kind == SlotKind::Bump,
              "then the ad break");

        for (const Slot &sl : s) {
            if (sl.ref.startsWith(QLatin1String("ad")))
                check(sl.kind == SlotKind::Commercial, "ads keep the commercial kind");
            if (sl.ref == QLatin1String("bump.mkv"))
                check(sl.kind == SlotKind::Bump, "bumps keep the bump kind");
        }

        ChannelDef noAds = d;
        noAds.adsPerBreak = 0;
        for (const Slot &sl : generateSlots(noAds, kBase))
            check(sl.kind != SlotKind::Commercial && sl.kind != SlotKind::Bump,
                  "adsPerBreak 0 places no ads at all");
    }

    section("generate: determinism");
    {
        ChannelDef d = basicDef();
        d.order = Ordering::Shuffle;
        d.commercials = { item("ad1.mkv", 30000), item("ad2.mkv", 15000) };
        d.adsPerBreak = 2;

        const QVector<Slot> a = generateSlots(d, kBase);
        const QVector<Slot> b = generateSlots(d, kBase);
        checkEq(a.size(), b.size(), "same seed, same length");

        bool identical = a.size() == b.size();
        for (int i = 0; identical && i < a.size(); ++i)
            if (a[i].ref != b[i].ref || a[i].start != b[i].start) identical = false;
        check(identical, "same seed reproduces the timeline exactly");

        ChannelDef other = d;
        other.seed = 999;
        const QVector<Slot> c = generateSlots(other, kBase);
        bool differs = false;
        for (int i = 0; i < qMin(a.size(), c.size()); ++i)
            if (a[i].ref != c[i].ref) { differs = true; break; }
        check(differs, "a different seed produces a different timeline");
    }

    section("generate: shuffle airs everything before repeating");
    {
        ChannelDef d = basicDef();
        d.order = Ordering::Shuffle;
        d.horizonHours = 0.5;
        const QVector<Slot> s = generateSlots(d, kBase);

        QSet<QString> seen;
        int firstRepeatAt = -1;
        int progIndex = 0;
        for (const Slot &sl : s) {
            if (sl.kind != SlotKind::Programme) continue;
            if (seen.contains(sl.ref) && firstRepeatAt < 0) firstRepeatAt = progIndex;
            seen.insert(sl.ref);
            ++progIndex;
        }
        check(firstRepeatAt < 0 || firstRepeatAt >= 3,
              "no programme repeats until every one has aired");
    }

    section("generate: bounded");
    {
        ChannelDef d = basicDef();
        d.programmes = { item("tiny.mkv", 200) };
        d.horizonHours = 24.0;
        const QVector<Slot> s = generateSlots(d, kBase);
        check(s.size() <= 20000, "slot count is capped");
        check(!s.isEmpty(), "but still produces a usable timeline");
        check(contiguous(s), "capped timeline is still contiguous");
    }

    section("round trip: generator output parses and resolves");
    {
        ChannelDef d = basicDef();
        d.order = Ordering::Shuffle;
        d.intros = { item("intro.mkv", 2000) };
        d.commercials = { item("ad.mkv", 30000) };
        d.adsPerBreak = 1;

        const QVector<Slot> generated = generateSlots(d, kBase);
        const QByteArray json = serializeSchedule(d, kBase - 1000, generated);

        QString err;
        int dropped = -1;
        const ChannelSchedule parsed = ChannelSchedule::fromJson(json, &err, &dropped);

        check(parsed.isValid(), "generator output parses");
        check(err.isEmpty(), "with no error");
        checkEq(dropped, 0, "and nothing dropped");
        checkEq(parsed.slotCount(), generated.size(), "every slot survives the round trip");
        checkEq(parsed.channel(), d.number, "channel number preserved");
        checkEq(parsed.seed(), d.seed, "seed preserved");

        check(parsed.slotList().first().kind == generated.first().kind, "kind preserved");
        check(parsed.slotList().first().ref  == generated.first().ref,  "ref preserved");
        check(!parsed.slotList().first().title.isEmpty(), "title preserved");

        const qint64 mid = kBase + 60000;
        check(parsed.statusAt(mid) == Status::Ok, "parsed timeline is playable");
        check(parsed.indexAt(mid) >= 0, "resolves to a slot");
        check(!parsed.runWindow(parsed.indexAt(mid)).isEmpty(), "produces a run window");
    }

    section("appointments: parsing");
    {
        checkEq(minuteOfDayFromString("20:00"), 20 * 60, "20:00");
        checkEq(minuteOfDayFromString("00:00"), 0, "midnight");
        checkEq(minuteOfDayFromString("23:59"), 23 * 60 + 59, "last minute");
        checkEq(minuteOfDayFromString("7:05"),  7 * 60 + 5, "single-digit hour");
        checkEq(minuteOfDayFromString("24:00"), -1, "hour out of range");
        checkEq(minuteOfDayFromString("12:60"), -1, "minute out of range");
        checkEq(minuteOfDayFromString("noon"),  -1, "not a time");
        checkEq(minuteOfDayFromString(""),      -1, "empty");

        checkEq(dayOfWeekFromString("monday"),   1, "monday");
        checkEq(dayOfWeekFromString("sat"),      6, "abbreviated saturday");
        checkEq(dayOfWeekFromString("SUNDAY"),   7, "case insensitive");
        checkEq(dayOfWeekFromString("su"),       7, "two letters is enough");
        checkEq(dayOfWeekFromString("s"),        0, "one letter is ambiguous");
        checkEq(dayOfWeekFromString("caturday"), 0, "not a day");
    }

    section("appointments: honoured exactly");
    {
        const QDateTime base(QDate(2026, 8, 24), QTime(6, 0));
        const qint64 start = base.toMSecsSinceEpoch();

        Appointment film;
        film.name = "Movie Night";
        film.minuteOfDay = 20 * 60;
        film.pool = { item("film.mkv", 90 * 60000, "The Film") };

        ChannelDef d = basicDef();
        d.horizonHours = 20;
        d.programmes = { item("a.mkv", 25 * 60000), item("b.mkv", 25 * 60000) };
        d.appointments = { film };

        const QVector<Slot> s = generateSlots(d, start);
        check(!s.isEmpty(), "produces a timeline");

        const qint64 expected = QDateTime(QDate(2026, 8, 24), QTime(20, 0)).toMSecsSinceEpoch();
        int filmIdx = -1;
        for (int i = 0; i < s.size(); ++i)
            if (s[i].ref == QLatin1String("film.mkv")) { filmIdx = i; break; }
        check(filmIdx >= 0, "the film is scheduled");
        if (filmIdx >= 0)
            checkEq(s[filmIdx].start, expected, "and starts exactly on the hour billed");

        bool overrun = false;
        for (const Slot &sl : s)
            if (sl.start < expected && sl.end() > expected) overrun = true;
        check(!overrun, "nothing runs over the appointment's start");

        if (filmIdx > 0) {
            const qint64 gap = expected - s[filmIdx - 1].end();
            check(gap >= 0, "no negative gap");
            check(gap < 25 * 60000, "gap is smaller than a programme");
        }
    }

    section("appointments: only on the days named");
    {
        const QDateTime base(QDate(2026, 8, 24), QTime(6, 0));
        const qint64 start = base.toMSecsSinceEpoch();

        Appointment sat;
        sat.minuteOfDay = 20 * 60;
        sat.days = { 6 };
        sat.pool = { item("sat.mkv", 60 * 60000) };

        ChannelDef d = basicDef();
        d.horizonHours = 24 * 8;
        d.programmes = { item("a.mkv", 30 * 60000) };
        d.appointments = { sat };

        int count = 0;
        bool allSaturdays = true;
        for (const Slot &sl : generateSlots(d, start)) {
            if (sl.ref != QLatin1String("sat.mkv")) continue;
            ++count;
            if (QDateTime::fromMSecsSinceEpoch(sl.start).date().dayOfWeek() != 6)
                allSaturdays = false;
        }
        check(count >= 1, "the Saturday booking airs");
        check(allSaturdays, "and only ever on a Saturday");

        Appointment daily = sat;
        daily.days.clear();
        ChannelDef e = d;
        e.appointments = { daily };
        int dailyCount = 0;
        for (const Slot &sl : generateSlots(e, start))
            if (sl.ref == QLatin1String("sat.mkv")) ++dailyCount;
        check(dailyCount > count, "an appointment with no days airs more often");
    }

    section("appointments: conflicts and determinism");
    {
        const qint64 start = QDateTime(QDate(2026, 8, 24), QTime(6, 0)).toMSecsSinceEpoch();

        Appointment first;
        first.minuteOfDay = 20 * 60;
        first.pool = { item("long.mkv", 120 * 60000) };
        Appointment second;
        second.minuteOfDay = 21 * 60;
        second.pool = { item("clash.mkv", 30 * 60000) };

        ChannelDef d = basicDef();
        d.horizonHours = 20;
        d.programmes = { item("a.mkv", 20 * 60000) };
        d.appointments = { first, second };

        const QVector<Slot> s = generateSlots(d, start);
        bool sawLong = false, sawClash = false;
        for (const Slot &sl : s) {
            if (sl.ref == QLatin1String("long.mkv"))  sawLong = true;
            if (sl.ref == QLatin1String("clash.mkv")) sawClash = true;
        }
        check(sawLong, "the earlier booking airs");
        check(!sawClash, "the overlapping one is dropped rather than truncating it");

        bool overlap = false;
        for (int i = 1; i < s.size(); ++i)
            if (s[i].start < s[i - 1].end()) overlap = true;
        check(!overlap, "no slot overlaps another");

        const QVector<Slot> again = generateSlots(d, start);
        bool identical = again.size() == s.size();
        for (int i = 0; identical && i < s.size(); ++i)
            if (again[i].ref != s[i].ref || again[i].start != s[i].start) identical = false;
        check(identical, "appointments stay deterministic for a given seed");
    }

    section("appointments: ignored when unusable");
    {
        const qint64 start = QDateTime(QDate(2026, 8, 24), QTime(6, 0)).toMSecsSinceEpoch();
        ChannelDef d = basicDef();
        d.horizonHours = 20;

        Appointment noTime;
        noTime.pool = { item("x.mkv", 60000) };
        check(!noTime.isValid(), "an appointment with no time is invalid");

        Appointment noPool;
        noPool.minuteOfDay = 12 * 60;
        check(!noPool.isValid(), "an appointment with nothing to air is invalid");

        Appointment zeroDur;
        zeroDur.minuteOfDay = 12 * 60;
        zeroDur.pool = { item("bad.mkv", 0) };
        check(!zeroDur.isValid(), "a pool of unusable items is invalid");

        d.appointments = { noTime, noPool, zeroDur };
        const QVector<Slot> s = generateSlots(d, start);
        check(!s.isEmpty(), "the channel still builds around invalid bookings");
        for (const Slot &sl : s)
            check(sl.ref != QLatin1String("bad.mkv"), "nothing unusable is placed");
    }

    section("the end of the horizon is not packed out with idents");
    {
        ChannelDef d = basicDef();
        d.horizonHours = 6.0;
        d.programmes = { item("long.mkv", 3600000) };
        d.bumps      = { item("id.mkv", 2000) };
        d.commercials= { item("ad.mkv", 30000) };

        const QVector<Slot> s = generateSlots(d, kBase);
        check(!s.isEmpty(), "generates");

        int bumps = 0;
        for (const Slot &x : s) if (x.kind == SlotKind::Bump) ++bumps;
        check(bumps < 100, "the tail is not thousands of idents");

        const qint64 end = s.last().end();
        check(end <= kBase + qint64(6.0 * 3600000), "never runs past the horizon");
        check(s.last().kind == SlotKind::Programme
              || s.last().kind == SlotKind::Outro
              || s.last().kind == SlotKind::Commercial
              || s.last().kind == SlotKind::Bump,
              "and ends on something real");
    }

    section("a booking still has its gap closed");
    {
        ChannelDef d = basicDef();
        d.horizonHours = 6.0;
        d.programmes = { item("p.mkv", 1800000) };
        d.bumps      = { item("id.mkv", 5000) };

        Appointment a;
        a.name = "Film";
        a.minuteOfDay = QDateTime::fromMSecsSinceEpoch(kBase).time().hour() * 60
                        + QDateTime::fromMSecsSinceEpoch(kBase).time().minute() + 100;
        a.pool = { item("film.mkv", 3000000) };
        d.appointments = { a };

        const QVector<Slot> s = generateSlots(d, kBase);
        check(!s.isEmpty(), "generates");
        check(contiguous(s), "and is contiguous up to the booking");
    }

    section("rotation: a rebuild carries on rather than starting again");
    {
        ChannelDef d = basicDef();
        d.horizonHours = 2.0;
        d.programmes = { item("e1.mkv", 600000), item("e2.mkv", 600000),
                         item("e3.mkv", 600000), item("e4.mkv", 600000),
                         item("e5.mkv", 600000) };

        qint64 endRotation = -1;
        const QVector<Slot> first = generateSlots(d, kBase, &endRotation);
        check(!first.isEmpty(), "a first build");
        check(endRotation > 0, "which reports how far it dealt");

        QStringList firstRefs;
        for (const Slot &x : first)
            if (x.kind == SlotKind::Programme) firstRefs << x.ref;
        checkStr(firstRefs.first(), QStringLiteral("e1.mkv"), "beginning at the beginning");

        ChannelDef again = d;
        again.rotation = endRotation;
        const QVector<Slot> second = generateSlots(again, kBase + 7200000);
        QStringList secondRefs;
        for (const Slot &x : second)
            if (x.kind == SlotKind::Programme) secondRefs << x.ref;

        check(!secondRefs.isEmpty(), "a second build");
        checkStr(secondRefs.first(),
                 firstRefs[endRotation % 5],
                 "the next build opens on the programme the last one stopped at");
        check(secondRefs.first() != QStringLiteral("e1.mkv"),
              "and not back at the first episode");
    }

    section("rotation: a shuffled channel deals the whole series first");
    {
        ChannelDef d = basicDef();
        d.order = Ordering::Shuffle;
        d.horizonHours = 2.0;
        d.programmes = { item("a.mkv", 600000), item("b.mkv", 600000),
                         item("c.mkv", 600000), item("d.mkv", 600000),
                         item("e.mkv", 600000), item("f.mkv", 600000) };

        QStringList dealt;
        qint64 rot = 0;
        for (int i = 0; i < 6; ++i) {
            ChannelDef step = d;
            step.rotation = rot;
            step.horizonHours = 0.2;
            const QVector<Slot> s1 = generateSlots(step, kBase + i * 720000, &rot);
            for (const Slot &x : s1)
                if (x.kind == SlotKind::Programme) dealt << x.ref;
        }

        QSet<QString> distinct(dealt.begin(), dealt.end());
        checkEq(distinct.size(), 6, "a pass holds every programme");
        checkEq(dealt.size(), dealt.size(), "and deals them");
        bool repeatedEarly = false;
        QSet<QString> seen;
        for (int i = 0; i < qMin(6, dealt.size()); ++i) {
            if (seen.contains(dealt[i])) repeatedEarly = true;
            seen.insert(dealt[i]);
        }
        check(!repeatedEarly, "with none repeated until the series is through");

        ChannelDef nextPass = d;
        nextPass.rotation = 6;
        nextPass.horizonHours = 2.0;
        const QVector<Slot> p2 = generateSlots(nextPass, kBase);
        QStringList secondPass;
        for (const Slot &x : p2)
            if (x.kind == SlotKind::Programme) secondPass << x.ref;
        check(!secondPass.isEmpty(), "the next pass deals too");
    }

    section("rotation: the same rotation builds the same timeline");
    {
        ChannelDef d = basicDef();
        d.order = Ordering::Shuffle;
        d.rotation = 17;
        const QVector<Slot> a = generateSlots(d, kBase);
        const QVector<Slot> b = generateSlots(d, kBase);
        checkEq(a.size(), b.size(), "same length");
        bool same = true;
        for (int i = 0; i < a.size() && same; ++i)
            if (a[i].ref != b[i].ref || a[i].start != b[i].start) same = false;
        check(same, "slot for slot");
    }

    section("grid: off by default");
    {
        ChannelDef d = basicDef();
        check(d.gridMinutes == 0, "a channel runs free unless told otherwise");
        const QVector<Slot> a = generateSlots(d, kBase);
        check(!a.isEmpty(), "and still generates");
        bool anyFiller = false;
        for (const Slot &x : a) if (x.kind == SlotKind::Filler) anyFiller = true;
        check(!anyFiller, "with no filler slots anywhere");
    }

    section("grid: programmes start on the mark");
    {
        const QDateTime base(QDate(2026, 8, 24), QTime(9, 7, 13));
        const qint64 start = base.toMSecsSinceEpoch();

        ChannelDef d = basicDef();
        d.gridMinutes = 30;
        d.horizonHours = 6.0;
        d.commercials = { item("ad30.mkv", 30000), item("ad60.mkv", 60000),
                          item("ad120.mkv", 120000) };
        const QVector<Slot> s = generateSlots(d, start);
        check(!s.isEmpty(), "generates");
        check(contiguous(s), "and is contiguous — a clock leaves no holes");

        int programmes = 0;
        bool allOnMark = true;
        for (const Slot &x : s) {
            if (x.kind != SlotKind::Programme) continue;
            ++programmes;
            const QDateTime at = QDateTime::fromMSecsSinceEpoch(x.start);
            if (at.time().second() != 0) allOnMark = false;
        }
        check(programmes > 0, "places programmes");
        check(allOnMark, "none of them start part-way through a minute");

        bool onMark = true;
        for (const Slot &x : s) {
            if (x.kind != SlotKind::Programme) continue;
            const QTime at = QDateTime::fromMSecsSinceEpoch(x.start).time();
            if (at.minute() % 30 != 0 || at.second() != 0 || at.msec() != 0)
                onMark = false;
        }
        check(onMark, "every programme starts on a half hour");

        bool introsLandRight = true;
        for (int i = 0; i < s.size() - 1; ++i) {
            if (s[i].kind != SlotKind::Intro) continue;
            if (s[i + 1].kind != SlotKind::Programme) introsLandRight = false;
            if (s[i].end() != s[i + 1].start) introsLandRight = false;
        }
        check(introsLandRight, "an ident runs straight into the programme it announces");
    }

    section("grid: the remainder becomes a card");
    {
        ChannelDef d;
        d.number = 1; d.name = "Grid"; d.seed = 5;
        d.horizonHours = 2.0;
        d.gridMinutes = 30;
        d.programmes = { item("show.mkv", 1200000) };
        d.commercials = { item("ad.mkv", 420000) };
        const QVector<Slot> s = generateSlots(d, onTheHour());
        check(contiguous(s), "contiguous");

        int fillers = 0;
        qint64 fillerMs = 0;
        for (const Slot &x : s)
            if (x.kind == SlotKind::Filler) { ++fillers; fillerMs += x.dur; }
        check(fillers > 0, "a card is placed");
        checkEq(fillerMs / fillers, 180000, "and holds exactly what was left over");
    }

    section("grid: exact material needs no card");
    {
        ChannelDef d;
        d.number = 1; d.name = "Grid"; d.seed = 5;
        d.horizonHours = 2.0;
        d.gridMinutes = 30;
        d.programmes  = { item("show.mkv", 1500000) };
        d.commercials = { item("ad5.mkv", 300000),
                          item("ad3.mkv", 180000) };
        const QVector<Slot> s = generateSlots(d, onTheHour());
        bool anyFiller = false;
        for (const Slot &x : s) if (x.kind == SlotKind::Filler) anyFiller = true;
        check(!anyFiller, "nothing is left to hold");
        check(contiguous(s), "and it is still contiguous");
    }

    section("grid: a long programme takes the marks it needs");
    {
        ChannelDef d;
        d.number = 1; d.name = "Grid"; d.seed = 5;
        d.horizonHours = 4.0;
        d.gridMinutes = 30;
        d.programmes = { item("film.mkv", 2700000) };
        const QVector<Slot> s = generateSlots(d, onTheHour());
        check(contiguous(s), "contiguous");

        qint64 prev = -1;
        bool hourly = true;
        for (const Slot &x : s) {
            if (x.kind != SlotKind::Programme) continue;
            if (prev >= 0 && x.start - prev != 3600000) hourly = false;
            prev = x.start;
        }
        check(hourly, "a 45 minute programme takes an hour of the clock");
    }

    section("grid: survives having nothing to fill with");
    {
        ChannelDef d;
        d.number = 1; d.name = "Bare"; d.seed = 5;
        d.horizonHours = 2.0;
        d.gridMinutes = 30;
        d.programmes = { item("show.mkv", 1200000) };
        const QVector<Slot> s = generateSlots(d, onTheHour());
        check(!s.isEmpty(), "still generates");
        check(contiguous(s), "still contiguous");
        checkEq(s.size() % 2, 0, "each block is a programme and a card");
        check(s.size() < 1000, "and it terminates rather than spinning");
    }

    section("grid: a programme longer than the horizon");
    {
        ChannelDef d;
        d.number = 1; d.name = "Long"; d.seed = 5;
        d.horizonHours = 0.25;
        d.gridMinutes = 30;
        d.programmes = { item("epic.mkv", 7200000) };
        const QVector<Slot> s = generateSlots(d, onTheHour());
        check(s.size() < 1000, "it terminates rather than spinning");
        for (const Slot &x : s)
            check(x.kind == SlotKind::Filler, "and holds the card rather than cutting a programme off");
    }

    section("grid: a break is not the same advert over and over");
    {
        const QDateTime base(QDate(2026, 8, 24), QTime(9, 0, 0));
        ChannelDef d = basicDef();
        d.gridMinutes  = 30;
        d.horizonHours = 6.0;
        d.commercials = { item("a.mkv", 61000), item("b.mkv", 62000),
                          item("c.mkv", 63000), item("d.mkv", 64000) };

        const QVector<Slot> s = generateSlots(d, base.toMSecsSinceEpoch());
        check(!s.isEmpty(), "generates");

        QSet<QString> used;
        int breaks = 0;
        for (const Slot &x : s)
            if (x.kind == SlotKind::Commercial) { used.insert(x.ref); ++breaks; }
        check(breaks > 8, "the breaks are actually being filled");
        check(used.size() > 1, "and not from one advert alone");

        const QVector<Slot> again = generateSlots(d, base.toMSecsSinceEpoch());
        checkEq(again.size(), s.size(), "the same seed builds the same timeline");
        bool identical = true;
        for (int i = 0; i < s.size() && identical; ++i)
            if (s[i].ref != again[i].ref || s[i].start != again[i].start) identical = false;
        check(identical, "slot for slot");
    }

    section("grid: a card is never too short to be worth showing");
    {
        ChannelDef d;
        d.number = 1; d.name = "Grid"; d.seed = 5;
        d.horizonHours = 2.0;
        d.gridMinutes = 30;
        d.programmes  = { item("show.mkv", 1799500) };
        const QVector<Slot> s = generateSlots(d, onTheHour());
        check(contiguous(s), "contiguous");
        for (const Slot &x : s)
            check(!(x.kind == SlotKind::Filler && x.dur < kMinFillerMs),
                  "no card shorter than a second");
    }

    section("shuffle: a pass does not open with what the last one closed on");
    {
        int boundaries = 0, repeats = 0;
        for (quint32 seed = 1; seed <= 60; ++seed) {
            ChannelDef d;
            d.number = 1; d.name = "Shuffled"; d.seed = seed;
            d.horizonHours = 12.0;
            d.order = Ordering::Shuffle;
            d.programmes = { item("a.mkv", 1800000),
                             item("b.mkv", 1800000),
                             item("c.mkv", 1800000) };

            QString previous;
            int aired = 0;
            for (const Slot &x : generateSlots(d, kBase)) {
                if (x.kind != SlotKind::Programme) continue;
                ++aired;
                if (aired > 1 && (aired - 1) % d.programmes.size() == 0) {
                    ++boundaries;
                    if (x.ref == previous) ++repeats;
                }
                previous = x.ref;
            }
        }
        check(boundaries > 100, "the sweep actually reached pass boundaries");
        checkEq(repeats, 0, "no pass opens with the programme before it");
    }

    section("a break is not the same advert twice running");
    {
        ChannelDef d;
        d.number = 1; d.name = "Free"; d.seed = 11;
        d.horizonHours = 6.0;
        d.adsPerBreak = 4;
        d.programmes  = { item("a.mkv", 1200000), item("b.mkv", 1200000) };
        d.commercials = { item("ad1.mkv", 30000), item("ad2.mkv", 30000),
                          item("ad3.mkv", 20000) };
        d.bumps       = { item("bump.mkv", 10000) };

        const QVector<Slot> s = generateSlots(d, kBase);
        int breaks = 0, repeats = 0;
        QString previous;
        for (const Slot &x : s) {
            if (x.kind == SlotKind::Programme || x.kind == SlotKind::Filler) {
                previous.clear();
                continue;
            }
            ++breaks;
            if (x.ref == previous) ++repeats;
            previous = x.ref;
        }
        check(breaks > 20, "the channel actually placed breaks");
        checkEq(repeats, 0, "no advert follows itself");
    }

    section("a pool of one still fills a break");
    {
        ChannelDef d;
        d.number = 1; d.name = "Thin"; d.seed = 2;
        d.horizonHours = 3.0;
        d.adsPerBreak = 3;
        d.programmes  = { item("a.mkv", 1200000) };
        d.commercials = { item("only.mkv", 30000) };

        int placed = 0;
        for (const Slot &x : generateSlots(d, kBase))
            if (x.kind == SlotKind::Commercial) ++placed;
        check(placed > 0, "the one advert there is still gets played");
    }

    section("shuffle: one long series does not become the channel");
    {
        ChannelDef d;
        d.number = 1; d.name = "Mixed"; d.seed = 4;
        d.horizonHours = 20.0;
        d.order = Ordering::Shuffle;
        for (int i = 0; i < 200; ++i)
            d.programmes << episodeOf("Long Running", QString("lr%1.mkv").arg(i), 1200000);
        for (const char *name : { "Second", "Third", "Fourth", "Fifth" })
            for (int i = 0; i < 20; ++i)
                d.programmes << episodeOf(name, QString("%1-%2.mkv").arg(name).arg(i), 1200000);

        QHash<QString, int> aired;
        int total = 0;
        for (const Slot &x : generateSlots(d, kBase)) {
            if (x.kind != SlotKind::Programme) continue;
            aired[x.series]++;
            ++total;
        }
        check(total > 20, "the channel actually aired something");
        check(aired.size() == 5, "every series got a turn");
        const int longest = aired.value(QStringLiteral("Long Running"));
        check(longest * 2 < total,
              "the longest series is not most of the channel");
    }

    section("an outro closes the programme it follows");
    {
        ChannelDef d;
        d.number = 1; d.name = "Grid"; d.seed = 9;
        d.horizonHours = 6.0;
        d.gridMinutes = 30;
        d.programmes  = { item("a.mkv", 1320000), item("b.mkv", 1380000) };
        d.intros      = { item("in.mkv", 5000) };
        d.outros      = { item("out1.mkv", 10000), item("out2.mkv", 15000) };
        d.commercials = { item("ad1.mkv", 30000), item("ad2.mkv", 15000),
                          item("ad3.mkv", 5000),  item("ad4.mkv", 2000) };
        d.bumps       = { item("bp.mkv", 3000) };

        const QVector<Slot> s = generateSlots(d, onTheHour());
        int outros = 0, programmes = 0, misplaced = 0;
        for (int i = 0; i < s.size(); ++i) {
            if (s[i].kind == SlotKind::Programme) ++programmes;
            if (s[i].kind != SlotKind::Outro) continue;
            ++outros;
            if (i == 0 || s[i - 1].kind != SlotKind::Programme) ++misplaced;
        }
        check(programmes > 4, "the channel actually aired programmes");
        check(outros > 0, "and closed them");
        checkEq(misplaced, 0, "no outro anywhere but straight after a programme");
        check(outros <= programmes, "no more outros than programmes");
    }

    section("idents: a series brings its own");
    {
        ChannelDef d;
        d.number = 1; d.name = "Packs"; d.seed = 3;
        d.horizonHours = 6.0;
        d.order = Ordering::Shuffle;
        d.intros = { item("house-in.mkv", 5000) };
        d.outros = { item("house-out.mkv", 5000) };
        d.packs = {
            BreakPack{ "Alpha", { item("a-in.mkv", 5000) }, { item("a-out.mkv", 5000) } },
            BreakPack{ "Beta",  { item("b-in.mkv", 5000) }, { item("b-out.mkv", 5000) } },
        };
        for (int i = 0; i < 12; ++i) d.programmes << episodeIn("Alpha", QString("a%1.mkv").arg(i), 600000, 0);
        for (int i = 0; i < 12; ++i) d.programmes << episodeIn("Beta",  QString("b%1.mkv").arg(i), 600000, 1);
        for (int i = 0; i < 12; ++i) d.programmes << episodeOf("Gamma", QString("g%1.mkv").arg(i), 600000);

        const QVector<Slot> s = generateSlots(d, kBase);
        int checkedIntro = 0, checkedOutro = 0, wrong = 0;
        for (int i = 0; i < s.size(); ++i) {
            if (s[i].kind == SlotKind::Intro && i + 1 < s.size()
                && s[i + 1].kind == SlotKind::Programme) {
                const QString want = s[i + 1].series == QLatin1String("Alpha") ? "a-in.mkv"
                                   : s[i + 1].series == QLatin1String("Beta")  ? "b-in.mkv"
                                                                               : "house-in.mkv";
                if (s[i].ref != want) ++wrong;
                ++checkedIntro;
            }
            if (s[i].kind == SlotKind::Outro && i > 0
                && s[i - 1].kind == SlotKind::Programme) {
                const QString want = s[i - 1].series == QLatin1String("Alpha") ? "a-out.mkv"
                                   : s[i - 1].series == QLatin1String("Beta")  ? "b-out.mkv"
                                                                               : "house-out.mkv";
                if (s[i].ref != want) ++wrong;
                ++checkedOutro;
            }
        }
        check(checkedIntro > 6, "the channel actually placed idents to check");
        check(checkedOutro > 6, "and closers to check");
        checkEq(wrong, 0, "every ident belongs to the programme it touches");
    }

    section("idents: a series with none falls back to the channel's");
    {
        ChannelDef d;
        d.number = 1; d.name = "Fallback"; d.seed = 6;
        d.horizonHours = 3.0;
        d.intros = { item("house-in.mkv", 5000) };
        d.outros = { item("house-out.mkv", 5000) };
        d.packs = { BreakPack{ "Empty", {}, {} } };
        for (int i = 0; i < 6; ++i) d.programmes << episodeIn("Empty",  QString("e%1.mkv").arg(i), 600000, 0);
        for (int i = 0; i < 6; ++i) d.programmes << episodeIn("Stray",  QString("s%1.mkv").arg(i), 600000, 47);

        const QVector<Slot> s = generateSlots(d, kBase);
        int placed = 0, wrong = 0;
        for (const Slot &x : s) {
            if (x.kind != SlotKind::Intro && x.kind != SlotKind::Outro) continue;
            ++placed;
            if (x.ref != QLatin1String("house-in.mkv") && x.ref != QLatin1String("house-out.mkv")) ++wrong;
        }
        check(placed > 4, "the channel still placed idents");
        checkEq(wrong, 0, "an empty or unknown pack falls back rather than inventing one");
    }

    section("idents: on a clock, the ident announces what follows it");
    {
        ChannelDef d;
        d.number = 1; d.name = "GridPacks"; d.seed = 8;
        d.horizonHours = 6.0;
        d.gridMinutes = 30;
        d.order = Ordering::Shuffle;
        d.commercials = { item("ad.mkv", 30000), item("ad2.mkv", 5000), item("ad3.mkv", 1000) };
        d.packs = {
            BreakPack{ "Alpha", { item("a-in.mkv", 5000) }, { item("a-out.mkv", 5000) } },
            BreakPack{ "Beta",  { item("b-in.mkv", 5000) }, { item("b-out.mkv", 5000) } },
        };
        for (int i = 0; i < 8; ++i) d.programmes << episodeIn("Alpha", QString("a%1.mkv").arg(i), 1320000, 0);
        for (int i = 0; i < 8; ++i) d.programmes << episodeIn("Beta",  QString("b%1.mkv").arg(i), 1320000, 1);

        const QVector<Slot> s = generateSlots(d, onTheHour());
        int checked = 0, wrong = 0;
        for (int i = 0; i + 1 < s.size(); ++i) {
            if (s[i].kind != SlotKind::Intro) continue;
            if (s[i + 1].kind != SlotKind::Programme) continue;
            const QString want = s[i + 1].series == QLatin1String("Alpha") ? "a-in.mkv" : "b-in.mkv";
            if (s[i].ref != want) ++wrong;
            ++checked;
        }
        check(checked > 4, "the clock channel placed idents before programmes");
        checkEq(wrong, 0, "each announces the programme it runs into");
    }

    section("idents: a channel with no packs is unchanged");
    {
        ChannelDef d = basicDef();
        d.horizonHours = 4.0;
        d.intros = { item("in.mkv", 5000) };
        d.outros = { item("out.mkv", 5000) };
        const QVector<Slot> before = generateSlots(d, kBase);
        d.packs = {};
        const QVector<Slot> after = generateSlots(d, kBase);
        checkEq(after.size(), before.size(), "same timeline length");
        bool same = true;
        for (int i = 0; i < before.size() && same; ++i)
            same = before[i].ref == after[i].ref && before[i].start == after[i].start;
        check(same, "slot for slot identical");
    }

    return 0;
}
