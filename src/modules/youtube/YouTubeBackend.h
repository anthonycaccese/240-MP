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
// Video lists come from each channel's official RSS feed (titles, exact publish
// dates, channel name — ~15 newest videos), fetched unauthenticated. Results are
// cached in memory per channel for the session (kCacheTtlMs TTL), so the first
// entry into Subscriptions or Channels fills the cache for every other view.
//
// Playlists come from <dataRoot>/youtube_playlists.txt (one playlist URL or ID
// per line, optional "My Name | <url>" display-name prefix). RSS feeds for
// playlists stop at 15 entries, so playlist contents are fetched by spawning
// yt-dlp --flat-playlist instead (async QProcess, same session cache TTL).
//
// Two user files besides subscriptions/playlists:
//   youtube_history.json     — watch history + resume positions, keyed by videoId
//   youtube_watch_later.json — ordered saved-video list (newest first)
class YouTubeBackend : public QObject {
    Q_OBJECT
public:
    explicit YouTubeBackend(const QString &appRoot, const QString &dataRoot,
                            QObject *parent = nullptr);

    // Synchronous subscriptions-file check for the menu view:
    // { ok: bool, error: QString, fileExists: bool, channelCount: int }
    Q_INVOKABLE QVariantMap check_subscriptions();

    Q_INVOKABLE void load_subscriptions_feed(bool forceRefresh = false);
    Q_INVOKABLE void load_channels(bool forceRefresh = false);
    Q_INVOKABLE void load_channel_videos(const QString &channelId, bool forceRefresh = false);

    // Synchronous playlists-file check, mirroring check_subscriptions():
    // { ok: bool, error: QString, fileExists: bool, playlistCount: int }
    Q_INVOKABLE QVariantMap check_playlists();

    Q_INVOKABLE void load_playlists(bool forceRefresh = false);
    Q_INVOKABLE void load_playlist_videos(const QString &playlistId, bool forceRefresh = false);

    // Maps the playback_resolution setting ("480p"/"720p"/"1080p", unknown → 480p)
    // to a yt-dlp format string. H.264 is preferred first for RPi hardware decode.
    Q_INVOKABLE QString ytdlFormatForResolution(const QString &resolution) const;

    // Watch history (youtube_history.json). A finished video stays in history
    // with pos 0 (so it lists under History but never prompts to resume);
    // entries are pruned to the kMaxHistoryItems most recently played.
    Q_INVOKABLE QVariantMap  getSavedPosition(const QString &videoId);
    Q_INVOKABLE void         savePosition(const QString &videoId, int positionMs,
                                          const QString &title, const QString &channelName);
    Q_INVOKABLE QVariantList getHistory() const;   // displayable entries, newest first
    Q_INVOKABLE void         delete_history();     // settings action slot

    // Watch later (youtube_watch_later.json), newest-saved first, manual removal only
    Q_INVOKABLE QVariantList getWatchLater() const;
    Q_INVOKABLE bool         isInWatchLater(const QString &videoId) const;
    Q_INVOKABLE void         addToWatchLater(const QString &videoId, const QString &title,
                                             const QString &channelName);
    Q_INVOKABLE void         removeFromWatchLater(const QString &videoId);
    Q_INVOKABLE void         delete_watch_later(); // settings action slot

signals:
    void subscriptionsFeedLoaded(const QVariant &videos);
    void channelsLoaded(const QVariant &channels);
    void channelVideosLoaded(const QString &channelId, const QVariant &videos);
    void playlistsLoaded(const QVariant &playlists);
    void playlistVideosLoaded(const QString &playlistId, const QVariant &videos);
    void errorOccurred(const QString &message);

private:
    struct ChannelEntry {
        QString      channelId;
        QString      channelName;      // from the RSS feed <title>
        QVariantList videos;           // newest first
        qint64       fetchedMs = 0;    // 0 = never fetched successfully
        bool         feedOk    = false;
    };

    struct PlaylistEntry {
        QString      playlistId;
        QString      fileName;         // optional "Name |" override from the file
        QString      fetchedTitle;     // playlist_title reported by yt-dlp
        QVariantList videos;           // playlist order
        qint64       fetchedMs = 0;
        bool         fetchOk   = false;
    };

    struct PlaylistFileRef {
        QString id;
        QString name;                  // empty when the line had no "Name |" prefix
    };

    QString      historyFilePath() const;
    QVariantMap  loadHistory() const;
    void         saveHistory(const QVariantMap &history);
    QString      watchLaterFilePath() const;
    QVariantList loadWatchLater() const;
    void         saveWatchLater(const QVariantList &list);

    QStringList  readSubscriptionIds(QString *error = nullptr) const;
    void         ensureFresh(bool forceRefresh);
    void         refreshChannel(const QString &channelId);
    void         finishAggregate();
    QVariantList buildFeed() const;
    QVariantList buildChannelList() const;
    QNetworkRequest makeRequest(const QUrl &url) const;

    QList<PlaylistFileRef> readPlaylistEntries(QString *error = nullptr) const;
    void         ensurePlaylistsFresh(bool forceRefresh);
    void         spawnNextPlaylistFetch();
    void         finishPlaylistAggregate();
    QVariantList buildPlaylistList() const;

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

    // Playlist mirror of the channel cache/refresh state, fed by yt-dlp
    // subprocesses instead of RSS requests.
    QHash<QString, PlaylistEntry> m_playlists;
    QStringList m_playlistOrder;              // playlist IDs in file order (deduped)
    QStringList m_playlistFetchQueue;         // stale IDs waiting for a process slot
    int m_pendingPlaylists       = 0;
    int m_activePlaylistFetches  = 0;

    bool    m_emitPlaylistsWhenDone = false;
    QString m_emitPlaylistVideosWhenDone;     // playlistId, or empty

    static constexpr qint64 kCacheTtlMs      = 15 * 60 * 1000;
    static constexpr int    kMaxFeedItems    = 100;
    static constexpr int    kMaxHistoryItems = 100;
    static constexpr int    kMaxPlaylistItems = 500;             // caps infinite Mix/Radio lists
    static constexpr int    kMaxConcurrentPlaylistFetches = 2;   // yt-dlp is heavy on the Pi
    static constexpr int    kPlaylistFetchTimeoutMs = 60000;
};
