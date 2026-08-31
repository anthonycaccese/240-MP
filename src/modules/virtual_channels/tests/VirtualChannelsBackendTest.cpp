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

// The write paths, exercised as the interface drives them.
//
// Every other test here reads: it builds a channel and asks what would air.
// That is how a whole class of fault reached a running box unnoticed -- a pool
// that generated a correct schedule but could not be edited, because the code
// that saves a list is not the code that reads one. These tests change things:
// they add, they remove, and they read the file back to see what was actually
// written. Several of them are here because the corresponding bug shipped.
//
// The other half of the class is all-or-nothing validation. A channel file on a
// real box accumulates rows that no longer resolve -- a folder that was
// deleted, an entry written by an older version -- and a save that refuses the
// whole list because of one of them locks the viewer out of the screen with no
// way to find or remove the offending row. So each pool test deliberately
// includes a row that cannot be resolved and asserts the save still lands.

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

// A data root shaped the way the app's is: a media library using the series/
// and movies/ layout, some break folders, and a channels.json.
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

// A channel with no server block is a local one: the source is inferred, not
// stored, so this is all it takes.
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

// ---------------------------------------------------------------------------

void testLocalFilmInProgrammePool() {
    section("Backend: a local film is a programme like any other");

    Fixture fx;
    fx.write(localChannel(3));
    VirtualChannelsBackend b(fx.data(), fx.data());

    // The picker offers Movies for a local channel and the generator plays what
    // it returns, but the save refused the kind outright, so choosing a film
    // reported "could not save that change" and nothing was written.
    QVariantList entries;
    entries.append(entry(QStringLiteral("series"), QStringLiteral("Batman Beyond")));
    entries.append(entry(QStringLiteral("movie"),  QStringLiteral("Mask of the Phantasm")));
    check(b.set_channel_pool(3, QStringLiteral("programmes"), entries),
          "a local film saves into a programme pool");

    const QStringList kinds = kindsIn(fx.read(3), "programmes");
    checkEq(kinds.size(), 2, "both entries were written");
    check(kinds.contains(QStringLiteral("movie")), "the film kept its kind");
    check(kinds.contains(QStringLiteral("series")), "the series is still there");

    // And the pool stays editable afterwards: re-saving a pool that holds a
    // film must not fail, or the film poisons every later edit of that screen.
    check(b.set_channel_pool(3, QStringLiteral("programmes"), entries),
          "a pool holding a film can be saved again");
}

void testOneBadRowDoesNotBlockThePool() {
    section("Backend: one unusable row does not block a pool");

    Fixture fx;
    fx.write(localChannel(3));
    VirtualChannelsBackend b(fx.data(), fx.data());

    // A folder that was deleted, and an entry of a kind this version does not
    // know. Neither is a reason to refuse the viewer's actual edit.
    QVariantList entries;
    entries.append(folderEntry(QStringLiteral("breaks/bumps")));
    entries.append(folderEntry(QStringLiteral("breaks/gone")));
    entries.append(entry(QStringLiteral("sponge"), QStringLiteral("Written By Some Later Version")));
    check(b.set_channel_pool(3, QStringLiteral("bumps"), entries),
          "a stale folder and an unknown kind still let the save land");

    const QStringList kinds = kindsIn(fx.read(3), "bumps");
    checkEq(kinds.size(), 3, "nothing was silently dropped");

    // Removing a row is the operation a viewer reaches for when a pool has gone
    // wrong, so it has to work while the pool is still wrong.
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

    // A series entry read as a folder has no name to show, which is what left
    // the per-show rows blank on screen.
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
    // Stated, because a channel holding entries from two sources cannot be
    // worked out from its entries -- and which source is rewritten here turns
    // on the answer.
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

    // Removing the server block is not enough on its own: the source is also
    // inferred from the entries in the pool, and a channel that was on Plex
    // still has Plex entries in it. Inference alone put the channel straight
    // back on Plex, so the switch appeared to do nothing and the next edit was
    // written to the server's block.
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

    // These refused local outright for as long as "local" meant a bare folder,
    // so every tick on the screen reported success and saved nothing.
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

    // A folder is stored relative to the media root, like every other folder in
    // the file. Storing what the picker handed over meant an absolute path that
    // only works on the box it was picked on.
    check(b.set_booking_folder(3, idx, fx.media() + QStringLiteral("/movies")),
          "a slot takes a folder");
    checkStr(fx.read(3).value(QLatin1String("appointments")).toArray().at(idx)
                 .toObject().value(QLatin1String("folder")).toString(),
             QStringLiteral("movies"), "the folder was stored relative to the media root");

    // And a folder outside the library is refused at the point the viewer picks
    // it, not silently ignored hours later when the schedule is built.
    check(!b.set_booking_folder(3, idx, QStringLiteral("/etc")),
          "a folder outside the media root is refused");

    // Films picked by name, which a local slot refused outright until the
    // library existed to pick them from. Genres and the rest stay refused:
    // local files have none, and silently accepting them would leave a slot
    // looking as though it had been given something to draw on.
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

    // Reading only the string form reported a channel as having no breaks at
    // all the moment one was added through the interface, which is what put
    // "NONE" on the channel screen beside a breaks screen counting hundreds.
    checkEq(row.value(QStringLiteral("count")).toInt(), 3,
            "clips under both folder shapes are counted");
    checkEq(row.value(QStringLiteral("sources")).toInt(), 3,
            "a server's collection counts as a source even with nothing to count");
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
    testSourceSwitchSticks();
    testExclusionsOnALocalChannel();
    testBookingWrites();
    testInterstitialsAreCounted();
    testChannelLifecycle();
    return 0;
}
