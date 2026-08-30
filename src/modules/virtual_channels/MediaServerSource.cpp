#include "MediaServerSource.h"

#include <QDebug>
#include <QMetaObject>
#include <QTimer>
#include <QVariantMap>
#include <algorithm>

namespace vchan {

namespace {

const QLatin1String kTvType("tvshows");
const QLatin1String kMovieType("movies");

const QLatin1String kEpisode("Episode");
const QLatin1String kMovie("Movie");
const QLatin1String kSeries("Series");
const QLatin1String kBoxSet("BoxSet");

bool listContainsCI(const QStringList &haystack, const QString &needle) {
    for (const QString &h : haystack)
        if (h.compare(needle, Qt::CaseInsensitive) == 0) return true;
    return false;
}

void sortForPlayout(QVector<MediaItem> &items) {
    std::stable_sort(items.begin(), items.end(),
                     [](const MediaItem &a, const MediaItem &b) {
        const int s = a.series.compare(b.series, Qt::CaseInsensitive);
        if (s != 0) return s < 0;
        if (a.ep != b.ep) return a.ep < b.ep;
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
}
}

MediaServerSource::MediaServerSource(QObject *parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(kServerTimeoutMs);
    connect(m_timer, &QTimer::timeout, this, &MediaServerSource::onTimeout);
}

QString MediaServerSource::providerName(SlotSource src) {
    switch (src) {
    case SlotSource::Jellyfin: return QStringLiteral("Jellyfin");
    case SlotSource::Emby:     return QStringLiteral("Emby");
    case SlotSource::Plex:     return QStringLiteral("Plex");
    case SlotSource::Local:    break;
    }
    return QStringLiteral("Local");
}

QString MediaServerSource::seasonKey(const QString &seriesId, int seasonNumber) {
    return seriesId + QLatin1Char(':') + QString::number(seasonNumber);
}

bool MediaServerSource::parseSeasonKey(const QString &key, QString *seriesId, int *seasonNumber) {
    const int at = key.lastIndexOf(QLatin1Char(':'));
    if (at <= 0 || at + 1 >= key.size()) return false;
    bool ok = false;
    const int n = key.mid(at + 1).toInt(&ok);
    if (!ok) return false;
    if (seriesId)     *seriesId = key.left(at);
    if (seasonNumber) *seasonNumber = n;
    return true;
}

void MediaServerSource::setBackend(SlotSource src, QObject *backend) {
    QObject **slotPtr = (src == SlotSource::Jellyfin) ? &m_jellyfin
                      : (src == SlotSource::Emby)     ? &m_emby
                                                      : nullptr;
    if (!slotPtr) return;
    *slotPtr = nullptr;
    if (!backend) return;

    const QMetaObject *mo = backend->metaObject();
    static const char *required[] = {
        "load_libraries()",
        "load_items(QString,QString,QString)",
        "load_seasons(QString)",
        "get_playback_url(QString,QString,int,int,bool)",
    };
    for (const char *m : required) {
        if (mo->indexOfMethod(m) < 0) {
            qWarning("[VirtualChannels] %s backend has no %s — that source is unavailable",
                     qPrintable(providerName(src)), m);
            return;
        }
    }
    if (mo->indexOfSignal("itemsLoaded(QVariant)") < 0
        || mo->indexOfSignal("librariesLoaded(QVariant)") < 0) {
        qWarning("[VirtualChannels] %s backend does not report its listings — that source is unavailable",
                 qPrintable(providerName(src)));
        return;
    }

    connect(backend, SIGNAL(librariesLoaded(QVariant)), this, SLOT(onLibrariesLoaded(QVariant)),
            Qt::UniqueConnection);
    connect(backend, SIGNAL(itemsLoaded(QVariant)), this, SLOT(onItemsLoaded(QVariant)),
            Qt::UniqueConnection);
    if (mo->indexOfSignal("seasonsLoaded(QVariant)") >= 0)
        connect(backend, SIGNAL(seasonsLoaded(QVariant)), this, SLOT(onSeasonsLoaded(QVariant)),
                Qt::UniqueConnection);
    if (mo->indexOfSignal("boxsetChildrenLoaded(QVariant)") >= 0)
        connect(backend, SIGNAL(boxsetChildrenLoaded(QVariant)),
                this, SLOT(onBoxsetChildrenLoaded(QVariant)), Qt::UniqueConnection);
    if (mo->indexOfSignal("errorOccurred(QString)") >= 0)
        connect(backend, SIGNAL(errorOccurred(QString)), this, SLOT(onBackendError(QString)),
                Qt::UniqueConnection);

    if (!m_wired.contains(backend)) {
        m_wired.insert(backend);
        connect(backend, &QObject::destroyed, this, [this, slotPtr, backend]() {
            m_wired.remove(backend);
            if (m_active == backend) fail(QStringLiteral("Server went away"));
            if (*slotPtr == backend) *slotPtr = nullptr;
        });
    }

    *slotPtr = backend;
}

QObject *MediaServerSource::backend(SlotSource src) const {
    if (src == SlotSource::Jellyfin) return m_jellyfin;
    if (src == SlotSource::Emby)     return m_emby;
    return nullptr;
}

bool MediaServerSource::busy() const { return m_stage != Stage::Idle; }

bool MediaServerSource::fromActive() const {
    return m_active && sender() == m_active && m_stage != Stage::Idle;
}

bool MediaServerSource::callBackend(const char *method, const QStringList &args) {
    if (!m_active) return false;
    bool ok = false;
    switch (args.size()) {
    case 0: ok = QMetaObject::invokeMethod(m_active, method); break;
    case 1: ok = QMetaObject::invokeMethod(m_active, method, Q_ARG(QString, args[0])); break;
    case 2: ok = QMetaObject::invokeMethod(m_active, method, Q_ARG(QString, args[0]),
                                           Q_ARG(QString, args[1])); break;
    default: ok = QMetaObject::invokeMethod(m_active, method, Q_ARG(QString, args[0]),
                                            Q_ARG(QString, args[1]),
                                            Q_ARG(QString, args[2])); break;
    }
    if (ok) m_timer->start();
    return ok;
}

void MediaServerSource::reset() {
    m_timer->stop();
    m_stage   = Stage::Idle;
    m_active  = nullptr;
    m_req             = Request{};
    m_expandingSeries = false;
    m_libs.clear();
    m_pendingSeries.clear();
    m_pendingBoxsets.clear();
    m_currentLib.clear();
    m_askedBoxsetsForLib = false;
    m_truncated = false;
    m_rejected  = 0;
    m_collected.clear();
    m_seenIds.clear();
    m_expandedSeries.clear();
    m_browseKind.clear();
    m_browseParent.clear();
    m_browseSeason = -1;
    m_browseSeries.clear();
    m_browseItems.clear();
}

void MediaServerSource::cancel() { reset(); }

void MediaServerSource::fail(const QString &reason) {
    if (m_stage == Stage::Idle) return;
    const QString kind = m_browseKind;
    reset();
    qWarning("[VirtualChannels] media server request failed: %s", qPrintable(reason));
    if (kind.isEmpty()) emit enumerationFailed(reason);
    else                emit browseFailed(kind, reason);
}

bool MediaServerSource::itemToMedia(const QVariantMap &m, SlotSource src, MediaItem *out) {
    if (!out) return false;
    const QString id = m.value(QStringLiteral("itemId")).toString();
    const qint64 dur = static_cast<qint64>(m.value(QStringLiteral("duration")).toDouble());
    if (id.isEmpty() || dur <= 0) return false;

    out->src   = src;
    out->ref   = id;
    out->partKey = m.value(QStringLiteral("mediaSourceId")).toString();
    out->durMs = dur;
    out->title = m.value(QStringLiteral("title")).toString();
    out->series= m.value(QStringLiteral("grandparentTitle")).toString();
    out->desc  = m.value(QStringLiteral("overview")).toString();
    out->art   = m.value(QStringLiteral("imageTag")).toString();

    const int season  = m.value(QStringLiteral("parentIndex")).toInt();
    const int episode = m.value(QStringLiteral("index")).toInt();
    out->ep = (season > 0 && episode > 0)
                  ? QStringLiteral("S%1E%2")
                        .arg(season,  2, 10, QLatin1Char('0'))
                        .arg(episode, 2, 10, QLatin1Char('0'))
                  : QString();
    return true;
}

QStringList MediaServerSource::eligibleLibraries(const QVariant &libraries, bool films,
                                                 const QStringList &wanted) const {
    QStringList out;
    const bool bothKinds = !m_req.collections.isEmpty()
                           || m_req.wants == Request::Wants::Anything;
    const QLatin1String need = films ? kMovieType : kTvType;

    for (const QVariant &v : libraries.toList()) {
        const QVariantMap m = v.toMap();
        const QString type = m.value(QStringLiteral("collectionType")).toString();
        if (bothKinds ? (type != kTvType && type != kMovieType) : (type != need))
            continue;
        if (!wanted.isEmpty() && !listContainsCI(wanted, m.value(QStringLiteral("title")).toString()))
            continue;
        QString id = m.value(QStringLiteral("itemId")).toString();
        if (id.isEmpty()) id = m.value(QStringLiteral("key")).toString();
        if (!id.isEmpty() && !out.contains(id)) out << id;
    }
    return out;
}

bool MediaServerSource::enumerate(const Request &req) {
    if (busy()) return false;
    QObject *b = backend(req.src);
    if (!b) return false;

    reset();
    m_active = b;
    m_req    = req;
    m_stage  = Stage::GenLibraries;
    if (!callBackend("load_libraries")) { reset(); return false; }
    return true;
}

void MediaServerSource::genNext() {
    if (!m_pendingBoxsets.isEmpty()) {
        m_stage = Stage::GenBoxsetChildren;
        if (!callBackend("load_boxset_children", { m_pendingBoxsets.takeFirst() }))
            fail(QStringLiteral("Server cannot list collection contents"));
        return;
    }
    if (!m_pendingSeries.isEmpty()) {
        m_stage = Stage::GenItems;
        m_expandingSeries = true;
        if (!callBackend("load_items", { m_pendingSeries.takeFirst(), kEpisode, QString() }))
            fail(QStringLiteral("Server cannot list episodes"));
        return;
    }

    const bool named = !m_req.match.isEmpty() || !m_req.titles.isEmpty()
                       || !m_req.genres.isEmpty();
    const bool takesAll = m_req.collections.isEmpty()
                          && (!wantsFilms(m_req.wants) || m_req.anyFilm);
    const bool wantsLibraryItems = named || takesAll;

    while (!m_libs.isEmpty()) {
        const QString lib = m_libs.first();
        if (!m_req.collections.isEmpty() && !m_askedBoxsetsForLib) {
            m_askedBoxsetsForLib = true;
            m_stage = Stage::GenBoxsets;
            if (!callBackend("load_items", { lib, kBoxSet, QStringLiteral("SortName") }))
                fail(QStringLiteral("Server cannot list collections"));
            return;
        }
        m_libs.removeFirst();
        m_askedBoxsetsForLib = false;
        if (wantsLibraryItems) {
            m_stage = Stage::GenItems;
            m_expandingSeries = false;
            const QString wantType = m_req.wants == Request::Wants::Anything
                                     ? QString()
                                     : QString(wantsFilms(m_req.wants) ? kMovie : kEpisode);
            if (!callBackend("load_items", { lib, wantType, QStringLiteral("SortName") }))
                fail(QStringLiteral("Server cannot list items"));
            return;
        }
    }
    genFinish();
}

void MediaServerSource::genFinish() {
    QVector<MediaItem> items = m_collected;
    const bool truncated = m_truncated;
    const int  rejected  = m_rejected;
    const SlotSource src = m_req.src;
    reset();
    sortForPlayout(items);
    if (items.isEmpty() && rejected > 0)
        qWarning("[VirtualChannels] %s returned %d item(s), none of them schedulable — "
                 "items with no duration cannot be placed on a timeline",
                 qPrintable(providerName(src)), rejected);
    if (truncated)
        qWarning("[VirtualChannels] library listing capped at %d items — the channel uses the first %lld",
                 kMaxServerItems, static_cast<long long>(items.size()));
    emit enumerationFinished(items);
}

bool MediaServerSource::browse(SlotSource src, const QString &kind, const QString &parentKey) {
    if (busy()) return false;
    QObject *b = backend(src);
    if (!b) return false;

    reset();
    m_active      = b;
    m_req         = Request{};
    m_req.src     = src;
    m_browseKind  = kind;
    m_browseParent= parentKey;

    if (kind == QLatin1String("seasons")) {
        m_stage = Stage::BrowseSeasons;
        if (!callBackend("load_seasons", { parentKey })) { reset(); return false; }
        return true;
    }
    if (kind == QLatin1String("episodes")) {
        QString seriesId;
        if (!parseSeasonKey(parentKey, &seriesId, &m_browseSeason)) { reset(); return false; }
        m_browseSeries = seriesId;
        m_stage = Stage::BrowseEpisodes;
        if (!callBackend("load_items", { seriesId, kEpisode, QStringLiteral("SortName") })) {
            reset();
            return false;
        }
        return true;
    }
    if (kind != QLatin1String("shows") && kind != QLatin1String("collections")
        && kind != QLatin1String("movies") && kind != QLatin1String("moviegenres")
        && kind != QLatin1String("moviecollections")) {
        reset();
        return false;
    }
    m_req.wants = (kind == QLatin1String("movies")
                   || kind == QLatin1String("moviegenres")
                   || kind == QLatin1String("moviecollections"))
                  ? Request::Wants::Films : Request::Wants::Episodes;
    if (kind == QLatin1String("collections") || kind == QLatin1String("moviecollections"))
        m_req.collections << QStringLiteral("*");
    m_stage = Stage::BrowseLibraries;
    if (!callBackend("load_libraries")) { reset(); return false; }
    return true;
}

void MediaServerSource::finishBrowse() {
    const QString kind = m_browseKind;
    QVariantList items = m_browseItems;
    if (kind == QLatin1String("moviegenres")) {
        std::sort(items.begin(), items.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("label")).toString().compare(
                       b.toMap().value(QStringLiteral("label")).toString(),
                       Qt::CaseInsensitive) < 0;
        });
    }
    reset();
    emit browseReady(kind, items);
}

void MediaServerSource::browseNextLibrary() {
    if (m_libs.isEmpty()) { finishBrowse(); return; }
    const QString lib = m_libs.takeFirst();

    QLatin1String type = kBoxSet;
    if (m_browseKind == QLatin1String("shows")) {
        m_stage = Stage::BrowseSeries; type = kSeries;
    } else if (m_browseKind == QLatin1String("movies")
               || m_browseKind == QLatin1String("moviegenres")) {
        m_stage = Stage::BrowseMovies; type = kMovie;
    } else {
        m_stage = Stage::BrowseCollections;
    }

    if (!callBackend("load_items", { lib, type, QStringLiteral("SortName") }))
        fail(QStringLiteral("Server cannot list that library"));
}

void MediaServerSource::onLibrariesLoaded(const QVariant &libraries) {
    if (!fromActive()) return;

    if (m_stage == Stage::GenLibraries) {
        m_libs = eligibleLibraries(libraries, wantsFilms(m_req.wants), m_req.libraries);
        if (m_libs.isEmpty()) { fail(QStringLiteral("No matching libraries")); return; }
        genNext();
        return;
    }
    if (m_stage == Stage::BrowseLibraries) {
        m_libs = eligibleLibraries(libraries, wantsFilms(m_req.wants), QStringList());
        if (m_libs.isEmpty()) { fail(QStringLiteral("No matching libraries")); return; }
        browseNextLibrary();
        return;
    }
}

void MediaServerSource::onItemsLoaded(const QVariant &items) {
    if (!fromActive()) return;
    const QVariantList list = items.toList();

    switch (m_stage) {
    case Stage::GenBoxsets: {
        for (const QVariant &v : list) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("type")).toString() != QLatin1String("boxset")) continue;
            if (!listContainsCI(m_req.collections, m.value(QStringLiteral("title")).toString()))
                continue;
            const QString id = m.value(QStringLiteral("itemId")).toString();
            if (!id.isEmpty() && !m_pendingBoxsets.contains(id)) m_pendingBoxsets << id;
        }
        genNext();
        return;
    }
    case Stage::GenItems: {
        for (const QVariant &v : list) {
            if (m_collected.size() >= kMaxServerItems) { m_truncated = true; break; }
            const QVariantMap m = v.toMap();
            const QString type = m.value(QStringLiteral("type")).toString();

            MediaItem mi;
            if (!itemToMedia(m, m_req.src, &mi)) { ++m_rejected; continue; }

            if (m_req.wants == Request::Wants::Anything) {
                if (type != QLatin1String("movie") && type != QLatin1String("episode"))
                    continue;
            } else if (wantsFilms(m_req.wants)) {
                if (type != QLatin1String("movie")) continue;
                if (!m_req.titles.isEmpty() || !m_req.genres.isEmpty()
                    || !m_req.match.isEmpty()) {
                    bool wanted = listContainsCI(m_req.titles, mi.title);
                    if (!wanted) {
                        for (const QString &g : m.value(QStringLiteral("genres")).toStringList())
                            if (listContainsCI(m_req.genres, g)) { wanted = true; break; }
                    }
                    if (!wanted) {
                        for (const QString &needle : m_req.match)
                            if (!needle.isEmpty()
                                && mi.title.contains(needle, Qt::CaseInsensitive)) {
                                wanted = true;
                                break;
                            }
                    }
                    if (!wanted) continue;
                }
            } else {
                if (type != QLatin1String("episode")) continue;
                if (!m_expandingSeries && !m_req.match.isEmpty()
                    && !listContainsCI(m_req.match, mi.series)) continue;

                const QString seriesId = m.value(QStringLiteral("seriesId")).toString();
                const int season = m.value(QStringLiteral("parentIndex")).toInt();
                if (!seriesId.isEmpty()
                    && m_req.excludeSeasons.contains(seasonKey(seriesId, season))) continue;
                if (m_req.excludeEpisodes.contains(mi.ref)) continue;
            }
            if (m_seenIds.contains(mi.ref)) continue;
            m_seenIds.insert(mi.ref);
            m_collected.append(mi);
        }
        genNext();
        return;
    }
    case Stage::BrowseSeries: {
        for (const QVariant &v : list) {
            if (m_browseItems.size() >= kMaxServerItems) break;
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("type")).toString() != QLatin1String("series")) continue;
            const QString id    = m.value(QStringLiteral("itemId")).toString();
            const QString title = m.value(QStringLiteral("title")).toString();
            if (id.isEmpty() || title.isEmpty()) continue;
            if (m_seenIds.contains(id)) continue;
            m_seenIds.insert(id);
            m_browseItems.append(QVariantMap{{"id", id}, {"label", title}, {"sub", QString()}});
        }
        browseNextLibrary();
        return;
    }
    case Stage::BrowseMovies: {
        const bool wantGenres = (m_browseKind == QLatin1String("moviegenres"));
        for (const QVariant &v : list) {
            if (m_browseItems.size() >= kMaxServerItems) break;
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("type")).toString() != QLatin1String("movie")) continue;

            QStringList values;
            if (wantGenres) values = m.value(QStringLiteral("genres")).toStringList();
            else            values << m.value(QStringLiteral("title")).toString();

            for (const QString &value : std::as_const(values)) {
                if (value.isEmpty() || m_seenIds.contains(value)) continue;
                m_seenIds.insert(value);
                m_browseItems.append(QVariantMap{{"id", value}, {"label", value},
                                                 {"sub", QString()}});
            }
        }
        browseNextLibrary();
        return;
    }
    case Stage::BrowseCollections: {
        for (const QVariant &v : list) {
            if (m_browseItems.size() >= kMaxServerItems) break;
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("type")).toString() != QLatin1String("boxset")) continue;
            const QString title = m.value(QStringLiteral("title")).toString();
            if (title.isEmpty()) continue;
            if (m_seenIds.contains(title)) continue;
            m_seenIds.insert(title);
            m_browseItems.append(QVariantMap{{"id", title}, {"label", title}, {"sub", QString()}});
        }
        browseNextLibrary();
        return;
    }
    case Stage::BrowseEpisodes: {
        for (const QVariant &v : list) {
            if (m_browseItems.size() >= kMaxServerItems) break;
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("type")).toString() != QLatin1String("episode")) continue;
            if (m.value(QStringLiteral("parentIndex")).toInt() != m_browseSeason) continue;
            const QString owner = m.value(QStringLiteral("seriesId")).toString();
            if (!owner.isEmpty() && owner != m_browseSeries) continue;
            const QString id = m.value(QStringLiteral("itemId")).toString();
            if (id.isEmpty()) continue;
            const int ep = m.value(QStringLiteral("index")).toInt();
            m_browseItems.append(QVariantMap{
                {"id", id},
                {"label", m.value(QStringLiteral("title")).toString()},
                {"sub", ep > 0 ? QStringLiteral("E%1").arg(ep, 2, 10, QLatin1Char('0')) : QString()}});
        }
        finishBrowse();
        return;
    }
    default:
        return;
    }
}

void MediaServerSource::onSeasonsLoaded(const QVariant &seasons) {
    if (!fromActive() || m_stage != Stage::BrowseSeasons) return;
    for (const QVariant &v : seasons.toList()) {
        if (m_browseItems.size() >= kMaxServerItems) break;
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() != QLatin1String("season")) continue;
        const int number = m.value(QStringLiteral("index")).toInt();
        m_browseItems.append(QVariantMap{
            {"id", seasonKey(m_browseParent, number)},
            {"label", m.value(QStringLiteral("title")).toString()},
            {"sub", QString()}});
    }
    finishBrowse();
}

void MediaServerSource::onBoxsetChildrenLoaded(const QVariant &children) {
    if (!fromActive() || m_stage != Stage::GenBoxsetChildren) return;
    for (const QVariant &v : children.toList()) {
        const QVariantMap m = v.toMap();
        const QString type = m.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("series")) {
            if (wantsFilms(m_req.wants)) continue;
            const QString id = m.value(QStringLiteral("itemId")).toString();
            if (!id.isEmpty() && !m_expandedSeries.contains(id)) {
                m_expandedSeries.insert(id);
                m_pendingSeries << id;
            }
            continue;
        }
        if (m_req.wants == Request::Wants::Episodes) continue;
        if (type != QLatin1String("movie")) continue;
        if (m_collected.size() >= kMaxServerItems) { m_truncated = true; break; }
        MediaItem mi;
        if (!itemToMedia(m, m_req.src, &mi)) { ++m_rejected; continue; }
        if (m_req.excludeEpisodes.contains(mi.ref)) continue;
        if (m_seenIds.contains(mi.ref)) continue;
        m_seenIds.insert(mi.ref);
        m_collected.append(mi);
    }
    genNext();
}

void MediaServerSource::onBackendError(const QString &message) {
    if (!fromActive()) return;
    fail(message.isEmpty() ? QStringLiteral("Server error") : message);
}

void MediaServerSource::onTimeout() {
    if (m_stage == Stage::Idle) return;
    fail(QStringLiteral("%1 did not respond").arg(providerName(m_req.src)));
}
}
