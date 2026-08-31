#include "../LocalLibrary.h"
#include "TestHarness.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace vchan;
using vtest::check;
using vtest::checkEq;
using vtest::checkStr;
using vtest::section;

namespace {

bool touch(const QString &path, qint64 bytes = 1) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QByteArray(int(bytes), 'x'));
    return true;
}

QString titleOf(const QString &file) {
    QString t;
    LocalLibrary::episodeFromFile(file, nullptr, nullptr, &t);
    return t;
}

int seasonOf(const QString &file) {
    int s = -1;
    LocalLibrary::episodeFromFile(file, &s, nullptr, nullptr);
    return s;
}

int numberOf(const QString &file) {
    int n = -1;
    LocalLibrary::episodeFromFile(file, nullptr, &n, nullptr);
    return n;
}

void testYearStripping() {
    section("LocalLibrary: names and years");
    int year = 0;
    checkStr(LocalLibrary::stripYear(QStringLiteral("Batman Beyond (1999)"), &year),
             QStringLiteral("Batman Beyond"), "bracketed year removed");
    checkEq(year, 1999, "bracketed year captured");

    checkStr(LocalLibrary::stripYear(QStringLiteral("Samurai Jack [2001]"), &year),
             QStringLiteral("Samurai Jack"), "square-bracketed year removed");
    checkEq(year, 2001, "square-bracketed year captured");

    checkStr(LocalLibrary::stripYear(QStringLiteral("Justice League 2001"), &year),
             QStringLiteral("Justice League"), "bare trailing year removed");

    // A title that IS a year must survive: stripping it would leave nothing.
    year = 0;
    checkStr(LocalLibrary::stripYear(QStringLiteral("1917"), &year),
             QStringLiteral("1917"), "a title that is only a year is kept");
    checkEq(year, 0, "no year captured from a bare numeric title");

    checkStr(LocalLibrary::stripYear(QStringLiteral("Dragon.Ball.Z"), &year),
             QStringLiteral("Dragon Ball Z"), "dots become spaces");
    checkStr(LocalLibrary::stripYear(QStringLiteral("The_New_Batman_Adventures"), &year),
             QStringLiteral("The New Batman Adventures"), "underscores become spaces");
}

void testSeasonFolders() {
    section("LocalLibrary: season folders");
    checkEq(LocalLibrary::seasonFromFolder(QStringLiteral("Season 1")),  1, "Season 1");
    checkEq(LocalLibrary::seasonFromFolder(QStringLiteral("Season 01")), 1, "Season 01 (padded)");
    checkEq(LocalLibrary::seasonFromFolder(QStringLiteral("season 12")), 12, "lower case");
    checkEq(LocalLibrary::seasonFromFolder(QStringLiteral("S03")),       3, "S03");
    checkEq(LocalLibrary::seasonFromFolder(QStringLiteral("Series 2")),  2, "Series 2 (UK usage)");
    checkEq(LocalLibrary::seasonFromFolder(QStringLiteral("4")),         4, "bare number");
    checkEq(LocalLibrary::seasonFromFolder(QStringLiteral("Specials")),  0, "Specials is season 0");
    checkEq(LocalLibrary::seasonFromFolder(QStringLiteral("Extras")),    0, "Extras is season 0");
    checkEq(LocalLibrary::seasonFromFolder(QStringLiteral("Featurettes")), -1,
            "an unrelated folder is not a season");
    checkEq(LocalLibrary::seasonFromFolder(QString()), -1, "empty name is not a season");
}

void testEpisodeParsing() {
    section("LocalLibrary: episode names");
    checkEq(seasonOf(QStringLiteral("Show S01E02 - Title.mkv")), 1, "SxxExx season");
    checkEq(numberOf(QStringLiteral("Show S01E02 - Title.mkv")), 2, "SxxExx number");
    checkStr(titleOf(QStringLiteral("Show S01E02 - Title.mkv")),
             QStringLiteral("Title"), "title after the marker");

    checkEq(seasonOf(QStringLiteral("Show 3x07.mp4")), 3, "NxNN season");
    checkEq(numberOf(QStringLiteral("Show 3x07.mp4")), 7, "NxNN number");

    checkEq(seasonOf(QStringLiteral("Show Season 2 Episode 5.mp4")), 2, "long form season");
    checkEq(numberOf(QStringLiteral("Show Season 2 Episode 5.mp4")), 5, "long form number");

    // A bare episode marker says nothing about the season, and must not invent one.
    checkEq(seasonOf(QStringLiteral("Show E04.mkv")), -1, "bare E04 has no season");
    checkEq(numberOf(QStringLiteral("Show E04.mkv")),  4, "bare E04 number");

    check(!LocalLibrary::episodeFromFile(QStringLiteral("Just A Name.mkv"),
                                         nullptr, nullptr, nullptr),
          "an unparseable name reports failure");
    checkStr(titleOf(QStringLiteral("Just A Name.mkv")),
             QStringLiteral("Just A Name"), "unparseable name keeps its stem as title");

    // Release noise is cut, but only after the useful part.
    checkStr(titleOf(QStringLiteral("Show S01E01 - Pilot 1080p BluRay x264.mkv")),
             QStringLiteral("Pilot"), "release noise trimmed from the title");

    check(LocalLibrary::isMediaFile(QStringLiteral("a.mkv")), "mkv is media");
    check(LocalLibrary::isMediaFile(QStringLiteral("a.MP4")), "extension case ignored");
    check(!LocalLibrary::isMediaFile(QStringLiteral("a.txt")), "txt is not media");
    check(!LocalLibrary::isMediaFile(QStringLiteral("noextension")), "no extension is not media");
}

void testSeasonKeys() {
    section("LocalLibrary: season keys");
    const QString k = LocalLibrary::seasonKey(QStringLiteral("series/Show"), 2);
    QString folder;
    int season = -99;
    check(LocalLibrary::splitSeasonKey(k, &folder, &season), "a season key round-trips");
    checkStr(folder, QStringLiteral("series/Show"), "folder recovered");
    checkEq(season, 2, "season recovered");
    check(!LocalLibrary::splitSeasonKey(QStringLiteral("nonsense"), &folder, &season),
          "a malformed key is rejected rather than guessed at");
}

void testScanning() {
    section("LocalLibrary: scanning a tree");
    QTemporaryDir tmp;
    check(tmp.isValid(), "temp dir created");
    const QString root = tmp.path();

    touch(root + "/series/Batman Beyond (1999)/Season 1/Batman Beyond S01E01 - Rebirth.mkv");
    touch(root + "/series/Batman Beyond (1999)/Season 1/Batman Beyond S01E02 - Golem.mkv");
    touch(root + "/series/Batman Beyond (1999)/Season 2/Batman Beyond S02E01 - Splicers.mkv");
    touch(root + "/series/Batman Beyond (1999)/Specials/Behind the Scenes.mkv");
    // A show stored flat, with no season folders at all.
    touch(root + "/series/Samurai Jack/Samurai Jack S01E01.mp4");
    touch(root + "/series/Samurai Jack/Samurai Jack S01E02.mp4");
    // A folder holding no media is not a show.
    QDir().mkpath(root + "/series/Empty Folder");
    // Non-media must not become episodes.
    touch(root + "/series/Samurai Jack/notes.txt");

    touch(root + "/movies/The Iron Giant (1999).mkv");
    touch(root + "/movies/Akira (1988)/Akira (1988).mkv", 5000);
    touch(root + "/movies/Akira (1988)/trailer.mkv", 10);

    LocalLibrary lib(root);
    const auto shows = lib.shows();
    checkEq(shows.size(), 2, "two shows found, the empty folder ignored");

    // Sorted by display name: Batman Beyond before Samurai Jack.
    checkStr(shows[0].name, QStringLiteral("Batman Beyond"), "year stripped from show name");
    checkEq(shows[0].year, 1999, "show year captured");
    checkEq(shows[0].seasons.size(), 3, "two numbered seasons plus specials");
    checkEq(shows[0].seasons[0].number, 0, "specials sort first as season 0");
    checkEq(shows[0].seasons[1].number, 1, "season 1 next");
    checkEq(shows[0].seasons[1].episodes.size(), 2, "season 1 has two episodes");
    checkEq(shows[0].episodeCount, 4, "episode count spans every season");

    checkStr(shows[1].name, QStringLiteral("Samurai Jack"), "flat show found");
    checkEq(shows[1].seasons.size(), 1, "flat show gets one catch-all season");
    checkEq(shows[1].seasons[0].episodes.size(), 2, "the txt file was not counted");

    const auto movies = lib.movies();
    checkEq(movies.size(), 2, "loose file and folder-per-film both found");
    checkStr(movies[0].name, QStringLiteral("Akira"), "movie year stripped");
    check(movies[0].ref.endsWith(QStringLiteral("Akira (1988).mkv")),
          "the feature is chosen over the trailer by size");

    // Exclusions
    const QString s1 = LocalLibrary::seasonKey(shows[0].folder, 1);
    auto kept = lib.episodesFor(QStringLiteral("Batman Beyond"), { s1 }, {});
    checkEq(kept.size(), 2, "excluding season 1 removes its two episodes");

    const QString oneRef = shows[0].seasons[1].episodes[0].ref;
    kept = lib.episodesFor(QStringLiteral("Batman Beyond"), {}, { oneRef });
    checkEq(kept.size(), 3, "excluding one episode removes exactly one");

    checkEq(lib.episodesFor(QStringLiteral("No Such Show"), {}, {}).size(), 0,
            "an unknown show yields nothing rather than everything");

    // What the browser actually stores, end to end.
    checkEq(lib.episodesFor(QStringLiteral("Batman Beyond (1999)"), {}, {}).size(), 4,
            "a pick stored as \"Name (Year)\" resolves to its episodes");
    checkStr(lib.movieRefFor(QStringLiteral("Akira (1988)")),
             QStringLiteral("movies/Akira (1988)/Akira (1988).mkv"),
             "a film stored as \"Name (Year)\" resolves to its file");
    checkStr(lib.movieRefFor(QStringLiteral("Nothing Here")), QString(),
             "an unknown film resolves to nothing");
}

void testNameMatching() {
    section("LocalLibrary: matching what the interface stored");
    // The browser stores the label it displayed, which carries the year. A
    // resolver that only knew the bare name would match nothing, and the
    // channel would air an empty list with no error anywhere.
    check(LocalLibrary::matchesName(QStringLiteral("Batman Beyond"), 1999,
                                    QStringLiteral("series/Batman Beyond (1999)"),
                                    QStringLiteral("Batman Beyond (1999)")),
          "the displayed \"Name (Year)\" form matches");
    check(LocalLibrary::matchesName(QStringLiteral("Batman Beyond"), 1999,
                                    QStringLiteral("series/Batman Beyond (1999)"),
                                    QStringLiteral("Batman Beyond")),
          "the bare name matches");
    check(LocalLibrary::matchesName(QStringLiteral("Batman Beyond"), 1999,
                                    QStringLiteral("series/Batman Beyond (1999)"),
                                    QStringLiteral("series/Batman Beyond (1999)")),
          "the full folder matches");
    check(LocalLibrary::matchesName(QStringLiteral("Batman Beyond"), 1999,
                                    QStringLiteral("series/Batman Beyond (1999)"),
                                    QStringLiteral("Batman Beyond (1999)").toUpper()),
          "matching ignores case");
    check(!LocalLibrary::matchesName(QStringLiteral("Batman Beyond"), 1999,
                                     QStringLiteral("series/Batman Beyond (1999)"),
                                     QStringLiteral("Batman")),
          "a partial name does not match");
    check(!LocalLibrary::matchesName(QStringLiteral("Batman Beyond"), 1999,
                                     QStringLiteral("series/Batman Beyond (1999)"),
                                     QString()),
          "an empty name matches nothing");
}

void testConventionIsTheContract() {
    section("LocalLibrary: only the documented folders are read");
    QTemporaryDir tmp;
    const QString root = tmp.path();
    // Media outside series/ and movies/ is not a programme source. This is what
    // stops a folder of bumps being offered as though it were a show.
    touch(root + "/Star Trek/Season 1/Star Trek S01E01.mkv");
    touch(root + "/breaks/action/bump/bump-01.mp4");

    LocalLibrary lib(root);
    checkEq(lib.shows().size(), 0, "a stray top-level folder is not a show");
    check(!lib.hasLibraryFolders(), "a root with neither folder reports it");

    touch(root + "/series/Star Trek/Season 1/Star Trek S01E01.mkv");
    lib.refresh();
    checkEq(lib.shows().size(), 1, "the same show under series/ is found");
    check(lib.hasLibraryFolders(), "a root with series/ reports it");
    checkEq(lib.shows()[0].episodeCount, 1, "and only the conventional copy counts");
}

void testCacheInvalidation() {
    section("LocalLibrary: a show added while running turns up");
    QTemporaryDir tmp;
    const QString root = tmp.path();
    touch(root + "/series/First Show/First Show S01E01.mkv");

    LocalLibrary lib(root);
    checkEq(lib.shows().size(), 1, "one show to begin with");

    // The scan is cached, so this is the case that used to need a restart: a
    // second show appearing after the first read.
    touch(root + "/series/Second Show/Second Show S01E01.mkv");
    checkEq(lib.shows().size(), 2, "the new show is seen without a restart");

    QDir(root + "/series/Second Show").removeRecursively();
    checkEq(lib.shows().size(), 1, "and a removed show stops being offered");
}

void testEmptyAndMissing() {
    section("LocalLibrary: nothing to read");
    LocalLibrary none(QStringLiteral("/nonexistent/path/for/testing"));
    checkEq(none.shows().size(), 0, "a missing root yields no shows, not a crash");
    checkEq(none.movies().size(), 0, "a missing root yields no movies");

    LocalLibrary blank((QString()));
    checkEq(blank.shows().size(), 0, "an empty root yields no shows");

    QTemporaryDir tmp;
    LocalLibrary empty(tmp.path());
    checkEq(empty.shows().size(), 0, "an empty directory yields no shows");
}

}  // namespace

void testExtrasAreNotFilms() {
    section("LocalLibrary: extras are not films");

    // The suffix form Jellyfin documents.
    check(LocalLibrary::isExtraPath(QStringLiteral("The Film (1999)-trailer.mkv")),
          "a -trailer suffix is an extra");
    check(LocalLibrary::isExtraPath(QStringLiteral("The Film-sample.mp4")),
          "a -sample suffix is an extra");
    check(LocalLibrary::isExtraPath(QStringLiteral("The Film-behindthescenes.mkv")),
          "a -behindthescenes suffix is an extra");
    check(LocalLibrary::isExtraPath(QStringLiteral("The Film-deleted2.mkv")),
          "a numbered extra suffix is an extra");

    // The folder form.
    check(LocalLibrary::isExtraPath(QStringLiteral("The Film (1999)/Trailers/teaser.mkv")),
          "anything under Trailers is an extra");
    check(LocalLibrary::isExtraPath(QStringLiteral("The Film (1999)/Behind The Scenes/making of.mkv")),
          "anything under Behind The Scenes is an extra");
    check(LocalLibrary::isExtraPath(QStringLiteral("movies/Film/Featurettes/one.mkv")),
          "anything under Featurettes is an extra");

    // The bare form, which is what was actually on the box.
    check(LocalLibrary::isExtraPath(QStringLiteral("The Film (1999)/trailer.mp4")),
          "a file called trailer is an extra");

    // And the things that must not be caught. A film whose title contains one
    // of these words is still a film.
    check(!LocalLibrary::isExtraPath(QStringLiteral("The Film (1999).mkv")),
          "an ordinary film is not an extra");
    check(!LocalLibrary::isExtraPath(QStringLiteral("Trailer Park Boys (2001).mkv")),
          "a title beginning with the word is not an extra");
    check(!LocalLibrary::isExtraPath(QStringLiteral("Scenes From A Marriage (1973).mkv")),
          "a title beginning with Scenes is not an extra");
    check(!LocalLibrary::isExtraPath(QStringLiteral("movies/Interview With The Vampire (1994).mkv")),
          "a title beginning with Interview is not an extra");

    // The scan agrees with the rule: a folder holding a feature and its trailer
    // yields the feature only.
    QTemporaryDir dir;
    const QString movies = dir.path() + QStringLiteral("/movies");
    touch(movies + QStringLiteral("/Mask of the Phantasm (1993)/Mask of the Phantasm.mkv"), 5000);
    touch(movies + QStringLiteral("/Mask of the Phantasm (1993)/trailer.mp4"), 9000);
    touch(movies + QStringLiteral("/Some Film (2001)-trailer.mkv"), 100);
    touch(movies + QStringLiteral("/Some Film (2001).mkv"), 100);

    LocalLibrary lib(dir.path());
    const QVector<LocalMovie> films = lib.movies();
    checkEq(films.size(), 2, "two films, not four files");
    for (const LocalMovie &mv : films)
        check(!mv.ref.contains(QStringLiteral("trailer")),
              "no trailer was taken for a film");
    // Deliberately the smaller file: size decides which is the feature, and the
    // trailer here is the larger one, so only the name can settle it.
    bool gotFeature = false;
    for (const LocalMovie &mv : films)
        if (mv.ref.endsWith(QStringLiteral("Mask of the Phantasm.mkv"))) gotFeature = true;
    check(gotFeature, "the feature was taken even though the trailer was bigger");
}

void testEpisodeAddedInsideAShow() {
    section("LocalLibrary: an episode added inside a show");

    QTemporaryDir dir;
    const QString season = dir.path()
        + QStringLiteral("/series/Batman Beyond (1999)/Season 1");
    touch(season + QStringLiteral("/Batman Beyond S01E01 - Rebirth.mkv"));

    LocalLibrary lib(dir.path());
    checkEq(lib.shows().size(), 1, "the show is found");
    checkEq(lib.shows().at(0).episodeCount, 1, "with its one episode");

    // The cheap stamp lists series/ and movies/, so a file appearing deeper
    // than that cannot change it. This is the case a rebuild exists to catch.
    touch(season + QStringLiteral("/Batman Beyond S01E02 - Black Out.mkv"));
    checkEq(lib.shows().at(0).episodeCount, 1,
            "the cheap check does not see inside a show it already knows");

    lib.refresh();
    checkEq(lib.shows().at(0).episodeCount, 2,
            "a refresh sees it, which is what a rebuild asks for");
}

int runLocalLibraryTests() {
    testEpisodeAddedInsideAShow();
    testExtrasAreNotFilms();
    testYearStripping();
    testSeasonFolders();
    testEpisodeParsing();
    testSeasonKeys();
    testScanning();
    testNameMatching();
    testConventionIsTheContract();
    testCacheInvalidation();
    testEmptyAndMissing();
    return 0;
}
