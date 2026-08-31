#include "LocalLibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace vchan {

namespace {

// A library that has run away with itself -- a whole drive pointed at the media
// root by accident -- should make the menus slow, not wedge them forever. These
// are far above any real collection and exist only as a backstop.
constexpr int kMaxShows      = 4000;
constexpr int kMaxMovies     = 8000;
constexpr int kMaxEpisodes   = 4000;   // per show

const QStringList &mediaExtensions() {
    static const QStringList kExt = {
        QStringLiteral("mp4"),  QStringLiteral("mkv"),  QStringLiteral("avi"),
        QStringLiteral("mov"),  QStringLiteral("m4v"),  QStringLiteral("webm"),
        QStringLiteral("mpg"),  QStringLiteral("mpeg"), QStringLiteral("ts"),
        QStringLiteral("m2ts"), QStringLiteral("wmv"),  QStringLiteral("flv"),
        QStringLiteral("ogv"),  QStringLiteral("divx")
    };
    return kExt;
}

// Trailing junk a release name leaves behind once the useful part is read:
// resolution, source, codec and so on. Cut at the first of these rather than
// trying to enumerate every variant, because the useful part is always in front.
const QRegularExpression &noiseTail() {
    static const QRegularExpression re(
        QStringLiteral("[ ._-]+(?:%1)\\b").arg(QStringLiteral(
            "1080p|720p|2160p|480p|4k|uhd|hdtv|bluray|blu-ray|bdrip|brrip|dvdrip|"
            "webrip|web-dl|webdl|x264|x265|h264|h265|hevc|xvid|divx|aac|ac3|dts|"
            "remux|proper|repack|internal|extended|uncut|remastered")),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString tidy(QString s) {
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    // Dots are separators in release names but belong inside initialisms, so
    // only collapse a run of them, never a single one between letters.
    s.replace(QRegularExpression(QStringLiteral("\\.{1,}")), QStringLiteral(" "));
    s.replace(QRegularExpression(QStringLiteral("\\s{2,}")), QStringLiteral(" "));
    return s.trimmed();
}

QString cutNoise(QString s) {
    const auto m = noiseTail().match(s);
    if (m.hasMatch() && m.capturedStart() > 0)
        s = s.left(m.capturedStart());
    return s.trimmed();
}

}  // namespace

LocalLibrary::LocalLibrary(QString mediaRoot) : m_root(std::move(mediaRoot)) {}

void LocalLibrary::setMediaRoot(const QString &root) {
    if (root == m_root) return;
    m_root    = root;
    m_scanned = false;
    m_shows.clear();
    m_movies.clear();
}

QStringList LocalLibrary::seriesDirNames() {
    return { QStringLiteral("series") };
}

QStringList LocalLibrary::moviesDirNames() {
    return { QStringLiteral("movies") };
}

void LocalLibrary::refresh() {
    m_scanned = false;
    m_stamp.clear();
    m_shows.clear();
    m_movies.clear();
}

bool LocalLibrary::isMediaFile(const QString &fileName) {
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return !suffix.isEmpty() && mediaExtensions().contains(suffix);
}

QString LocalLibrary::stripYear(const QString &name, int *year) {
    if (year) *year = 0;
    // A year in brackets is the convention; a bare trailing year is common
    // enough to accept too, but only when something precedes it, so that a
    // folder legitimately called "1917" survives.
    static const QRegularExpression bracketed(
        QStringLiteral("[\\s._-]*[\\(\\[](19\\d{2}|20\\d{2})[\\)\\]]\\s*$"));
    static const QRegularExpression bare(
        QStringLiteral("(?<=\\S)[\\s._-]+(19\\d{2}|20\\d{2})\\s*$"));

    QString out = name;
    auto m = bracketed.match(out);
    if (!m.hasMatch()) m = bare.match(out);
    if (m.hasMatch()) {
        if (year) *year = m.captured(1).toInt();
        out = out.left(m.capturedStart());
    }
    return tidy(out);
}

int LocalLibrary::seasonFromFolder(const QString &folderName) {
    const QString n = folderName.trimmed();
    if (n.isEmpty()) return -1;

    // Extras and specials both mean season zero, which is where every server
    // files material that sits outside the numbered run.
    static const QRegularExpression specials(
        QStringLiteral("^(specials?|extras?|bonus)$"), QRegularExpression::CaseInsensitiveOption);
    if (specials.match(n).hasMatch()) return 0;

    static const QRegularExpression seasonish(
        QStringLiteral("^(?:season|series|s)[\\s._-]*(\\d{1,4})$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = seasonish.match(n);
    if (m.hasMatch()) return m.captured(1).toInt();

    // A folder that is nothing but digits is a season in plenty of libraries.
    static const QRegularExpression digits(QStringLiteral("^(\\d{1,4})$"));
    const auto d = digits.match(n);
    if (d.hasMatch()) return d.captured(1).toInt();

    return -1;
}

bool LocalLibrary::episodeFromFile(const QString &fileName, int *season, int *number,
                                   QString *title) {
    const QString stem = QFileInfo(fileName).completeBaseName();
    if (season) *season = -1;
    if (number) *number = -1;
    if (title)  *title  = tidy(cutNoise(stem));

    // Ordered most specific first. S01E02 is unambiguous; 1x02 nearly so; a
    // bare E02 only says which episode, never which season.
    struct Pattern { const char *re; bool hasSeason; };
    static const Pattern kPatterns[] = {
        { "[Ss](\\d{1,4})[\\s._-]*[Ee](\\d{1,4})",            true  },
        { "(?<![0-9])(\\d{1,4})[Xx](\\d{1,3})(?![0-9])",      true  },
        { "[Ss]eason[\\s._-]*(\\d{1,4})[\\s._-]*"
          "[Ee]pisode[\\s._-]*(\\d{1,4})",                    true  },
        { "(?:^|[\\s._-])[Ee](?:p|pisode)?[\\s._-]*(\\d{1,4})(?![0-9])", false },
    };

    for (const Pattern &p : kPatterns) {
        const QRegularExpression re(QString::fromLatin1(p.re));
        const auto m = re.match(stem);
        if (!m.hasMatch()) continue;

        if (p.hasSeason) {
            if (season) *season = m.captured(1).toInt();
            if (number) *number = m.captured(2).toInt();
        } else {
            if (number) *number = m.captured(1).toInt();
        }
        if (title) {
            // Whatever follows the marker is the episode's own title, once the
            // separator and any release noise are taken off. Nothing after it
            // is not an error -- plenty of files are only ever numbered.
            QString rest = stem.mid(m.capturedEnd());
            rest.remove(QRegularExpression(QStringLiteral("^[\\s._-]+")));
            rest = tidy(cutNoise(rest));
            *title = rest.isEmpty() ? tidy(cutNoise(stem)) : rest;
        }
        return true;
    }
    return false;
}

QString LocalLibrary::seasonKey(const QString &showFolder, int season) {
    return showFolder + QStringLiteral("::s") + QString::number(season);
}

bool LocalLibrary::splitSeasonKey(const QString &key, QString *showFolder, int *season) {
    const int at = key.lastIndexOf(QStringLiteral("::s"));
    if (at <= 0) return false;
    bool ok = false;
    const int n = key.mid(at + 3).toInt(&ok);
    if (!ok) return false;
    if (showFolder) *showFolder = key.left(at);
    if (season)     *season     = n;
    return true;
}

// Cheap enough to run on every access: a listing of two directories, no walk
// into them.
//
// Timestamps alone are not enough. Their resolution is a millisecond at best,
// and adding a folder then looking for it happens well inside that -- which is
// exactly how the first version of this passed review and still needed a
// restart to see a new show. The names of the immediate children settle it: they
// change when a show is added, removed or renamed, whatever the clock says.
QString LocalLibrary::rootStamp() const {
    const QString absRoot = QFileInfo(m_root).canonicalFilePath();
    if (absRoot.isEmpty()) return {};

    QString out;
    for (const QString &n : seriesDirNames() + moviesDirNames()) {
        const QString path = absRoot + QLatin1Char('/') + n;
        const QFileInfo fi(path);
        if (!fi.exists()) { out += QStringLiteral("-|"); continue; }
        out += QString::number(fi.lastModified().toMSecsSinceEpoch()) + QLatin1Char(':');
        const QStringList kids =
            QDir(path).entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        out += QString::number(kids.size()) + QLatin1Char(':') + kids.join(QLatin1Char(','));
        out += QLatin1Char('|');
    }
    return out;
}

void LocalLibrary::ensureScanned() const {
    const QString now = rootStamp();
    if (m_scanned && now == m_stamp) return;
    m_stamp = now;
    scan();
}

void LocalLibrary::scan() const {
    m_scanned = true;
    m_shows.clear();
    m_movies.clear();

    const QString absRoot = QFileInfo(m_root).canonicalFilePath();
    if (absRoot.isEmpty() || !QFileInfo(absRoot).isDir()) return;

    // Only the documented folders are read. Guessing at an arbitrary tree is
    // what the old behaviour did, and it is why the picker used to offer break
    // folders as though they were programmes: without a convention there is no
    // way to tell what a directory is for. A root missing these is reported by
    // hasLibraryFolders() so the interface can say so plainly.
    for (const QString &name : seriesDirNames()) {
        const QString abs = absRoot + QLatin1Char('/') + name;
        if (QFileInfo(abs).isDir()) scanShowsUnder(abs, name);
    }
    for (const QString &name : moviesDirNames()) {
        const QString abs = absRoot + QLatin1Char('/') + name;
        if (QFileInfo(abs).isDir()) scanMoviesUnder(abs, name);
    }

    std::sort(m_shows.begin(), m_shows.end(),
              [](const LocalShow &a, const LocalShow &b) {
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    std::sort(m_movies.begin(), m_movies.end(),
              [](const LocalMovie &a, const LocalMovie &b) {
                  return a.name.localeAwareCompare(b.name) < 0;
              });
}

void LocalLibrary::scanShowsUnder(const QString &absDir, const QString &relDir) const {
    QDir dir(absDir);
    const QFileInfoList showDirs =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &showFi : showDirs) {
        if (m_shows.size() >= kMaxShows) return;

        LocalShow show;
        show.folder = relDir.isEmpty() ? showFi.fileName()
                                       : relDir + QLatin1Char('/') + showFi.fileName();
        show.name   = stripYear(showFi.fileName(), &show.year);
        if (show.name.isEmpty()) show.name = showFi.fileName();

        // Seasons are subdirectories that name themselves as such. Anything
        // else that holds media is folded into an unnumbered season, so a show
        // stored flat still airs.
        QVector<LocalSeason> seasons;
        LocalSeason loose;
        loose.number = -1;
        loose.label  = QStringLiteral("Episodes");

        QDir showDir(showFi.absoluteFilePath());
        for (const QFileInfo &fi : showDir.entryInfoList(QDir::Files, QDir::Name)) {
            if (!isMediaFile(fi.fileName())) continue;
            if (loose.episodes.size() >= kMaxEpisodes) break;
            LocalEpisode ep;
            ep.ref    = show.folder + QLatin1Char('/') + fi.fileName();
            ep.parsed = episodeFromFile(fi.fileName(), &ep.season, &ep.number, &ep.title);
            loose.episodes.append(ep);
        }

        for (const QFileInfo &seasonFi :
             showDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const int num = seasonFromFolder(seasonFi.fileName());
            LocalSeason season;
            season.number = num;
            season.label  = num < 0  ? seasonFi.fileName()
                          : num == 0 ? QStringLiteral("Specials")
                                     : QStringLiteral("Season %1").arg(num);

            const QString seasonRel =
                show.folder + QLatin1Char('/') + seasonFi.fileName();
            QDir sd(seasonFi.absoluteFilePath());
            for (const QFileInfo &fi : sd.entryInfoList(QDir::Files, QDir::Name)) {
                if (!isMediaFile(fi.fileName())) continue;
                if (season.episodes.size() >= kMaxEpisodes) break;
                LocalEpisode ep;
                ep.ref    = seasonRel + QLatin1Char('/') + fi.fileName();
                ep.parsed = episodeFromFile(fi.fileName(), &ep.season, &ep.number, &ep.title);
                // The folder is the authority on which season this is: a file
                // whose name disagrees is more often mislabelled than the
                // folder it was deliberately filed into.
                if (num >= 0) ep.season = num;
                season.episodes.append(ep);
            }
            if (!season.episodes.isEmpty()) seasons.append(season);
        }

        if (!loose.episodes.isEmpty()) seasons.append(loose);
        if (seasons.isEmpty()) continue;    // a folder with no media is not a show

        std::sort(seasons.begin(), seasons.end(),
                  [](const LocalSeason &a, const LocalSeason &b) {
                      // Unnumbered last: it is the catch-all, not episode one.
                      if ((a.number < 0) != (b.number < 0)) return b.number < 0;
                      return a.number < b.number;
                  });
        for (LocalSeason &s : seasons) {
            std::sort(s.episodes.begin(), s.episodes.end(),
                      [](const LocalEpisode &a, const LocalEpisode &b) {
                          if (a.number != b.number) {
                              if (a.number < 0) return false;
                              if (b.number < 0) return true;
                              return a.number < b.number;
                          }
                          return a.ref.localeAwareCompare(b.ref) < 0;
                      });
            show.episodeCount += s.episodes.size();
        }

        show.seasons = seasons;
        m_shows.append(show);
    }
}

void LocalLibrary::scanMoviesUnder(const QString &absDir, const QString &relDir) const {
    QDir dir(absDir);

    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files, QDir::Name)) {
        if (m_movies.size() >= kMaxMovies) return;
        if (!isMediaFile(fi.fileName())) continue;
        LocalMovie mv;
        mv.ref  = relDir.isEmpty() ? fi.fileName()
                                   : relDir + QLatin1Char('/') + fi.fileName();
        mv.name = stripYear(QFileInfo(fi.fileName()).completeBaseName(), &mv.year);
        if (mv.name.isEmpty()) mv.name = fi.completeBaseName();
        m_movies.append(mv);
    }

    // One folder per film is the other common layout. The largest media file
    // inside is the feature; the rest are trailers, samples and extras.
    for (const QFileInfo &sub : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (m_movies.size() >= kMaxMovies) return;
        QDir sd(sub.absoluteFilePath());
        QFileInfo biggest;
        for (const QFileInfo &fi : sd.entryInfoList(QDir::Files, QDir::Name)) {
            if (!isMediaFile(fi.fileName())) continue;
            if (!biggest.exists() || fi.size() > biggest.size()) biggest = fi;
        }
        if (!biggest.exists()) continue;

        const QString subRel = relDir.isEmpty() ? sub.fileName()
                                                : relDir + QLatin1Char('/') + sub.fileName();
        LocalMovie mv;
        mv.ref  = subRel + QLatin1Char('/') + biggest.fileName();
        mv.name = stripYear(sub.fileName(), &mv.year);
        if (mv.name.isEmpty()) mv.name = sub.fileName();
        m_movies.append(mv);
    }
}

bool LocalLibrary::hasLibraryFolders() const {
    const QString absRoot = QFileInfo(m_root).canonicalFilePath();
    if (absRoot.isEmpty()) return false;
    for (const QString &n : seriesDirNames() + moviesDirNames())
        if (QFileInfo(absRoot + QLatin1Char('/') + n).isDir()) return true;
    return false;
}

// The interface stores what it displayed -- "Name (Year)" -- because that is
// what it stores for every other source. Matching has to accept that form as
// well as the bare name and the folder, or a pick made in the menus resolves to
// nothing at schedule time and the channel quietly airs an empty list.
bool LocalLibrary::matchesName(const QString &name, int year, const QString &folder,
                               const QString &wanted) {
    if (wanted.isEmpty()) return false;
    if (name.compare(wanted, Qt::CaseInsensitive) == 0)   return true;
    if (folder.compare(wanted, Qt::CaseInsensitive) == 0) return true;
    if (year > 0) {
        const QString display = QStringLiteral("%1 (%2)").arg(name).arg(year);
        if (display.compare(wanted, Qt::CaseInsensitive) == 0) return true;
    }
    // A folder's leaf, for an entry stored before the library existed.
    const QString leaf = folder.section(QLatin1Char('/'), -1);
    return !leaf.isEmpty() && leaf.compare(wanted, Qt::CaseInsensitive) == 0;
}

QString LocalLibrary::movieRefFor(const QString &wanted) const {
    ensureScanned();
    for (const LocalMovie &mv : m_movies)
        if (matchesName(mv.name, mv.year, mv.ref, wanted)) return mv.ref;
    return {};
}

QVector<LocalShow> LocalLibrary::shows() const {
    ensureScanned();
    return m_shows;
}

QVector<LocalMovie> LocalLibrary::movies() const {
    ensureScanned();
    return m_movies;
}

LocalShow LocalLibrary::showByKey(const QString &key) const {
    ensureScanned();
    for (const LocalShow &s : m_shows)
        if (s.folder == key) return s;
    return {};
}

QVector<LocalEpisode> LocalLibrary::episodesOf(const QString &showKey, int season) const {
    const LocalShow show = showByKey(showKey);
    for (const LocalSeason &s : show.seasons)
        if (s.number == season) return s.episodes;
    return {};
}

QVector<LocalEpisode> LocalLibrary::episodesFor(const QString &showName,
                                                const QStringList &excludedSeasonKeys,
                                                const QStringList &excludedEpisodeRefs) const {
    ensureScanned();

    // Matched on the display name so that a channel keeps working when a folder
    // is renamed from "Show (2003)" to "Show", and on the folder as a fallback
    // so an exact path still resolves.
    const LocalShow *found = nullptr;
    for (const LocalShow &s : m_shows) {
        if (matchesName(s.name, s.year, s.folder, showName)) {
            found = &s;
            break;
        }
    }
    if (!found) return {};

    const QSet<QString> badSeasons(excludedSeasonKeys.cbegin(), excludedSeasonKeys.cend());
    const QSet<QString> badEpisodes(excludedEpisodeRefs.cbegin(), excludedEpisodeRefs.cend());

    QVector<LocalEpisode> out;
    for (const LocalSeason &s : found->seasons) {
        if (badSeasons.contains(seasonKey(found->folder, s.number))) continue;
        for (const LocalEpisode &e : s.episodes)
            if (!badEpisodes.contains(e.ref)) out.append(e);
    }
    return out;
}

}  // namespace vchan
