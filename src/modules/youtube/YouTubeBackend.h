#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QNetworkAccessManager>

// Backend for the YouTube module (V1 "feed" approach — no auth).
//
// The user lists channel IDs (one per line) in <dataRoot>/youtube_subscriptions.txt.
// Video lists come from two unauthenticated sources per channel, fetched in parallel:
//   - the official RSS feed (titles, exact publish dates, channel name — ~15 newest videos)
//   - an InnerTube browse request (duration strings, joined by videoId; best-effort only —
//     any InnerTube failure just leaves durations blank, it never fails a load)
// Results are cached in memory per channel for the session (kCacheTtlMs TTL), so the
// first entry into Subscriptions or Channels fills the cache for every other view.
class YouTubeBackend : public QObject {
    Q_OBJECT
public:
    explicit YouTubeBackend(const QString &appRoot, const QString &dataRoot,
                            QObject *parent = nullptr);

    // Synchronous subscriptions-file check for the menu view:
    // { ok: bool, error: QString, channelCount: int }
    Q_INVOKABLE QVariantMap check_subscriptions();

    Q_INVOKABLE void load_subscriptions_feed(bool forceRefresh = false);
    Q_INVOKABLE void load_channels(bool forceRefresh = false);
    Q_INVOKABLE void load_channel_videos(const QString &channelId, bool forceRefresh = false);

    // Maps the playback_resolution setting ("480p"/"720p"/"1080p", unknown → 480p)
    // to a yt-dlp format string. H.264 is preferred first for RPi hardware decode.
    Q_INVOKABLE QString ytdlFormatForResolution(const QString &resolution) const;

    // Resume history — youtube_history.json, keyed by videoId
    Q_INVOKABLE QVariantMap getSavedPosition(const QString &videoId);
    Q_INVOKABLE void savePosition(const QString &videoId, int positionMs);
    Q_INVOKABLE void clearPosition(const QString &videoId);

signals:
    void subscriptionsFeedLoaded(const QVariant &videos);
    void channelsLoaded(const QVariant &channels);
    void channelVideosLoaded(const QString &channelId, const QVariant &videos);
    void errorOccurred(const QString &message);

private:
    struct ChannelEntry {
        QString      channelId;
        QString      channelName;          // from the RSS feed <title>
        QVariantList videos;               // newest first, durations merged in
        qint64       fetchedMs = 0;        // 0 = never fetched successfully
        bool         feedOk    = false;
        // transient per-refresh state:
        int          repliesPending = 0;   // 2 = RSS + InnerTube in flight
        QHash<QString, QString> durationsById;
    };

    QString      historyFilePath() const;
    QVariantMap  loadHistory() const;
    void         saveHistory(const QVariantMap &history);

    QStringList  readSubscriptionIds(QString *error = nullptr) const;
    void         ensureFresh(bool forceRefresh);
    void         refreshChannel(const QString &channelId);
    void         onChannelReplyDone(const QString &channelId);
    void         finishAggregate();
    QVariantList buildFeed() const;
    QVariantList buildChannelList() const;
    QNetworkRequest makeRequest(const QUrl &url) const;

    QString m_appRoot;
    QString m_dataRoot;
    QNetworkAccessManager m_nam;

    QHash<QString, ChannelEntry> m_channels;  // in-memory session cache
    QStringList m_channelOrder;               // channel IDs in file order (deduped)
    int m_pendingChannels = 0;

    // Emit-when-done flags: while one refresh is in flight, additional load
    // calls just queue their result signal on it instead of re-requesting.
    bool    m_emitFeedWhenDone     = false;
    bool    m_emitChannelsWhenDone = false;
    QString m_emitChannelVideosWhenDone;      // channelId, or empty

    static constexpr qint64 kCacheTtlMs   = 15 * 60 * 1000;
    static constexpr int    kMaxFeedItems = 100;
};
