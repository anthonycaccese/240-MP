#pragma once
#include <QObject>
#include <QVariant>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <openssl/evp.h>

class PlexBackend : public QObject {
    Q_OBJECT
public:
    explicit PlexBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);

    // Sync — no HTTP
    Q_INVOKABLE QString  get_auth_state();
    Q_INVOKABLE QString  get_active_user_name();
    Q_INVOKABLE QString  get_active_server_name();
    Q_INVOKABLE QVariantList get_switchable_servers();
    Q_INVOKABLE void     build_stream_url(const QString &ratingKey,
                                          const QString &partKey,
                                          const QString &sessionId);

    // Auth flow
    Q_INVOKABLE void start_pin_auth();
    Q_INVOKABLE void load_users_from_cache();
    Q_INVOKABLE void select_user(const QString &userId);
    Q_INVOKABLE void reauth_select_user(const QString &userId);
    Q_INVOKABLE void select_server(const QString &machineId);
    Q_INVOKABLE void logout();

    // Browse
    Q_INVOKABLE void load_libraries();
    Q_INVOKABLE void load_continue_watching();
    Q_INVOKABLE void load_section_hubs(const QString &sectionId);
    Q_INVOKABLE void load_items_for_hub(const QString &hubKey);
    Q_INVOKABLE void load_library_all(const QString &sectionId);
    Q_INVOKABLE void load_collections(const QString &sectionId);
    Q_INVOKABLE void load_collection_items(const QString &ratingKey);
    Q_INVOKABLE void load_playlists(const QString &sectionId);
    Q_INVOKABLE void load_playlist_items(const QString &ratingKey);
    Q_INVOKABLE void load_categories(const QString &sectionId);
    Q_INVOKABLE void load_category_items(const QString &sectionId, const QString &filterKey);
    Q_INVOKABLE void check_section_capabilities(const QString &sectionId);
    Q_INVOKABLE void load_children(const QString &ratingKey);
    // Loads the extras (trailers, deleted scenes, …) attached to an item and
    // emits extrasLoaded with a list of playable details (buildItemDetail shape
    // plus extraTypeLabel). Emits an empty list on failure — extras are an
    // enhancement, so errors must not disturb the detail views' main flows.
    Q_INVOKABLE void load_extras(const QString &ratingKey);
    Q_INVOKABLE void load_on_deck_for(const QString &ratingKey);
    // Resolves the next episode in the same season as currentRatingKey and emits
    // nextEpisodeReady with a full playable detail (same shape as load_item_detail),
    // or an empty map when there is no next episode / on any failure.
    Q_INVOKABLE void load_next_episode(const QString &currentRatingKey);

    // Playback
    Q_INVOKABLE void load_item_detail(const QString &ratingKey);
    Q_INVOKABLE void request_transcode(const QString &ratingKey, const QString &partKey,
                                       const QString &sessionId,
                                       const QString &audioId, const QString &subtitleId,
                                       int offsetMs);
    Q_INVOKABLE void update_timeline(const QString &ratingKey, const QString &partKey,
                                     const QString &state, int timeMs, int durationMs);
    Q_INVOKABLE void set_audio_stream(const QString &streamId, const QString &partId);
    Q_INVOKABLE void set_subtitle_stream(const QString &streamId, const QString &partId);

    // Live TV — minimal "watch live channels" support (no DVR/recording features).
    // load_live_channels lists the channel lineup of the first available DVR;
    // tune_channel allocates a tuner + HLS transcode session and emits streamUrlReady;
    // update_live_timeline keeps that tuner alive (state "playing") or releases it
    // (state "stopped"). The grabbed media key is remembered between calls.
    Q_INVOKABLE void load_live_channels();
    Q_INVOKABLE void tune_channel(const QString &channelId, const QString &sessionId);
    Q_INVOKABLE void update_live_timeline(const QString &state);
    // Stops the live transcode for sessionId and forgets the tuned key, so the
    // server can release the tuner. Called on exit and before a channel change.
    Q_INVOKABLE void stop_live_session(const QString &sessionId);

    // Settings dynamic options
    Q_INVOKABLE void getUsers();
    Q_INVOKABLE void getServers();
    Q_INVOKABLE void getLibraries();
    Q_INVOKABLE void getVideoQualities();
    Q_INVOKABLE void get_resume_playback_options();
    Q_INVOKABLE void applyCurrentUserSetting();
    Q_INVOKABLE void applyCurrentServerSetting();
    Q_INVOKABLE void reset_device_check();

signals:
    void pinReady(const QString &code, const QString &pinId);
    void authSuccess();
    void usersLoaded(const QVariant &users);
    void serversLoaded(const QVariant &servers);
    void logoutComplete();
    void authStateChanged();
    void authRevoked();

    void librariesLoaded(const QVariant &libraries);
    void continueWatchingLoaded(const QVariant &items);
    void hubsLoaded(const QVariant &hubs);
    void itemsLoaded(const QVariant &items);
    void collectionsLoaded(const QVariant &collections);
    void playlistsLoaded(const QVariant &playlists);
    void categoriesLoaded(const QVariant &categories);
    void capabilitiesLoaded(const QVariant &capabilities);

    void itemLoaded(const QVariant &detail);
    void streamUrlReady(const QString &url, const QString &plexToken);
    void childrenLoaded(const QVariant &items);
    void extrasLoaded(const QVariant &items);
    void inProgressEpisodeLoaded(const QVariant &item);
    void nextEpisodeReady(const QVariant &detail);

    void liveChannelsLoaded(const QVariant &channels);

    void dynamicOptionsReady(const QString &key, const QVariant &options);

    void errorOccurred(const QString &message);

private:
    // Auth file I/O
    QJsonObject loadAuth() const;
    void saveAuth(const QJsonObject &auth) const;

    // Config file helpers (shared with AppCore)
    QJsonObject loadConfig() const;
    void saveConfig(const QJsonObject &cfg) const;

    // Plex HTTP helpers
    QNetworkRequest plexRequest(const QUrl &url, const QString &token) const;
    QNetworkReply  *plexGet(const QUrl &url, const QString &token);
    QNetworkReply  *plexPost(const QUrl &url, const QString &token);
    QNetworkReply  *plexPut(const QUrl &url, const QString &token);
    QNetworkReply  *plexDelete(const QUrl &url, const QString &token);

    // Convenience SSL-ignore connect
    void ignoreSslErrors(QNetworkReply *reply);

    // Auth state accessors (read from in-memory / file)
    QString serverUrl() const;
    QString serverToken() const;
    QString accountToken() const;
    QString userToken() const;
    QString videoQuality() const;
    QString clientId() const;   // UUID, stored in plex_auth.json

    // Item formatting helpers
    QVariantMap formatItem(const QJsonObject &m) const;
    // Builds the full playable detail map (streams, selections, transcode flag)
    // from a single /library/metadata Metadata object. Shared by load_item_detail
    // and load_next_episode.
    QVariantMap buildItemDetail(const QJsonObject &meta) const;
    // Returns the server-side file path of a metadata object's first media part,
    // or an empty string when unavailable. Used to detect stacked multi-episode
    // files (several episode entries backed by one physical file).
    static QString mediaFilePath(const QJsonObject &meta);
    static QString msToDisplay(int ms);

    // Expands any season-type items in rawItems into their child episodes, then calls callback.
    // Order is preserved. callback is called synchronously if no seasons are present.
    void flattenSeasons(const QVariantList &rawItems, std::function<void(const QVariantList &)> callback);

    // Connection probing
    void probeConnections(const QJsonArray &connections,
                          std::function<void(QString)> callback);
    void probeNext(const QList<QString> &uris, int index,
                   std::function<void(QString)> callback);

    // Browse implementation (separated so startup check can wrap it)
    void load_libraries_impl();

    // User activation — single path for all three switch callers
    bool isAccountOwner(const QString &userId) const;
    void activateUser(const QString &userId,
                      std::function<void(const QVariantList &accessibleServers)> callback);

    // PIN auth
    void pollPinTick();
    void fetchUsersAndServers(const QString &token);

    // Logout helpers
    void deleteDeviceThenAuth(const QString &token, std::function<void()> finish);

    // JWT key management
    QByteArray  generateAndSaveKeyPair(const QString &keyId);  // returns JWK JSON bytes
    EVP_PKEY*   loadPrivateKey() const;                        // caller must EVP_PKEY_free

    // JWT building + signing
    QString     buildDeviceJwt(EVP_PKEY *key, const QString &keyId,
                                const QString &nonce = {}) const;
    static qint64  jwtExpClaim(const QString &jwt);
    static QString jwtUserIdClaim(const QString &jwt);         // parse user.id from JWT payload

    // Auth lifecycle
    void checkAndRefreshOnStartup(std::function<void()> callback);
    void migrateLegacyToken(std::function<void()> callback);
    void refreshJwt(std::function<void(bool ok)> callback);

    // 498 retry
    void handle498(std::function<void()> retryOp);

    // HTTP helper — POST with JSON body
    QNetworkReply* plexPostJson(const QUrl &url, const QString &token, const QByteArray &body);

    QString m_appRoot;
    QString m_dataRoot;
    QNetworkAccessManager *m_nam;
    QTimer *m_pollTimer;
    QString m_pendingPinId;
    QString m_clientId;          // cached after first load
    bool    m_refreshInFlight  = false;
    bool    m_deviceVerified   = false; // set after first successful plex.tv check per session

    // Live TV session state. m_liveDvrId is cached from the last load_live_channels.
    // The rest are set by tune_channel and drive the timeline keep-alive that stops
    // Plex from reaping the DVR grab (which would 404 the stream after a few
    // minutes): m_liveTimelineKey is the grabbed live-session key, m_liveRatingKey
    // and m_liveDurationMs identify the airing, m_liveSessionId ties the timeline to
    // the transcode session, and m_liveStartedMs gives the keep-alive an advancing
    // playback time.
    QString m_liveDvrId;
    QString m_liveTimelineKey;
    QString m_liveRatingKey;
    QString m_liveSessionId;
    int     m_liveDurationMs = 0;
    qint64  m_liveStartedMs  = 0;
};
