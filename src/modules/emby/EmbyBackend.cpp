#include "EmbyBackend.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>
#include <QCoreApplication>
#include <QUuid>
#include <QSslError>
#include <QSysInfo>
#include <QSet>
#include <QRegularExpression>

static const QString kModuleId = QStringLiteral("com.240mp.emby");

// Library CollectionTypes the module knows how to browse + play. Anything else
// (music, books, photos, mixed/empty, etc.) is hidden from both the browse list
// and the settings multiselect. Mirrors the Plex module's kSupportedLibraryTypes.
static const QSet<QString> kSupportedCollectionTypes = {
    QStringLiteral("movies"), QStringLiteral("tvshows"), QStringLiteral("homevideos"),
    QStringLiteral("boxsets")
};

static QString authHeaderValue(const QString &token, const QString &deviceId) {
    QString auth = QStringLiteral("MediaBrowser Client=\"240-MP\", Device=\"%1\", DeviceId=\"%2\", Version=\"%3\"")
                       .arg(QSysInfo::machineHostName(), deviceId, QCoreApplication::applicationVersion());
    if (!token.isEmpty())
        auth += QStringLiteral(", Token=\"%1\"").arg(token);
    return auth;
}

// X-Application header the Emby Connect cloud service (connect.emby.media)
// expects — "AppName/AppVersion".
static QString connectAppHeader() {
    return QStringLiteral("240-MP/%1").arg(QCoreApplication::applicationVersion());
}

static const QString kConnectBaseUrl = QStringLiteral("https://connect.emby.media/service");

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

EmbyBackend::EmbyBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_nam(new QNetworkAccessManager(this))
{
    loadAuthState();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    // Seed the change guard from the restored state so a token that is still
    // valid at startup is not reported as a transition (see notifyAuthState).
    m_lastAuthState = get_auth_state();
}

// authStateChanged drives navigation in Root.qml, so emitting it when nothing
// actually changed reloads the current view on top of itself. check_auth()
// re-validating a good token is the common case: Root has already shown
// Libraries by then, and an unconditional emit loaded it a second time. Only
// signal real transitions.
void EmbyBackend::notifyAuthState() {
    const QString state = get_auth_state();
    if (state == m_lastAuthState)
        return;
    m_lastAuthState = state;
    emit authStateChanged();
}

// ---------------------------------------------------------------------------
// Auth state persistence
// ---------------------------------------------------------------------------

QString EmbyBackend::normalizeServerUrl(const QString &url) {
    QString u = url.trimmed();
    while (u.endsWith('/'))
        u.chop(1);
    return u;
}

void EmbyBackend::loadAuthState() {
    QFile f(m_dataRoot + "/emby_auth.json");
    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    QJsonObject auth = doc.object();
    m_serverUrl  = normalizeServerUrl(auth["serverUrl"].toString());
    m_accessToken = auth["accessToken"].toString();
    m_userId     = auth["userId"].toString();
    m_userName   = auth["userName"].toString();
    m_serverName = auth["serverName"].toString();
    m_deviceId   = auth["deviceId"].toString();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
}

void EmbyBackend::saveAuthState() {
    QJsonObject auth;
    auth["serverUrl"]   = m_serverUrl;
    auth["accessToken"] = m_accessToken;
    auth["userId"]      = m_userId;
    auth["userName"]    = m_userName;
    auth["serverName"]  = m_serverName;
    auth["deviceId"]    = m_deviceId;

    QFile f(m_dataRoot + "/emby_auth.json");
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[EmbyBackend] Could not write emby_auth.json: %s", qPrintable(f.errorString()));
        return;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(auth).toJson(QJsonDocument::Indented));
    f.close();
}

void EmbyBackend::clearAuthState() {
    m_serverUrl.clear();
    m_accessToken.clear();
    m_userId.clear();
    m_userName.clear();
    m_serverName.clear();
    m_connectUserId.clear();
    m_connectAccessToken.clear();
    m_currentPlaySessionId.clear();
    // Sign out will wipe the auth file as the device is removed from access
    // on the server end as well. This will generate a fresh deviceId so any
    // in-session re-login / QuickConnect creates one that will be synced with the 
    // new device we give access to on the server and it will persist until either
    // the user signs out or manually de-auths the device from the server end.
    // saveAuthState() will recreate the file on the next successful login.
    m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QFile::remove(m_dataRoot + "/emby_auth.json");
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------

QJsonObject EmbyBackend::loadConfig() const {
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    return {};
}

QJsonObject EmbyBackend::moduleConfig() const {
    return loadConfig()["modules"].toObject()[kModuleId].toObject();
}

int EmbyBackend::videoQualityBitrate() const {
    QString quality = moduleConfig()["video_quality"].toString("auto");
    if (quality == QLatin1String("auto"))  return 0; // direct play — no cap
    if (quality == QLatin1String("1080p")) return 10000000;
    if (quality == QLatin1String("720p"))  return 6000000;
    if (quality == QLatin1String("576p"))  return 4500000;
    return 4000000; // 480p default
}

int EmbyBackend::videoQualityMaxHeight() const {
    QString quality = moduleConfig()["video_quality"].toString("auto");
    if (quality == QLatin1String("auto"))  return 0; // direct play — no cap
    if (quality == QLatin1String("1080p")) return 1080;
    if (quality == QLatin1String("720p"))  return 720;
    if (quality == QLatin1String("576p"))  return 576;
    return 480;
}

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

QNetworkRequest EmbyBackend::embyRequest(const QUrl &url) const {
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    // Emby's canonical auth header (Jellyfin renamed this to "Authorization").
    req.setRawHeader("X-Emby-Authorization", authHeaderValue(m_accessToken, m_deviceId).toLatin1());
    if (!m_accessToken.isEmpty())
        req.setRawHeader("X-Emby-Token", m_accessToken.toLatin1());
    return req;
}

QNetworkReply *EmbyBackend::embyGet(const QUrl &url) {
    auto *reply = m_nam->get(embyRequest(url));
    ignoreSslErrors(reply);
    return reply;
}

QNetworkReply *EmbyBackend::embyPost(const QUrl &url, const QByteArray &body) {
    QNetworkRequest req = embyRequest(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = m_nam->post(req, body);
    ignoreSslErrors(reply);
    return reply;
}

static QList<QSslError> filterExpectedSslErrors(const QList<QSslError> &errors) {
    static const QSet<QSslError::SslError> kExpected = {
        QSslError::SelfSignedCertificate,
        QSslError::HostNameMismatch,
        QSslError::UnableToGetLocalIssuerCertificate,
        QSslError::UnableToVerifyFirstCertificate,
    };
    QList<QSslError> allowed;
    for (const QSslError &e : errors) {
        if (kExpected.contains(e.error()))
            allowed.append(e);
    }
    return allowed;
}

void EmbyBackend::ignoreSslErrors(QNetworkReply *reply) const {
    // Snapshot the configured host now, while the request is issued — the async
    // sslErrors callback fires later, and logout() clears m_serverUrl the moment
    // it kicks off its device-deauth/logout requests, so reading it live would
    // fail the host match and refuse to relax self-signed LAN certs.
    const QString serverHost = QUrl(m_serverUrl).host();
    connect(reply, &QNetworkReply::sslErrors, reply, [reply, serverHost](const QList<QSslError> &errors) {
        // Only relax for the configured Emby server — typical of self-signed LAN certs
        if (reply->url().host() != serverHost)
            return;
        QList<QSslError> allowed = filterExpectedSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

bool EmbyBackend::has_auth() {
    return !m_accessToken.isEmpty() && !m_userId.isEmpty() && !m_serverUrl.isEmpty();
}

QString EmbyBackend::get_server_name() {
    return m_serverName;
}

QString EmbyBackend::get_user_name() {
    return m_userName;
}

QString EmbyBackend::get_auth_state() {
    return has_auth() ? QStringLiteral("authed") : QStringLiteral("none");
}

void EmbyBackend::check_auth() {
    if (!has_auth()) {
        notifyAuthState();
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId);
    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401) {
            qWarning("[EmbyBackend] Token rejected — signing out");
            clearAuthState();
            emit authRevoked();
            notifyAuthState();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("AUTH CHECK FAILED: " + reply->errorString());
            return;
        }
        notifyAuthState();
    });
}

void EmbyBackend::logout() {
    // Revoke the access token server-side so it can't be reused, and drop the
    // device registration. /Sessions/Logout alone leaves the device listed under
    // Dashboard > Devices (Jellyfin removes it), so signing out in 240-MP would
    // otherwise leave a stale entry behind on every sign-out — and since
    // clearAuthState() regenerates m_deviceId, the next sign-in registers a new
    // one rather than reusing it. DELETE /Devices takes either the reported
    // device id or Emby's internal numeric id; we only know the former.
    if (has_auth()) {
        // Build both requests up front, while the token is still valid.
        QUrl devUrl(m_serverUrl + "/Devices");
        { QUrlQuery dq; dq.addQueryItem("Id", m_deviceId); devUrl.setQuery(dq); }
        const QNetworkRequest deviceReq = embyRequest(devUrl);
        const QNetworkRequest logoutReq = embyRequest(QUrl(m_serverUrl + "/Sessions/Logout"));

        // Deauth the device first, then revoke the token — the other order would
        // 401 the delete.
        auto *devReply = m_nam->deleteResource(deviceReq);
        ignoreSslErrors(devReply);
        connect(devReply, &QNetworkReply::finished, this, [this, devReply, logoutReq]() {
            devReply->deleteLater();
            if (devReply->error() != QNetworkReply::NoError)
                qWarning("[Emby] device deauth failed: %s", qPrintable(devReply->errorString()));
            auto *reply = m_nam->post(logoutReq, QByteArray());
            ignoreSslErrors(reply);
            connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
        });
    }
    clearAuthState();
    emit logoutComplete();
    notifyAuthState();
}

// ---------------------------------------------------------------------------
// Username / password sign-in
//
// Emby has no Quick Connect API (that is Jellyfin-only), so the module signs in
// with the classic /Users/AuthenticateByName endpoint. The response carries the
// access token and the user record; the server name is fetched separately from
// /System/Info/Public for the header, mirroring the Plex/Jellyfin modules.
// ---------------------------------------------------------------------------

void EmbyBackend::authenticate(const QString &serverUrl, const QString &username, const QString &password) {
    QString normalized = normalizeServerUrl(serverUrl);
    if (normalized.isEmpty()) {
        emit errorOccurred("SERVER URL REQUIRED");
        return;
    }
    if (username.isEmpty()) {
        emit errorOccurred("USERNAME REQUIRED");
        return;
    }

    // Set the server URL up front so ignoreSslErrors() can match the host of the
    // in-flight reply against it (self-signed LAN certs).
    m_serverUrl = normalized;

    QUrl url(normalized + "/Users/AuthenticateByName");
    QJsonObject body;
    body["Username"] = username;
    body["Pw"]       = password;   // Emby's plaintext-over-TLS password field
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Emby-Authorization", authHeaderValue(QString(), m_deviceId).toLatin1());

    auto *reply = m_nam->post(req, payload);
    ignoreSslErrors(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray respBody = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            if (status == 401)
                emit errorOccurred("INCORRECT USERNAME OR PASSWORD");
            else if (status > 0)
                emit errorOccurred("SIGN IN FAILED (HTTP " + QString::number(status) + ")");
            else
                emit errorOccurred("CONNECTION FAILED: " + reply->errorString());
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(respBody).object();
        QString token = data["AccessToken"].toString();
        QJsonObject user = data["User"].toObject();

        if (token.isEmpty() || user["Id"].toString().isEmpty()) {
            emit errorOccurred("INVALID AUTH RESPONSE");
            return;
        }

        m_accessToken = token;
        m_userId      = user["Id"].toString();
        m_userName    = user["Name"].toString();
        m_serverName  = m_serverUrl; // temp: fetch real name below

        // Fetch actual server name for the header
        {
            QUrl infoUrl(m_serverUrl + "/System/Info/Public");
            auto *infoReply = embyGet(infoUrl);
            connect(infoReply, &QNetworkReply::finished, this, [this, infoReply]() {
                infoReply->deleteLater();
                if (infoReply->error() == QNetworkReply::NoError) {
                    QJsonObject infoData = QJsonDocument::fromJson(infoReply->readAll()).object();
                    QString name = infoData["ServerName"].toString();
                    if (!name.isEmpty()) {
                        m_serverName = name;
                        saveAuthState();
                    }
                }
            });
        }

        saveAuthState();

        // server_url is persisted to config.json by Root.qml (via appCore.save_setting)
        // once authStateChanged routes to Libraries — the single funnel for both the
        // direct and Emby Connect sign-in paths.
        notifyAuthState();
    });
}

// ---------------------------------------------------------------------------
// Emby Connect (cloud account linking)
//
// Step 1: POST connect.emby.media/service/user/authenticate {nameOrEmail, rawpw}
//         -> ConnectAccessToken + ConnectUserId
// Step 2: GET  connect.emby.media/service/servers?userId={ConnectUserId}
//         -> [{ Name, Url, LocalAddress, AccessKey, SystemId }]
// Step 3: GET  {serverUrl}/Connect/Exchange?format=json&ConnectUserId={id}
//         with X-MediaBrowser-Token: {AccessKey}
//         -> LocalUserId + AccessToken (the normal per-server credentials)
// ---------------------------------------------------------------------------

void EmbyBackend::connect_authenticate(const QString &usernameOrEmail, const QString &password) {
    if (usernameOrEmail.isEmpty()) {
        emit connectFailed("EMBY CONNECT EMAIL REQUIRED");
        return;
    }

    QUrl url(kConnectBaseUrl + "/user/authenticate");
    QJsonObject body;
    body["nameOrEmail"] = usernameOrEmail;
    body["rawpw"]       = password;
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Application", connectAppHeader().toLatin1());

    // connect.emby.media has a valid public certificate — do NOT relax SSL here
    // (ignoreSslErrors only relaxes for the configured local server host anyway).
    auto *reply = m_nam->post(req, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray respBody = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            if (status == 401)
                emit connectFailed("INCORRECT EMBY CONNECT EMAIL OR PASSWORD");
            else if (status > 0)
                emit connectFailed("EMBY CONNECT SIGN IN FAILED (HTTP " + QString::number(status) + ")");
            else
                emit connectFailed("CONNECTION FAILED: " + reply->errorString());
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(respBody).object();
        // The service returns AccessToken + User.Id; some responses spell these
        // ConnectAccessToken + ConnectUserId — accept either.
        QString token = data["AccessToken"].toString();
        if (token.isEmpty()) token = data["ConnectAccessToken"].toString();
        QString connectUserId = data["User"].toObject()["Id"].toString();
        if (connectUserId.isEmpty()) connectUserId = data["ConnectUserId"].toString();

        if (token.isEmpty() || connectUserId.isEmpty()) {
            emit connectFailed("INVALID EMBY CONNECT RESPONSE");
            return;
        }

        m_connectAccessToken = token;
        m_connectUserId      = connectUserId;
        fetchConnectServers();
    });
}

void EmbyBackend::fetchConnectServers() {
    QUrl url(kConnectBaseUrl + "/servers");
    { QUrlQuery q; q.addQueryItem("userId", m_connectUserId); url.setQuery(q); }

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Application", connectAppHeader().toLatin1());
    req.setRawHeader("X-Connect-UserToken", m_connectAccessToken.toLatin1());

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit connectFailed("FAILED TO LOAD SERVERS: " + reply->errorString());
            return;
        }

        QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
        QVariantList servers;
        for (const QJsonValue &v : arr) {
            QJsonObject s = v.toObject();
            const QString local  = normalizeServerUrl(s["LocalAddress"].toString());
            const QString remote = normalizeServerUrl(s["Url"].toString());
            // Prefer the LAN address (a CRT device usually shares the network with
            // its server); fall back to the remote/WAN address.
            const QString address = !local.isEmpty() ? local : remote;
            if (address.isEmpty())
                continue;
            servers.append(QVariantMap{
                {"name",          s["Name"].toString()},
                {"address",       address},
                {"localAddress",  local},
                {"remoteAddress", remote},
                {"accessKey",     s["AccessKey"].toString()},
                {"systemId",      s["SystemId"].toString()},
            });
        }

        if (servers.isEmpty()) {
            emit connectFailed("NO SERVERS LINKED TO THIS ACCOUNT");
            return;
        }
        if (servers.size() == 1) {
            // Only one server — skip the picker and exchange straight away.
            const QVariantMap only = servers.first().toMap();
            connect_select_server(only["address"].toString(), only["accessKey"].toString());
            return;
        }
        emit connectServersReady(servers);
    });
}

void EmbyBackend::connect_select_server(const QString &serverUrl, const QString &accessKey) {
    const QString normalized = normalizeServerUrl(serverUrl);
    if (normalized.isEmpty() || accessKey.isEmpty()) {
        emit connectFailed("INVALID SERVER SELECTION");
        return;
    }

    // Set the server URL up front so ignoreSslErrors() matches the exchange
    // reply's host (self-signed LAN servers).
    m_serverUrl = normalized;

    QUrl url(normalized + "/Connect/Exchange");
    { QUrlQuery q; q.addQueryItem("format", "json"); q.addQueryItem("ConnectUserId", m_connectUserId); url.setQuery(q); }

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Application", connectAppHeader().toLatin1());
    req.setRawHeader("X-MediaBrowser-Token", accessKey.toLatin1());
    req.setRawHeader("X-Emby-Token", accessKey.toLatin1());
    req.setRawHeader("X-Emby-Authorization", authHeaderValue(QString(), m_deviceId).toLatin1());

    auto *reply = m_nam->get(req);
    ignoreSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit connectFailed("SERVER EXCHANGE FAILED (HTTP " + QString::number(status) + ")");
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        const QString localUserId = data["LocalUserId"].toString();
        const QString token       = data["AccessToken"].toString();
        if (localUserId.isEmpty() || token.isEmpty()) {
            emit connectFailed("INVALID EXCHANGE RESPONSE");
            return;
        }

        m_userId      = localUserId;
        m_accessToken = token;
        m_serverName  = m_serverUrl; // real name fetched in persistConnectAuthAndFinish
        persistConnectAuthAndFinish();
    });
}

void EmbyBackend::persistConnectAuthAndFinish() {
    saveAuthState();

    // server_url is persisted to config.json by Root.qml (via appCore.save_setting)
    // on authStateChanged — same funnel as the direct sign-in path.

    // Fetch the display name of the local user (the exchange only returns an id).
    {
        QUrl userUrl(m_serverUrl + "/Users/" + m_userId);
        auto *userReply = embyGet(userUrl);
        connect(userReply, &QNetworkReply::finished, this, [this, userReply]() {
            userReply->deleteLater();
            if (userReply->error() == QNetworkReply::NoError) {
                QString name = QJsonDocument::fromJson(userReply->readAll()).object()["Name"].toString();
                if (!name.isEmpty()) {
                    m_userName = name;
                    saveAuthState();
                }
            }
        });
    }

    // Fetch the friendly server name for the header.
    {
        QUrl infoUrl(m_serverUrl + "/System/Info/Public");
        auto *infoReply = embyGet(infoUrl);
        connect(infoReply, &QNetworkReply::finished, this, [this, infoReply]() {
            infoReply->deleteLater();
            if (infoReply->error() == QNetworkReply::NoError) {
                QString name = QJsonDocument::fromJson(infoReply->readAll()).object()["ServerName"].toString();
                if (!name.isEmpty()) {
                    m_serverName = name;
                    saveAuthState();
                }
            }
        });
    }

    notifyAuthState();
}

// ---------------------------------------------------------------------------
// Item formatting
// ---------------------------------------------------------------------------

QVariantMap EmbyBackend::formatItem(const QJsonObject &item) const {
    QJsonObject userData = item["UserData"].toObject();
    QJsonObject imageTags = item["ImageTags"].toObject();
    QJsonArray mediaSources = item["MediaSources"].toArray();
    QJsonObject mediaSource = mediaSources.isEmpty() ? QJsonObject() : mediaSources[0].toObject();
    QJsonArray streams = mediaSource["MediaStreams"].toArray();

    QVariantList audioStreams;
    QVariantList subtitleStreams;
    for (const QJsonValue &v : streams) {
        QJsonObject s = v.toObject();
        QString type = s["Type"].toString();
        if (type == QLatin1String("Audio")) {
            QVariantMap as;
            as["id"]          = QString::number(s["Index"].toInt());
            as["language"]    = s["Language"].toString();
            as["codec"]       = s["Codec"].toString();
            as["channels"]    = s["ChannelLayout"].toString().isEmpty()
                                   ? s["Channels"].toVariant()
                                   : QVariant(s["ChannelLayout"].toString());
            as["selected"]    = s["IsDefault"].toBool();
            as["displayTitle"]= s["DisplayTitle"].toString();
            as["title"]       = s["Title"].toString();
            audioStreams.append(as);
        } else if (type == QLatin1String("Subtitle")) {
            const int idx     = s["Index"].toInt();
            const QString codec = s["Codec"].toString().toLower();
            const bool isText = s["IsTextSubtitleStream"].toBool();
            QVariantMap ss;
            ss["id"]          = QString::number(idx);
            ss["language"]    = s["Language"].toString();
            ss["codec"]       = codec;
            ss["selected"]    = s["IsDefault"].toBool();
            ss["forced"]      = s["IsForced"].toBool();
            ss["displayTitle"]= s["DisplayTitle"].toString();
            ss["title"]       = s["Title"].toString();
            // Image subs (PGS/VOBSUB) have no text sidecar — mpv renders them
            // from the embedded (direct-played) stream via --sid.
            ss["imageSubtitle"] = !isText;
            // Text subs are fetched as a sidecar file and handed to mpv as a
            // --sub-file, so direct play never has to transcode to show them.
            // (Mirrors PlexBackend's per-stream subUrl.)
            QString subUrl;
            if (isText) {
                const QString ext = (codec == "ass" || codec == "ssa") ? "ass"
                                  : (codec == "subrip" || codec == "srt") ? "srt"
                                  : "vtt";
                subUrl = m_serverUrl + "/Videos/" + item["Id"].toString() + "/"
                       + mediaSource["Id"].toString() + "/Subtitles/"
                       + QString::number(idx) + "/Stream." + ext;
                // mpv fetches sidecars itself and only carries the Jellyfin-style
                // Authorization header; Emby authenticates the sidecar via the
                // api_key query param, so embed the token in the URL.
                if (!m_accessToken.isEmpty())
                    subUrl += "?api_key=" + m_accessToken;
            }
            ss["subUrl"] = subUrl;
            subtitleStreams.append(ss);
        }
    }

    QVariantList genres;
    for (const QJsonValue &v : item["Genres"].toArray())
        genres.append(v.toVariant());

    QVariantMap map;
    map["itemId"]          = item["Id"].toString();
    map["seriesId"]        = item["SeriesId"].toString();
    map["title"]           = item["Name"].toString();
    // Server-side sort key (only present when the caller asked for the SortName
    // field). The letter-nav panel buckets on this so its letters agree with the
    // sortBy=SortName ordering the browse queries use.
    map["titleSort"]       = item["SortName"].toString().toUpper();
    map["type"]            = item["Type"].toString().toLower();
    map["overview"]        = item["Overview"].toString();
    QString releaseDate = item["ReleaseDate"].toString();
    QString premiereDate = item["PremiereDate"].toString();
    map["releaseDate"]    = releaseDate.isEmpty() ? premiereDate : releaseDate;
    map["year"]            = item["ProductionYear"].toVariant();
    map["genres"]          = genres;
    map["duration"]        = item["RunTimeTicks"].toDouble() / 10000.0;
    map["viewOffset"]      = userData["PlaybackPositionTicks"].toDouble() / 10000.0;
    map["played"]          = userData["Played"].toBool();
    map["isFolder"]        = item["IsFolder"].toBool();
    map["leafCount"]       = item["ChildCount"].toInt();
    map["index"]           = item["IndexNumber"].toInt();
    map["parentIndex"]     = item["ParentIndexNumber"].toInt();
    map["grandparentTitle"]= item["SeriesName"].toString().isEmpty()
                                ? item["Album"].toString()
                                : item["SeriesName"].toString();
    map["imageTag"]        = imageTags["Primary"].toString();
    map["mediaSourceId"]   = mediaSource["Id"].toString();
    map["audioStreams"]    = audioStreams;
    map["subtitleStreams"]= subtitleStreams;
    return map;
}


// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------

void EmbyBackend::load_libraries() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Views");
    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD LIBRARIES FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        // Honour the user's library filter (Settings → Libraries). Empty map ==
        // never configured, so show everything; otherwise hide explicitly-disabled ones.
        QJsonObject libEnabled = moduleConfig()["libraries"].toObject();
        QVariantList libraries;
        for (const QJsonValue &v : items) {
            QJsonObject item = v.toObject();
            if (!kSupportedCollectionTypes.contains(item["CollectionType"].toString()))
                continue;
            QString libId = item["Id"].toString();
            if (!libEnabled.isEmpty() && !libEnabled[libId].toBool(true))
                continue;
            libraries.append(QVariantMap{
                {"key",            libId},
                {"itemId",         libId},
                {"title",          item["Name"].toString().toUpper()},
                {"collectionType", item["CollectionType"].toString()},
            });
        }

        // Prepend the Continue Watching / Up Next shelves, but only when they
        // actually have content. Probe each (limit=1) before emitting the list.
        // mediaTypes is required: Emby's Resume returns an empty list (HTTP 200,
        // TotalRecordCount 0) without a media-type or item-type filter, however
        // many resume points exist. Jellyfin needs no such filter.
        QUrl resumeUrl(m_serverUrl + "/Users/" + m_userId + "/Items/Resume");
        { QUrlQuery rq; rq.addQueryItem("mediaTypes", "Video");
                        rq.addQueryItem("limit", "1"); resumeUrl.setQuery(rq); }
        QUrl nextUrl(m_serverUrl + "/Shows/NextUp");
        { QUrlQuery nq; nq.addQueryItem("userId", m_userId); nq.addQueryItem("limit", "1"); nextUrl.setQuery(nq); }

        probeHasItems(resumeUrl, [this, libraries, nextUrl](bool hasResume) {
            probeHasItems(nextUrl, [this, libraries, hasResume](bool hasUpNext) {
                QVariantList combined = libraries;
                if (hasUpNext)
                    combined.prepend(QVariantMap{{"key", "up_next"}, {"title", "NEXT UP"}});
                if (hasResume)
                    combined.prepend(QVariantMap{{"key", "continue_watching"}, {"title", "CONTINUE WATCHING"}});
                emit librariesLoaded(combined);
            });
        });
    });
}

void EmbyBackend::probeHasItems(const QUrl &url, std::function<void(bool)> cb) {
    const QString probePath = url.path();
    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [reply, cb, probePath]() {
        reply->deleteLater();
        bool has = false;
        if (reply->error() == QNetworkReply::NoError) {
            has = !QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray().isEmpty();
        } else {
            // A failed probe hides the shelf exactly like an empty one, so warn —
            // otherwise a broken request is indistinguishable from "nothing to
            // resume" and stays invisible (as the missing mediaTypes=Video did).
            qWarning("[Emby] shelf probe failed (%s): %s",
                     qPrintable(probePath), qPrintable(reply->errorString()));
        }
        cb(has);
    });
}

void EmbyBackend::load_items(const QString &parentId, const QString &includeTypes, const QString &sortBy) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", parentId);
    q.addQueryItem("recursive", "true");
    // Browse rows only need title/year/overview/played state. Skip the heavy
    // per-item MediaSources/MediaStreams here (now that the list is unbounded) —
    // Item.qml re-fetches full detail via load_item_detail when an item is opened.
    q.addQueryItem("fields", "Overview,Genres,UserData,SortName");
    if (!includeTypes.isEmpty())
        q.addQueryItem("includeItemTypes", includeTypes);
    if (!sortBy.isEmpty()) {
        q.addQueryItem("sortBy", sortBy);
        q.addQueryItem("sortOrder", "Ascending");
    }
    // No limit — return the full library so the list is complete A–Z (matches the
    // Plex module's unbounded /library/sections/{id}/all). Emby returns all
    // matching items when limit is omitted.
    url.setQuery(q);

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD ITEMS FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit itemsLoaded(result);
    });
}

void EmbyBackend::load_item_detail(const QString &itemId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/" + itemId);
    QUrlQuery q;
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD ITEM DETAIL FAILED: " + reply->errorString());
            return;
        }

        QJsonObject item = QJsonDocument::fromJson(reply->readAll()).object();
        emit itemLoaded(formatItem(item));
    });
}

void EmbyBackend::load_children(const QString &itemId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", itemId);
    q.addQueryItem("recursive", "false");
    q.addQueryItem("includeItemTypes", "Season,Episode");
    q.addQueryItem("limit", "500");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("sortBy", "SortName");
    q.addQueryItem("sortOrder", "Ascending");
    url.setQuery(q);

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD CHILDREN FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit childrenLoaded(result);
    });
}

void EmbyBackend::load_boxset_children(const QString &parentId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    // Direct members only (recursive=false) and no item-type filter — a box set
    // can hold movies, series, episodes, and nested box sets, and we want them
    // all. Used both for the library-level box-set list (parentId = library) and
    // for an individual box set's contents (parentId = box-set id). recursive=false
    // keeps nested box sets out of the parent listing so nesting only surfaces by
    // drilling into a box set.
    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", parentId);
    q.addQueryItem("recursive", "false");
    q.addQueryItem("fields", "Overview,Genres,UserData,ChildCount,ReleaseDate,PremiereDate");
    q.addQueryItem("sortBy", "SortName");
    q.addQueryItem("sortOrder", "Ascending");
    url.setQuery(q);

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD BOXSET FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit boxsetChildrenLoaded(result);
    });
}

void EmbyBackend::load_folder_children(const QString &parentId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    // homevideos browse: a homevideos library is a tree, so we list one level at
    // a time (recursive=false) with no item-type filter — a folder can hold both
    // sub-folders and videos. Same query shape as load_boxset_children; ChildCount
    // is requested so the QML can tell containers from leaves.
    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", parentId);
    q.addQueryItem("recursive", "false");
    q.addQueryItem("fields", "Overview,Genres,UserData,ChildCount,SortName");
    q.addQueryItem("sortBy", "SortName");
    q.addQueryItem("sortOrder", "Ascending");
    url.setQuery(q);

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD FOLDER FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items) {
            QVariantMap map = formatItem(v.toObject());
            // Keep navigable folders and playable videos; drop photos / photo
            // albums / audio so nothing un-playable can be selected.
            const QString t = map["type"].toString();
            if (map["isFolder"].toBool()) {
                if (t != "photoalbum") result.append(map);
            } else if (t == "video" || t == "movie" || t == "episode") {
                result.append(map);
            }
        }
        emit folderChildrenLoaded(result);
    });
}

void EmbyBackend::load_seasons(const QString &seriesId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Shows/" + seriesId + "/Seasons");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("fields", "Overview,MediaSources,MediaStreams,UserData");
    q.addQueryItem("enableUserData", "true");
    url.setQuery(q);
    // [dev] qDebug("[EmbyBackend] load_seasons series=%s", qPrintable(seriesId));

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD SEASONS FAILED: " + reply->errorString());
            return;
        }
        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        // [dev] qDebug("[EmbyBackend] load_seasons got %d seasons", items.size());
        emit seasonsLoaded(result);
    });
}

void EmbyBackend::load_episodes(const QString &seriesId, const QString &seasonId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Shows/" + seriesId + "/Episodes");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("seasonId", seasonId);
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("enableUserData", "true");
    q.addQueryItem("limit", "500");
    // No sortBy: Emby accepts the param but has no aired-order sort value, and
    // "AiredEpisodeOrder" (a Jellyfin sort name) makes it 500 with a
    // SQLiteException. Emby returns season episodes in index order natively.
    url.setQuery(q);
    // [dev] qDebug("[EmbyBackend] load_episodes series=%s season=%s", qPrintable(seriesId), qPrintable(seasonId));

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD EPISODES FAILED: " + reply->errorString());
            return;
        }
        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        // [dev] qDebug("[EmbyBackend] load_episodes got %d episodes", items.size());
        emit episodesLoaded(result);
    });
}

void EmbyBackend::load_continue_watching() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/Resume");
    QUrlQuery q;
    q.addQueryItem("mediaTypes", "Video");   // required on Emby — see load_libraries
    q.addQueryItem("limit", "20");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD CONTINUE WATCHING FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit continueWatchingLoaded(result);
    });
}

void EmbyBackend::load_up_next() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Shows/NextUp");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("limit", "20");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("enableUserData", "true");
    url.setQuery(q);

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD UP NEXT FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit upNextLoaded(result);
    });
}

void EmbyBackend::load_series_next_up(const QString &seriesId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    // Server computes the resume-or-next-unwatched episode for this series.
    QUrl url(m_serverUrl + "/Shows/NextUp");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("seriesId", seriesId);
    q.addQueryItem("limit", "1");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("enableUserData", "true");
    url.setQuery(q);

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD SERIES NEXT UP FAILED: " + reply->errorString());
            return;
        }
        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        // Empty when the series has never been started — caller falls back to
        // playing the first season's first episode.
        emit seriesNextUpReady(items.isEmpty() ? QVariantMap{}
                                               : formatItem(items[0].toObject()));
    });
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void EmbyBackend::get_playback_url(const QString &itemId, const QString &mediaSourceId,
                                       int audioStreamIndex, int subtitleStreamIndex,
                                       bool forceTranscode) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    // "auto" (Direct Play) lets the server serve the original file untouched
    // when the source is compatible; any other value forces a quality-capped
    // HLS transcode. forceTranscode overrides "auto" for a fallback retry after
    // a direct-play failure (see Player.qml onPlaybackEnded).
    const bool directPlay = !forceTranscode
                          && (moduleConfig()["video_quality"].toString("auto")
                              == QLatin1String("auto"));
    int maxBitrate = videoQualityBitrate();
    int maxHeight  = videoQualityMaxHeight();
    // Dolby Vision → SDR re-request: cap an otherwise-uncapped ("auto") transcode
    // to 1080p/20Mbps so the server can start it promptly and keep up in real time.
    if (m_forceSdrTranscodeCap) {
        if (maxBitrate <= 0) maxBitrate = 20000000;
        if (maxHeight  <= 0) maxHeight  = 1080;
        m_forceSdrTranscodeCap = false;
    }

    QUrl url(m_serverUrl + "/Items/" + itemId + "/PlaybackInfo");
    QJsonObject body;
    body["UserId"]                 = m_userId;
    body["MediaSourceId"]          = mediaSourceId;
    if (audioStreamIndex >= 0)
        body["AudioStreamIndex"]   = audioStreamIndex;
    // For direct play the device profile already advertises Embed for every
    // subtitle format (including PGS/VOBSUB), so the server knows we handle
    // them client-side and won't force a transcode.  Pass the user's actual
    // selection (or omit if off) so the static stream includes all tracks.
    // Hardcoding -1 here caused newer *Jellyfin* servers to strip sub tracks
    // from the stream, breaking both --sid for image subs and sidecar URLs.
    // Carried over from the Jellyfin module; not verified against Emby.
    if (subtitleStreamIndex >= 0)
        body["SubtitleStreamIndex"] = subtitleStreamIndex;
    if (maxBitrate > 0) body["MaxStreamingBitrate"] = maxBitrate;
    if (maxHeight  > 0) body["MaxHeight"]           = maxHeight;
    body["EnableDirectPlay"]       = directPlay;
    body["EnableDirectStream"]     = directPlay;

    // Device profile — advertises the HLS transcode target, plus (for direct
    // play) the broad set of containers/codecs mpv can play natively.
    QJsonObject profile;
    QJsonArray transcodingProfiles;
    QJsonObject tp;
    tp["Container"]  = QStringLiteral("ts");
    tp["Type"]       = QStringLiteral("Video");
    tp["VideoCodec"]  = QStringLiteral("h264");
    tp["AudioCodec"]  = QStringLiteral("aac,mp3");
    tp["Protocol"]    = QStringLiteral("hls");
    transcodingProfiles.append(tp);
    profile["TranscodingProfiles"] = transcodingProfiles;
    QJsonArray subtitleProfiles;
    if (directPlay) {
        // Direct play serves the original file whole; the video stream is never
        // touched. How the *subtitle* reaches mpv depends on its kind, and the
        // Method we advertise per format has to match — otherwise Emby, unlike
        // Jellyfin, refuses direct play the moment a subtitle is selected and
        // falls back to a full transcode (SubtitleMethod=Encode / burn-in).
        //
        //   Text subs (subrip/ass/vtt/…): the client fetches them as a sidecar
        //   file (see subUrl in extract_item_detail) and hands them to mpv as
        //   --sub-file. That is External delivery, so advertise Method=External.
        //   Advertising only Embed (as the Jellyfin port did) is why Emby
        //   transcoded on every subtitle-on: it couldn't embed the selected sub
        //   into a *static* stream and had no permitted alternative, so it burnt
        //   it in. Jellyfin defaults external text subs to External regardless;
        //   Emby honours only what the profile declares.
        //
        //   Image subs (PGS/VOBSUB): no text sidecar exists, so mpv selects them
        //   from the embedded stream via --sid. That is Embed delivery.
        auto addSub = [&](const char *fmt, const char *method) {
            QJsonObject s;
            s["Format"] = QString::fromLatin1(fmt);
            s["Method"] = QString::fromLatin1(method);
            subtitleProfiles.append(s);
        };
        addSub("subrip",   "External");
        addSub("srt",      "External");
        addSub("ass",      "External");
        addSub("ssa",      "External");
        addSub("vtt",      "External");
        addSub("webvtt",   "External");
        addSub("mov_text", "External");
        addSub("pgssub",   "Embed");
        addSub("dvbsub",   "Embed");
        addSub("dvdsub",   "Embed");
    } else {
        // Transcode: text subs are delivered as a sidecar, exactly as they are
        // for direct play — only image subs are burnt in.
        //
        // Burning text subs in (Method=Encode) is what the Plex module does and
        // what this module used to do, but it makes the server re-encode the
        // video with the subtitle painted on, and Emby 500s on that for every
        // subrip tested here, SDR sources included. A burnt-in sub therefore
        // meant no playback at all on the transcode path.
        //
        // External is safe because it is NOT the soft HLS subtitle rendition
        // that mpv renders unreliably after a seek: the sidecar is a separate
        // /Videos/.../Subtitles/{i}/Stream.srt fetch, independent of the HLS
        // video, and Player.qml hands the selected one to mpv as --sub-file on
        // the transcode path too (see doStartPlayback). Verified
        // against Emby 4.9.5.0: with Method=External the returned
        // TranscodingUrl carries no SubtitleMethod and no SubtitleStreamIndex,
        // so the video transcodes untouched.
        //
        // Image subs (PGS/VOBSUB) have no text sidecar and cannot ride along in
        // an HLS rendition, so burn-in stays their only option.
        auto addSub = [&](const char *fmt, const char *method) {
            QJsonObject s;
            s["Format"] = QString::fromLatin1(fmt);
            s["Method"] = QString::fromLatin1(method);
            subtitleProfiles.append(s);
        };
        addSub("subrip",   "External");
        addSub("srt",      "External");
        addSub("ass",      "External");
        addSub("ssa",      "External");
        addSub("vtt",      "External");
        addSub("webvtt",   "External");
        addSub("mov_text", "External");
        addSub("pgssub",   "Encode");
        addSub("dvbsub",   "Encode");
        addSub("dvdsub",   "Encode");
    }
    profile["SubtitleProfiles"] = subtitleProfiles;

    // Emby applies a default streaming bitrate cap when the profile doesn't
    // declare one, and silently refuses direct play for any source above it —
    // SupportsDirectPlay=0 / SupportsDirectStream=0 with no TranscodeReasons.
    // Anything much past a couple of Mbps is denied, which is most real media.
    // Advertise an effectively unlimited cap for direct play; the quality tiers
    // keep their own cap so the server still sizes the transcode correctly.
    profile["MaxStreamingBitrate"] = maxBitrate > 0 ? maxBitrate : 2000000000;

    QJsonArray directPlayProfiles;
    if (directPlay) {
        // mpv plays virtually anything, so advertise a match-all profile —
        // omitted Container/VideoCodec/AudioCodec fields match every value in
        // Emby's profile matcher too (verified against Emby 4.9.5.0: mkv/mp4/avi
        // with h264/hevc/vc1/mpeg2 all direct-play through this wildcard).
        // A codec whitelist here silently forced
        // transcodes on exact-name misses (pcm_s16le vs pcm, webvtt vs vtt, …).
        // If mpv truly can't play a file, the transcode retry in Player.qml
        // onPlaybackEnded is the safety net. An empty array (transcode mode)
        // tells the server nothing can be direct-played.
        QJsonObject dp;
        dp["Type"] = QStringLiteral("Video");
        directPlayProfiles.append(dp);
    }
    profile["DirectPlayProfiles"] = directPlayProfiles;
    body["DeviceProfile"] = profile;

    auto *reply = embyPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    // [dev] qDebug("[EmbyBackend] PlaybackInfo POST %s audio=%d sub=%d bitrate=%d",
    // [dev]        qPrintable(itemId), audioStreamIndex, subtitleStreamIndex, videoQualityBitrate());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId, mediaSourceId, audioStreamIndex, subtitleStreamIndex, directPlay]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("PLAYBACK INFO FAILED: " + reply->errorString());
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonArray sources = data["MediaSources"].toArray();
        if (sources.isEmpty()) {
            emit errorOccurred("NO PLAYABLE SOURCE");
            return;
        }

        QJsonObject source = sources[0].toObject();

        // Dolby Vision won't render. mpv on our targets (macOS videotoolbox, Pi)
        // can't play the DV enhancement layer — it decodes the file but bails at
        // video output ("Dolby Vision enhancement-layer playback is not supported"),
        // so a DV title direct-plays for a moment then exits. Force an SDR H.264
        // transcode instead: the server tone-maps DV→SDR (what Plex/Jellyfin do),
        // and a CRT is SDR anyway. Done client-side off the response's reported
        // range because a DeviceProfile CodecProfile rejecting VideoRangeType=DOVI
        // was verified to be ignored by Emby 4.9.5.0 (the wildcard DirectPlay
        // profile still won). forceTranscode=true on the re-request means directPlay
        // is false there, so this never recurses.
        if (directPlay) {
            QString videoRange;
            for (const QJsonValue &sv : source["MediaStreams"].toArray()) {
                const QJsonObject st = sv.toObject();
                if (st["Type"].toString() == QLatin1String("Video")) {
                    videoRange = st["VideoRange"].toString();
                    if (videoRange.isEmpty())
                        videoRange = st["VideoRangeType"].toString();
                    break;
                }
            }
            if (videoRange.contains(QLatin1String("dolby"), Qt::CaseInsensitive)
                || videoRange.compare(QLatin1String("DOVI"), Qt::CaseInsensitive) == 0) {
                qInfo("[Emby] Dolby Vision source (range=%s) — forcing SDR transcode; mpv can't render the DV enhancement layer",
                      qPrintable(videoRange));
                // Keep the selected subtitle if it can survive the transcode.
                // This used to drop every subtitle, because the transcode path
                // burnt the selected sub in (SubtitleMethod=Encode) and this
                // server 500s on burn-in, so a burned sub meant no playback at
                // all. Text subs are now delivered as a sidecar (Method=External
                // in the transcode profile) and never touch the video, so there
                // is nothing left to work around for them.
                //
                // Image subs (PGS/VOBSUB) have no sidecar and are still burnt in,
                // which is exactly the case that takes the whole transcode down,
                // so those are still dropped rather than losing playback.
                int keptSubtitle = subtitleStreamIndex;
                if (keptSubtitle >= 0) {
                    bool isTextSub = false;
                    for (const QJsonValue &sv : source["MediaStreams"].toArray()) {
                        const QJsonObject st = sv.toObject();
                        if (st["Type"].toString() == QLatin1String("Subtitle")
                            && st["Index"].toInt() == keptSubtitle) {
                            isTextSub = st["IsTextSubtitleStream"].toBool();
                            break;
                        }
                    }
                    if (!isTextSub) {
                        qInfo("[Emby] DV transcode: dropping image subtitle %d (burn-in would fail)",
                              keptSubtitle);
                        keptSubtitle = -1;
                    }
                }
                m_forceSdrTranscodeCap = true;
                get_playback_url(itemId, mediaSourceId, audioStreamIndex,
                                 keptSubtitle, /*forceTranscode=*/true);
                return;
            }
        }

        // Direct play when requested AND the server confirms the source is
        // compatible. Serves the original file via /Videos/{id}/stream?static=true;
        // mpv handles embedded audio/subtitle tracks (Player.qml direct-play branch).
        // PlaySessionId from the PlaybackInfo response is reused for the stream
        // URL and every /Sessions report so the server correlates the dashboard
        // session with this stream — and tears the transcode down on Stopped.
        const QString playSessionId = data["PlaySessionId"].toString();

        if (directPlay && (source["SupportsDirectPlay"].toBool()
                           || source["SupportsDirectStream"].toBool())) {
            const QString srcId = source["Id"].toString(mediaSourceId);
            QString directUrl = m_serverUrl + "/Videos/" + itemId + "/stream"
                              + "?static=true"
                              + "&mediaSourceId=" + srcId;
            if (!playSessionId.isEmpty())
                directUrl += "&PlaySessionId=" + playSessionId;
            // Emby authenticates the media request via api_key; mpv only carries
            // the Jellyfin-style Authorization header, so embed the token here.
            if (!m_accessToken.isEmpty())
                directUrl += "&api_key=" + m_accessToken;
            m_currentPlaySessionId = playSessionId;
            m_currentPlayMethod    = QStringLiteral("DirectPlay");
            report_playback_start(itemId, mediaSourceId,
                                  audioStreamIndex >= 0 ? QString::number(audioStreamIndex) : QString(),
                                  subtitleStreamIndex >= 0 ? QString::number(subtitleStreamIndex) : QString());
            emit streamUrlReady(directUrl);
            return;
        }

        // Transcode path — used for the bitrate tiers, and as a graceful
        // fallback when the server reports the source can't be direct-played.
        if (directPlay) {
            // TranscodeReasons is an array of strings on older servers, a
            // comma-joined flags string on 10.9+.
            const QJsonValue tr = source["TranscodeReasons"];
            QString reasons = tr.toString();
            if (tr.isArray()) {
                QStringList list;
                for (const QJsonValue &r : tr.toArray())
                    list << r.toString();
                reasons = list.join(", ");
            }
            qWarning("[Emby] direct play denied (SupportsDirectPlay=%d SupportsDirectStream=%d): %s",
                     source["SupportsDirectPlay"].toBool(),
                     source["SupportsDirectStream"].toBool(),
                     reasons.isEmpty() ? "no TranscodeReasons given" : qPrintable(reasons));
        }
        QString transcodeUrl = source["TranscodingUrl"].toString();
        if (transcodeUrl.isEmpty()) {
            emit errorOccurred("NO TRANSCODE URL");
            return;
        }
        m_currentPlaySessionId = playSessionId;
        m_currentPlayMethod    = QStringLiteral("Transcode");

        // Build the full URL. Unlike the Jellyfin module (which strips api_key and
        // relies on mpv's Authorization header), Emby authenticates the HLS request
        // via api_key, so keep the server-supplied token and add one if it's absent.
        // When the user selected OFF, still strip any SubtitleStreamIndex and
        // SubtitleMethod the server may have added from default metadata.
        QUrl parsedUrl(m_serverUrl + transcodeUrl);
        {
            QUrlQuery q(parsedUrl);
            bool hasApiKey = false;
            const auto items = q.queryItems();
            for (const auto &kv : items) {
                if (kv.first.compare(QLatin1String("api_key"), Qt::CaseInsensitive) == 0 ||
                    kv.first.compare(QLatin1String("apikey"),  Qt::CaseInsensitive) == 0)
                    hasApiKey = true;
            }
            if (!hasApiKey && !m_accessToken.isEmpty())
                q.addQueryItem("api_key", m_accessToken);
            if (subtitleStreamIndex < 0) {
                q.removeAllQueryItems("SubtitleStreamIndex");
                q.removeAllQueryItems("SubtitleMethod");
            }
            parsedUrl.setQuery(q);
        }
        QString fullUrl = parsedUrl.toString();

        // Pin the URL's PlaySessionId to the one we report with (they should
        // already match; this guarantees it even if the server differs).
        if (!playSessionId.isEmpty())
            fullUrl.replace(QRegularExpression("PlaySessionId=[^&]+"),
                            "PlaySessionId=" + playSessionId);

        // Enforce max height from quality setting — the server's TranscodingUrl may
        // include a VideoBitrate cap (from our PlaybackInfo POST) but omit MaxHeight,
        // resulting in a full-resolution transcode. Inject it here so 480p/720p etc.
        // actually constrain the output resolution.
        {
            const int maxHeight = videoQualityMaxHeight();
            if (maxHeight > 0) {
                QRegularExpression heightRe("(MaxHeight|Height)=[^&]+",
                                            QRegularExpression::CaseInsensitiveOption);
                const QString replacement = "MaxHeight=" + QString::number(maxHeight);
                if (fullUrl.contains(heightRe))
                    fullUrl.replace(heightRe, replacement);
                else
                    fullUrl += "&" + replacement;
            }
        }

        // [dev] qDebug("[EmbyBackend] PlaybackInfo URL ready audio=%d sub=%d psId=%s",
        // [dev]        audioStreamIndex, subtitleStreamIndex, qPrintable(playSessionId.left(8)));
        report_playback_start(itemId, mediaSourceId,
                              audioStreamIndex >= 0 ? QString::number(audioStreamIndex) : QString(),
                              subtitleStreamIndex >= 0 ? QString::number(subtitleStreamIndex) : QString());
        emit streamUrlReady(fullUrl);
    });
}

void EmbyBackend::load_next_episode(const QString &currentItemId) {
    if (!has_auth()) {
        emit nextEpisodeReady(QVariantMap{});
        return;
    }

    // Step 1: fetch current episode to get seriesId + episode position
    QUrl detailUrl(m_serverUrl + "/Users/" + m_userId + "/Items/" + currentItemId);
    QUrlQuery detailQ;
    detailQ.addQueryItem("fields", "MediaSources");
    detailUrl.setQuery(detailQ);

    auto *detailReply = embyGet(detailUrl);
    connect(detailReply, &QNetworkReply::finished, this, [this, detailReply]() {
        detailReply->deleteLater();
        if (detailReply->error() != QNetworkReply::NoError) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }
        QJsonObject item = QJsonDocument::fromJson(detailReply->readAll()).object();
        QString seriesId      = item["SeriesId"].toString();
        int     currentIndex  = item["IndexNumber"].toInt();
        int     currentSeason = item["ParentIndexNumber"].toInt();

        if (seriesId.isEmpty() || item["Type"].toString() != QLatin1String("Episode")) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }

        // Step 2: fetch all episodes for the series. No sortBy — see load_episodes;
        // the scan below picks by season/index number, so order doesn't matter here.
        QUrl epUrl(m_serverUrl + "/Shows/" + seriesId + "/Episodes");
        QUrlQuery epQ;
        epQ.addQueryItem("userId", m_userId);
        epQ.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
        epQ.addQueryItem("enableUserData", "true");
        epQ.addQueryItem("limit", "500");
        epUrl.setQuery(epQ);

        auto *epReply = embyGet(epUrl);
        connect(epReply, &QNetworkReply::finished, this,
                [this, epReply, currentIndex, currentSeason]() {
            epReply->deleteLater();
            if (epReply->error() != QNetworkReply::NoError) {
                emit nextEpisodeReady(QVariantMap{});
                return;
            }
            QJsonArray episodes = QJsonDocument::fromJson(epReply->readAll())
                                      .object()["Items"].toArray();

            // Find the next episode: smallest (season > currentSeason) or
            // (same season, episode index > currentIndex).
            QJsonObject nextEp;
            int nextSeason = 0;
            int nextIndex  = 0;
            for (const auto &ev : episodes) {
                QJsonObject e = ev.toObject();
                int s = e["ParentIndexNumber"].toInt();
                int i = e["IndexNumber"].toInt();

                if (s > currentSeason || (s == currentSeason && i > currentIndex)) {
                    if (nextEp.isEmpty() || s < nextSeason ||
                        (s == nextSeason && i < nextIndex)) {
                        nextEp     = e;
                        nextSeason = s;
                        nextIndex  = i;
                    }
                }
            }

            if (nextEp.isEmpty()) {
                emit nextEpisodeReady(QVariantMap{});
                return;
            }
            emit nextEpisodeReady(formatItem(nextEp));
        });
    });
}

void EmbyBackend::report_playback_start(const QString &itemId, const QString &mediaSourceId,
                                            const QString &audioStreamId, const QString &subtitleStreamId,
                                            qint64 startPositionTicks) {
    if (!has_auth()) return;

    // Called from get_playback_url() once PlaybackInfo resolves, so the session
    // id and play method are authoritative and shared with the stream URL and
    // the Progress/Stopped reports.
    QJsonObject body;
    body["ItemId"]            = itemId;
    body["MediaSourceId"]     = mediaSourceId;
    if (!m_currentPlaySessionId.isEmpty())
        body["PlaySessionId"] = m_currentPlaySessionId;
    body["PlayMethod"]        = m_currentPlayMethod.isEmpty() ? QStringLiteral("Transcode")
                                                             : m_currentPlayMethod;
    body["IsPaused"]          = false;
    body["CanSeek"]           = true;
    if (startPositionTicks > 0)
        body["StartPositionTicks"] = startPositionTicks;
    if (!audioStreamId.isEmpty())
        body["AudioStreamIndex"] = audioStreamId.toInt();
    if (!subtitleStreamId.isEmpty())
        body["SubtitleStreamIndex"] = subtitleStreamId.toInt();

    QUrl url(m_serverUrl + "/Sessions/Playing");
    auto *reply = embyPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            qDebug("[Emby] Playback session started: %s", qPrintable(m_currentPlaySessionId));
        else
            qWarning("[Emby] Failed to start playback session: %s",
                     qPrintable(reply->errorString()));
    });
}

void EmbyBackend::update_playback_progress(const QString &itemId, const QString &mediaSourceId,
                                               qint64 positionTicks, bool isPaused) {
    if (!has_auth()) return;

    QJsonObject body;
    body["ItemId"]         = itemId;
    body["MediaSourceId"]  = mediaSourceId;
    body["PositionTicks"]  = positionTicks;
    body["IsPaused"]       = isPaused;
    body["PlayMethod"]     = m_currentPlayMethod.isEmpty() ? QStringLiteral("Transcode")
                                                           : m_currentPlayMethod;
    body["CanSeek"]        = true;
    if (!m_currentPlaySessionId.isEmpty())
        body["PlaySessionId"] = m_currentPlaySessionId;

    QUrl url(m_serverUrl + "/Sessions/Playing/Progress");
    auto *reply = embyPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError)
            qWarning("[Emby] progress update failed: %s", qPrintable(reply->errorString()));
        reply->deleteLater();
    });
}

void EmbyBackend::report_playback_stopped(const QString &itemId, const QString &mediaSourceId,
                                              qint64 positionTicks, bool failed) {
    if (!has_auth()) return;

    QJsonObject body;
    body["ItemId"]        = itemId;
    body["MediaSourceId"] = mediaSourceId;
    body["PositionTicks"] = positionTicks;
    body["PlayMethod"]    = m_currentPlayMethod.isEmpty() ? QStringLiteral("Transcode")
                                                          : m_currentPlayMethod;
    body["Failed"]        = failed;
    if (!m_currentPlaySessionId.isEmpty())
        body["PlaySessionId"] = m_currentPlaySessionId;

    QUrl url(m_serverUrl + "/Sessions/Playing/Stopped");
    auto *reply = embyPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    // Only clear the session id if it's still the one we reported on — the
    // transcode retry in Player.qml starts a new session right after reporting
    // the failed one stopped, and this reply may land after that.
    const QString reportedSessionId = m_currentPlaySessionId;
    connect(reply, &QNetworkReply::finished, this, [this, reply, reportedSessionId]() {
        if (reply->error() != QNetworkReply::NoError)
            qWarning("[Emby] report stopped failed: %s", qPrintable(reply->errorString()));
        reply->deleteLater();
        if (m_currentPlaySessionId == reportedSessionId)
            m_currentPlaySessionId.clear();
    });
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void EmbyBackend::getLibraries() {
    if (!has_auth()) {
        emit dynamicOptionsReady("libraries", QVariantList());
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Views");
    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QVariantList options;
        if (reply->error() != QNetworkReply::NoError) {
            emit dynamicOptionsReady("libraries", options);
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        for (const QJsonValue &v : items) {
            QJsonObject item = v.toObject();
            if (!kSupportedCollectionTypes.contains(item["CollectionType"].toString()))
                continue;
            options.append(QVariantMap{
                {"id",      item["Id"].toString()},
                {"label",   item["Name"].toString().toUpper()},
            });
        }
        emit dynamicOptionsReady("libraries", options);
    });
}

void EmbyBackend::getVideoQualities() {
    QVariantList options;
    auto add = [&](const QString &value, const QString &label) {
        QVariantMap m;
        m["id"]    = value;
        m["label"] = label;
        options.append(m);
    };
    add("auto",  "Direct Play");
    add("480p",  "480p (NTSC CRT)");
    add("576p",  "576p (PAL CRT)");
    add("720p",  "720p");
    add("1080p", "1080p");
    emit dynamicOptionsReady("video_quality", options);
}

void EmbyBackend::get_resume_playback_options() {
    QVariantList options;
    auto add = [&](const QString &value, const QString &label) {
        QVariantMap m;
        m["id"]    = value;
        m["label"] = label;
        options.append(m);
    };
    add("ask",    "Ask");      // prompt resume vs. start over when a position exists
    add("always", "Always");   // resume directly, no prompt
    emit dynamicOptionsReady("resume_playback", options);
}

void EmbyBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value) {
    if (moduleId != kModuleId)
        return;

    if (key == QLatin1String("server_url")) {
        m_serverUrl = normalizeServerUrl(value.toString());
    }
}

QString EmbyBackend::get_last_audio_lang() const {
    return m_lastAudioLang;
}

QString EmbyBackend::get_last_sub_lang() const {
    return m_lastSubLang;
}

int EmbyBackend::get_last_audio_lang_idx() const {
    return m_lastAudioLangIdx;
}

int EmbyBackend::get_last_sub_lang_idx() const {
    return m_lastSubLangIdx;
}

void EmbyBackend::set_last_track_langs(const QString &audioLang, const QString &subLang,
                                           int audioLangIdx, int subLangIdx) {
    m_lastAudioLang = audioLang;
    m_lastSubLang = subLang;
    m_lastAudioLangIdx = audioLangIdx;
    m_lastSubLangIdx = subLangIdx;
}

void EmbyBackend::load_server_preferences() {
    if (!has_auth()) {
        emit serverLanguagePreferencesReady(m_lastAudioLang, m_lastSubLang, QString());
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId);
    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit serverLanguagePreferencesReady(m_lastAudioLang, m_lastSubLang, QString());
            return;
        }
        QJsonObject userData = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonObject config = userData["Configuration"].toObject();
        QString audioLang = !m_lastAudioLang.isEmpty() ? m_lastAudioLang : config["AudioLanguagePreference"].toString();
        QString subLang   = !m_lastSubLang.isEmpty()   ? m_lastSubLang   : config["SubtitleLanguagePreference"].toString();
        QString subMode   = config["SubtitleMode"].toString();
        emit serverLanguagePreferencesReady(audioLang, subLang, subMode);
    });
}

void EmbyBackend::fetchSegments(const QString &itemId) {
    if (!has_auth()) return;

    // Emby has no MediaSegments API (that is Jellyfin 10.10+). Instead it records
    // intro/credit boundaries as chapter markers on the item — chapters whose
    // MarkerType is IntroStart / IntroEnd / CreditsStart. Fetch the item with the
    // Chapters field and synthesise the same {type,startMs,endMs} segments the
    // Player consumes: an "Intro" (IntroStart→IntroEnd) and an "Outro"
    // (CreditsStart→end of file). Markers only exist once the server has detected
    // them, so an item without detection simply yields no segments.
    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/" + itemId);
    QUrlQuery q;
    q.addQueryItem("fields", "Chapters");
    url.setQuery(q);

    auto *reply = embyGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonObject item = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonArray chapters = item["Chapters"].toArray();
        const double runtimeMs = item["RunTimeTicks"].toDouble() / 10000.0;

        double introStartMs  = -1.0;
        double introEndMs    = -1.0;
        double creditsStartMs = -1.0;
        for (const QJsonValue &val : chapters) {
            QJsonObject ch = val.toObject();
            const QString marker = ch["MarkerType"].toString();
            const double posMs = ch["StartPositionTicks"].toDouble() / 10000.0;
            if (marker == QLatin1String("IntroStart"))        introStartMs  = posMs;
            else if (marker == QLatin1String("IntroEnd"))     introEndMs    = posMs;
            else if (marker == QLatin1String("CreditsStart")) creditsStartMs = posMs;
        }

        QVariantList segments;
        if (introStartMs >= 0.0 && introEndMs > introStartMs) {
            segments.append(QVariantMap{
                {"type",    QStringLiteral("Intro")},
                {"startMs", introStartMs},
                {"endMs",   introEndMs},
            });
        }
        if (creditsStartMs >= 0.0) {
            // Credits run to the end of the file; fall back to a fixed window if
            // the runtime is unknown.
            const double endMs = runtimeMs > creditsStartMs ? runtimeMs
                                                            : creditsStartMs + 600000.0;
            segments.append(QVariantMap{
                {"type",    QStringLiteral("Outro")},
                {"startMs", creditsStartMs},
                {"endMs",   endMs},
            });
        }

        emit segmentsReady(itemId, segments);
    });
}
