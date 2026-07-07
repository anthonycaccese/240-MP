#include "JellyfinBackend.h"

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

static const QString kModuleId = QStringLiteral("com.240mp.jellyfin");

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

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

JellyfinBackend::JellyfinBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_nam(new QNetworkAccessManager(this))
{
    loadAuthState();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
}

// ---------------------------------------------------------------------------
// Auth state persistence
// ---------------------------------------------------------------------------

QString JellyfinBackend::normalizeServerUrl(const QString &url) {
    QString u = url.trimmed();
    while (u.endsWith('/'))
        u.chop(1);
    return u;
}

void JellyfinBackend::loadAuthState() {
    QFile f(m_dataRoot + "/jellyfin_auth.json");
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

void JellyfinBackend::saveAuthState() {
    QJsonObject auth;
    auth["serverUrl"]   = m_serverUrl;
    auth["accessToken"] = m_accessToken;
    auth["userId"]      = m_userId;
    auth["userName"]    = m_userName;
    auth["serverName"]  = m_serverName;
    auth["deviceId"]    = m_deviceId;

    QFile f(m_dataRoot + "/jellyfin_auth.json");
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[JellyfinBackend] Could not write jellyfin_auth.json: %s", qPrintable(f.errorString()));
        return;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(auth).toJson(QJsonDocument::Indented));
    f.close();
}

void JellyfinBackend::clearAuthState() {
    m_serverUrl.clear();
    m_accessToken.clear();
    m_userId.clear();
    m_userName.clear();
    m_serverName.clear();
    m_currentPlaySessionId.clear();
    // Sign out will wipe the auth file as the device is removed from access
    // on the server end as well. This will generate a fresh deviceId so any
    // in-session re-login / QuickConnect creates one that will be synced with the 
    // new device we give access to on the server and it will persist until either
    // the user signs out or manually de-auths the device from the server end.
    // saveAuthState() will recreate the file on the next successful login.
    m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QFile::remove(m_dataRoot + "/jellyfin_auth.json");
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------

QJsonObject JellyfinBackend::loadConfig() const {
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    return {};
}

void JellyfinBackend::saveConfig(const QJsonObject &cfg) const {
    QFile f(m_dataRoot + "/config.json");
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[JellyfinBackend] Could not write config.json: %s", qPrintable(f.errorString()));
        return;
    }
    f.write(QJsonDocument(cfg).toJson(QJsonDocument::Indented));
}

QJsonObject JellyfinBackend::moduleConfig() const {
    return loadConfig()["modules"].toObject()[kModuleId].toObject();
}

int JellyfinBackend::videoQualityBitrate() const {
    QString quality = moduleConfig()["video_quality"].toString("auto");
    if (quality == QLatin1String("auto"))  return 0; // direct play — no cap
    if (quality == QLatin1String("1080p")) return 10000000;
    if (quality == QLatin1String("720p"))  return 6000000;
    if (quality == QLatin1String("576p"))  return 4500000;
    return 4000000; // 480p default
}

int JellyfinBackend::videoQualityMaxHeight() const {
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

QNetworkRequest JellyfinBackend::jellyfinRequest(const QUrl &url) const {
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", authHeaderValue(m_accessToken, m_deviceId).toLatin1());
    return req;
}

QNetworkReply *JellyfinBackend::jellyfinGet(const QUrl &url) {
    auto *reply = m_nam->get(jellyfinRequest(url));
    ignoreSslErrors(reply);
    return reply;
}

QNetworkReply *JellyfinBackend::jellyfinPost(const QUrl &url, const QByteArray &body) {
    QNetworkRequest req = jellyfinRequest(url);
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

void JellyfinBackend::ignoreSslErrors(QNetworkReply *reply) const {
    connect(reply, &QNetworkReply::sslErrors, reply, [this, reply](const QList<QSslError> &errors) {
        // Only relax for the configured Jellyfin server — typical of self-signed LAN certs
        QUrl serverUrl(m_serverUrl);
        if (reply->url().host() != serverUrl.host())
            return;
        QList<QSslError> allowed = filterExpectedSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

bool JellyfinBackend::has_auth() {
    return !m_accessToken.isEmpty() && !m_userId.isEmpty() && !m_serverUrl.isEmpty();
}

QString JellyfinBackend::get_server_name() {
    return m_serverName;
}

QString JellyfinBackend::get_user_name() {
    return m_userName;
}

QString JellyfinBackend::get_auth_state() {
    return has_auth() ? QStringLiteral("authed") : QStringLiteral("none");
}

void JellyfinBackend::check_auth() {
    if (!has_auth()) {
        emit authStateChanged();
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId);
    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401) {
            qWarning("[JellyfinBackend] Token rejected — signing out");
            clearAuthState();
            emit authRevoked();
            emit authStateChanged();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("AUTH CHECK FAILED: " + reply->errorString());
            return;
        }
        emit authStateChanged();
        probeCapabilities();
    });
}

void JellyfinBackend::logout() {
    // Revoke the access token server-side so it can't be reused
    if (has_auth()) {
        QUrl url(m_serverUrl + "/Sessions/Logout");
        auto *reply = m_nam->post(jellyfinRequest(url), QByteArray());
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    }
    clearAuthState();
    emit logoutComplete();
    emit authStateChanged();
}

// ---------------------------------------------------------------------------
// Quick Connect
// ---------------------------------------------------------------------------

void JellyfinBackend::quick_connect_initiate(const QString &serverUrl) {
    QString normalized = normalizeServerUrl(serverUrl);
    if (normalized.isEmpty()) {
        emit errorOccurred("SERVER URL REQUIRED");
        return;
    }

    m_quickConnectServerUrl = normalized;
    QUrl url(normalized + "/QuickConnect/Initiate");

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", authHeaderValue(QString(), m_deviceId).toLatin1());

    // Initiate uses empty POST body
    auto *reply = m_nam->post(req, QByteArray());
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        for (const QSslError &e : errors)
            qDebug("[JellyfinBackend] QC SSL error (ignored): %s", qPrintable(e.errorString()));
        QList<QSslError> allowed = filterExpectedSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            emit quickConnectFailed("CONNECTION FAILED: " + reply->errorString());
            return;
        }
        if (status >= 400) {
            qWarning("[JellyfinBackend] QC Initiate HTTP %d", status);
            if (status == 401)
                emit quickConnectFailed("QUICK CONNECT NOT ENABLED ON SERVER");
            else
                emit quickConnectFailed("SERVER ERROR (HTTP " + QString::number(status) + ")");
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(body).object();
        QString secret = data["Secret"].toString();
        QString code   = data["Code"].toString();

        if (secret.isEmpty() || code.isEmpty()) {
            emit quickConnectFailed("INVALID RESPONSE FROM SERVER");
            return;
        }

        m_quickConnectSecret = secret;
        emit quickConnectCodeReady(code, secret);
    });
}

void JellyfinBackend::quick_connect_poll(const QString &secret) {
    if (secret.isEmpty()) {
        emit quickConnectFailed("NO SECRET");
        return;
    }

    QUrl url(m_quickConnectServerUrl + "/QuickConnect/Connect?secret=" + secret);
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", authHeaderValue(QString(), m_deviceId).toLatin1());

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        for (const QSslError &e : errors)
            qDebug("[JellyfinBackend] QC SSL error (ignored): %s", qPrintable(e.errorString()));
        QList<QSslError> allowed = filterExpectedSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, secret]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            emit quickConnectFailed("POLL FAILED: " + reply->errorString());
            return;
        }
        if (status >= 400) {
            // 404 means secret expired or never existed
            if (status == 404)
                emit quickConnectFailed("CODE EXPIRED — RETRY");
            else
                emit quickConnectFailed("SERVER ERROR (HTTP " + QString::number(status) + ")");
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(body).object();
        if (data["Authenticated"].toBool()) {
            emit quickConnectApproved();
        }
        // else: still waiting — QML Timer continues polling
    });
}

void JellyfinBackend::quick_connect_authenticate(const QString &secret) {
    if (secret.isEmpty()) {
        emit errorOccurred("NO SECRET");
        return;
    }

    QUrl url(m_quickConnectServerUrl + "/Users/AuthenticateWithQuickConnect");
    QJsonObject body;
    body["Secret"] = secret;
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", authHeaderValue(QString(), m_deviceId).toLatin1());

    auto *reply = m_nam->post(req, payload);
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        for (const QSslError &e : errors)
            qDebug("[JellyfinBackend] QC SSL error (ignored): %s", qPrintable(e.errorString()));
        QList<QSslError> allowed = filterExpectedSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("AUTH FAILED: " + reply->errorString());
            return;
        }
        if (status >= 400) {
            qWarning("[JellyfinBackend] QC Auth HTTP %d", status);
            emit errorOccurred("AUTH FAILED (HTTP " + QString::number(status) + ")");
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(body).object();
        QString token = data["AccessToken"].toString();
        QJsonObject user = data["User"].toObject();

        if (token.isEmpty() || user["Id"].toString().isEmpty()) {
            emit errorOccurred("INVALID AUTH RESPONSE");
            return;
        }

        m_serverUrl   = m_quickConnectServerUrl;
        m_accessToken = token;
        m_userId      = user["Id"].toString();
        m_userName    = user["Name"].toString();
        m_serverName  = m_quickConnectServerUrl; // temp: fetch real name below
        m_quickConnectSecret.clear();

        // Fetch actual server name
        {
            QUrl infoUrl(m_serverUrl + "/System/Info/Public");
            QNetworkRequest infoReq(infoUrl);
            infoReq.setRawHeader("Accept", "application/json");
            infoReq.setRawHeader("Authorization", authHeaderValue(m_accessToken, m_deviceId).toLatin1());
            auto *infoReply = m_nam->get(infoReq);
            connect(infoReply, &QNetworkReply::sslErrors, infoReply, [](const QList<QSslError>&){});
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

        QJsonObject cfg = loadConfig();
        QJsonObject modules = cfg["modules"].toObject();
        QJsonObject modCfg  = modules[kModuleId].toObject();
        modCfg["server_url"] = m_serverUrl;
        modules[kModuleId] = modCfg;
        cfg["modules"] = modules;
        saveConfig(cfg);

        probeCapabilities();
        emit authStateChanged();
    });
}

void JellyfinBackend::quick_connect_cancel() {
    if (m_quickConnectSecret.isEmpty())
        return;
    QUrl url(m_quickConnectServerUrl + "/QuickConnect/Connect?secret=" + m_quickConnectSecret);
    QNetworkRequest req(url);
    m_nam->deleteResource(req);
    m_quickConnectSecret.clear();
}

// ---------------------------------------------------------------------------
// Item formatting
// ---------------------------------------------------------------------------

QVariantMap JellyfinBackend::formatItem(const QJsonObject &item) const {
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
    map["type"]            = item["Type"].toString().toLower();
    map["overview"]        = item["Overview"].toString();
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

void JellyfinBackend::load_libraries() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Views");
    auto *reply = jellyfinGet(url);
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
        QUrl resumeUrl(m_serverUrl + "/Users/" + m_userId + "/Items/Resume");
        { QUrlQuery rq; rq.addQueryItem("limit", "1"); resumeUrl.setQuery(rq); }
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

void JellyfinBackend::probeHasItems(const QUrl &url, std::function<void(bool)> cb) {
    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        bool has = false;
        if (reply->error() == QNetworkReply::NoError)
            has = !QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray().isEmpty();
        cb(has);
    });
}

void JellyfinBackend::load_items(const QString &parentId, const QString &includeTypes, const QString &sortBy) {
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
    q.addQueryItem("fields", "Overview,Genres,UserData");
    if (!includeTypes.isEmpty())
        q.addQueryItem("includeItemTypes", includeTypes);
    if (!sortBy.isEmpty()) {
        q.addQueryItem("sortBy", sortBy);
        q.addQueryItem("sortOrder", "Ascending");
    }
    // No limit — return the full library so the list is complete A–Z (matches the
    // Plex module's unbounded /library/sections/{id}/all). Jellyfin returns all
    // matching items when limit is omitted.
    url.setQuery(q);

    auto *reply = jellyfinGet(url);
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

void JellyfinBackend::load_item_detail(const QString &itemId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/" + itemId);
    QUrlQuery q;
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);

    auto *reply = jellyfinGet(url);
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

void JellyfinBackend::load_children(const QString &itemId) {
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

    auto *reply = jellyfinGet(url);
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

void JellyfinBackend::load_boxset_children(const QString &parentId) {
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
    q.addQueryItem("fields", "Overview,Genres,UserData,ChildCount");
    q.addQueryItem("sortBy", "SortName");
    q.addQueryItem("sortOrder", "Ascending");
    url.setQuery(q);

    auto *reply = jellyfinGet(url);
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

void JellyfinBackend::load_folder_children(const QString &parentId) {
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
    q.addQueryItem("fields", "Overview,Genres,UserData,ChildCount");
    q.addQueryItem("sortBy", "SortName");
    q.addQueryItem("sortOrder", "Ascending");
    url.setQuery(q);

    auto *reply = jellyfinGet(url);
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

void JellyfinBackend::load_seasons(const QString &seriesId) {
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
    // [dev] qDebug("[JellyfinBackend] load_seasons series=%s", qPrintable(seriesId));

    auto *reply = jellyfinGet(url);
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
        // [dev] qDebug("[JellyfinBackend] load_seasons got %d seasons", items.size());
        emit seasonsLoaded(result);
    });
}

void JellyfinBackend::load_episodes(const QString &seriesId, const QString &seasonId) {
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
    q.addQueryItem("sortBy", "AiredEpisodeOrder");
    url.setQuery(q);
    // [dev] qDebug("[JellyfinBackend] load_episodes series=%s season=%s", qPrintable(seriesId), qPrintable(seasonId));

    auto *reply = jellyfinGet(url);
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
        // [dev] qDebug("[JellyfinBackend] load_episodes got %d episodes", items.size());
        emit episodesLoaded(result);
    });
}

void JellyfinBackend::load_continue_watching() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/Resume");
    QUrlQuery q;
    q.addQueryItem("limit", "20");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);

    auto *reply = jellyfinGet(url);
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

void JellyfinBackend::load_up_next() {
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

    auto *reply = jellyfinGet(url);
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

void JellyfinBackend::load_series_next_up(const QString &seriesId) {
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

    auto *reply = jellyfinGet(url);
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

void JellyfinBackend::get_playback_url(const QString &itemId, const QString &mediaSourceId,
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
    const int maxBitrate = videoQualityBitrate();
    const int maxHeight  = videoQualityMaxHeight();

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
    // Hardcoding -1 here caused newer Jellyfin servers to strip sub tracks
    // from the stream, breaking both --sid for image subs and sidecar URLs.
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
        // Direct play serves the original file whole and mpv renders embedded
        // subtitles itself. Advertise Embed (not Encode) so the server doesn't
        // force a transcode just to burn in / convert the selected subtitle.
        auto addEmbed = [&](const char *fmt) {
            QJsonObject s;
            s["Format"] = QString::fromLatin1(fmt);
            s["Method"] = QStringLiteral("Embed");
            subtitleProfiles.append(s);
        };
        addEmbed("subrip");
        addEmbed("srt");
        addEmbed("ass");
        addEmbed("ssa");
        addEmbed("vtt");
        addEmbed("webvtt");
        addEmbed("mov_text");
        addEmbed("pgssub");
        addEmbed("dvbsub");
        addEmbed("dvdsub");
    } else {
        // Transcode: burn the selected subtitle into the video (like the Plex
        // module). Soft HLS subtitle renditions are unreliable in mpv — they
        // report as selected but frequently never render, especially after a
        // seek — so we always burn here. The server bakes SubtitleMethod=Encode
        // into TranscodingUrl from this profile + the body's SubtitleStreamIndex.
        auto addBurnin = [&](const char *fmt) {
            QJsonObject s;
            s["Format"] = QString::fromLatin1(fmt);
            s["Method"] = QStringLiteral("Encode");
            subtitleProfiles.append(s);
        };
        addBurnin("subrip");
        addBurnin("srt");
        addBurnin("ass");
        addBurnin("ssa");
        addBurnin("vtt");
        addBurnin("mov_text");
        addBurnin("pgssub");
        addBurnin("dvbsub");
        addBurnin("dvdsub");
    }
    profile["SubtitleProfiles"] = subtitleProfiles;
    QJsonArray directPlayProfiles;
    if (directPlay) {
        // mpv plays virtually anything, so advertise a match-all profile —
        // omitted Container/VideoCodec/AudioCodec fields match every value in
        // Jellyfin's profile matcher. A codec whitelist here silently forced
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

    auto *reply = jellyfinPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    // [dev] qDebug("[JellyfinBackend] PlaybackInfo POST %s audio=%d sub=%d bitrate=%d",
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
            qWarning("[Jellyfin] direct play denied (SupportsDirectPlay=%d SupportsDirectStream=%d): %s",
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

        // Build the full URL, then strip any api_key from the server-generated
        // TranscodingUrl — the Authorization header (passed via --http-header-fields
        // to mpv) covers authentication. When the user selected OFF, also strip
        // any SubtitleStreamIndex and SubtitleMethod the server may have added
        // from default metadata.
        QUrl parsedUrl(m_serverUrl + transcodeUrl);
        {
            QUrlQuery q(parsedUrl);
            const auto items = q.queryItems();
            for (const auto &kv : items) {
                if (kv.first.compare(QLatin1String("api_key"), Qt::CaseInsensitive) == 0 ||
                    kv.first.compare(QLatin1String("apikey"),  Qt::CaseInsensitive) == 0)
                    q.removeAllQueryItems(kv.first);
            }
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

        // [dev] qDebug("[JellyfinBackend] PlaybackInfo URL ready audio=%d sub=%d psId=%s",
        // [dev]        audioStreamIndex, subtitleStreamIndex, qPrintable(playSessionId.left(8)));
        report_playback_start(itemId, mediaSourceId,
                              audioStreamIndex >= 0 ? QString::number(audioStreamIndex) : QString(),
                              subtitleStreamIndex >= 0 ? QString::number(subtitleStreamIndex) : QString());
        emit streamUrlReady(fullUrl);
    });
}

void JellyfinBackend::load_next_episode(const QString &currentItemId) {
    if (!has_auth()) {
        emit nextEpisodeReady(QVariantMap{});
        return;
    }

    // Step 1: fetch current episode to get seriesId + episode position
    QUrl detailUrl(m_serverUrl + "/Users/" + m_userId + "/Items/" + currentItemId);
    QUrlQuery detailQ;
    detailQ.addQueryItem("fields", "MediaSources");
    detailUrl.setQuery(detailQ);

    auto *detailReply = jellyfinGet(detailUrl);
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

        // Step 2: fetch all episodes for the series, sorted by air order
        QUrl epUrl(m_serverUrl + "/Shows/" + seriesId + "/Episodes");
        QUrlQuery epQ;
        epQ.addQueryItem("userId", m_userId);
        epQ.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
        epQ.addQueryItem("enableUserData", "true");
        epQ.addQueryItem("limit", "500");
        epQ.addQueryItem("sortBy", "AiredEpisodeOrder");
        epUrl.setQuery(epQ);

        auto *epReply = jellyfinGet(epUrl);
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

void JellyfinBackend::report_playback_start(const QString &itemId, const QString &mediaSourceId,
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
    auto *reply = jellyfinPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            qDebug("[Jellyfin] Playback session started: %s", qPrintable(m_currentPlaySessionId));
        else
            qWarning("[Jellyfin] Failed to start playback session: %s",
                     qPrintable(reply->errorString()));
    });
}

void JellyfinBackend::update_playback_progress(const QString &itemId, const QString &mediaSourceId,
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
    auto *reply = jellyfinPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError)
            qWarning("[Jellyfin] progress update failed: %s", qPrintable(reply->errorString()));
        reply->deleteLater();
    });
}

void JellyfinBackend::report_playback_stopped(const QString &itemId, const QString &mediaSourceId,
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
    auto *reply = jellyfinPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    // Only clear the session id if it's still the one we reported on — the
    // transcode retry in Player.qml starts a new session right after reporting
    // the failed one stopped, and this reply may land after that.
    const QString reportedSessionId = m_currentPlaySessionId;
    connect(reply, &QNetworkReply::finished, this, [this, reply, reportedSessionId]() {
        if (reply->error() != QNetworkReply::NoError)
            qWarning("[Jellyfin] report stopped failed: %s", qPrintable(reply->errorString()));
        reply->deleteLater();
        if (m_currentPlaySessionId == reportedSessionId)
            m_currentPlaySessionId.clear();
    });
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void JellyfinBackend::getLibraries() {
    if (!has_auth()) {
        emit dynamicOptionsReady("libraries", QVariantList());
        return;
    }

    // Re-emit cached capabilities so ModuleSettings.qml can filter settings
    // correctly on every pageload (the signal may have been missed if
    // ModuleSettings.qml was destroyed/recreated after the initial probe).
    probeCapabilities();

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Views");
    auto *reply = jellyfinGet(url);
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

void JellyfinBackend::getVideoQualities() {
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

void JellyfinBackend::get_resume_playback_options() {
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

void JellyfinBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value) {
    if (moduleId != kModuleId)
        return;

    if (key == QLatin1String("server_url")) {
        m_serverUrl = normalizeServerUrl(value.toString());
    }
}

QString JellyfinBackend::get_last_audio_lang() const {
    return m_lastAudioLang;
}

QString JellyfinBackend::get_last_sub_lang() const {
    return m_lastSubLang;
}

int JellyfinBackend::get_last_audio_lang_idx() const {
    return m_lastAudioLangIdx;
}

int JellyfinBackend::get_last_sub_lang_idx() const {
    return m_lastSubLangIdx;
}

void JellyfinBackend::set_last_track_langs(const QString &audioLang, const QString &subLang,
                                           int audioLangIdx, int subLangIdx) {
    m_lastAudioLang = audioLang;
    m_lastSubLang = subLang;
    m_lastAudioLangIdx = audioLangIdx;
    m_lastSubLangIdx = subLangIdx;
}

void JellyfinBackend::load_server_preferences() {
    if (!has_auth()) {
        emit serverLanguagePreferencesReady(m_lastAudioLang, m_lastSubLang, QString());
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId);
    auto *reply = jellyfinGet(url);
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

void JellyfinBackend::probeCapabilities() {
    if (!has_auth()) return;

    // Already probed: re-emit cached state. This is important because
    // ModuleSettings.qml is destroyed/recreated on navigation and the
    // one-shot dynamicOptionsReady signal may have been missed.
    if (m_capabilitiesProbed) {
        if (m_hasCapability)
            emit dynamicOptionsReady("_capabilities",
                QVariantList{QString("mediasegments")});
        else
            emit dynamicOptionsReady("_capabilities", QVariantList{});
        return;
    }

    // First probe: use a null GUID — if the MediaSegments route exists
    // (plugin installed), the server returns a non-404 HTTP response.
    // If the route doesn't exist, ASP.NET returns 404.
    //
    // The non-404 on capable servers is because Jellyfin's GetItemById
    // throws on an empty GUID (→ 400/500) before its item-not-found 404
    // path is reached. If a future Jellyfin returns plain 404 for the
    // empty GUID, this probe reports "no capability" and the skip settings
    // stay hidden; switch to a /System/Info/Public version check (the
    // MediaSegments API is core since 10.10) if that ever happens.
    QUrl url(m_serverUrl + "/MediaSegments/00000000-0000-0000-0000-000000000000");
    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        // fetchSegments may have handled the probe while we were waiting
        if (m_capabilitiesProbed) {
            // fetchSegments already emitted — sync our cached flag
            // m_hasCapability was already set by fetchSegments
            return;
        }

        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200) {
            // Got a definitive HTTP response from the server
            m_capabilitiesProbed = true;
            m_hasCapability = (status != 404);
            if (m_hasCapability) {
                emit dynamicOptionsReady("_capabilities",
                    QVariantList{QString("mediasegments")});
            } else {
                emit dynamicOptionsReady("_capabilities", QVariantList{});
            }
        }
        // Network error (status == 0): leave m_capabilitiesProbed false so
        // fetchSegments can retry with a real item ID
    });
}

void JellyfinBackend::fetchSegments(const QString &itemId) {
    QUrl url(m_serverUrl + "/MediaSegments/" + itemId);
    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();

        // One-shot capability probe on first call (fallback if
        // probeCapabilities failed or was never called). Like
        // probeCapabilities, only latch on a definitive HTTP response —
        // a transient network error (status 0) must not permanently mark
        // the server as lacking the capability.
        if (!m_capabilitiesProbed) {
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 200) {
                m_capabilitiesProbed = true;
                m_hasCapability = (status != 404);
                if (m_hasCapability) {
                    emit dynamicOptionsReady("_capabilities",
                        QVariantList{QString("mediasegments")});
                } else {
                    emit dynamicOptionsReady("_capabilities", QVariantList{});
                    return;  // no segments to process, server doesn't support it
                }
            }
        }

        // Parse segments from the response
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonArray items = root["Items"].toArray();

        QVariantList segments;
        for (const QJsonValue &val : items) {
            QJsonObject item = val.toObject();
            QString type = item["Type"].toString();
            // Only include Intro and Outro segments
            if (type != "Intro" && type != "Outro") continue;

            QVariantMap seg;
            seg["type"]    = type;                                  // "Intro" or "Outro"
            seg["startMs"] = item["StartTicks"].toDouble() / 10000.0;  // ticks → ms
            seg["endMs"]   = item["EndTicks"].toDouble()   / 10000.0;
            segments.append(seg);
        }

        emit segmentsReady(itemId, segments);
    });
}
