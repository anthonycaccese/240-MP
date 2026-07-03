#include "YouTubeBackend.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>

static const char *kSubscriptionsFileName = "youtube_subscriptions.txt";

static QString watchUrlFor(const QString &videoId) {
    return QStringLiteral("https://www.youtube.com/watch?v=") + videoId;
}

YouTubeBackend::YouTubeBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot)
{
}

// ---------------------------------------------------------------------------
// Subscriptions file
// ---------------------------------------------------------------------------

QStringList YouTubeBackend::readSubscriptionIds(QString *error) const {
    const QString path = m_dataRoot + "/" + kSubscriptionsFileName;
    if (!QFile::exists(path)) {
        if (error)
            *error = QStringLiteral("NO SUBSCRIPTIONS FILE FOUND\n"
                                    "CREATE YOUTUBE_SUBSCRIPTIONS.TXT IN THE DATA DIRECTORY\n"
                                    "WITH ONE CHANNEL ID PER LINE");
        return {};
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("COULD NOT READ YOUTUBE_SUBSCRIPTIONS.TXT");
        return {};
    }
    QStringList ids;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        // Be lenient with pasted channel URLs: take the segment after "channel/".
        const int slash = line.indexOf(QLatin1String("channel/"));
        if (slash >= 0) {
            line = line.mid(slash + 8);
            const int end = line.indexOf(QRegularExpression(QStringLiteral("[/?#]")));
            if (end >= 0)
                line = line.left(end);
        }
        if (!line.isEmpty() && !ids.contains(line))
            ids << line;
    }
    if (ids.isEmpty() && error)
        *error = QStringLiteral("NO CHANNELS FOUND IN YOUTUBE_SUBSCRIPTIONS.TXT");
    return ids;
}

QVariantMap YouTubeBackend::check_subscriptions() {
    QString error;
    const QStringList ids = readSubscriptionIds(&error);
    QVariantMap result;
    result["ok"]           = error.isEmpty();
    result["error"]        = error;
    result["channelCount"] = ids.size();
    return result;
}

// ---------------------------------------------------------------------------
// Loaders — all route through one cache-fill path so a single in-flight
// refresh can serve every waiting view.
// ---------------------------------------------------------------------------

void YouTubeBackend::load_subscriptions_feed(bool forceRefresh) {
    m_emitFeedWhenDone = true;
    ensureFresh(forceRefresh);
}

void YouTubeBackend::load_channels(bool forceRefresh) {
    m_emitChannelsWhenDone = true;
    ensureFresh(forceRefresh);
}

void YouTubeBackend::load_channel_videos(const QString &channelId, bool forceRefresh) {
    m_emitChannelVideosWhenDone = channelId;
    ensureFresh(forceRefresh);
}

void YouTubeBackend::ensureFresh(bool forceRefresh) {
    if (m_pendingChannels > 0)
        return; // refresh already in flight — the emit flags queue on it

    QString error;
    const QStringList ids = readSubscriptionIds(&error);
    if (ids.isEmpty()) {
        m_emitFeedWhenDone     = false;
        m_emitChannelsWhenDone = false;
        m_emitChannelVideosWhenDone.clear();
        emit errorOccurred(error);
        return;
    }
    m_channelOrder = ids;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList stale;
    for (const QString &id : ids) {
        ChannelEntry &entry = m_channels[id];
        entry.channelId = id;
        if (forceRefresh || !entry.feedOk || now - entry.fetchedMs > kCacheTtlMs)
            stale << id;
    }

    if (stale.isEmpty()) {
        finishAggregate(); // everything fresh — serve from cache
        return;
    }
    m_pendingChannels = stale.size();
    for (const QString &id : stale)
        refreshChannel(id);
}

// ---------------------------------------------------------------------------
// Per-channel fetch: the official RSS feed
// ---------------------------------------------------------------------------

QNetworkRequest YouTubeBackend::makeRequest(const QUrl &url) const {
    QNetworkRequest req(url);
    req.setTransferTimeout(10000);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                                 "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    return req;
}

// Atom feed → channel name + video maps (newest first, as served).
// Partial parses are kept: only a parse error with zero entries counts as failure.
static bool parseRssFeed(const QByteArray &data, const QString &channelId,
                         QString *channelName, QVariantList *videos) {
    static const QLatin1String kAtomNs("http://www.w3.org/2005/Atom");
    QXmlStreamReader xml(data);
    bool inEntry = false;
    QString videoId, title, altLink;
    QDateTime published;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("entry")) {
                inEntry = true;
                videoId.clear();
                title.clear();
                altLink.clear();
                published = QDateTime();
            } else if (!inEntry && name == QLatin1String("title") && channelName->isEmpty()) {
                *channelName = xml.readElementText();
            } else if (inEntry && name == QLatin1String("videoId")) {
                videoId = xml.readElementText();
            } else if (inEntry && title.isEmpty() && name == QLatin1String("title")
                       && xml.namespaceUri() == kAtomNs) {
                // namespace check keeps <media:title> (inside media:group) out
                title = xml.readElementText();
            } else if (inEntry && name == QLatin1String("published")) {
                published = QDateTime::fromString(xml.readElementText(), Qt::ISODate);
            } else if (inEntry && name == QLatin1String("link")
                       && xml.attributes().value(QLatin1String("rel")) == QLatin1String("alternate")) {
                // Shorts expose a /shorts/<id> alternate href; normal uploads use /watch?v=<id>
                altLink = xml.attributes().value(QLatin1String("href")).toString();
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("entry")) {
            inEntry = false;
            if (videoId.isEmpty())
                continue;
            QVariantMap v;
            v["videoId"]     = videoId;
            v["title"]       = title;
            v["channelId"]   = channelId;
            v["channelName"] = QString(); // filled in once the feed title is known
            v["publishedAt"] = published.isValid() ? published.toUTC().toString(Qt::ISODate)
                                                   : QString();
            v["publishedMs"] = published.isValid() ? published.toMSecsSinceEpoch() : qint64(0);
            v["url"]         = watchUrlFor(videoId);
            v["isShort"]     = altLink.contains(QLatin1String("/shorts/"));
            videos->append(v);
        }
    }
    return !(xml.hasError() && videos->isEmpty());
}

void YouTubeBackend::refreshChannel(const QString &channelId) {
    QUrl rssUrl(QStringLiteral("https://www.youtube.com/feeds/videos.xml"));
    rssUrl.setQuery(QStringLiteral("channel_id=") + channelId);
    QNetworkReply *reply = m_nam.get(makeRequest(rssUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply, channelId]() {
        reply->deleteLater();
        ChannelEntry &e = m_channels[channelId];
        if (reply->error() == QNetworkReply::NoError) {
            QString name;
            QVariantList videos;
            if (parseRssFeed(reply->readAll(), channelId, &name, &videos)) {
                for (QVariant &v : videos) {
                    QVariantMap m = v.toMap();
                    m["channelName"] = name;
                    v = m;
                }
                e.channelName = name;
                e.videos      = videos;
                e.feedOk      = true;
                e.fetchedMs   = QDateTime::currentMSecsSinceEpoch();
            }
        }
        // On failure: keep any previously cached videos (stale beats empty);
        // fetchedMs stays old so the next load retries this channel.
        if (--m_pendingChannels <= 0) {
            m_pendingChannels = 0;
            finishAggregate();
        }
    });
}

void YouTubeBackend::finishAggregate() {
    const bool    feedWanted     = m_emitFeedWhenDone;
    const bool    channelsWanted = m_emitChannelsWhenDone;
    const QString videosWanted   = m_emitChannelVideosWhenDone;
    m_emitFeedWhenDone     = false;
    m_emitChannelsWhenDone = false;
    m_emitChannelVideosWhenDone.clear();

    bool anyOk = false;
    for (const QString &id : m_channelOrder)
        anyOk = anyOk || m_channels.value(id).feedOk;
    if (!anyOk) {
        emit errorOccurred(QStringLiteral("COULD NOT LOAD SUBSCRIPTIONS\n"
                                          "CHECK YOUR NETWORK CONNECTION"));
        return;
    }

    if (feedWanted)
        emit subscriptionsFeedLoaded(buildFeed());
    if (channelsWanted)
        emit channelsLoaded(buildChannelList());
    if (!videosWanted.isEmpty()) {
        const ChannelEntry entry = m_channels.value(videosWanted);
        if (entry.feedOk)
            emit channelVideosLoaded(videosWanted, entry.videos);
        else
            emit errorOccurred(QStringLiteral("COULD NOT LOAD CHANNEL FEED"));
    }
}

QVariantList YouTubeBackend::buildFeed() const {
    QVariantList all;
    for (const QString &id : m_channelOrder)
        all += m_channels.value(id).videos;
    std::sort(all.begin(), all.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("publishedMs").toLongLong()
             > b.toMap().value("publishedMs").toLongLong();
    });
    return all.mid(0, kMaxFeedItems);
}

QVariantList YouTubeBackend::buildChannelList() const {
    QVariantList channels;
    for (const QString &id : m_channelOrder) {
        const ChannelEntry entry = m_channels.value(id);
        QVariantMap c;
        // Fall back to the raw ID so a channel whose feed failed is still visible
        c["channelId"]  = id;
        c["title"]      = entry.channelName.isEmpty() ? id : entry.channelName;
        c["videoCount"] = entry.videos.size();
        channels << c;
    }
    std::sort(channels.begin(), channels.end(), [](const QVariant &a, const QVariant &b) {
        return QString::compare(a.toMap().value("title").toString(),
                                b.toMap().value("title").toString(),
                                Qt::CaseInsensitive) < 0;
    });
    return channels;
}

// ---------------------------------------------------------------------------
// Playback resolution → yt-dlp format
// ---------------------------------------------------------------------------

QString YouTubeBackend::ytdlFormatForResolution(const QString &resolution) const {
    int height = 480;
    if (resolution == QLatin1String("720p"))
        height = 720;
    else if (resolution == QLatin1String("1080p"))
        height = 1080;
    // H.264 first (RPi hardware decode), then any codec at the cap, then best
    return QStringLiteral("bestvideo[height<=?%1][vcodec^=avc1]+bestaudio/"
                          "bestvideo[height<=?%1]+bestaudio/"
                          "best[height<=?%1]/best")
        .arg(height);
}

// ---------------------------------------------------------------------------
// Watch history (youtube_history.json, keyed by videoId)
// Entry: { pos: <ms>, title, channelName, lastPlayed: <epoch ms> }
// Legacy pos-only entries are tolerated: they resume fine but are skipped by
// the History list (nothing to display) and pruned first (lastPlayed 0).
// ---------------------------------------------------------------------------

QString YouTubeBackend::historyFilePath() const {
    return m_dataRoot + "/youtube_history.json";
}

QVariantMap YouTubeBackend::loadHistory() const {
    QFile file(historyFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

void YouTubeBackend::saveHistory(const QVariantMap &history) {
    QFile file(historyFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(QJsonObject::fromVariantMap(history)).toJson(QJsonDocument::Compact));
}

QVariantMap YouTubeBackend::getSavedPosition(const QString &videoId) {
    const QVariant val = loadHistory().value(videoId);
    if (!val.isValid())
        return {};
    return val.toMap();
}

void YouTubeBackend::savePosition(const QString &videoId, int positionMs,
                                  const QString &title, const QString &channelName) {
    QVariantMap history = loadHistory();
    QVariantMap entry;
    entry["pos"]         = positionMs;
    entry["title"]       = title;
    entry["channelName"] = channelName;
    entry["lastPlayed"]  = QDateTime::currentMSecsSinceEpoch();
    history[videoId] = entry;

    if (history.size() > kMaxHistoryItems) {
        QStringList keys = history.keys();
        std::sort(keys.begin(), keys.end(), [&history](const QString &a, const QString &b) {
            return history.value(a).toMap().value("lastPlayed").toLongLong()
                 > history.value(b).toMap().value("lastPlayed").toLongLong();
        });
        for (int i = kMaxHistoryItems; i < keys.size(); ++i)
            history.remove(keys[i]);
    }
    saveHistory(history);
}

QVariantList YouTubeBackend::getHistory() const {
    const QVariantMap history = loadHistory();
    QVariantList items;
    for (auto it = history.begin(); it != history.end(); ++it) {
        const QVariantMap entry = it.value().toMap();
        const QString title = entry.value("title").toString();
        if (title.isEmpty())
            continue; // legacy resume-only entry — nothing to display
        QVariantMap v;
        v["videoId"]     = it.key();
        v["title"]       = title;
        v["channelName"] = entry.value("channelName").toString();
        v["lastPlayed"]  = entry.value("lastPlayed").toLongLong();
        v["url"]         = watchUrlFor(it.key());
        items << v;
    }
    std::sort(items.begin(), items.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("lastPlayed").toLongLong()
             > b.toMap().value("lastPlayed").toLongLong();
    });
    return items;
}

void YouTubeBackend::delete_history() {
    QFile::remove(historyFilePath());
}

// ---------------------------------------------------------------------------
// Watch later (youtube_watch_later.json — JSON array, newest-saved first)
// Entry: { videoId, title, channelName, addedMs }
// ---------------------------------------------------------------------------

QString YouTubeBackend::watchLaterFilePath() const {
    return m_dataRoot + "/youtube_watch_later.json";
}

QVariantList YouTubeBackend::loadWatchLater() const {
    QFile file(watchLaterFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).array().toVariantList();
}

void YouTubeBackend::saveWatchLater(const QVariantList &list) {
    QFile file(watchLaterFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact));
}

QVariantList YouTubeBackend::getWatchLater() const {
    QVariantList items = loadWatchLater();
    for (QVariant &v : items) {
        QVariantMap m = v.toMap();
        m["url"] = watchUrlFor(m.value("videoId").toString());
        v = m;
    }
    return items;
}

bool YouTubeBackend::isInWatchLater(const QString &videoId) const {
    const QVariantList list = loadWatchLater();
    for (const QVariant &v : list) {
        if (v.toMap().value("videoId").toString() == videoId)
            return true;
    }
    return false;
}

void YouTubeBackend::addToWatchLater(const QString &videoId, const QString &title,
                                     const QString &channelName) {
    if (videoId.isEmpty() || isInWatchLater(videoId))
        return;
    QVariantList list = loadWatchLater();
    QVariantMap entry;
    entry["videoId"]     = videoId;
    entry["title"]       = title;
    entry["channelName"] = channelName;
    entry["addedMs"]     = QDateTime::currentMSecsSinceEpoch();
    list.prepend(entry);
    saveWatchLater(list);
}

void YouTubeBackend::removeFromWatchLater(const QString &videoId) {
    QVariantList list = loadWatchLater();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list[i].toMap().value("videoId").toString() == videoId)
            list.removeAt(i);
    }
    if (list.isEmpty())
        QFile::remove(watchLaterFilePath());
    else
        saveWatchLater(list);
}

void YouTubeBackend::delete_watch_later() {
    QFile::remove(watchLaterFilePath());
}
