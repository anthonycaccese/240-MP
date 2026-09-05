#include "../VirtualChannelsBackend.h"
#include "TestHarness.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

using vtest::check;
using vtest::checkEq;
using vtest::checkStr;
using vtest::section;

namespace {

bool touch(const QString &path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write("x");
    return true;
}

class Fixture {
public:
    Fixture() {
        touch(media() + "/series/Batman Beyond (1999)/Season 1/Batman Beyond S01E01 - Rebirth.mkv");
        touch(media() + "/series/Batman Beyond (1999)/Season 1/Batman Beyond S01E02 - Golem.mkv");
        touch(media() + "/series/Samurai Jack (2001)/Season 1/Samurai Jack S01E01 - The Beginning.mkv");
        touch(media() + "/movies/Mask of the Phantasm (1993).mkv");
        touch(media() + "/breaks/bumps/bump one.mkv");
        touch(media() + "/breaks/bumps/bump two.mkv");
        touch(media() + "/breaks/intros/intro one.mkv");
    }

    QString data()  const { return m_dir.path(); }
    QString media() const { return m_dir.path() + QStringLiteral("/media"); }

    void write(const QJsonObject &channel) {
        QJsonArray a;
        a.append(channel);
        QJsonObject root;
        root["channels"] = a;
        QDir().mkpath(data() + QStringLiteral("/channels"));
        QFile f(data() + QStringLiteral("/channels/channels.json"));
        if (!f.open(QIODevice::WriteOnly)) return;
        f.write(QJsonDocument(root).toJson());
    }

    QJsonObject read(int number) const {
        QFile f(data() + QStringLiteral("/channels/channels.json"));
        if (!f.open(QIODevice::ReadOnly)) return {};
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue &v : root.value(QLatin1String("channels")).toArray()) {
            const QJsonObject o = v.toObject();
            if (o.value(QLatin1String("number")).toInt(-1) == number) return o;
        }
        return {};
    }

private:
    QTemporaryDir m_dir;
};

QJsonObject localChannel(int number) {
    QJsonObject o;
    o["number"] = number;
    o["name"]   = QStringLiteral("Local Test");
    return o;
}

QVariantMap entry(const QString &kind, const QString &name) {
    QVariantMap m;
    m["src"]  = QStringLiteral("local");
    m["kind"] = kind;
    m["name"] = name;
    return m;
}

QVariantMap folderEntry(const QString &path) {
    QVariantMap m;
    m["src"]  = QStringLiteral("local");
    m["kind"] = QStringLiteral("folder");
    m["name"] = path;
    return m;
}

QStringList kindsIn(const QJsonObject &channel, const char *pool) {
    QStringList out;
    for (const QJsonValue &v : channel.value(QLatin1String(pool)).toArray()) {
        if (v.isString()) { out << QStringLiteral("string"); continue; }
        const QJsonObject o = v.toObject();
        out << (o.contains(QLatin1String("folder"))
                    ? QStringLiteral("folder")
                    : o.value(QLatin1String("kind")).toString());
    }
    return out;
}

void testLocalFilmInProgrammePool() {
    section("Backend: a local film is a programme like any other");

    Fixture fx;
    fx.write(localChannel(3));
    VirtualChannelsBackend b(fx.data(), fx.data());

    QVariantList entries;
    entries.append(entry(QStringLiteral("series"), QStringLiteral("Batman Beyond")));
    entries.append(entry(QStringLiteral("movie"),  QStringLiteral("Mask of the Phantasm")));
    check(b.set_channel_pool(3, QStringLiteral("programmes"), entries),
          "a local film saves into a programme pool");

    const QStringList kinds = kindsIn(fx.read(3), "programmes");
    checkEq(kinds.size(), 2, "both entries were written");
    check(kinds.contains(QStringLiteral("movie")), "the film kept its kind");
    check(kinds.contains(QStringLiteral("series")), "the series is still there");

    check(b.set_channel_pool(3, QStringLiteral("programmes"), entries),
          "a pool holding a film can be saved again");
}

void testOneBadRowDoesNotBlockThePool() {
    section("Backend: one unusable row does not block a pool");

    Fixture fx;
    fx.write(localChannel(3));
    VirtualChannelsBackend b(fx.data(), fx.data());

    QVariantList entries;
    entries.append(folderEntry(QStringLiteral("breaks/bumps")));
    entries.append(folderEntry(QStringLiteral("breaks/gone")));
    entries.append(entry(QStringLiteral("sponge"), QStringLiteral("Written By Some Later Version")));
    check(b.set_channel_pool(3, QStringLiteral("bumps"), entries),
          "a stale folder and an unknown kind still let the save land");

    const QStringList kinds = kindsIn(fx.read(3), "bumps");
    checkEq(kinds.size(), 3, "nothing was silently dropped");

    QVariantList fewer;
    fewer.append(folderEntry(QStringLiteral("breaks/bumps")));
    check(b.set_channel_pool(3, QStringLiteral("bumps"), fewer),
          "a row can be removed from a pool that holds a stale row");
    checkEq(kindsIn(fx.read(3), "bumps").size(), 1, "the removal was written");
}

void testPoolReadBack() {
    section("Backend: a pool reads back as what it is");

    Fixture fx;
    fx.write(localChannel(3));
    VirtualChannelsBackend b(fx.data(), fx.data());

    QVariantList entries;
    entries.append(entry(QStringLiteral("series"), QStringLiteral("Batman Beyond")));
    entries.append(folderEntry(QStringLiteral("breaks/bumps")));
    check(b.set_channel_pool(3, QStringLiteral("programmes"), entries), "mixed pool saves");

    const QVariantList back = b.channel_pool(3, QStringLiteral("programmes"));
    checkEq(back.size(), 2, "both rows read back");

    const QVariantMap series = back.at(0).toMap();
    checkStr(series.value(QStringLiteral("name")).toString(),
             QStringLiteral("Batman Beyond"), "a series row keeps its name");
    checkStr(series.value(QStringLiteral("kind")).toString(),
             QStringLiteral("series"), "a series row is not called a folder");

    const QVariantMap folder = back.at(1).toMap();
    checkStr(folder.value(QStringLiteral("kind")).toString(),
             QStringLiteral("folder"), "a folder row is a folder");
    checkEq(folder.value(QStringLiteral("count")).toInt(), 2, "a folder row counts its clips");
}

void testIdentsSurviveASeriesRewrite() {
    section("Backend: rewriting the series list keeps what hangs off it");

    Fixture fx;
    QJsonObject ch = localChannel(3);

    QJsonObject withIdent;
    withIdent["src"]    = QStringLiteral("local");
    withIdent["kind"]   = QStringLiteral("series");
    withIdent["name"]   = QStringLiteral("Batman Beyond");
    QJsonArray intros;
    intros.append(QStringLiteral("breaks/intros"));
    withIdent["intros"] = intros;

    QJsonObject foreign;               // another source's row, not ours to touch
    foreign["src"]  = QStringLiteral("plex");
    foreign["kind"] = QStringLiteral("series");
    foreign["name"] = QStringLiteral("Something On Plex");

    QJsonArray programmes;
    programmes.append(withIdent);
    programmes.append(foreign);
    programmes.append(QStringLiteral("breaks/bumps"));   // an older file's bare string
    ch["programmes"] = programmes;
    ch["source"] = QStringLiteral("local");
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());
    check(b.set_channel_list(3, QStringLiteral("match"),
                             { QStringLiteral("Batman Beyond"), QStringLiteral("Samurai Jack") }),
          "the series list saves");

    const QJsonObject after = fx.read(3);
    bool keptIdent = false, keptForeign = false, keptString = false, addedJack = false;
    for (const QJsonValue &v : after.value(QLatin1String("programmes")).toArray()) {
        if (v.isString()) { keptString = true; continue; }
        const QJsonObject o = v.toObject();
        const QString name = o.value(QLatin1String("name")).toString();
        if (name == QLatin1String("Batman Beyond")
            && !o.value(QLatin1String("intros")).toArray().isEmpty()) keptIdent = true;
        if (name == QLatin1String("Something On Plex")) keptForeign = true;
        if (name == QLatin1String("Samurai Jack")) addedJack = true;
    }
    check(keptIdent,   "a show's own intro survived the rewrite");
    check(keptForeign, "another source's entry was left alone");
    check(keptString,  "an older file's bare folder string was carried across");
    check(addedJack,   "the newly ticked show was added");
}

void testSeriesIdsAreKept() {
    section("Backend: a picked series remembers its id on its source");

    Fixture fx;
    QJsonObject ch = localChannel(3);
    ch["source"] = QStringLiteral("local");
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());

    check(b.set_channel_list(3, QStringLiteral("match"),
                             { QStringLiteral("Batman Beyond"), QStringLiteral("Samurai Jack") },
                             { QStringLiteral("8324"), QStringLiteral("") }),
          "a series list saves with the ids the picker knew");

    QString beyondRef, jackRef;
    for (const QJsonValue &v : fx.read(3).value(QLatin1String("programmes")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("name")).toString() == QLatin1String("Batman Beyond"))
            beyondRef = o.value(QLatin1String("ref")).toString();
        if (o.value(QLatin1String("name")).toString() == QLatin1String("Samurai Jack"))
            jackRef = o.value(QLatin1String("ref")).toString();
    }
    checkStr(beyondRef, QStringLiteral("8324"), "the id is stored against the show");
    check(jackRef.isEmpty(), "a show the picker had no id for is stored without one");

    // A screen that never saw the ids must not wipe them off.
    check(b.set_channel_list(3, QStringLiteral("match"),
                             { QStringLiteral("Batman Beyond"), QStringLiteral("Samurai Jack") }),
          "the same list saves again from a screen with no ids");

    QString afterRef;
    for (const QJsonValue &v : fx.read(3).value(QLatin1String("programmes")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("name")).toString() == QLatin1String("Batman Beyond"))
            afterRef = o.value(QLatin1String("ref")).toString();
    }
    checkStr(afterRef, QStringLiteral("8324"), "and the id it already had is still there");

    check(b.set_channel_list(3, QStringLiteral("match"), { QStringLiteral("Samurai Jack") }),
          "dropping a show saves");
    bool beyondGone = true;
    for (const QJsonValue &v : fx.read(3).value(QLatin1String("programmes")).toArray())
        if (v.toObject().value(QLatin1String("name")).toString() == QLatin1String("Batman Beyond"))
            beyondGone = false;
    check(beyondGone, "and takes its id with it");
}

void testPoolSaveKeepsWhatItDoesNotOwn() {
    section("Backend: saving a pool keeps the ids, exclusions and collections");

    Fixture fx;
    QJsonObject ch = localChannel(3);

    QJsonObject entry;
    entry["src"]  = QStringLiteral("plex");
    entry["kind"] = QStringLiteral("series");
    entry["name"] = QStringLiteral("Deep Space Nine");
    entry["ref"]  = QStringLiteral("6049");
    QJsonArray programmes;
    programmes.append(entry);
    ch["programmes"] = programmes;

    // The things the entries do not carry, which used to go with the block.
    QJsonObject excl;
    QJsonArray seasons;
    seasons.append(QStringLiteral("s1"));
    excl["seasons"] = seasons;
    QJsonObject plex;
    plex["exclude"] = excl;
    QJsonArray cols;
    cols.append(QStringLiteral("STARGATE"));
    plex["collections"] = cols;
    QJsonArray legacy;
    legacy.append(QStringLiteral("An Old Name"));
    plex["match"] = legacy;
    ch["plex"] = plex;
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());

    QVariantList round;
    for (const QVariant &v : b.channel_pool(3, QStringLiteral("programmes")))
        round.append(v);
    check(!round.isEmpty(), "the pool reads back");
    check(b.set_channel_pool(3, QStringLiteral("programmes"), round),
          "and saves again unchanged");

    const QJsonObject after = fx.read(3);
    QString keptRef;
    for (const QJsonValue &v : after.value(QLatin1String("programmes")).toArray())
        if (v.toObject().value(QLatin1String("name")).toString()
            == QLatin1String("Deep Space Nine"))
            keptRef = v.toObject().value(QLatin1String("ref")).toString();
    checkStr(keptRef, QStringLiteral("6049"), "the show keeps the id it was picked with");

    const QJsonObject blockAfter = after.value(QLatin1String("plex")).toObject();
    checkEq(blockAfter.value(QLatin1String("exclude")).toObject()
                      .value(QLatin1String("seasons")).toArray().size(), 1,
            "a season switched off stays switched off");
    checkEq(blockAfter.value(QLatin1String("collections")).toArray().size(), 1,
            "a collection on the channel is still there");
    check(!blockAfter.contains(QLatin1String("match")),
          "while the legacy series list is retired, so nothing airs twice");
}

void testMovieChannel() {
    section("Backend: a movie channel keeps its slots but does not air them");

    Fixture fx;
    QJsonObject ch = localChannel(3);
    ch["kind"] = QStringLiteral("movies");

    QJsonObject slot;
    slot["name"] = QStringLiteral("Movie Slot");
    slot["at"]   = QStringLiteral("20:00");
    QJsonArray booked;          // not "slots": Qt defines that as a keyword
    booked.append(slot);
    ch["appointments"] = booked;
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());
    const QVariantMap cfg = b.channel_source_config(3);
    checkStr(cfg.value(QStringLiteral("kind")).toString(), QStringLiteral("movies"),
             "the screen is told it is a movie channel");
    checkStr(cfg.value(QStringLiteral("filmsFrom")).toString(), QStringLiteral("selection"),
             "and that a selection is the default");

    // Nothing airs from the slot, but it is still in the file to come back to.
    checkEq(fx.read(3).value(QLatin1String("appointments")).toArray().size(), 1,
            "the slot it already had is left in the file");

    check(b.set_channel_films_from(3, QStringLiteral("playlist")),
          "a movie channel can be switched to a playlist");
    checkStr(b.channel_source_config(3).value(QStringLiteral("filmsFrom")).toString(),
             QStringLiteral("playlist"), "and says so afterwards");

    check(!b.set_channel_films_from(3, QStringLiteral("whatever")),
          "a films-from it does not understand is refused");
    check(!b.set_channel_kind(3, QStringLiteral("films")),
          "and so is a kind it does not understand");
    checkStr(b.channel_source_config(3).value(QStringLiteral("filmsFrom")).toString(),
             QStringLiteral("playlist"), "a refusal changes nothing");

    check(b.set_channel_kind(3, QStringLiteral("tv")),
          "switching back to TV is allowed");
    checkEq(fx.read(3).value(QLatin1String("appointments")).toArray().size(), 1,
            "and the slot is still there, exactly as it was");
}

void testFilmPoolEntries() {
    section("Backend: films and genres are pool entries a channel can hold");

    Fixture fx;
    QJsonObject ch = localChannel(3);
    ch["kind"] = QStringLiteral("movies");
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());

    QVariantList entries;
    QVariantMap film;
    film["src"] = QStringLiteral("plex");
    film["kind"] = QStringLiteral("movie");
    film["name"] = QStringLiteral("The Thing");
    film["ref"]  = QStringLiteral("4242");
    entries.append(film);
    QVariantMap genre;
    genre["src"] = QStringLiteral("plex");
    genre["kind"] = QStringLiteral("genre");
    genre["name"] = QStringLiteral("Film-Noir");
    entries.append(genre);

    check(b.set_channel_pool(3, QStringLiteral("programmes"), entries),
          "a pool of a film and a genre saves");

    QString filmKind, genreKind, filmRef;
    for (const QJsonValue &v : fx.read(3).value(QLatin1String("programmes")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("name")).toString() == QLatin1String("The Thing")) {
            filmKind = o.value(QLatin1String("kind")).toString();
            filmRef  = o.value(QLatin1String("ref")).toString();
        }
        if (o.value(QLatin1String("name")).toString() == QLatin1String("Film-Noir"))
            genreKind = o.value(QLatin1String("kind")).toString();
    }
    checkStr(filmKind,  QStringLiteral("movie"), "the film is stored as a film");
    checkStr(filmRef,   QStringLiteral("4242"),  "with the id it was picked by");
    checkStr(genreKind, QStringLiteral("genre"), "and the genre as a genre");

    const QVariantList back = b.channel_pool(3, QStringLiteral("programmes"));
    checkEq(back.size(), 2, "both read back for the screen");
}

void testFilmAndShowListsAreSeparate() {
    section("Backend: saving films leaves the shows alone, and the other way round");

    Fixture fx;
    QJsonObject ch = localChannel(3);
    ch["source"] = QStringLiteral("local");
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());
    check(b.set_channel_list(3, QStringLiteral("match"),
                             { QStringLiteral("Batman Beyond") }),
          "a show saves");
    check(b.set_channel_list(3, QStringLiteral("films"),
                             { QStringLiteral("Mask of the Phantasm") }),
          "a film saves beside it");
    check(b.set_channel_list(3, QStringLiteral("genres"),
                             { QStringLiteral("Film-Noir") }),
          "and a genre beside both");

    const QVariantMap cfg = b.channel_source_config(3);
    checkEq(cfg.value(QStringLiteral("match")).toStringList().size(),  1, "one show");
    checkEq(cfg.value(QStringLiteral("films")).toStringList().size(),  1, "one film");
    checkEq(cfg.value(QStringLiteral("genres")).toStringList().size(), 1, "one genre");

    // Rewriting one list must not disturb the others.
    check(b.set_channel_list(3, QStringLiteral("films"), {}), "clearing the films saves");
    const QVariantMap after = b.channel_source_config(3);
    checkEq(after.value(QStringLiteral("films")).toStringList().size(),  0, "the films are gone");
    checkEq(after.value(QStringLiteral("match")).toStringList().size(),  1, "the show is untouched");
    checkEq(after.value(QStringLiteral("genres")).toStringList().size(), 1, "so is the genre");

    check(!b.set_channel_list(3, QStringLiteral("nonsense"), { QStringLiteral("x") }),
          "a list the backend does not know is refused");
}

void testSourceSwitchSticks() {
    section("Backend: switching a channel's source takes effect");

    Fixture fx;
    QJsonObject ch = localChannel(3);
    ch["plex"] = QJsonObject{};
    QJsonObject row;
    row["src"]  = QStringLiteral("plex");
    row["kind"] = QStringLiteral("series");
    row["name"] = QStringLiteral("Something On Plex");
    QJsonArray programmes;
    programmes.append(row);
    ch["programmes"] = programmes;
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());
    check(b.set_channel_source(3, QStringLiteral("local")), "the source can be set to local");

    checkStr(b.channel_source_config(3).value(QStringLiteral("source")).toString(),
             QStringLiteral("local"), "the channel is on local afterwards");
}

void testExclusionsOnALocalChannel() {
    section("Backend: episodes can be switched off on a local channel");

    Fixture fx;
    fx.write(localChannel(3));
    VirtualChannelsBackend b(fx.data(), fx.data());

    QVariantList entries;
    entries.append(entry(QStringLiteral("series"), QStringLiteral("Batman Beyond")));
    b.set_channel_pool(3, QStringLiteral("programmes"), entries);

    const QString ep = QStringLiteral("series/Batman Beyond (1999)/Season 1/Batman Beyond S01E02 - Golem.mkv");
    check(b.set_channel_excluded(3, QStringLiteral("episodes"), ep, true,
                                 QStringLiteral("series/Batman Beyond (1999)|1")),
          "an episode can be switched off");
    check(!fx.read(3).value(QLatin1String("local")).toObject()
              .value(QLatin1String("exclude")).toObject().isEmpty(),
          "the exclusion was written to the channel file");

    check(b.set_channel_excluded(3, QStringLiteral("episodes"), ep, false,
                                 QStringLiteral("series/Batman Beyond (1999)|1")),
          "and switched back on");
    check(b.clear_episode_exclusions(3, QStringLiteral("series/Batman Beyond (1999)|1")),
          "a whole season can be switched back on");
}

void testBookingWrites() {
    section("Backend: movie slots");

    Fixture fx;
    fx.write(localChannel(3));
    VirtualChannelsBackend b(fx.data(), fx.data());

    const int idx = b.add_booking(3);
    check(idx >= 0, "a slot can be added");
    check(b.set_booking_name(3, idx, QStringLiteral("Saturday Feature")), "a slot can be named");
    check(b.set_booking_time(3, idx, QStringLiteral("20:00")), "a slot can be timed");
    check(b.set_booking_days(3, idx, { QStringLiteral("sat") }), "a slot can be given days");

    check(b.set_booking_folder(3, idx, fx.media() + QStringLiteral("/movies")),
          "a slot takes a folder");
    checkStr(fx.read(3).value(QLatin1String("appointments")).toArray().at(idx)
                 .toObject().value(QLatin1String("folder")).toString(),
             QStringLiteral("movies"), "the folder was stored relative to the media root");

    check(!b.set_booking_folder(3, idx, QStringLiteral("/etc")),
          "a folder outside the media root is refused");

    check(b.set_booking_list(3, idx, QStringLiteral("titles"),
                             { QStringLiteral("Mask of the Phantasm (1993)") }),
          "a local slot takes films picked by name");
    checkEq(b.channel_bookings(3).at(idx).toMap()
                .value(QStringLiteral("films")).toInt(), 1,
            "the slot reports the film it was given");
    checkStr(fx.read(3).value(QLatin1String("appointments")).toArray().at(idx)
                 .toObject().value(QLatin1String("local")).toObject()
                 .value(QLatin1String("titles")).toArray().at(0).toString(),
             QStringLiteral("Mask of the Phantasm (1993)"),
             "the film was written where the generator reads it");
    check(!b.set_booking_list(3, idx, QStringLiteral("genres"), { QStringLiteral("Horror") }),
          "a local slot still refuses genres, which local files do not have");

    check(b.delete_booking(3, idx), "a slot can be deleted");
    checkEq(b.channel_bookings(3).size(), 0, "the deletion was written");
}

void testInterstitialsAreCounted() {
    section("Backend: a channel's breaks are counted whatever shape they are in");

    Fixture fx;
    QJsonObject ch = localChannel(3);
    QJsonArray bumps;
    bumps.append(QStringLiteral("breaks/bumps"));          // an older file's string
    QJsonObject asEntry;                                    // what is written now
    asEntry["src"]    = QStringLiteral("local");
    asEntry["folder"] = QStringLiteral("breaks/intros");
    bumps.append(asEntry);
    QJsonObject fromServer;                                 // no clips on disk to count
    fromServer["src"]  = QStringLiteral("plex");
    fromServer["kind"] = QStringLiteral("collection");
    fromServer["name"] = QStringLiteral("Bumpers");
    bumps.append(fromServer);
    ch["bumps"] = bumps;
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());
    QVariantMap row;
    for (const QVariant &v : b.channel_interstitials(3))
        if (v.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("bumps"))
            row = v.toMap();

    checkEq(row.value(QStringLiteral("count")).toInt(), 3,
            "clips under both folder shapes are counted");
    checkEq(row.value(QStringLiteral("sources")).toInt(), 3,
            "a server's collection counts as a source even with nothing to count");
}

void testSourceSwitchAndPicks() {
    section("Backend: what a source change does to the picks");

    Fixture fx;
    QJsonObject ch = localChannel(3);
    ch["source"] = QStringLiteral("local");
    QJsonObject mine;
    mine["src"]  = QStringLiteral("local");
    mine["kind"] = QStringLiteral("series");
    mine["name"] = QStringLiteral("Batman Beyond");
    QJsonArray programmes;
    programmes.append(mine);
    ch["programmes"] = programmes;
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());
    checkEq(b.channel_source_config(3).value(QStringLiteral("match")).toStringList().size(), 1,
            "the channel starts with one pick");

    const QJsonArray before = fx.read(3).value(QLatin1String("programmes")).toArray();
    b.set_channel_source(3, QStringLiteral("local"));
    const QJsonArray after = fx.read(3).value(QLatin1String("programmes")).toArray();
    checkEq(after.size(), before.size(), "a source change leaves the entries in place");
    checkStr(after.at(0).toObject().value(QLatin1String("name")).toString(),
             QStringLiteral("Batman Beyond"), "and leaves them intact");
}

void testChannelLifecycle() {
    section("Backend: channels can be made, renamed and removed");

    Fixture fx;
    fx.write(localChannel(3));
    VirtualChannelsBackend b(fx.data(), fx.data());

    const int made = b.create_channel(QStringLiteral("A New One"));
    check(made > 0, "a channel can be created");
    check(b.rename_channel(made, QStringLiteral("Renamed")), "a channel can be renamed");
    check(b.delete_channel(made), "a channel can be deleted");

    bool stillThere = false;
    for (const QVariant &v : b.list_channels())
        if (v.toMap().value(QStringLiteral("number")).toInt() == made) stillThere = true;
    check(!stillThere, "the deleted channel is gone");
}

}  // namespace

int runVirtualChannelsBackendTests() {
    testLocalFilmInProgrammePool();
    testOneBadRowDoesNotBlockThePool();
    testPoolReadBack();
    testIdentsSurviveASeriesRewrite();
    testSeriesIdsAreKept();
    testMovieChannel();
    testFilmPoolEntries();
    testFilmAndShowListsAreSeparate();
    testPoolSaveKeepsWhatItDoesNotOwn();
    testSourceSwitchSticks();
    testExclusionsOnALocalChannel();
    testBookingWrites();
    testInterstitialsAreCounted();
    testSourceSwitchAndPicks();
    testChannelLifecycle();
    return 0;
}
