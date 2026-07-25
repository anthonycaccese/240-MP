#pragma once
#include <QObject>
#include <QVariant>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonObject>
#include <QUrl>
#include <functional>

class EmbyBackend : public QObject {
    Q_OBJECT
public:
    explicit EmbyBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);

    // Auth
    Q_INVOKABLE bool has_auth();
    Q_INVOKABLE QString get_server_name();
    Q_INVOKABLE QString get_user_name();
    Q_INVOKABLE void check_auth();
    Q_INVOKABLE void logout();

    // Username/password sign-in — direct to a single server.
    Q_INVOKABLE void authenticate(const QString &serverUrl, const QString &username, const QString &password);

    // Emby Connect — cloud account linking (Emby's equivalent of a "connect
    // once, pick your server" flow). Three steps: authenticate against
    // connect.emby.media, list the account's servers, then exchange the chosen
    // server's access key for a local server token.
    Q_INVOKABLE void connect_authenticate(const QString &usernameOrEmail, const QString &password);
    Q_INVOKABLE void connect_select_server(const QString &serverUrl, const QString &accessKey);

    // Browse
    Q_INVOKABLE void load_libraries();
    Q_INVOKABLE void load_items(const QString &parentId, const QString &includeTypes, const QString &sortBy);
    Q_INVOKABLE void load_item_detail(const QString &itemId);
    Q_INVOKABLE void load_children(const QString &itemId);
    Q_INVOKABLE void load_boxset_children(const QString &parentId);
    Q_INVOKABLE void load_folder_children(const QString &parentId);
    Q_INVOKABLE void load_seasons(const QString &seriesId);
    Q_INVOKABLE void load_episodes(const QString &seriesId, const QString &seasonId);
    Q_INVOKABLE void load_continue_watching();
    Q_INVOKABLE void load_up_next();
    Q_INVOKABLE void load_series_next_up(const QString &seriesId);

    // Playback
    Q_INVOKABLE void get_playback_url(const QString &itemId, const QString &mediaSourceId,
                                       int audioStreamIndex, int subtitleStreamIndex,
                                       bool forceTranscode = false);
    Q_INVOKABLE void load_next_episode(const QString &currentItemId);
    Q_INVOKABLE void update_playback_progress(const QString &itemId, const QString &mediaSourceId, qint64 positionTicks, bool isPaused);
    Q_INVOKABLE void report_playback_stopped(const QString &itemId, const QString &mediaSourceId, qint64 positionTicks, bool failed = false);
    Q_INVOKABLE void report_playback_start(const QString &itemId, const QString &mediaSourceId, const QString &audioStreamId, const QString &subtitleStreamId, qint64 startPositionTicks = 0);

    // Intro/credit skip — derived from Emby's chapter markers (IntroStart/
    // IntroEnd/CreditsStart). Emits Intro/Outro segments the Player consumes.
    Q_INVOKABLE void fetchSegments(const QString &itemId);

    // URL helpers for QML
    Q_INVOKABLE QString get_access_token() const { return m_accessToken; }
    Q_INVOKABLE QString get_server_url() const { return m_serverUrl; }

    // Settings
    Q_INVOKABLE QString get_auth_state();
    Q_INVOKABLE void getLibraries();
    Q_INVOKABLE void getVideoQualities();
    Q_INVOKABLE void get_resume_playback_options();
    Q_INVOKABLE void load_server_preferences();

    Q_INVOKABLE QString get_last_audio_lang() const;
    Q_INVOKABLE QString get_last_sub_lang() const;
    Q_INVOKABLE int get_last_audio_lang_idx() const;
    Q_INVOKABLE int get_last_sub_lang_idx() const;
    Q_INVOKABLE void set_last_track_langs(const QString &audioLang, const QString &subLang,
                                           int audioLangIdx = -1, int subLangIdx = -1);

signals:
    void authStateChanged();
    void librariesLoaded(const QVariant &libraries);
    void itemsLoaded(const QVariant &items);
    void itemLoaded(const QVariant &detail);
    void childrenLoaded(const QVariant &children);
    void boxsetChildrenLoaded(const QVariant &children);
    void folderChildrenLoaded(const QVariant &children);
    void seasonsLoaded(const QVariant &seasons);
    void episodesLoaded(const QVariant &episodes);
    void continueWatchingLoaded(const QVariant &items);
    void upNextLoaded(const QVariant &items);
    // Resume-or-next-unwatched episode for a series (empty map if none)
    void seriesNextUpReady(const QVariantMap &detail);
    void streamUrlReady(const QString &url);
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void segmentsReady(const QString &itemId, const QVariantList &segments);
    void errorOccurred(const QString &message);
    void logoutComplete();
    void authRevoked();

    // Emby Connect: emitted with the list of servers on the account when more
    // than one is available so the UI can present a picker. Each entry is a map
    // of {name, address, localAddress, remoteAddress, accessKey, systemId}.
    void connectServersReady(const QVariantList &servers);
    void connectFailed(const QString &message);

    // Emitted when server-side language preferences are loaded
    void serverLanguagePreferencesReady(const QString &audioLanguage, const QString &subtitleLanguage, const QString &subtitleMode);

    // Emitted with the next episode's full detail (empty map if none)
    void nextEpisodeReady(const QVariantMap &detail);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

private:
    QString m_appRoot;
    QString m_dataRoot;
    QNetworkAccessManager *m_nam;

    QString m_serverUrl;
    QString m_accessToken;
    QString m_userId;
    // Last auth state broadcast to QML, so notifyAuthState() can suppress
    // no-op signals that would re-navigate to the view already showing.
    QString m_lastAuthState;
    QString m_userName;
    QString m_serverName;
    QString m_connectUserId;      // Emby Connect account user id (cloud)
    QString m_connectAccessToken; // Emby Connect account token (cloud)
    QString m_currentPlaySessionId;
    QString m_currentPlayMethod; // "DirectPlay" or "Transcode" — for /Sessions reporting
    // One-shot: set when re-requesting a Dolby Vision source as an SDR transcode
    // so the profile caps it to 1080p/20Mbps (a full-4K/uncapped transcode is too
    // heavy to start promptly, and the display targets are SDR anyway). Consumed
    // and cleared by the next get_playback_url profile build.
    bool m_forceSdrTranscodeCap = false;
    QString m_deviceId;
    QString m_lastAudioLang;
    QString m_lastSubLang;
    int m_lastAudioLangIdx = -1;
    int m_lastSubLangIdx = -1;

    static QString normalizeServerUrl(const QString &url);

    QNetworkRequest embyRequest(const QUrl &url) const;
    QNetworkReply *embyGet(const QUrl &url);
    QNetworkReply *embyPost(const QUrl &url, const QByteArray &body);

    // Lightweight "does this list have any items?" probe (GET with limit=1).
    void probeHasItems(const QUrl &url, std::function<void(bool)> cb);

    // Emby Connect helpers (step 2 of the flow, and post-exchange persistence).
    void fetchConnectServers();
    void persistConnectAuthAndFinish();

    QVariantMap formatItem(const QJsonObject &item) const;

    void ignoreSslErrors(QNetworkReply *reply) const;

    void loadAuthState();
    void saveAuthState();
    void clearAuthState();
    void notifyAuthState();

    QJsonObject loadConfig() const;

    int videoQualityBitrate() const;
    int videoQualityMaxHeight() const;
    QJsonObject moduleConfig() const;
};
