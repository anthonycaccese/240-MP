#include "../ChannelSchedule.h"
#include "TestHarness.h"

#include <QByteArray>
#include <QString>
#include <cstdio>

using namespace vchan;
using vtest::check;
using vtest::checkEq;
using vtest::checkStr;
using vtest::section;

namespace {

constexpr qint64 kBase = 1787000000000LL;

QByteArray contiguousJson(int n, qint64 durMs) {
    QString slotJson;
    for (int i = 0; i < n; ++i) {
        if (i) slotJson += QLatin1String(",");
        slotJson += QStringLiteral(
                     R"({"start":%1,"dur":%2,"kind":"programme","src":"local",)"
                     R"("ref":"Shows/ep%3.mkv","title":"Ep %3"})")
                     .arg(kBase + i * durMs).arg(durMs).arg(i);
    }
    return QStringLiteral(
               R"({"channel":3,"seed":42,"generated_at":%1,"horizon_end":%2,"slots":[%3]})")
        .arg(kBase - 1000).arg(kBase + n * durMs).arg(slotJson).toUtf8();
}
}

int runChannelScheduleTests() {
    section("parse: well-formed");
    {
        QString err;
        int dropped = -1;
        const auto s = ChannelSchedule::fromJson(contiguousJson(4, 60000), &err, &dropped);
        check(s.isValid(), "valid schedule parses");
        check(err.isEmpty(), "no error reported");
        checkEq(dropped, 0, "nothing dropped");
        checkEq(s.channel(), 3, "channel number");
        checkEq(s.slotCount(), 4, "slot count");
        checkEq(s.seed(), 42, "seed");
        check(s.slotList().first().kind == SlotKind::Programme, "kind parsed");
        check(s.slotList().first().src == SlotSource::Local, "src parsed");
    }

    section("parse: rejects and drops rather than crashing");
    {
        QString err;
        check(!ChannelSchedule::fromJson("", &err).isValid(), "empty input invalid");
        check(!err.isEmpty(), "empty input reports why");

        check(!ChannelSchedule::fromJson("{not json", &err).isValid(), "garbage invalid");
        check(!ChannelSchedule::fromJson("[]", &err).isValid(), "array top level invalid");
        check(!ChannelSchedule::fromJson(R"({"slots":[]})", &err).isValid(),
              "missing channel invalid");
        check(!ChannelSchedule::fromJson(R"({"channel":1})", &err).isValid(),
              "missing slots invalid");

        int dropped = -1;
        const QByteArray mixed = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"("not an object",)"
            R"({"start":%2,"dur":0,"ref":"a.mkv"},)"
            R"({"start":0,"dur":1000,"ref":"b.mkv"},)"
            R"({"start":%2,"dur":1000,"ref":"  "},)"
            R"({"start":%2,"dur":1000,"ref":"good.mkv"}]})")
            .arg(kBase - 1000).arg(kBase).toUtf8();
        const auto s = ChannelSchedule::fromJson(mixed, &err, &dropped);
        check(s.isValid(), "survives a mostly-bad slot list");
        checkEq(s.slotCount(), 1, "only the good slot kept");
        checkEq(dropped, 4, "four slots dropped");
    }

    section("parse: defensive ordering");
    {
        const QByteArray unsorted = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"({"start":%4,"dur":1000,"ref":"c.mkv"},)"
            R"({"start":%2,"dur":1000,"ref":"a.mkv"},)"
            R"({"start":%3,"dur":1000,"ref":"b.mkv"}]})")
            .arg(kBase - 1000).arg(kBase).arg(kBase + 1000).arg(kBase + 2000).toUtf8();
        const auto s = ChannelSchedule::fromJson(unsorted);
        check(s.isValid(), "unsorted input still valid");
        checkEq(s.slotCount(), 3, "all three kept");
        check(s.slotList()[0].ref == QLatin1String("a.mkv"), "sorted: first");
        check(s.slotList()[2].ref == QLatin1String("c.mkv"), "sorted: last");

        int dropped = -1;
        const QByteArray overlap = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"({"start":%2,"dur":5000,"ref":"a.mkv"},)"
            R"({"start":%3,"dur":5000,"ref":"b.mkv"}]})")
            .arg(kBase - 1000).arg(kBase).arg(kBase + 2000).toUtf8();
        const auto o = ChannelSchedule::fromJson(overlap, nullptr, &dropped);
        checkEq(o.slotCount(), 1, "overlapping slot dropped");
        checkEq(dropped, 1, "overlap counted as a drop");
    }

    section("parse: the horizon comes from the slots, not the header");
    {
        const auto over = ChannelSchedule::fromJson(QStringLiteral(
            R"({"channel":1,"generated_at":%1,"horizon_end":%4,"slots":[)"
            R"({"start":%2,"dur":1000,"ref":"a.mkv"},)"
            R"({"start":%3,"dur":1000,"ref":"b.mkv"}]})")
            .arg(kBase - 1000).arg(kBase).arg(kBase + 1000)
            .arg(kBase + 99999999).toUtf8());
        check(over.isValid(), "over-claiming header still parses");
        checkEq(over.horizonEnd(), kBase + 2000, "header claiming more is ignored");

        const auto under = ChannelSchedule::fromJson(QStringLiteral(
            R"({"channel":1,"generated_at":%1,"horizon_end":%2,"slots":[)"
            R"({"start":%2,"dur":1000,"ref":"a.mkv"},)"
            R"({"start":%3,"dur":1000,"ref":"b.mkv"}]})")
            .arg(kBase - 1000).arg(kBase).arg(kBase + 1000).toUtf8());
        checkEq(under.horizonEnd(), kBase + 2000, "header claiming less is ignored");

        int dropped = -1;
        const auto lost = ChannelSchedule::fromJson(QStringLiteral(
            R"({"channel":1,"generated_at":%1,"horizon_end":%4,"slots":[)"
            R"({"start":%2,"dur":1000,"ref":"a.mkv"},)"
            R"({"start":%3,"dur":0,"ref":"b.mkv"}]})")
            .arg(kBase - 1000).arg(kBase).arg(kBase + 1000)
            .arg(kBase + 2000).toUtf8(), nullptr, &dropped);
        checkEq(dropped, 1, "zero-length slot dropped");
        checkEq(lost.horizonEnd(), kBase + 1000, "horizon follows what survived");
    }

    section("indexAt / offsetInto");
    {
        const auto s = ChannelSchedule::fromJson(contiguousJson(4, 60000));
        checkEq(s.indexAt(kBase - 1),          -1, "before first slot");
        checkEq(s.indexAt(kBase),               0, "exactly at first start");
        checkEq(s.indexAt(kBase + 59999),       0, "last ms of slot 0");
        checkEq(s.indexAt(kBase + 60000),       1, "boundary belongs to next slot");
        checkEq(s.indexAt(kBase + 180000),      3, "final slot");
        checkEq(s.indexAt(kBase + 239999),      3, "last ms of timeline");
        checkEq(s.indexAt(kBase + 240000),     -1, "one ms past the end");

        checkEq(s.offsetInto(0, kBase + 15000), 15000, "offset mid-slot");
        checkEq(s.offsetInto(0, kBase - 5000),      0, "never seeks negative");
        checkEq(s.offsetInto(0, kBase + 999999), 59999, "clamped inside the slot");
        checkEq(s.offsetInto(-1, kBase),            0, "bad index is 0, not UB");
        checkEq(s.offsetInto(99, kBase),            0, "out-of-range index is 0");
    }

    section("statusAt");
    {
        const auto s = ChannelSchedule::fromJson(contiguousJson(3, 60000));
        check(s.statusAt(kBase + 1000)   == Status::Ok,          "playable");
        check(s.statusAt(kBase - 5000)   == Status::BeforeStart, "before start");
        check(s.statusAt(kBase + 180000) == Status::PastHorizon, "past horizon");

        check(s.statusAt(1000) == Status::ClockUnsane, "epoch-1970 clock refused");

        check(ChannelSchedule{}.statusAt(kBase) == Status::NoSchedule,
              "default-constructed is NoSchedule");

        const QByteArray gapped = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"({"start":%2,"dur":1000,"ref":"a.mkv"},)"
            R"({"start":%3,"dur":1000,"ref":"b.mkv"}]})")
            .arg(kBase - 1000).arg(kBase).arg(kBase + 60000).toUtf8();
        const auto g = ChannelSchedule::fromJson(gapped);
        check(g.statusAt(kBase + 5000) == Status::OffAirGap, "gap is OffAirGap");
    }

    section("runWindow");
    {
        const auto s = ChannelSchedule::fromJson(contiguousJson(50, 60000));

        const auto byItems = s.runWindow(0, /*windowMs*/ 24LL * 3600 * 1000, /*maxItems*/ 5);
        checkEq(byItems.size(), 5, "item cap honoured");
        checkEq(byItems.first(), 0, "window starts where asked");

        const auto byTime = s.runWindow(0, 30LL * 60 * 1000, 1000);
        checkEq(byTime.size(), 30, "duration cap honoured");

        const auto atEnd = s.runWindow(49, kRunWindowMs, kMaxRunItems);
        checkEq(atEnd.size(), 1, "stops at end of timeline");

        check(s.runWindow(-1).isEmpty(), "negative index yields nothing");
        check(s.runWindow(999).isEmpty(), "out-of-range index yields nothing");
        checkEq(s.runWindow(0, kRunWindowMs, 0).size(), 1, "maxItems<1 still returns one");

        const QByteArray gapped = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"({"start":%2,"dur":1000,"ref":"a.mkv"},)"
            R"({"start":%3,"dur":1000,"ref":"b.mkv"},)"
            R"({"start":%4,"dur":1000,"ref":"c.mkv"}]})")
            .arg(kBase - 1000).arg(kBase).arg(kBase + 1000).arg(kBase + 60000).toUtf8();
        const auto g = ChannelSchedule::fromJson(gapped);
        checkEq(g.runWindow(0).size(), 2, "run stops at the gap");
    }

    section("clockLooksSane");
    {
        check(!clockLooksSane(0, 0),                    "epoch zero is not sane");
        check(!clockLooksSane(1000, 0),                 "1970 is not sane");
        check(clockLooksSane(kBase, 0),                 "2026 with no stamp is sane");
        check(clockLooksSane(kBase, kBase - 1000),      "just after generation is sane");
        check(!clockLooksSane(kBase, kBase + 3600000),  "an hour behind generation is not");
        check(clockLooksSane(kBase, kBase + 1000),      "tiny skew tolerated");
    }

    section("programmeBlocks");
    {
        QString j;
        qint64 t = kBase;
        const char *kinds[] = {"intro","programme","outro","commercial"};
        const qint64 durs[] = {2000, 60000, 3000, 30000};
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (int k = 0; k < 4; ++k) {
                if (!j.isEmpty()) j += QLatin1String(",");
                j += QStringLiteral(R"({"start":%1,"dur":%2,"kind":"%3","src":"local","ref":"r%4.mkv","title":"Show %5"})")
                        .arg(t).arg(durs[k]).arg(QLatin1String(kinds[k])).arg(cycle*4+k).arg(cycle);
                t += durs[k];
            }
        }
        const QByteArray json = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[%2]})").arg(kBase-1000).arg(j).toUtf8();
        const auto s = ChannelSchedule::fromJson(json);
        check(s.isValid(), "block fixture parses");

        const auto blocks = s.programmeBlocks(kBase, t);
        checkEq(blocks.size(), 3, "one block per programme, not per slot");

        checkEq(blocks[0].start, kBase, "block starts at its intro");
        checkEq(blocks[0].dur, 95000, "block absorbs outro and ad break");
        check(blocks[0].title == QLatin1String("Show 0"), "titled by the programme");

        bool tiles = true;
        for (int i = 1; i < blocks.size(); ++i)
            if (blocks[i].start != blocks[i-1].end()) tiles = false;
        check(tiles, "blocks tile the timeline with no gaps");

        check(s.programmeBlocks(kBase, kBase + 1).size() == 1, "window clips to overlap");
        check(s.programmeBlocks(t + 100000, t + 200000).isEmpty(), "window past the end is empty");
        check(s.programmeBlocks(kBase, kBase).isEmpty(), "zero-width window is empty");
        check(s.programmeBlocks(kBase + 96000, t).size() == 2, "window starting mid-timeline");

        const QByteArray adsOnly = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"({"start":%2,"dur":1000,"kind":"commercial","src":"local","ref":"a.mkv"}]})")
            .arg(kBase-1000).arg(kBase).toUtf8();
        check(ChannelSchedule::fromJson(adsOnly).programmeBlocks(kBase, t).isEmpty(),
              "no programmes means no listings");
    }

    section("guide: slivers are gathered, real listings are not");
    {
        const auto s = ChannelSchedule::fromJson(contiguousJson(20, 12000));
        check(s.isValid(), "twenty twelve-second programmes");

        const auto raw = s.programmeBlocks(kBase, kBase + 240000);
        checkEq(raw.size(), 20, "each is its own listing when nothing is asked for");
        checkEq(raw.first().count, 1, "and stands for itself");

        const auto few = s.programmeBlocks(kBase, kBase + 240000, 60000);
        checkEq(few.size(), 4, "a run of slivers becomes readable listings");
        checkEq(few.first().count, 5, "each saying how many it stands for");
        checkEq(few.first().dur, 60000, "and spanning them");
        checkEq(few.first().start, kBase, "starting where the run did");
        qint64 covered = 0;
        for (const auto &b : few) covered += b.dur;
        checkEq(covered, 240000, "with the run still tiled end to end");

        const auto all = s.programmeBlocks(kBase, kBase + 240000, 5000);
        checkEq(all.size(), 20, "listings wide enough to read are left alone");
    }

    section("guide: a long programme is never folded into its neighbours");
    {
        const QByteArray mixed = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"({"start":%2,"dur":10000,"kind":"programme","ref":"a.mkv","title":"A"},)"
            R"({"start":%3,"dur":10000,"kind":"programme","ref":"b.mkv","title":"B"},)"
            R"({"start":%4,"dur":600000,"kind":"programme","ref":"c.mkv","title":"C"},)"
            R"({"start":%5,"dur":10000,"kind":"programme","ref":"d.mkv","title":"D"},)"
            R"({"start":%6,"dur":10000,"kind":"programme","ref":"e.mkv","title":"E"}]})")
            .arg(kBase - 1000).arg(kBase).arg(kBase + 10000).arg(kBase + 20000)
            .arg(kBase + 620000).arg(kBase + 630000).toUtf8();

        const auto s = ChannelSchedule::fromJson(mixed);
        check(s.isValid(), "parses");
        const auto b = s.programmeBlocks(kBase, kBase + 700000, 60000);
        checkEq(b.size(), 3, "two gathered runs and the programme between them");
        checkEq(b[0].count, 2, "the run before");
        checkEq(b[1].count, 1, "the programme itself, untouched");
        checkStr(b[1].title, QStringLiteral("C"), "and still named");
        checkEq(b[2].count, 2, "the run after");
    }

    section("what is on is the programme, not the advert in its break");
    {
        const QByteArray withBreak = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"({"start":%2,"dur":5000,"kind":"intro","ref":"id.mkv","title":"IDENT"},)"
            R"({"start":%3,"dur":60000,"kind":"programme","ref":"ep.mkv",)"
            R"("title":"The Episode","series":"The Show"},)"
            R"({"start":%4,"dur":5000,"kind":"outro","ref":"out.mkv","title":"OUTRO"},)"
            R"({"start":%5,"dur":30000,"kind":"commercial","ref":"ad.mkv","title":"ADVERT"},)"
            R"({"start":%6,"dur":60000,"kind":"programme","ref":"ep2.mkv",)"
            R"("title":"The Next One","series":"The Show"}]})")
            .arg(kBase - 1000).arg(kBase).arg(kBase + 5000).arg(kBase + 65000)
            .arg(kBase + 70000).arg(kBase + 100000).toUtf8();

        const auto s = ChannelSchedule::fromJson(withBreak);
        check(s.isValid(), "parses");

        ChannelSchedule::Block b;
        check(s.blockAt(kBase + 1000, &b), "something is on during the ident");
        checkStr(b.title, QStringLiteral("The Episode"), "and it is the programme it leads into");

        check(s.blockAt(kBase + 30000, &b), "something is on mid-programme");
        checkStr(b.title, QStringLiteral("The Episode"), "which is the programme");

        check(s.blockAt(kBase + 80000, &b), "something is on during the break");
        checkStr(b.title, QStringLiteral("The Episode"), "still the programme, not the advert");
        checkStr(b.series, QStringLiteral("The Show"), "with its series");

        checkEq(b.start, kBase, "the listing began with the ident");
        checkEq(b.dur, 100000, "and runs to where the next one starts");

        check(s.blockAt(kBase + 110000, &b), "the next programme is on later");
        checkStr(b.title, QStringLiteral("The Next One"), "and is named for itself");

        check(!s.blockAt(kBase - 50000), "nothing is on before it begins");
    }

    section("slot sources");
    {
        check(slotSourceFromString("local")    == SlotSource::Local,    "local");
        check(slotSourceFromString("plex")     == SlotSource::Plex,     "plex");
        check(slotSourceFromString("jellyfin") == SlotSource::Jellyfin, "jellyfin");
        check(slotSourceFromString("emby")     == SlotSource::Emby,     "emby");
        check(slotSourceFromString("JELLYFIN") == SlotSource::Jellyfin, "case insensitive");
        check(slotSourceFromString(" emby ")   == SlotSource::Emby,     "trimmed");
        check(slotSourceFromString("kodi")     == SlotSource::Local,    "unknown reads as local");
        check(slotSourceFromString("")         == SlotSource::Local,    "empty reads as local");

        const SlotSource all[] = { SlotSource::Local, SlotSource::Plex,
                                   SlotSource::Jellyfin, SlotSource::Emby };
        bool roundTrips = true;
        for (SlotSource src : all)
            if (slotSourceFromString(slotSourceToString(src)) != src) roundTrips = false;
        check(roundTrips, "every source round trips through its string");

        check(!isServerSource(SlotSource::Local),    "local is not a server source");
        check(isServerSource(SlotSource::Plex),      "plex is");
        check(isServerSource(SlotSource::Jellyfin),  "jellyfin is");
        check(isServerSource(SlotSource::Emby),      "emby is");

        const QByteArray j = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"({"start":%2,"dur":1000,"kind":"programme","src":"jellyfin","ref":"abc123"}]})")
            .arg(kBase - 1000).arg(kBase).toUtf8();
        const auto s2 = ChannelSchedule::fromJson(j);
        check(s2.isValid(), "a jellyfin slot parses");
        check(s2.slotList().first().src == SlotSource::Jellyfin, "and keeps its source");
    }

    section("guide: a station card is not a listing");
    {
        const QByteArray j = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"({"start":%2,"dur":600000,"kind":"programme","src":"local","ref":"a.mkv","title":"A"},)"
            R"({"start":%3,"dur":300000,"kind":"filler","src":"local","ref":"","title":"Station"},)"
            R"({"start":%4,"dur":600000,"kind":"programme","src":"local","ref":"b.mkv","title":"B"}]})")
            .arg(kBase - 1000).arg(kBase).arg(kBase + 600000).arg(kBase + 900000).toUtf8();

        const auto s = ChannelSchedule::fromJson(j);
        check(s.isValid(), "parses");
        checkEq(s.slotCount(), 3, "all three slots are kept");
        check(s.slotList()[1].kind == SlotKind::Filler, "the card keeps its kind");

        const auto blocks = s.programmeBlocks(kBase, kBase + 1500000);
        checkEq(blocks.size(), 2, "but the guide lists only the two programmes");
        checkStr(blocks[0].title, QStringLiteral("A"), "first listing");
        checkEq(blocks[0].dur, 900000, "and that listing covers the dead air after it");
        checkStr(blocks[1].title, QStringLiteral("B"), "second listing");
    }

    section("tuner: a station card is playable, not a gap");
    {
        const QByteArray j = QStringLiteral(
            R"({"channel":1,"generated_at":%1,"slots":[)"
            R"({"start":%2,"dur":600000,"kind":"filler","src":"local","ref":"","title":"Station"}]})")
            .arg(kBase - 1000).arg(kBase).toUtf8();
        const auto s = ChannelSchedule::fromJson(j);
        check(s.isValid(), "parses");
        check(s.statusAt(kBase + 1000) == Status::Ok, "and is on air while it holds");
        checkEq(s.indexAt(kBase + 1000), 0, "landing on the card itself");
    }

    return 0;
}
