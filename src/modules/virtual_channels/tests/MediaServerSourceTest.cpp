#include "../MediaServerSource.h"
#include "FakeMediaServer.h"
#include "TestHarness.h"

#include <QVariantList>

using namespace vchan;
using vtest::check;
using vtest::checkEq;
using vtest::checkStr;
using vtest::section;

namespace {

struct Result {
    bool finished = false;
    bool failed   = false;
    QString reason;
    QVector<MediaItem> items;
    QString browseKind;
    QVariantList browseItems;

    void attach(MediaServerSource *src) {
        QObject::connect(src, &MediaServerSource::enumerationFinished,
                         [this](const QVector<MediaItem> &i) { finished = true; items = i; });
        QObject::connect(src, &MediaServerSource::enumerationFailed,
                         [this](const QString &r) { failed = true; reason = r; });
        QObject::connect(src, &MediaServerSource::browseReady,
                         [this](const QString &k, const QVariantList &i) {
                             finished = true; browseKind = k; browseItems = i; });
        QObject::connect(src, &MediaServerSource::browseFailed,
                         [this](const QString &k, const QString &r) {
                             failed = true; browseKind = k; reason = r; });
    }
};

void stockTvServer(FakeMediaServer &fake) {
    fake.libraries = QVariantList{
        FakeMediaServer::shelf("continue_watching", "CONTINUE WATCHING"),
        FakeMediaServer::shelf("up_next", "NEXT UP"),
        FakeMediaServer::library("tv1", "TV SHOWS", "tvshows"),
        FakeMediaServer::library("mv1", "MOVIES", "movies"),
    };
    fake.itemsFor["tv1|Episode"] = QVariantList{
        FakeMediaServer::episode("e1", "sA", "Alpha", 1, 1, 1200000.0),
        FakeMediaServer::episode("e2", "sA", "Alpha", 1, 2, 1200000.0),
        FakeMediaServer::episode("e3", "sA", "Alpha", 2, 1, 1200000.0),
        FakeMediaServer::episode("e4", "sB", "Beta",  1, 1,  600000.0),
    };
    fake.itemsFor["mv1|Movie"] = QVariantList{
        FakeMediaServer::movie("m1", "A Film", 5400000.0,
                               { QStringLiteral("Horror"), QStringLiteral("Comedy") }),
        FakeMediaServer::movie("m2", "Another Film", 5400000.0,
                               { QStringLiteral("Comedy") }),
    };
}

QStringList refsOf(const QVector<MediaItem> &items) {
    QStringList out;
    for (const MediaItem &m : items) out << m.ref;
    return out;
}
}

int runMediaServerSourceTests() {
    section("media server: season keys");
    {
        checkStr(MediaServerSource::seasonKey("abc", 3), QStringLiteral("abc:3"), "key format");

        QString series; int number = -1;
        check(MediaServerSource::parseSeasonKey("abc:3", &series, &number), "parses");
        checkStr(series, QStringLiteral("abc"), "series id recovered");
        checkEq(number, 3, "season number recovered");

        check(MediaServerSource::parseSeasonKey("abc:0", &series, &number), "season zero parses");
        checkEq(number, 0, "season zero survives");

        check(!MediaServerSource::parseSeasonKey("abc", nullptr, nullptr), "no colon rejected");
        check(!MediaServerSource::parseSeasonKey("abc:", nullptr, nullptr), "empty number rejected");
        check(!MediaServerSource::parseSeasonKey(":3", nullptr, nullptr), "empty series rejected");
        check(!MediaServerSource::parseSeasonKey("abc:x", nullptr, nullptr), "non-numeric rejected");
        check(!MediaServerSource::parseSeasonKey("", nullptr, nullptr), "empty rejected");
    }

    section("media server: item mapping");
    {
        MediaItem mi;
        check(MediaServerSource::itemToMedia(
                  FakeMediaServer::episode("e1", "sA", "Alpha", 1, 2, 1200000.0, "Pilot"),
                  SlotSource::Jellyfin, &mi), "an episode maps");
        checkEq(mi.durMs, 1200000, "duration is taken as milliseconds, not reconverted");
        checkStr(mi.ref, QStringLiteral("e1"), "ref is the item id");
        checkStr(mi.series, QStringLiteral("Alpha"), "series from grandparentTitle");
        checkStr(mi.ep, QStringLiteral("S01E02"), "episode label is zero padded");
        checkStr(mi.title, QStringLiteral("Pilot"), "title");
        check(mi.src == SlotSource::Jellyfin, "source is carried through");

        QVariantMap odd = FakeMediaServer::episode("e9", "sA", "Alpha", 1, 1, 1200000.4);
        check(MediaServerSource::itemToMedia(odd, SlotSource::Emby, &mi), "fractional ms maps");
        checkEq(mi.durMs, 1200000, "fractional milliseconds truncate, not corrupt");

        QVariantMap noDur = FakeMediaServer::episode("e2", "sA", "Alpha", 1, 1, 0.0);
        check(!MediaServerSource::itemToMedia(noDur, SlotSource::Jellyfin, &mi),
              "no duration is refused");
        QVariantMap negative = FakeMediaServer::episode("e3", "sA", "Alpha", 1, 1, -5.0);
        check(!MediaServerSource::itemToMedia(negative, SlotSource::Jellyfin, &mi),
              "negative duration is refused");
        QVariantMap noId = FakeMediaServer::episode("", "sA", "Alpha", 1, 1, 1000.0);
        check(!MediaServerSource::itemToMedia(noId, SlotSource::Jellyfin, &mi),
              "no id is refused");
        check(!MediaServerSource::itemToMedia(QVariantMap{}, SlotSource::Jellyfin, nullptr),
              "null out is refused");

        check(MediaServerSource::itemToMedia(FakeMediaServer::movie("m1", "A Film", 5400000.0),
                                             SlotSource::Emby, &mi), "a film maps");
        check(mi.ep.isEmpty(), "a film gets no episode label");
    }

    section("media server: availability");
    {
        MediaServerSource src;
        check(!src.available(SlotSource::Jellyfin), "unset source is unavailable");
        check(!src.available(SlotSource::Emby), "unset emby is unavailable");

        MediaServerSource::Request req;
        req.src = SlotSource::Jellyfin;
        check(!src.enumerate(req), "enumerating an unavailable source is refused");

        QObject notABackend;
        src.setBackend(SlotSource::Jellyfin, &notABackend);
        check(!src.available(SlotSource::Jellyfin), "a backend missing its API is refused");

        FakeMediaServer fake;
        src.setBackend(SlotSource::Jellyfin, &fake);
        check(src.available(SlotSource::Jellyfin), "a complete backend is accepted");
        check(!src.available(SlotSource::Emby), "and does not make the other one available");

        src.setBackend(SlotSource::Plex, &fake);
        check(!src.available(SlotSource::Plex), "plex is not handled here");
    }

    section("media server: enumeration");
    {
        FakeMediaServer fake;
        stockTvServer(fake);
        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);
        Result r; r.attach(&src);

        MediaServerSource::Request req;
        req.src = SlotSource::Jellyfin;
        check(src.enumerate(req), "enumeration starts");
        check(r.finished && !r.failed, "and finishes");
        checkEq(r.items.size(), 4, "every episode is collected");
        check(!src.busy(), "and the source is idle again");

        check(!fake.calls.contains("load_items(mv1,Episode)"), "the film library is not walked");
        check(!fake.calls.contains("load_items(continue_watching,Episode)"),
              "the continue-watching shelf is not mistaken for a library");
        check(!fake.calls.contains("load_items(up_next,Episode)"),
              "the next-up shelf is not mistaken for a library");

        checkStr(refsOf(r.items).join(","), QStringLiteral("e1,e2,e3,e4"), "playout order");
    }

    section("media server: series filter");
    {
        FakeMediaServer fake;
        stockTvServer(fake);
        MediaServerSource src;
        src.setBackend(SlotSource::Emby, &fake);
        Result r; r.attach(&src);

        MediaServerSource::Request req;
        req.src   = SlotSource::Emby;
        req.match = QStringList{ QStringLiteral("beta") };
        check(src.enumerate(req), "starts");
        checkEq(r.items.size(), 1, "only the named series is kept, matched case-insensitively");
        checkStr(r.items.first().ref, QStringLiteral("e4"), "and it is the right one");
        check(r.items.first().src == SlotSource::Emby, "tagged as emby, not jellyfin");
    }

    section("media server: exclusions");
    {
        FakeMediaServer fake;
        stockTvServer(fake);
        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);
        Result r; r.attach(&src);

        MediaServerSource::Request req;
        req.src = SlotSource::Jellyfin;
        req.excludeSeasons.insert(MediaServerSource::seasonKey("sA", 1));
        req.excludeEpisodes.insert(QStringLiteral("e4"));
        check(src.enumerate(req), "starts");
        checkEq(r.items.size(), 1, "an excluded season and episode both drop out");
        checkStr(r.items.first().ref, QStringLiteral("e3"), "the surviving episode is season two");

        FakeMediaServer fake2;
        stockTvServer(fake2);
        MediaServerSource src2;
        src2.setBackend(SlotSource::Jellyfin, &fake2);
        Result r2; r2.attach(&src2);
        MediaServerSource::Request req2;
        req2.src = SlotSource::Jellyfin;
        req2.excludeSeasons.insert(MediaServerSource::seasonKey("sB", 1));
        check(src2.enumerate(req2), "starts");
        checkEq(r2.items.size(), 3, "only the named series' season is excluded");
    }

    section("media server: collections");
    {
        FakeMediaServer fake;
        stockTvServer(fake);
        fake.itemsFor["tv1|BoxSet"] = QVariantList{ FakeMediaServer::boxset("bs1", "Saturday Night") };
        fake.itemsFor["mv1|BoxSet"] = QVariantList{ FakeMediaServer::boxset("bs2", "Ignored Set") };
        fake.boxsetChildrenFor["bs1"] = QVariantList{
            FakeMediaServer::movie("m9", "Feature", 5400000.0),
            FakeMediaServer::series("sB", "Beta"),
        };
        fake.itemsFor["sB|Episode"] = QVariantList{
            FakeMediaServer::episode("e4", "sB", "Beta", 1, 1, 600000.0),
            FakeMediaServer::episode("e5", "sB", "Beta", 1, 2, 600000.0),
        };

        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);
        Result r; r.attach(&src);

        MediaServerSource::Request req;
        req.src = SlotSource::Jellyfin;
        req.collections = QStringList{ QStringLiteral("Saturday Night") };
        check(src.enumerate(req), "starts");
        check(r.finished && !r.failed, "finishes");

        QStringList got = refsOf(r.items);
        got.sort();
        checkStr(got.join(","), QStringLiteral("e4,e5"),
                 "a series channel takes the collection's series, not its films");

        MediaServerSource src2;
        src2.setBackend(SlotSource::Jellyfin, &fake);
        Result again; again.attach(&src2);
        check(src2.enumerate(req), "the same request runs again");
        checkStr(refsOf(again.items).join(","), refsOf(r.items).join(","),
                 "and produces exactly the same order");
        check(fake.calls.contains("load_boxset_children(bs1)"), "the named collection is expanded");
        check(!fake.calls.contains("load_boxset_children(bs2)"),
              "a collection this channel did not name is left alone");

        check(!fake.calls.contains("load_items(tv1,Episode)"),
              "naming only collections does not pull the entire library");
    }

    section("media server: collection series is not re-filtered");
    {
        FakeMediaServer fake;
        stockTvServer(fake);
        fake.itemsFor["tv1|BoxSet"] = QVariantList{ FakeMediaServer::boxset("bs1", "Set") };
        fake.boxsetChildrenFor["bs1"] = QVariantList{ FakeMediaServer::series("sB", "Beta") };
        fake.itemsFor["sB|Episode"] = QVariantList{
            FakeMediaServer::episode("e4", "sB", "Beta", 1, 1, 600000.0) };

        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);
        Result r; r.attach(&src);

        MediaServerSource::Request req;
        req.src = SlotSource::Jellyfin;
        req.collections = QStringList{ QStringLiteral("Set") };
        req.match       = QStringList{ QStringLiteral("Alpha") };
        check(src.enumerate(req), "starts");
        check(refsOf(r.items).contains(QStringLiteral("e4")),
              "the collection's series survives a series filter that excludes it");
    }

    section("media server: nothing airs twice");
    {
        FakeMediaServer fake;
        stockTvServer(fake);
        fake.itemsFor["tv1|BoxSet"] = QVariantList{
            FakeMediaServer::boxset("bs1", "Set One"),
            FakeMediaServer::boxset("bs2", "Set Two") };
        fake.boxsetChildrenFor["bs1"] = QVariantList{
            FakeMediaServer::series("sB", "Beta"), FakeMediaServer::movie("m9", "Feature", 5400000.0) };
        fake.boxsetChildrenFor["bs2"] = QVariantList{
            FakeMediaServer::series("sB", "Beta"), FakeMediaServer::movie("m9", "Feature", 5400000.0) };
        fake.itemsFor["sB|Episode"] = QVariantList{
            FakeMediaServer::episode("e4", "sB", "Beta", 1, 1, 600000.0) };

        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);
        Result r; r.attach(&src);
        MediaServerSource::Request req;
        req.src = SlotSource::Jellyfin;
        req.collections = QStringList{ QStringLiteral("Set One"), QStringLiteral("Set Two") };
        check(src.enumerate(req), "starts");
        checkStr(refsOf(r.items).join(","), QStringLiteral("e4"),
                 "a show in two collections is expanded once, not twice");
        checkEq(fake.calls.count("load_items(sB,Episode)"), 1,
                "and asked for only once");

        MediaServerSource src2;
        src2.setBackend(SlotSource::Jellyfin, &fake);
        Result films; films.attach(&src2);
        MediaServerSource::Request freq;
        freq.src = SlotSource::Jellyfin;
        freq.wants = MediaServerSource::Request::Wants::Films;
        freq.collections = req.collections;
        check(src2.enumerate(freq), "the film slot starts");
        checkStr(refsOf(films.items).join(","), QStringLiteral("m9"),
                 "a film slot takes the collection's films, not its series");
    }

    section("media server: films");
    {
        FakeMediaServer fake;
        stockTvServer(fake);
        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);
        Result r; r.attach(&src);

        MediaServerSource::Request req;
        req.src = SlotSource::Jellyfin;
        req.wants = MediaServerSource::Request::Wants::Films;
        check(src.enumerate(req), "starts");
        checkEq(r.items.size(), 2, "the film library is used when films are wanted");
        check(!fake.calls.contains("load_items(tv1,Movie)"),
              "and the TV library is not asked for films");
    }

    section("media server: library filter");
    {
        FakeMediaServer fake;
        stockTvServer(fake);
        fake.libraries.append(FakeMediaServer::library("tv2", "KIDS TV", "tvshows"));
        fake.itemsFor["tv2|Episode"] = QVariantList{
            FakeMediaServer::episode("k1", "sK", "Kids Show", 1, 1, 600000.0) };

        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);
        Result r; r.attach(&src);

        MediaServerSource::Request req;
        req.src = SlotSource::Jellyfin;
        req.libraries = QStringList{ QStringLiteral("kids tv") };
        check(src.enumerate(req), "starts");
        checkEq(r.items.size(), 1, "only the named library is walked");
        checkStr(r.items.first().ref, QStringLiteral("k1"), "and it is the right one");
    }

    section("media server: browsing");
    {
        FakeMediaServer fake;
        stockTvServer(fake);
        fake.itemsFor["tv1|Series"] = QVariantList{
            FakeMediaServer::series("sA", "Alpha"), FakeMediaServer::series("sB", "Beta") };
        fake.seasonsFor["sA"] = QVariantList{
            FakeMediaServer::season("sn1", "sA", 1), FakeMediaServer::season("sn2", "sA", 2) };
        fake.itemsFor["sA|Episode"] = fake.itemsFor["tv1|Episode"];

        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);

        {
            Result r; r.attach(&src);
            check(src.browse(SlotSource::Jellyfin, "shows", ""), "shows browse starts");
            checkStr(r.browseKind, QStringLiteral("shows"), "answers for the right kind");
            checkEq(r.browseItems.size(), 2, "both series listed");
            checkStr(r.browseItems.first().toMap().value("id").toString(),
                     QStringLiteral("sA"), "a series is keyed by id for drilling in");
            checkStr(r.browseItems.first().toMap().value("label").toString(),
                     QStringLiteral("Alpha"), "and labelled by name for storing");
        }
        {
            Result r; r.attach(&src);
            check(src.browse(SlotSource::Jellyfin, "seasons", "sA"), "seasons browse starts");
            checkEq(r.browseItems.size(), 2, "both seasons listed");
            checkStr(r.browseItems.first().toMap().value("id").toString(),
                     QStringLiteral("sA:1"), "a season is keyed by series and number");
        }
        {
            Result r; r.attach(&src);
            check(src.browse(SlotSource::Jellyfin, "episodes", "sA:1"), "episodes browse starts");
            checkEq(r.browseItems.size(), 2,
                    "only this series' season-one episodes are listed");
            checkStr(r.browseItems.first().toMap().value("id").toString(),
                     QStringLiteral("e1"), "an episode is keyed by its own id");
            check(fake.calls.contains("load_items(sA,Episode)"),
                  "episodes are fetched under the series");
        }
        {
            Result r; r.attach(&src);
            check(!src.browse(SlotSource::Jellyfin, "episodes", "not-a-key"),
                  "a malformed season key is refused up front");
            check(!r.finished && !r.failed, "and nothing is emitted for it");
            check(!src.busy(), "leaving the source idle");
        }
        {
            Result r; r.attach(&src);
            check(!src.browse(SlotSource::Jellyfin, "playlists", ""),
                  "playlists are refused: these servers have no equivalent here");
            check(!src.busy(), "and the source stays idle");
        }
    }

    section("media server: films by genre and by collection");
    {
        FakeMediaServer fake;
        stockTvServer(fake);
        fake.itemsFor["mv1|BoxSet"] = QVariantList{
            FakeMediaServer::boxset("bx1", "HAMMER HORROR") };
        fake.itemsFor["tv1|BoxSet"] = QVariantList{
            FakeMediaServer::boxset("bx1", "HAMMER HORROR") };
        fake.boxsetChildrenFor["bx1"] = QVariantList{
            FakeMediaServer::movie("m9", "Dracula", 4800000.0,
                                   { QStringLiteral("Horror") }) };

        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);

        {
            Result r; r.attach(&src);
            check(src.browse(SlotSource::Jellyfin, "moviegenres", ""), "genre browse starts");
            checkEq(r.browseItems.size(), 2, "each genre offered once, not once per film");
            checkStr(r.browseItems.first().toMap().value("label").toString(),
                     QStringLiteral("Comedy"), "genres come back sorted");
        }
        {
            Result r; r.attach(&src);
            check(src.browse(SlotSource::Jellyfin, "moviecollections", ""),
                  "collection browse starts");
            checkEq(r.browseItems.size(), 1, "a collection in two libraries is listed once");
            checkStr(r.browseItems.first().toMap().value("id").toString(),
                     QStringLiteral("HAMMER HORROR"), "and is keyed by the name a channel stores");
        }
        {
            Result r; r.attach(&src);
            MediaServerSource::Request req;
            req.src        = SlotSource::Jellyfin;
            req.wants = MediaServerSource::Request::Wants::Films;
            req.genres     = QStringList{ QStringLiteral("horror") };
            check(src.enumerate(req), "genre enumeration starts");
            checkEq(r.items.size(), 1, "only the film carrying that genre");
            checkStr(r.items.first().ref, QStringLiteral("m1"), "and it is the right one");
        }
        {
            fake.calls.clear();
            Result r; r.attach(&src);
            MediaServerSource::Request req;
            req.src         = SlotSource::Jellyfin;
            req.wants  = MediaServerSource::Request::Wants::Films;
            req.collections = QStringList{ QStringLiteral("hammer horror") };
            check(src.enumerate(req), "collection enumeration starts");
            checkEq(r.items.size(), 1, "the collection's film");
            checkStr(r.items.first().ref, QStringLiteral("m9"), "and only it");
            check(!fake.calls.contains("load_items(mv1,Movie)"),
                  "the whole film library is not swept as well");
        }
    }

    section("media server: a break takes whatever the pool holds");
    {
        FakeMediaServer fake;
        stockTvServer(fake);

        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);
        Result r; r.attach(&src);

        MediaServerSource::Request req;
        req.src   = SlotSource::Jellyfin;
        req.wants = MediaServerSource::Request::Wants::Anything;
        check(src.enumerate(req), "starts");
        checkEq(r.items.size(), 6, "both libraries contribute");
        check(fake.calls.contains("load_items(tv1,)"), "the TV library was asked, untyped");
        check(fake.calls.contains("load_items(mv1,)"), "and the film library too");
    }

    section("media server: an unusable answer is not an empty one");
    {
        FakeMediaServer fake;
        fake.libraries = QVariantList{ FakeMediaServer::library("mv1", "MOVIES", "movies") };
        fake.itemsFor["mv1|Movie"] = QVariantList{
            FakeMediaServer::movie("m1", "No Duration", 0.0),
            FakeMediaServer::movie("m2", "Also None",   0.0) };

        MediaServerSource src;
        src.setBackend(SlotSource::Jellyfin, &fake);
        Result r; r.attach(&src);

        MediaServerSource::Request req;
        req.src = SlotSource::Jellyfin;
        req.wants = MediaServerSource::Request::Wants::Films;
        check(src.enumerate(req), "starts");
        check(r.finished, "the walk still completes rather than failing");
        check(r.items.isEmpty(), "and yields nothing schedulable");
        check(!src.busy(), "leaving the source idle");
    }

    section("media server: failure handling");
    {
        {
            FakeMediaServer fake;
            fake.libraries = QVariantList{ FakeMediaServer::library("mv1", "MOVIES", "movies") };
            MediaServerSource src;
            src.setBackend(SlotSource::Jellyfin, &fake);
            Result r; r.attach(&src);
            MediaServerSource::Request req; req.src = SlotSource::Jellyfin;
            check(src.enumerate(req), "starts");
            check(r.failed && !r.finished, "a TV channel with no TV library fails");
            check(!src.busy(), "and the source is left idle, not stuck");
        }
        {
            FakeMediaServer fake;
            stockTvServer(fake);
            fake.errorOnCall = QStringLiteral("load_items");
            MediaServerSource src;
            src.setBackend(SlotSource::Jellyfin, &fake);
            Result r; r.attach(&src);
            MediaServerSource::Request req; req.src = SlotSource::Jellyfin;
            check(src.enumerate(req), "starts");
            check(r.failed, "a backend error fails the enumeration");
            checkStr(r.reason, QStringLiteral("SERVER SAID NO"), "and reports what the server said");
            check(!src.busy(), "and the source is idle");
        }
        {
            FakeMediaServer fake;
            stockTvServer(fake);
            fake.silent = true;
            MediaServerSource src;
            src.setBackend(SlotSource::Jellyfin, &fake);
            Result r; r.attach(&src);
            MediaServerSource::Request req; req.src = SlotSource::Jellyfin;
            check(src.enumerate(req), "starts");
            check(!r.finished && !r.failed, "a silent server produces nothing yet");
            check(src.busy(), "the request is still in flight");

            check(!src.enumerate(req), "a second enumeration is refused while busy");
            check(!src.browse(SlotSource::Jellyfin, "shows", ""), "and so is a browse");

            src.cancel();
            check(!src.busy(), "cancel releases it");
            check(!r.finished && !r.failed, "and cancelling emits nothing");
        }
        {
            auto *fake = new FakeMediaServer;
            stockTvServer(*fake);
            fake->silent = true;
            MediaServerSource src;
            src.setBackend(SlotSource::Jellyfin, fake);
            Result r; r.attach(&src);
            MediaServerSource::Request req; req.src = SlotSource::Jellyfin;
            check(src.enumerate(req), "starts");
            delete fake;
            check(r.failed, "losing the backend mid-request fails cleanly");
            check(!src.available(SlotSource::Jellyfin), "and the source becomes unavailable");
            check(!src.busy(), "and is idle");
        }
    }

    return 0;
}
