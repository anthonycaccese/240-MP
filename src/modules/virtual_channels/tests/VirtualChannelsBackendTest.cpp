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

void testFilmsFromDecidesWhatAirs() {
    section("Backend: a film channel reads only the way of choosing it is set to");

    Fixture fx;
    QJsonObject ch = localChannel(3);
    ch["kind"] = QStringLiteral("movies");
    ch["films_from"] = QStringLiteral("playlist");
    ch["source"] = QStringLiteral("local");

    // Both ways of choosing films are present in the file at once.
    QJsonObject film;
    film["src"] = QStringLiteral("local");
    film["kind"] = QStringLiteral("movie");
    film["name"] = QStringLiteral("Mask of the Phantasm");
    QJsonArray programmes;
    programmes.append(film);
    ch["programmes"] = programmes;
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());

    // Nothing is deleted by the mode: the film is still in the file, waiting
    // for the channel to be switched back to a selection.
    bool stillThere = false;
    for (const QJsonValue &v : fx.read(3).value(QLatin1String("programmes")).toArray())
        if (v.toObject().value(QLatin1String("name")).toString()
            == QLatin1String("Mask of the Phantasm")) stillThere = true;
    check(stillThere, "a film is left in the file while the channel plays a playlist");

    check(b.set_channel_films_from(3, QStringLiteral("selection")),
          "switching back to a selection saves");
    checkEq(b.channel_source_config(3).value(QStringLiteral("films")).toStringList().size(), 1,
            "and the film is offered again, exactly as it was");
}

void testPlansAreRead() {
    section("Backend: a channel's day plans are read, and bad rows dropped");

    Fixture fx;
    QJsonObject ch = localChannel(3);

    QJsonObject good;
    good["type"]    = QStringLiteral("series");
    good["name"]    = QStringLiteral("Batman Beyond");
    good["ref"]     = QStringLiteral("8324");
    good["minutes"] = 120;

    QJsonObject film;
    film["type"]    = QStringLiteral("movie");
    film["minutes"] = 90;

    QJsonObject noLength;                       // a block of no length
    noLength["type"] = QStringLiteral("series");
    noLength["name"] = QStringLiteral("Nothing");
    noLength["minutes"] = 0;

    QJsonObject blank;                          // added, not yet told what it plays
    blank["type"] = QStringLiteral("series");
    blank["minutes"] = 60;

    QJsonArray blocks;
    blocks.append(good);
    blocks.append(film);
    blocks.append(noLength);
    blocks.append(blank);

    QJsonObject plan;
    plan["name"]      = QStringLiteral("WEEKDAY");
    plan["starts_at"] = QStringLiteral("06:00");
    plan["blocks"]    = blocks;
    QJsonArray days;
    for (int d = 1; d <= 5; ++d) days.append(d);
    plan["days"] = days;

    QJsonObject dayless;                        // airs on no day, so nothing reaches it
    dayless["name"] = QStringLiteral("NOWHERE");
    dayless["blocks"] = QJsonArray();

    QJsonObject fresh;                          // a day added but not yet filled in
    fresh["name"] = QStringLiteral("SUNDAY");
    fresh["blocks"] = QJsonArray();
    QJsonArray sunday;
    sunday.append(7);
    fresh["days"] = sunday;

    QJsonArray plans;
    plans.append(plan);
    plans.append(dayless);
    plans.append(fresh);
    ch["plans"] = plans;
    fx.write(ch);

    const QVector<vchan::DayPlan> read = VirtualChannelsBackend::readPlans(ch);

    checkEq(read.size(), 2, "the plan airing on no day is dropped, the other two kept");
    if (read.size() < 2) return;

    checkStr(read.last().name, QStringLiteral("SUNDAY"),
             "a day with no blocks yet survives being read, so the screen can show it");
    checkEq(read.last().blocks.size(), 0, "with nothing in it");
    checkEq(read.last().totalMinutes(), 0, "and nothing to run");

    checkStr(read.first().name, QStringLiteral("WEEKDAY"), "by name");
    checkEq(read.first().days.size(), 5, "on the days it names");
    checkEq(read.first().blocks.size(), 3,
            "the block of no length is dropped; the blank one is kept as no content");
    checkEq(read.first().totalMinutes(), 270, "adding up to what they run for");

    check(read.first().blocks[0].id != read.first().blocks[1].id,
          "each block has its own id, which is how its programmes find it again");
    check(read.first().blocks[1].draws == vchan::PlanBlock::Draws::Movie,
          "a movie block is read as one");
    checkStr(read.first().blocks[0].ref, QStringLiteral("8324"),
             "and a picked series keeps the id it was picked by");
}

// A day is laid out with gaps in it -- a block put there to hold the morning
// so the lineup starts in the evening. Such a block names nothing, and asking
// a server for a show with no name matches nothing. A programme pool that
// draws nothing fails the whole channel, so one held morning would take the
// whole evening off the air with it.
void testBlankBlocksGatherNothing() {
    section("Backend: a block with nothing in it asks the server for nothing");

    Fixture fx;
    QJsonObject ch = localChannel(3);
    ch["schedule"] = QStringLiteral("day_plan");

    QJsonObject held;                       // holds the morning, plays nothing
    held["type"]    = QStringLiteral("series");
    held["minutes"] = 1080;

    QJsonObject show;
    show["type"]    = QStringLiteral("series");
    show["name"]    = QStringLiteral("Samurai Jack");
    show["ref"]     = QStringLiteral("15087");
    show["minutes"] = 60;

    QJsonObject anyFilm;                    // a movie block names no film on purpose
    anyFilm["type"]    = QStringLiteral("movie");
    anyFilm["minutes"] = 150;

    QJsonArray blocks;
    blocks.append(held);
    blocks.append(show);
    blocks.append(anyFilm);

    QJsonObject plan;
    plan["name"]   = QStringLiteral("SATURDAY");
    plan["days"]   = QJsonArray{ 6 };
    plan["blocks"] = blocks;
    ch["plans"]    = QJsonArray{ plan };
    fx.write(ch);

    VirtualChannelsBackend b(fx.data(), fx.data());
    vchan::ChannelDef def;
    def.plans = VirtualChannelsBackend::readPlans(ch);
    const QVector<VirtualChannelsBackend::PoolJob> jobs = b.readPools(ch, def);

    int programmes = 0, named = 0, films = 0;
    for (const VirtualChannelsBackend::PoolJob &j : jobs) {
        if (j.pool != vchan::SlotKind::Programme) continue;
        ++programmes;
        if (j.anyFilm) ++films;
        for (const QString &m : j.match) if (!m.trimmed().isEmpty()) ++named;
        for (const QString &m : j.match)
            check(!m.trimmed().isEmpty(), "no job asks for a show with no name");
    }
    checkEq(programmes, 2, "the held block asks for nothing; the show and the film still do");
    checkEq(named, 1, "one job names a show");
    checkEq(films, 1, "and one takes any film, which is what a movie block with no film means");
}

// An episode picked twice over should air once -- but two blocks that draw on
// the same collection are not the same road twice. Sweeping the channel would
// hand every episode to whichever block asked first and leave the other with
// nothing to play, which airs as the card for as long as that block runs.
void testProgrammesAreKeptOncePerBlock() {
    section("Backend: a programme is kept once, and once per block");

    const auto ep = [](const QString &ref, int block) {
        vchan::MediaItem m;
        m.ref  = ref;
        m.planBlock = block;
        return m;
    };

    QVector<vchan::MediaItem> in;
    in << ep(QStringLiteral("101"), -1) << ep(QStringLiteral("101"), -1)
       << ep(QStringLiteral("102"), -1);
    QVector<vchan::MediaItem> out = VirtualChannelsBackend::keepEachProgrammeOnce(in);
    checkEq(out.size(), 2, "on a channel with no blocks, the second copy goes");

    in.clear();
    in << ep(QStringLiteral("101"), 1) << ep(QStringLiteral("102"), 1)
       << ep(QStringLiteral("101"), 2) << ep(QStringLiteral("102"), 2);
    out = VirtualChannelsBackend::keepEachProgrammeOnce(in);
    checkEq(out.size(), 4, "two blocks drawing the same collection each keep their own");

    in.clear();
    in << ep(QStringLiteral("101"), 3) << ep(QStringLiteral("101"), 3);
    out = VirtualChannelsBackend::keepEachProgrammeOnce(in);
    checkEq(out.size(), 1, "but one block still holds it only once");

    in.clear();
    in << ep(QString(), 1) << ep(QString(), 1);
    out = VirtualChannelsBackend::keepEachProgrammeOnce(in);
    checkEq(out.size(), 2, "a programme with no id is not a duplicate of another with none");
}

void testPlansRoundTrip() {
    section("Backend: plans survive being read out and handed back");

    Fixture fx;
    fx.write(localChannel(3));
    VirtualChannelsBackend b(fx.data(), fx.data());

    QVariantMap cartoons;
    cartoons["type"] = QStringLiteral("series");
    cartoons["name"] = QStringLiteral("Batman Beyond");
    cartoons["ref"]  = QStringLiteral("8324");
    cartoons["minutes"] = 120;

    QVariantMap film;
    film["type"] = QStringLiteral("movie");
    film["minutes"] = 90;

    QVariantMap junk;                       // a type the generator would not know
    junk["type"] = QStringLiteral("wallpaper");
    junk["minutes"] = 30;

    QVariantMap plan;
    plan["name"]     = QStringLiteral("WEEKDAY");
    plan["gridMinutes"] = 30;
    plan["days"]     = QVariantList{ 1, 2, 3, 4, 5 };
    plan["blocks"]   = QVariantList{ cartoons, film, junk };

    check(b.set_channel_plans(3, QVariantList{ plan }), "a plan saves");

    const QVariantList back = b.channel_plans(3);
    checkEq(back.size(), 1, "and reads back");
    if (back.isEmpty()) return;

    const QVariantMap got = back.first().toMap();
    checkStr(got.value(QStringLiteral("name")).toString(), QStringLiteral("WEEKDAY"), "by name");
    checkEq(got.value(QStringLiteral("days")).toList().size(), 5, "on five days");
    checkEq(got.value(QStringLiteral("totalMinutes")).toInt(), 210,
            "running as long as its blocks do");

    const QVariantList blocks = got.value(QStringLiteral("blocks")).toList();
    checkEq(blocks.size(), 2, "the block of a type nobody knows was refused");
    checkStr(blocks.first().toMap().value(QStringLiteral("ref")).toString(),
             QStringLiteral("8324"), "a picked series keeps the id it was picked by");
    checkEq(blocks.first().toMap().value(QStringLiteral("startsAtMinute")).toInt(), 0,
            "the first block starts at midnight, where every day starts");
    checkEq(blocks.at(1).toMap().value(QStringLiteral("startsAtMinute")).toInt(), 120,
            "and the next one where the first ended");

    // Reordering is the screen handing back the list it was given, swapped.
    QVariantList reordered = blocks;
    reordered.swapItemsAt(0, 1);
    QVariantMap again = got;
    again["blocks"] = reordered;
    check(b.set_channel_plans(3, QVariantList{ again }), "a reordered plan saves");
    const QVariantList after = b.channel_plans(3).first().toMap()
                                 .value(QStringLiteral("blocks")).toList();
    checkStr(after.first().toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("movie"), "the moved block is first now");
    checkEq(after.first().toMap().value(QStringLiteral("startsAtMinute")).toInt(), 0,
            "and takes midnight with it");
    checkEq(after.at(1).toMap().value(QStringLiteral("startsAtMinute")).toInt(), 90,
            "shifting what follows onto the clock");

    check(b.set_channel_plans(3, QVariantList{}), "clearing the plans saves");
    checkEq(b.channel_plans(3).size(), 0, "and leaves the channel without any");

    // Switching a channel to blocks seeds three empty days. If reading them
    // back dropped the empty ones, the screen would never see them -- and the
    // first save it made would write back only the day it could see, taking
    // the other two off the channel.
    QVariantMap weekdays;
    weekdays["name"] = QStringLiteral("WEEKDAYS");
    weekdays["gridMinutes"] = 30;
    weekdays["days"] = QVariantList{ 1, 2, 3, 4, 5 };
    weekdays["blocks"] = QVariantList{};

    QVariantMap saturday = weekdays;
    saturday["name"] = QStringLiteral("SATURDAY");
    saturday["days"] = QVariantList{ 6 };

    QVariantMap sunday = weekdays;
    sunday["name"] = QStringLiteral("SUNDAY");
    sunday["days"] = QVariantList{ 7 };

    check(b.set_channel_plans(3, QVariantList{ weekdays, saturday, sunday }),
          "three empty days save");
    checkEq(b.channel_plans(3).size(), 3, "and all three read back");

    // Now fill one in, the way the screen would, and hand all three back.
    QVariantList days = b.channel_plans(3);
    QVariantMap first = days.first().toMap();
    first["blocks"] = QVariantList{ cartoons };
    days[0] = first;
    check(b.set_channel_plans(3, days), "filling one day in saves");

    const QVariantList kept = b.channel_plans(3);
    checkEq(kept.size(), 3, "and the two still empty are still there");
    checkEq(kept.first().toMap().value(QStringLiteral("blocks")).toList().size(), 1,
            "with the block that was added on the day it was added to");
    checkStr(kept.last().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("SUNDAY"), "and the last day still named");

    // A block is played in and out as itself, so it carries its own bumpers.
    QVariantMap withBumpers = cartoons;
    withBumpers["intros"] = QVariantList{ QStringLiteral("interstitials/jack-in"),
                                          QStringLiteral("  ") };
    withBumpers["outros"] = QVariantList{ QStringLiteral("interstitials/jack-out") };

    QVariantMap onlyDay;
    onlyDay["name"] = QStringLiteral("WEEKDAYS");
    onlyDay["gridMinutes"] = 30;
    onlyDay["days"] = QVariantList{ 1, 2, 3, 4, 5 };
    onlyDay["blocks"] = QVariantList{ withBumpers };

    check(b.set_channel_plans(3, QVariantList{ onlyDay }), "a block with its own bumpers saves");
    const QVariantMap block = b.channel_plans(3).first().toMap()
                                .value(QStringLiteral("blocks")).toList().first().toMap();
    checkEq(block.value(QStringLiteral("intros")).toStringList().size(), 1,
            "the blank folder is dropped, the real one kept");
    checkStr(block.value(QStringLiteral("intros")).toStringList().first(),
             QStringLiteral("interstitials/jack-in"), "by name");
    checkStr(block.value(QStringLiteral("outros")).toStringList().first(),
             QStringLiteral("interstitials/jack-out"), "and the outro with it");
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
    testPlansAreRead();
    testBlankBlocksGatherNothing();
    testProgrammesAreKeptOncePerBlock();
    testPlansRoundTrip();
    testFilmPoolEntries();
    testFilmAndShowListsAreSeparate();
    testFilmsFromDecidesWhatAirs();
    testPoolSaveKeepsWhatItDoesNotOwn();
    testSourceSwitchSticks();
    testExclusionsOnALocalChannel();
    testBookingWrites();
    testInterstitialsAreCounted();
    testSourceSwitchAndPicks();
    testChannelLifecycle();
    return 0;
}
