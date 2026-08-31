#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vchan {

// A local media folder read the way a media server reads one.
//
// The rest of the module already thinks in shows, seasons and episodes because
// that is what Plex, Jellyfin and Emby return. Local files were the exception:
// they were a bare folder path, so the only thing the interface could offer for
// them was a list of directories, and picking one told nobody what it held.
// This turns a directory tree into the same shape the servers hand back, so one
// browser can present all four.
//
// Naming follows the conventions Jellyfin and Plex document, loosely and in
// that order of preference. Loosely is deliberate: a viewer's own files are not
// a database, and a folder that almost matches should still air rather than
// disappear with no explanation. Anything unparseable keeps its filename as its
// title and sorts last, which is visible and fixable, unlike being dropped.

struct LocalEpisode {
    QString ref;        // path relative to the media root; the schedule's ref
    QString title;      // episode title if the name carried one, else the stem
    int     season  = -1;
    int     number  = -1;
    bool    parsed  = false;   // false when no SxxEyy-style marker was found
};

struct LocalSeason {
    int                   number = -1;   // 0 is Specials, -1 is "no season"
    QString               label;
    QVector<LocalEpisode> episodes;
};

struct LocalShow {
    QString              name;      // display name, year stripped
    QString              folder;    // path relative to the media root
    int                  year = 0;
    QVector<LocalSeason> seasons;
    int                  episodeCount = 0;
};

struct LocalMovie {
    QString ref;
    QString name;
    int     year = 0;
};

// Scans on demand and holds the result until the folders it read change.
//
// A browse is a keypress away from another browse -- descending from a show to
// its seasons to its episodes is three in a row -- and re-walking a large
// library each time is the difference between a menu that responds and one that
// stalls. The timestamps of series/ and movies/ are checked on every access,
// which is two stat calls, so a show added while the player is running turns up
// without a restart.
class LocalLibrary {
public:
    explicit LocalLibrary(QString mediaRoot = QString());

    void setMediaRoot(const QString &root);
    QString mediaRoot() const { return m_root; }

    // The only subdirectories read. The layout is the contract:
    //   <media>/series/<Show Name (Year)>/Season 1/<Show> S01E01 - Title.mkv
    //   <media>/movies/<Film Name (Year)>.mkv
    // Anything outside them is not a programme source, which is what lets the
    // picker tell a show from a folder of bumps.
    static QStringList seriesDirNames();
    static QStringList moviesDirNames();

    // False when the media root has neither folder, so the interface can say
    // what is missing instead of showing an empty list.
    bool hasLibraryFolders() const;

    void refresh();                       // force a rescan on next access
    QVector<LocalShow>  shows() const;
    QVector<LocalMovie> movies() const;

    // Lookup by the ids handed to the interface. Empty when nothing matches,
    // never a default-constructed row pretending to be real.
    LocalShow  showByKey(const QString &key) const;
    QVector<LocalEpisode> episodesOf(const QString &showKey, int season) const;

    // Every episode of a show, in airing order, with excluded seasons and
    // episodes already removed. This is what the generator consumes.
    QVector<LocalEpisode> episodesFor(const QString &showName,
                                      const QStringList &excludedSeasonKeys,
                                      const QStringList &excludedEpisodeRefs) const;

    // Parsing helpers, exposed so they can be tested directly rather than only
    // through a filesystem.
    static QString stripYear(const QString &name, int *year = nullptr);
    static int     seasonFromFolder(const QString &folderName);
    static bool    episodeFromFile(const QString &fileName, int *season, int *number,
                                   QString *title);
    static bool    isMediaFile(const QString &fileName);

    // True for the files a media server files under a film rather than as one:
    // trailers, samples, featurettes, deleted scenes and the like, named by the
    // conventions Jellyfin documents -- a "-trailer" suffix, or a subfolder
    // called Trailers, Extras, Featurettes and so on. A movie slot pointed at a
    // folder used to treat every file in it as a film, so a trailer sitting
    // beside the feature had an even chance of being what aired at eight.
    static bool    isExtraPath(const QString &relPath);

    // True when `wanted` names this item in any form the interface might have
    // stored: the bare name, "Name (Year)", the folder, or the folder's leaf.
    static bool    matchesName(const QString &name, int year, const QString &folder,
                               const QString &wanted);
    // The file a film entry refers to, empty when nothing matches.
    QString        movieRefFor(const QString &wanted) const;

    // Stable ids for the interface: a show is its folder, a season is the show
    // plus its number. Paths rather than indices, so a row keeps its identity
    // when the library is rescanned and something has been added above it.
    static QString showKey(const LocalShow &s)   { return s.folder; }
    static QString seasonKey(const QString &showFolder, int season);
    static bool    splitSeasonKey(const QString &key, QString *showFolder, int *season);

private:
    void ensureScanned() const;
    void scan() const;
    void scanShowsUnder(const QString &absDir, const QString &relDir) const;
    void scanMoviesUnder(const QString &absDir, const QString &relDir) const;

    // The stamps the cached scan was taken from; a change to either means the
    // set of shows or films moved and the scan is stale.
    QString                      rootStamp() const;

    QString m_root;
    mutable QString              m_stamp;
    mutable bool                 m_scanned = false;
    mutable QVector<LocalShow>   m_shows;
    mutable QVector<LocalMovie>  m_movies;
};

}
