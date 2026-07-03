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
// Per-channel fetch: RSS (video list) + InnerTube (durations) in parallel
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
    QString videoId, title;
    QDateTime published;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("entry")) {
                inEntry = true;
                videoId.clear();
                title.clear();
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
            v["duration"]    = QString();
            v["publishedAt"] = published.isValid() ? published.toUTC().toString(Qt::ISODate)
                                                   : QString();
            v["publishedMs"] = published.isValid() ? published.toMSecsSinceEpoch() : qint64(0);
            v["url"]         = QStringLiteral("https://www.youtube.com/watch?v=") + videoId;
            videos->append(v);
        }
    }
    return !(xml.hasError() && videos->isEmpty());
}

// A duration badge is any thumbnailBadgeViewModel whose text looks like a
// timestamp ("35:53", "1:02:10") — the pattern check keeps "LIVE"/"SHORTS"
// style badges out.
static QString findDurationBadge(const QJsonValue &val) {
    static const QRegularExpression kTimePattern(QStringLiteral("^\\d+(:\\d{2})+$"));
    if (val.isObject()) {
        const QJsonObject obj = val.toObject();
        if (obj.contains(QLatin1String("thumbnailBadgeViewModel"))) {
            const QString t = obj.value(QLatin1String("thumbnailBadgeViewModel"))
                                  .toObject().value(QLatin1String("text")).toString();
            if (kTimePattern.match(t).hasMatch())
                return t;
        }
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString r = findDurationBadge(it.value());
            if (!r.isEmpty())
                return r;
        }
    } else if (val.isArray()) {
        const QJsonArray arr = val.toArray();
        for (const QJsonValue &v : arr) {
            const QString r = findDurationBadge(v);
            if (!r.isEmpty())
                return r;
        }
    }
    return {};
}

// Defensive InnerTube parse: rather than hard-coding the deep renderer path
// (which YouTube reshuffles), walk the whole response and collect durations
// from any recognized video object. Two shapes are handled:
//   - lockupViewModel (current WEB client): contentId + duration badge text
//   - videoRenderer (legacy): videoId + lengthText
static void collectDurations(const QJsonValue &val, QHash<QString, QString> *out) {
    if (val.isObject()) {
        const QJsonObject obj = val.toObject();
        if (obj.contains(QLatin1String("lockupViewModel"))) {
            const QJsonObject lv = obj.value(QLatin1String("lockupViewModel")).toObject();
            const QString cid = lv.value(QLatin1String("contentId")).toString();
            if (!cid.isEmpty() && !out->contains(cid)) {
                const QString d = findDurationBadge(lv.value(QLatin1String("contentImage")));
                if (!d.isEmpty())
                    out->insert(cid, d);
            }
        }
        const QString vid = obj.value(QLatin1String("videoId")).toString();
        if (!vid.isEmpty() && obj.contains(QLatin1String("lengthText"))) {
            const QJsonObject lt = obj.value(QLatin1String("lengthText")).toObject();
            QString d = lt.value(QLatin1String("simpleText")).toString();
            if (d.isEmpty()) {
                const QJsonArray runs = lt.value(QLatin1String("runs")).toArray();
                if (!runs.isEmpty())
                    d = runs.first().toObject().value(QLatin1String("text")).toString();
            }
            if (!d.isEmpty() && !out->contains(vid))
                out->insert(vid, d);
        }
        for (auto it = obj.begin(); it != obj.end(); ++it)
            collectDurations(it.value(), out);
    } else if (val.isArray()) {
        const QJsonArray arr = val.toArray();
        for (const QJsonValue &v : arr)
            collectDurations(v, out);
    }
}

void YouTubeBackend::refreshChannel(const QString &channelId) {
    ChannelEntry &entry = m_channels[channelId];
    entry.repliesPending = 2;
    entry.durationsById.clear();

    // RSS — the authoritative video list
    QUrl rssUrl(QStringLiteral("https://www.youtube.com/feeds/videos.xml"));
    rssUrl.setQuery(QStringLiteral("channel_id=") + channelId);
    QNetworkReply *rssReply = m_nam.get(makeRequest(rssUrl));
    connect(rssReply, &QNetworkReply::finished, this, [this, rssReply, channelId]() {
        rssReply->deleteLater();
        ChannelEntry &e = m_channels[channelId];
        if (rssReply->error() == QNetworkReply::NoError) {
            QString name;
            QVariantList videos;
            if (parseRssFeed(rssReply->readAll(), channelId, &name, &videos)) {
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
        onChannelReplyDone(channelId);
    });

    // InnerTube — durations only, best-effort
    QNetworkRequest itReq =
        makeRequest(QUrl(QStringLiteral("https://www.youtube.com/youtubei/v1/browse?prettyPrint=false")));
    itReq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject client{{"clientName", "WEB"}, {"clientVersion", "2.20240101.00.00"}};
    QJsonObject body{{"context", QJsonObject{{"client", client}}},
                     {"browseId", channelId},
                     {"params", "EgZ2aWRlb3MYAyAAMAE%3D"}}; // "Videos" tab
    QNetworkReply *itReply =
        m_nam.post(itReq, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(itReply, &QNetworkReply::finished, this, [this, itReply, channelId]() {
        itReply->deleteLater();
        ChannelEntry &e = m_channels[channelId];
        if (itReply->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(itReply->readAll());
            if (!doc.isNull())
                collectDurations(doc.object(), &e.durationsById);
        }
        onChannelReplyDone(channelId);
    });
}

void YouTubeBackend::onChannelReplyDone(const QString &channelId) {
    ChannelEntry &entry = m_channels[channelId];
    if (--entry.repliesPending > 0)
        return;

    // Both replies in — join durations onto the video list by videoId
    if (!entry.durationsById.isEmpty()) {
        for (QVariant &v : entry.videos) {
            QVariantMap m = v.toMap();
            const QString d = entry.durationsById.value(m.value("videoId").toString());
            if (!d.isEmpty()) {
                m["duration"] = d;
                v = m;
            }
        }
    }
    entry.durationsById.clear();

    if (--m_pendingChannels <= 0) {
        m_pendingChannels = 0;
        finishAggregate();
    }
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
// Resume history (youtube_history.json, keyed by videoId)
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

void YouTubeBackend::savePosition(const QString &videoId, int positionMs) {
    QVariantMap history = loadHistory();
    QVariantMap entry;
    entry["pos"] = positionMs;
    history[videoId] = entry;
    saveHistory(history);
}

void YouTubeBackend::clearPosition(const QString &videoId) {
    QVariantMap history = loadHistory();
    history.remove(videoId);
    saveHistory(history);
}
