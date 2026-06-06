#include "PlexBackend.h"

#include <QFile>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSslError>
#include <QUrlQuery>
#include <QCoreApplication>
#include <QSysInfo>
#include <QUuid>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>
#include <QDateTime>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

static const QString PLEX_TV = QStringLiteral("https://plex.tv");

static const QSet<QString> kSupportedLibraryTypes = {"movie", "show", "clip"};

#ifdef Q_OS_MAC
static const QString kPlexPlatform = QStringLiteral("macOS");
#else
static const QString kPlexPlatform = QStringLiteral("Linux");
#endif

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PlexBackend::PlexBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_nam(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(2000);
    connect(m_pollTimer, &QTimer::timeout, this, &PlexBackend::pollPinTick);
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

QJsonObject PlexBackend::loadAuth() const {
    QFile f(m_dataRoot + "/plex_auth.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    return {};
}

void PlexBackend::saveAuth(const QJsonObject &auth) const {
    QFile f(m_dataRoot + "/plex_auth.json");
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[PlexBackend] Could not write plex_auth.json: %s", qPrintable(f.errorString()));
        return;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(auth).toJson(QJsonDocument::Indented));
    f.close();
}

QJsonObject PlexBackend::loadConfig() const {
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    return {};
}

void PlexBackend::saveConfig(const QJsonObject &cfg) const {
    QFile f(m_dataRoot + "/config.json");
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(cfg).toJson(QJsonDocument::Indented));
}

// ---------------------------------------------------------------------------
// Plex HTTP helpers
// ---------------------------------------------------------------------------

QString PlexBackend::clientId() const {
    if (!m_clientId.isEmpty()) return m_clientId;
    QJsonObject auth = loadAuth();
    QString id = auth["client_identifier"].toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject a = auth;
        a["client_identifier"] = id;
        saveAuth(a);
    }
    const_cast<PlexBackend*>(this)->m_clientId = id;
    return id;
}

QNetworkRequest PlexBackend::plexRequest(const QUrl &url, const QString &token) const {
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Plex-Client-Identifier", clientId().toLatin1());
    req.setRawHeader("X-Plex-Product", "240-MP");
    req.setRawHeader("X-Plex-Version", QCoreApplication::applicationVersion().toLatin1());
    req.setRawHeader("X-Plex-Platform", kPlexPlatform.toLatin1());
    req.setRawHeader("X-Plex-Device", "240-MP");
    req.setRawHeader("X-Plex-Device-Name", QSysInfo::machineHostName().toLatin1());
    if (!token.isEmpty())
        req.setRawHeader("X-Plex-Token", token.toLatin1());
    return req;
}

void PlexBackend::ignoreSslErrors(QNetworkReply *reply) {
    connect(reply, &QNetworkReply::sslErrors, reply,
            [reply](const QList<QSslError> &errors) {
        // Only relax verification for Plex's LAN direct addresses. The *.plex.direct
        // wildcard cert is Let's Encrypt-signed but may fail on systems with an
        // incomplete CA bundle (e.g. RPi OS Lite without ca-certificates). Suppress
        // only the chain-verification errors we expect for that host — not globally.
        if (!reply->url().host().endsWith(QStringLiteral(".plex.direct")))
            return;
        static const QSet<QSslError::SslError> kExpected = {
            QSslError::UnableToGetLocalIssuerCertificate,
            QSslError::UnableToVerifyFirstCertificate,
            QSslError::SelfSignedCertificateInChain,
        };
        QList<QSslError> allowed;
        for (const QSslError &e : errors)
            if (kExpected.contains(e.error())) allowed.append(e);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });
}

QNetworkReply *PlexBackend::plexGet(const QUrl &url, const QString &token) {
    auto *reply = m_nam->get(plexRequest(url, token));
    ignoreSslErrors(reply);
    return reply;
}

QNetworkReply *PlexBackend::plexPost(const QUrl &url, const QString &token) {
    auto *reply = m_nam->post(plexRequest(url, token), QByteArray{});
    ignoreSslErrors(reply);
    return reply;
}

QNetworkReply *PlexBackend::plexPut(const QUrl &url, const QString &token) {
    auto *reply = m_nam->put(plexRequest(url, token), QByteArray{});
    ignoreSslErrors(reply);
    return reply;
}

QNetworkReply *PlexBackend::plexDelete(const QUrl &url, const QString &token) {
    auto *reply = m_nam->deleteResource(plexRequest(url, token));
    ignoreSslErrors(reply);
    return reply;
}

QNetworkReply *PlexBackend::plexPostJson(const QUrl &url, const QString &token,
                                          const QByteArray &body) {
    QNetworkRequest req = plexRequest(url, token);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = m_nam->post(req, body);
    ignoreSslErrors(reply);
    return reply;
}

// ---------------------------------------------------------------------------
// JWT key management
// ---------------------------------------------------------------------------

QByteArray PlexBackend::generateAndSaveKeyPair(const QString &keyId) {
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY_keygen_init(pctx);
    EVP_PKEY *pkey = nullptr;
    EVP_PKEY_keygen(pctx, &pkey);
    EVP_PKEY_CTX_free(pctx);

    // Save private key as PEM
    BIO *bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    BUF_MEM *bptr = nullptr;
    BIO_get_mem_ptr(bio, &bptr);
    QByteArray pemData(bptr->data, static_cast<int>(bptr->length));
    BIO_free(bio);

    QFile keyFile(m_dataRoot + "/plex_key.pem");
    if (keyFile.open(QIODevice::WriteOnly)) {
        keyFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        keyFile.write(pemData);
        keyFile.close();
    } else {
        qWarning("[PlexBackend] Could not write plex_key.pem: %s",
                 qPrintable(keyFile.errorString()));
    }

    // Export 32-byte raw public key for JWK
    size_t pubLen = 32;
    unsigned char pub[32];
    EVP_PKEY_get_raw_public_key(pkey, pub, &pubLen);
    EVP_PKEY_free(pkey);

    QString x = QByteArray(reinterpret_cast<const char*>(pub), 32)
                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    QJsonObject jwk;
    jwk["kty"] = "OKP";
    jwk["crv"] = "Ed25519";
    jwk["x"]   = x;
    jwk["kid"] = keyId;
    jwk["alg"] = "EdDSA";

    QJsonObject body;
    body["jwk"] = jwk;
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

EVP_PKEY *PlexBackend::loadPrivateKey() const {
    QFile f(m_dataRoot + "/plex_key.pem");
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    QByteArray pem = f.readAll();
    BIO *bio = BIO_new_mem_buf(pem.constData(), pem.size());
    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    return pkey;
}

// ---------------------------------------------------------------------------
// JWT building + signing
// ---------------------------------------------------------------------------

QString PlexBackend::buildDeviceJwt(EVP_PKEY *key, const QString &keyId,
                                     const QString &nonce) const {
    qint64 now = QDateTime::currentSecsSinceEpoch();

    QJsonObject header;
    header["alg"] = "EdDSA";
    header["kid"] = keyId;
    header["typ"] = "JWT";

    QJsonObject payload;
    payload["aud"] = "plex.tv";
    payload["iss"] = clientId();
    payload["iat"] = now;
    payload["exp"] = now + 3600;
    if (!nonce.isEmpty()) {
        payload["nonce"] = nonce;
        payload["scope"] = "username,friendly_name";
    }

    auto b64url = [](const QByteArray &data) {
        return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    };

    QByteArray headerB64  = b64url(QJsonDocument(header).toJson(QJsonDocument::Compact));
    QByteArray payloadB64 = b64url(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QByteArray sigInput   = headerB64 + '.' + payloadB64;

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, key);
    size_t sigLen = 64;
    unsigned char sig[64];
    EVP_DigestSign(mdctx, sig, &sigLen,
                   reinterpret_cast<const unsigned char*>(sigInput.constData()), sigInput.size());
    EVP_MD_CTX_free(mdctx);

    QByteArray sigB64 = b64url(QByteArray(reinterpret_cast<const char*>(sig), sigLen));
    return QString::fromLatin1(sigInput + '.' + sigB64);
}

qint64 PlexBackend::jwtExpClaim(const QString &jwt) {
    QStringList parts = jwt.split('.');
    if (parts.size() < 2) return 0;
    QByteArray payload = QByteArray::fromBase64(
        parts[1].toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QJsonObject obj = QJsonDocument::fromJson(payload).object();
    return static_cast<qint64>(obj["exp"].toDouble());
}

QString PlexBackend::jwtUserIdClaim(const QString &jwt) {
    QStringList parts = jwt.split('.');
    if (parts.size() < 2) return {};
    QByteArray payload = QByteArray::fromBase64(
        parts[1].toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QJsonObject obj = QJsonDocument::fromJson(payload).object();
    int id = obj["user"].toObject()["id"].toInt();
    return id > 0 ? QString::number(id) : QString{};
}

// ---------------------------------------------------------------------------
// JWT refresh + startup check
// ---------------------------------------------------------------------------

void PlexBackend::refreshJwt(std::function<void(bool ok)> callback) {
    if (m_refreshInFlight) { callback(false); return; }
    m_refreshInFlight = true;

    // Step 1: get nonce
    auto *nonceReply = plexGet(QUrl(PLEX_TV + "/api/v2/auth/nonce"), {});
    connect(nonceReply, &QNetworkReply::finished, this, [this, nonceReply, callback]() {
        nonceReply->deleteLater();
        if (nonceReply->error() != QNetworkReply::NoError) {
            qWarning("[PlexBackend] JWT refresh: nonce request failed");
            m_refreshInFlight = false;
            callback(false);
            return;
        }
        QString nonce = QJsonDocument::fromJson(nonceReply->readAll())
                        .object()["nonce"].toString();
        if (nonce.isEmpty()) {
            qWarning("[PlexBackend] JWT refresh: empty nonce");
            m_refreshInFlight = false;
            callback(false);
            return;
        }

        // Step 2: sign device JWT with nonce
        QJsonObject auth = loadAuth();
        QString kid = auth["jwt_key_id"].toString();
        EVP_PKEY *pkey = loadPrivateKey();
        if (!pkey || kid.isEmpty()) {
            qWarning("[PlexBackend] JWT refresh: no private key or key ID");
            if (pkey) EVP_PKEY_free(pkey);
            m_refreshInFlight = false;
            callback(false);
            return;
        }
        QString deviceJwt = buildDeviceJwt(pkey, kid, nonce);
        EVP_PKEY_free(pkey);

        // Step 3: exchange for Plex JWT
        QJsonObject body;
        body["jwt"] = deviceJwt;
        auto *tokenReply = plexPostJson(
            QUrl(PLEX_TV + "/api/v2/auth/token"), {},
            QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(tokenReply, &QNetworkReply::finished, this, [this, tokenReply, callback]() {
            tokenReply->deleteLater();
            m_refreshInFlight = false;
            if (tokenReply->error() != QNetworkReply::NoError) {
                qWarning("[PlexBackend] JWT refresh: token exchange failed: %s",
                         qPrintable(tokenReply->errorString()));
                callback(false);
                return;
            }
            QString newJwt = QJsonDocument::fromJson(tokenReply->readAll())
                             .object()["auth_token"].toString();
            if (newJwt.isEmpty()) {
                qWarning("[PlexBackend] JWT refresh: empty token in response");
                callback(false);
                return;
            }
            qint64 exp = jwtExpClaim(newJwt);
            QJsonObject a = loadAuth();
            a["auth_token"] = newJwt;
            a["jwt_exp"]    = exp;
            QString uid = jwtUserIdClaim(newJwt);
            if (!uid.isEmpty()) a["account_user_id"] = uid;
            saveAuth(a);
            qDebug("[PlexBackend] JWT refreshed, exp=%lld", static_cast<long long>(exp));
            callback(true);
        });
    });
}

void PlexBackend::checkAndRefreshOnStartup(std::function<void()> callback) {
    QJsonObject auth = loadAuth();
    QString token = auth["auth_token"].toString();

    if (token.isEmpty()) {
        callback();
        return;
    }

    bool hasKey = QFile::exists(m_dataRoot + "/plex_key.pem");

    if (!hasKey) {
        // Legacy token present but no key — run silent migration
        migrateLegacyToken(callback);
        return;
    }

    // Check expiry
    qint64 exp = static_cast<qint64>(auth["jwt_exp"].toDouble());
    if (exp == 0) exp = jwtExpClaim(token);
    qint64 now = QDateTime::currentSecsSinceEpoch();

    // After the JWT is confirmed valid (fresh or freshly refreshed), verify the
    // device is still registered with plex.tv. A 401 from the resources endpoint
    // means the device was deauthorized via the Plex admin UI. Check runs once
    // per session; subsequent load_libraries calls skip it via m_deviceVerified.
    auto proceed = [this, callback]() {
        if (m_deviceVerified) { callback(); return; }
        auto *reply = plexGet(QUrl(PLEX_TV + "/api/v2/resources"), accountToken());
        connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
            reply->deleteLater();
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401) {
                qWarning("[PlexBackend] Device no longer authorized — triggering reauth");
                emit authRevoked();
                return;
            }
            m_deviceVerified = true;
            callback();
        });
    };

    if (exp == 0 || now >= exp - 86400) {
        // Expired or within 24h — refresh proactively, then verify
        refreshJwt([proceed](bool) { proceed(); });
    } else {
        proceed();
    }
}

void PlexBackend::migrateLegacyToken(std::function<void()> callback) {
    QString token = accountToken();
    if (token.isEmpty()) { callback(); return; }

    qDebug("[PlexBackend] Migrating legacy token to JWT");
    QString kid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray jwkBody = generateAndSaveKeyPair(kid);

    // Save kid immediately so refreshJwt can find it even if we restart
    QJsonObject auth = loadAuth();
    auth["jwt_key_id"] = kid;
    saveAuth(auth);

    // Register public key with existing legacy token
    auto *regReply = plexPostJson(QUrl(PLEX_TV + "/api/v2/auth/jwk"), token, jwkBody);
    connect(regReply, &QNetworkReply::finished, this, [this, regReply, callback]() {
        regReply->deleteLater();
        int status = regReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (regReply->error() != QNetworkReply::NoError && status != 422) {
            // 422 = key already registered (e.g. retried) — still proceed with refresh
            qWarning("[PlexBackend] Migration: JWK registration failed (%d), will retry next launch",
                     status);
            callback();
            return;
        }
        qDebug("[PlexBackend] Migration: JWK registered, fetching first JWT");
        refreshJwt([callback](bool) { callback(); });
    });
}

// ---------------------------------------------------------------------------
// 498 retry helper
// ---------------------------------------------------------------------------

void PlexBackend::handle498(std::function<void()> retryOp) {
    if (m_refreshInFlight) {
        // A refresh is already running; once it finishes it will call the retry
        // via the original invoker. This call is a no-op to avoid stacking retries.
        return;
    }
    refreshJwt([this, retryOp](bool ok) {
        if (ok) retryOp();
        else emit authRevoked();
    });
}

// ---------------------------------------------------------------------------
// Auth state accessors
// ---------------------------------------------------------------------------

QString PlexBackend::serverUrl() const {
    return loadAuth()["active_server_uri"].toString();
}

QString PlexBackend::serverToken() const {
    QJsonObject auth = loadAuth();
    QString mid = auth["active_server_machine_id"].toString();
    if (mid.isEmpty()) return accountToken();

    // Managed user: their per-server token takes precedence
    QJsonObject managed = auth["managed_user_tokens"].toObject();
    if (!managed.isEmpty() && managed.contains(mid))
        return managed[mid].toString();

    // Owner: pre-swapped token stored at activateUser time
    QJsonObject ust = auth["user_server_tokens"].toObject();
    if (ust.contains(mid))
        return ust[mid].toString();

    return accountToken();
}

bool PlexBackend::isAccountOwner(const QString &userId) const {
    QString aid = loadAuth()["account_user_id"].toString();
    return !aid.isEmpty() && aid == userId;
}

// Single consolidated user-switch path.  All three public callers (select_user,
// reauth_select_user, applyCurrentUserSetting) delegate here and supply a callback
// that handles the caller-specific signal and any post-switch state writes.
void PlexBackend::activateUser(const QString &userId,
                                std::function<void(const QVariantList &)> callback) {
    QJsonObject auth = loadAuth();
    QJsonObject user;
    for (const auto &v : auth["users"].toArray()) {
        if (v.toObject()["id"].toString() == userId) { user = v.toObject(); break; }
    }
    if (user.isEmpty()) { emit errorOccurred("USER NOT FOUND"); return; }

    QString switchKey = user["uuid"].toString();
    if (switchKey.isEmpty()) switchKey = userId;
    QString masterToken = accountToken();
    bool owner = isAccountOwner(userId);

    auto *switchReply = plexPost(
        QUrl(PLEX_TV + "/api/v2/home/users/" + switchKey + "/switch"), masterToken);
    connect(switchReply, &QNetworkReply::finished, this,
            [this, switchReply, userId, masterToken, owner, callback]() mutable {
        switchReply->deleteLater();
        QString switchToken = masterToken;
        if (switchReply->error() == QNetworkReply::NoError) {
            QString t = QJsonDocument::fromJson(switchReply->readAll()).object()["authToken"].toString();
            if (!t.isEmpty()) switchToken = t;
        }

        QUrl resUrl(PLEX_TV + "/api/v2/resources");
        QUrlQuery q;
        q.addQueryItem("includeHttps", "1");
        q.addQueryItem("includeRelay", "1");
        q.addQueryItem("includeIPv6", "1");
        resUrl.setQuery(q);
        auto *resReply = plexGet(resUrl, owner ? masterToken : switchToken);
        connect(resReply, &QNetworkReply::finished, this,
                [this, resReply, userId, masterToken, switchToken, owner, callback]() {
            resReply->deleteLater();

            QJsonObject tokenMap;
            if (resReply->error() == QNetworkReply::NoError) {
                for (const auto &rv : QJsonDocument::fromJson(resReply->readAll()).array()) {
                    QJsonObject r = rv.toObject();
                    if (!r["provides"].toString().contains("server")) continue;
                    QString mid = r["clientIdentifier"].toString();
                    QString at  = r["accessToken"].toString();
                    if (!mid.isEmpty() && !at.isEmpty()) tokenMap[mid] = at;
                }
            }

            QJsonObject a = loadAuth();
            a["active_user_id"]    = userId;
            a["active_user_token"] = masterToken; // always master JWT for plex.tv API

            if (owner) {
                // Swap JWT-bearing entries for the legacy switch token so PMS accepts them
                if (!switchToken.isEmpty() && switchToken != masterToken) {
                    for (auto it = tokenMap.begin(); it != tokenMap.end(); ++it) {
                        if (it.value().toString().startsWith(QLatin1String("eyJ")))
                            tokenMap[it.key()] = switchToken;
                    }
                }
                if (!tokenMap.isEmpty())
                    a["user_server_tokens"] = tokenMap;
                a["managed_user_tokens"] = QJsonObject{}; // cleared on owner switch
            } else {
                // Managed user: store their tokens; leave owner's user_server_tokens intact
                if (!tokenMap.isEmpty())
                    a["managed_user_tokens"] = tokenMap;
                // else: preserve existing managed_user_tokens if fetch failed
            }

            // Build the accessible server list for this user
            QJsonArray allServers = a["servers"].toArray();
            QJsonObject effectiveManaged = a["managed_user_tokens"].toObject();
            QVariantList accessible;
            if (!owner && !effectiveManaged.isEmpty()) {
                for (const auto &sv : allServers) {
                    QJsonObject s = sv.toObject();
                    if (effectiveManaged.contains(s["machineId"].toString()))
                        accessible.append(s.toVariantMap());
                }
            } else {
                for (const auto &sv : allServers) accessible.append(sv.toObject().toVariantMap());
            }

            saveAuth(a);
            callback(accessible);
        });
    });
}

QString PlexBackend::accountToken() const {
    return loadAuth()["auth_token"].toString();
}

QString PlexBackend::userToken() const {
    QJsonObject auth = loadAuth();
    QString t = auth["active_user_token"].toString();
    return t.isEmpty() ? auth["auth_token"].toString() : t;
}

QString PlexBackend::videoQuality() const {
    QJsonObject cfg = loadConfig();
    return cfg["modules"].toObject()["com.240mp.plex"].toObject()["video_quality"].toString("auto");
}

// ---------------------------------------------------------------------------
// Item formatting
// ---------------------------------------------------------------------------

QString PlexBackend::msToDisplay(int ms) {
    if (ms <= 0) return QStringLiteral("0MIN");
    int totalMin = ms / 60000;
    int hours = totalMin / 60;
    int mins  = totalMin % 60;
    if (hours > 0)
        return QStringLiteral("%1HR:%2MIN").arg(hours).arg(mins, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1MIN").arg(mins);
}

QVariantMap PlexBackend::formatItem(const QJsonObject &m) const {
    return QVariantMap{
        {"ratingKey",              m["ratingKey"].toString()},
        {"title",                  m["title"].toString().toUpper()},
        {"editionTitle",           m["editionTitle"].toString()},
        {"year",                   m["year"].toVariant()},
        {"duration",               m["duration"].toInt()},
        {"viewOffset",             m["viewOffset"].toInt()},
        {"viewCount",              m["viewCount"].toInt()},
        {"summary",                m["summary"].toString()},
        {"type",                   m["type"].toString("movie")},
        {"durationDisplay",        msToDisplay(m["duration"].toInt())},
        {"grandparentTitle",       m["grandparentTitle"].toString()},
        {"parentTitle",            m["parentTitle"].toString()},
        {"index",                  m["index"].toInt()},
        {"parentIndex",            m["parentIndex"].toInt()},
        {"leafCount",              m["leafCount"].toInt()},
        {"viewedLeafCount",        m["viewedLeafCount"].toInt()},
        {"originallyAvailableAt",  m["originallyAvailableAt"].toString()},
    };
}

void PlexBackend::flattenSeasons(const QVariantList &rawItems,
                                  std::function<void(const QVariantList &)> callback) {
    int seasonCount = 0;
    for (const auto &v : rawItems) {
        if (v.toMap()["type"].toString() == "season") seasonCount++;
    }
    if (seasonCount == 0) { callback(rawItems); return; }

    // One slot per raw item; non-seasons fill their slot immediately,
    // seasons fill theirs once their children arrive.
    auto *itemSlots = new QList<QVariantList>(rawItems.size());
    auto *pending   = new int(seasonCount);

    for (int i = 0; i < rawItems.size(); i++) {
        QVariantMap item = rawItems[i].toMap();
        if (item["type"].toString() == "season") {
            int idx = i;
            QString key = item["ratingKey"].toString();
            auto *reply = plexGet(
                QUrl(serverUrl() + "/library/metadata/" + key + "/children"), serverToken());
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply, itemSlots, pending, idx, callback]() {
                reply->deleteLater();
                QVariantList eps;
                if (reply->error() == QNetworkReply::NoError) {
                    QJsonArray meta = QJsonDocument::fromJson(reply->readAll())
                                     .object()["MediaContainer"].toObject()["Metadata"].toArray();
                    for (const auto &mv : meta) eps.append(formatItem(mv.toObject()));
                }
                (*itemSlots)[idx] = eps;
                if (--(*pending) == 0) {
                    QVariantList result;
                    for (const auto &bucket : *itemSlots) result.append(bucket);
                    callback(result);
                    delete itemSlots;
                    delete pending;
                }
            });
        } else {
            (*itemSlots)[i] = {rawItems[i]};
        }
    }
}

// ---------------------------------------------------------------------------
// Sync Q_INVOKABLE slots
// ---------------------------------------------------------------------------

void PlexBackend::reset_device_check() {
    m_deviceVerified = false;
}

QString PlexBackend::get_auth_state() {
    QJsonObject auth = loadAuth();
    if (auth["auth_token"].toString().isEmpty()) return QStringLiteral("none");
    if (auth["active_server_uri"].toString().isEmpty()) return QStringLiteral("needs_user");
    return QStringLiteral("authed");
}

QString PlexBackend::get_active_user_name() {
    QJsonObject auth = loadAuth();
    QString uid = auth["active_user_id"].toString();
    for (const auto &v : auth["users"].toArray()) {
        QJsonObject u = v.toObject();
        if (u["id"].toString() == uid)
            return u["title"].toString();
    }
    return {};
}

QString PlexBackend::get_active_server_name() {
    QJsonObject auth = loadAuth();
    QString mid = auth["active_server_machine_id"].toString();
    for (const auto &v : auth["servers"].toArray()) {
        QJsonObject s = v.toObject();
        if (s["machineId"].toString() == mid)
            return s["name"].toString();
    }
    return {};
}

void PlexBackend::build_stream_url(const QString &ratingKey,
                                   const QString &partKey,
                                   const QString &sessionId) {
    QString uri = serverUrl();
    QString token = serverToken();
    QString url = uri + partKey
                + "?X-Plex-Client-Identifier="  + clientId()
                + "&X-Plex-Session-Identifier=" + sessionId;
    qDebug() << "[Plex] Playback: DIRECT PLAY";
    emit streamUrlReady(url, token);
}

// ---------------------------------------------------------------------------
// PIN auth
// ---------------------------------------------------------------------------

void PlexBackend::start_pin_auth() {
    // Generate a fresh ED25519 key pair for this registration
    QString kid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray body = generateAndSaveKeyPair(kid);

    // Persist the key ID so pollPinTick can find it
    QJsonObject auth = loadAuth();
    auth["jwt_key_id"] = kid;
    saveAuth(auth);

    QUrl url(PLEX_TV + "/api/v2/pins");
    auto *reply = plexPostJson(url, {}, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("PIN REQUEST FAILED: " + reply->errorString());
            return;
        }
        QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        m_pendingPinId = QString::number(data["id"].toInt());
        QString code   = data["code"].toString();
        emit pinReady(code, m_pendingPinId);
        m_pollTimer->start();
    });
}

void PlexBackend::pollPinTick() {
    if (m_pendingPinId.isEmpty()) { m_pollTimer->stop(); return; }

    QJsonObject auth = loadAuth();
    QString kid = auth["jwt_key_id"].toString();
    EVP_PKEY *pkey = loadPrivateKey();

    QUrl url(PLEX_TV + "/api/v2/pins/" + m_pendingPinId);
    if (pkey && !kid.isEmpty()) {
        // JWT flow: sign a device JWT and include it as a query param
        QString deviceJwt = buildDeviceJwt(pkey, kid);
        EVP_PKEY_free(pkey);
        QUrlQuery q;
        q.addQueryItem("deviceJWT", deviceJwt);
        url.setQuery(q);
    } else {
        if (pkey) EVP_PKEY_free(pkey);
    }

    auto *reply = plexGet(url, {});
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return; // silent — keep polling
        QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        QString token = data["authToken"].toString();
        if (token.isEmpty()) return;
        m_pollTimer->stop();
        m_pendingPinId.clear();
        QJsonObject a = loadAuth();
        a["auth_token"] = token;
        qint64 exp = jwtExpClaim(token);
        if (exp > 0) a["jwt_exp"] = exp;
        QString uid = jwtUserIdClaim(token);
        if (!uid.isEmpty()) a["account_user_id"] = uid;
        saveAuth(a);
        fetchUsersAndServers(token);
    });
}

// ---------------------------------------------------------------------------
// fetchUsersAndServers — called after PIN auth succeeds
// ---------------------------------------------------------------------------

void PlexBackend::fetchUsersAndServers(const QString &token) {
    // Step 1: fetch home users
    QUrl usersUrl(PLEX_TV + "/api/v2/home/users");
    auto *usersReply = plexGet(usersUrl, token);
    connect(usersReply, &QNetworkReply::finished, this, [this, token, usersReply]() {
        usersReply->deleteLater();
        if (usersReply->error() != QNetworkReply::NoError) {
            emit errorOccurred("FAILED TO LOAD ACCOUNT: " + usersReply->errorString());
            return;
        }
        QJsonObject usersData = QJsonDocument::fromJson(usersReply->readAll()).object();
        QJsonArray rawUsers = usersData["users"].toArray();
        QJsonArray users;
        for (const auto &v : rawUsers) {
            QJsonObject u = v.toObject();
            QJsonObject entry;
            entry["id"]      = QString::number(u["id"].toInt());
            entry["uuid"]    = u["uuid"].toString();
            entry["title"]   = u["title"].toString(u["username"].toString()).toUpper();
            entry["managed"] = u["managed"].toBool();
            users.append(entry);
        }

        // Step 2: fetch resources (servers)
        QUrl resUrl(PLEX_TV + "/api/v2/resources");
        QUrlQuery q;
        q.addQueryItem("includeHttps", "1");
        q.addQueryItem("includeRelay", "1");
        q.addQueryItem("includeIPv6", "1");
        resUrl.setQuery(q);
        auto *resReply = plexGet(resUrl, token);
        connect(resReply, &QNetworkReply::finished, this, [this, users, resReply, token]() {
            resReply->deleteLater();
            if (resReply->error() != QNetworkReply::NoError) {
                // Still emit users even if resource fetch fails
                QVariantList ul;
                for (const auto &v : users) ul.append(v.toObject().toVariantMap());
                emit usersLoaded(ul);
                return;
            }
            QJsonArray resources = QJsonDocument::fromJson(resReply->readAll()).array();

            // Collect all connections to probe
            struct ServerInfo {
                QString clientIdentifier;
                QString name;
                QJsonArray connections;
                QString accessToken;
            };
            QList<ServerInfo> pending;
            for (const auto &rv : resources) {
                QJsonObject res = rv.toObject();
                if (!res["provides"].toString().contains("server")) continue;
                pending.append({
                    res["clientIdentifier"].toString(),
                    res["name"].toString().toUpper(),
                    res["connections"].toArray(),
                    res["accessToken"].toString(token)
                });
            }

            // Probe servers sequentially, build list, then save + emit
            auto *serverList = new QJsonArray();
            auto *remaining  = new int(pending.size());

            if (pending.isEmpty()) {
                QJsonObject auth = loadAuth();
                auth["users"]   = users;
                auth["servers"] = QJsonArray{};
                saveAuth(auth);
                QVariantList ul;
                for (const auto &v : users) ul.append(v.toObject().toVariantMap());
                emit usersLoaded(ul);
                delete serverList;
                delete remaining;
                return;
            }

            // Collect per-server access tokens for all discovered servers so
            // serverToken() can use the right token for each server (critical
            // for shared/friend servers whose token differs from the account JWT).
            auto *allServerTokens = new QJsonObject();
            for (const auto &si : pending) {
                if (!si.accessToken.isEmpty())
                    (*allServerTokens)[si.clientIdentifier] = si.accessToken;
            }

            for (const auto &si : pending) {
                probeConnections(si.connections, [this, si, serverList, remaining, users, allServerTokens](QString uri) {
                    bool isLocal = !uri.isEmpty(); // probe only tries local; non-empty = LAN
                    bool isRelay = false;
                    if (uri.isEmpty()) {
                        // No local connection found. Prefer relay (Plex's own proxy,
                        // works for both API calls and file streaming from WAN).
                        for (const auto &cv : si.connections) {
                            QJsonObject c = cv.toObject();
                            if (c["relay"].toBool()) { uri = c["uri"].toString(); isRelay = true; break; }
                        }
                        // Owned servers often have no relay entry — fall back to a
                        // WAN IPv4 direct URL so the server still appears in the list.
                        // Skip IPv6-encoded plex.direct addresses (ULA range, local-only):
                        // IPv4 hostnames contain only digits+dashes; IPv6 contain hex letters.
                        if (uri.isEmpty()) {
                            for (const auto &cv : si.connections) {
                                QJsonObject c = cv.toObject();
                                if (c["relay"].toBool() || c["local"].toBool()) continue;
                                QString host = QUrl(c["uri"].toString()).host();
                                QString encoded = host.left(host.indexOf('.'));
                                bool isIpv6 = std::any_of(encoded.cbegin(), encoded.cend(),
                                                           [](QChar ch){ return ch.isLetter(); });
                                if (!isIpv6) { uri = c["uri"].toString(); break; }
                            }
                        }
                    }
                    if (!uri.isEmpty()) {
                        QJsonObject s;
                        s["machineId"] = si.clientIdentifier;
                        s["name"]      = si.name;
                        s["uri"]       = uri;
                        s["local"]     = isLocal;
                        s["relay"]     = isRelay;
                        serverList->append(s);
                    }
                    (*remaining)--;
                    if (*remaining == 0) {
                        QJsonObject auth = loadAuth();
                        auth["users"]   = users;
                        auth["servers"] = *serverList;
                        if (!allServerTokens->isEmpty())
                            auth["user_server_tokens"] = *allServerTokens;
                        saveAuth(auth);
                        QVariantList ul;
                        for (const auto &v : users) ul.append(v.toObject().toVariantMap());
                        emit usersLoaded(ul);
                        delete serverList;
                        delete remaining;
                        delete allServerTokens;
                    }
                });
            }
        });
    });
}

// ---------------------------------------------------------------------------
// Connection probing
// ---------------------------------------------------------------------------

void PlexBackend::probeConnections(const QJsonArray &connections,
                                   std::function<void(QString)> callback) {
    // Priority: local → remote (WAN direct) → relay.
    // Relay is last because it adds relay-server latency and not all PMS instances
    // support relay-based direct play. Remote WAN direct is tried before relay so
    // servers with proper external connectivity (NEWMINIFLIX etc.) get their best URL.
    // If direct play fails at playback time, Player.qml retries with transcode.
    QList<QString> uris;
    QList<QString> remote, relay;
    for (const auto &cv : connections) {
        QJsonObject c = cv.toObject();
        QString uri = c["uri"].toString();
        if (uri.isEmpty()) continue;
        if (!c["relay"].toBool() && c["local"].toBool())
            uris.append(uri);
        else if (c["relay"].toBool())
            relay.append(uri);
        else
            remote.append(uri);
    }
    uris += remote;
    uris += relay;

    if (uris.isEmpty()) { callback({}); return; }
    probeNext(uris, 0, callback);
}

void PlexBackend::probeNext(const QList<QString> &uris, int index,
                            std::function<void(QString)> callback) {
    if (index >= uris.size()) { callback({}); return; }
    QString uri = uris[index];
    auto *reply = plexGet(QUrl(uri + "/"), {});

    // 3-second timeout per probe
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(3000);
    connect(timer, &QTimer::timeout, this, [reply, timer]() {
        timer->deleteLater();
        reply->abort();
    });
    timer->start();

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, timer, uris, index, uri, callback]() {
        timer->stop();
        timer->deleteLater();
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError ||
            reply->error() == QNetworkReply::AuthenticationRequiredError) {
            // Any HTTP response means the host is reachable
            callback(uri);
        } else if (reply->error() == QNetworkReply::OperationCanceledError) {
            // Timed out — try next
            probeNext(uris, index + 1, callback);
        } else {
            probeNext(uris, index + 1, callback);
        }
    });
}

// ---------------------------------------------------------------------------
// load_users_from_cache
// ---------------------------------------------------------------------------

void PlexBackend::load_users_from_cache() {
    QJsonObject auth = loadAuth();
    QVariantList users;
    for (const auto &v : auth["users"].toArray())
        users.append(v.toObject().toVariantMap());
    emit usersLoaded(users);
}

// ---------------------------------------------------------------------------
// select_user
// ---------------------------------------------------------------------------

void PlexBackend::select_user(const QString &userId) {
    activateUser(userId, [this, userId](const QVariantList &accessibleServers) {
        QJsonObject cfg = loadConfig();
        QJsonObject mods = cfg["modules"].toObject();
        QJsonObject plexCfg = mods["com.240mp.plex"].toObject();
        plexCfg["current_user_id"] = userId;
        mods["com.240mp.plex"] = plexCfg;
        cfg["modules"] = mods;
        saveConfig(cfg);
        emit serversLoaded(accessibleServers);
    });
}

// ---------------------------------------------------------------------------
// select_server
// ---------------------------------------------------------------------------

void PlexBackend::select_server(const QString &machineId) {
    QJsonObject auth = loadAuth();
    QJsonObject server;
    for (const auto &v : auth["servers"].toArray()) {
        QJsonObject s = v.toObject();
        if (s["machineId"].toString() == machineId) { server = s; break; }
    }
    if (server.isEmpty()) { emit errorOccurred("SERVER NOT FOUND"); return; }

    auth["active_server_uri"]        = server["uri"].toString();
    auth["active_server_machine_id"] = machineId;
    saveAuth(auth);

    QJsonObject cfg = loadConfig();
    QJsonObject mods = cfg["modules"].toObject();
    QJsonObject plexCfg = mods["com.240mp.plex"].toObject();
    plexCfg["server_machine_id"] = machineId;
    mods["com.240mp.plex"] = plexCfg;
    cfg["modules"] = mods;
    saveConfig(cfg);

    emit authSuccess();
}

// ---------------------------------------------------------------------------
// logout
// ---------------------------------------------------------------------------

void PlexBackend::logout() {
    QString token = accountToken();
    auto finish = [this]() {
        QFile::remove(m_dataRoot + "/plex_auth.json");
        QFile::remove(m_dataRoot + "/plex_key.pem");
        m_clientId.clear();
        m_deviceVerified = false;
        emit logoutComplete();
        emit authStateChanged();
    };
    if (token.isEmpty()) { finish(); return; }
    deleteDeviceThenAuth(token, finish);
}

void PlexBackend::deleteDeviceThenAuth(const QString &token, std::function<void()> finish) {
    // Fetch the device list to find our device's numeric Plex ID (not the UUID)
    auto *listReply = plexGet(QUrl(PLEX_TV + "/api/v2/devices"), token);
    connect(listReply, &QNetworkReply::finished, this,
            [this, listReply, token, finish]() {
        listReply->deleteLater();

        // No JWT-era self-deregistration endpoint exists yet in Plex's public API.
        // DELETE /api/v2/devices/{uuid} → 405, DELETE /api/v2/auth/jwk/{kid} → 404.
        // The legacy XML path returns 200 but only removes the legacy device record,
        // not the JWT registration visible in app.plex.tv. We fire it anyway as a
        // best-effort cleanup. The device is functionally dead after logout regardless:
        // plex_key.pem is deleted so it can never sign a refresh JWT.
        QString numericId;
        if (listReply->error() == QNetworkReply::NoError) {
            QJsonArray devices = QJsonDocument::fromJson(listReply->readAll()).array();
            QString ourId = clientId();
            for (const auto &dv : devices) {
                QJsonObject d = dv.toObject();
                if (d["clientIdentifier"].toString() == ourId) {
                    numericId = QString::number(d["id"].toInt());
                    break;
                }
            }
        }

        if (numericId.isEmpty()) { finish(); return; }

        auto *devReply = plexDelete(QUrl(PLEX_TV + "/devices/" + numericId + ".xml"), token);
        connect(devReply, &QNetworkReply::finished, this, [devReply, finish]() {
            devReply->deleteLater();
            finish();
        });
    });
}

// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------

void PlexBackend::load_libraries() {
    checkAndRefreshOnStartup([this]() {
        load_libraries_impl();
    });
}

void PlexBackend::load_libraries_impl() {
    QString uri   = serverUrl();
    QString token = serverToken();
    if (uri.isEmpty()) { emit errorOccurred("NO SERVER CONFIGURED"); return; }

    // Check continue watching
    QUrl cwUrl(uri + "/hubs/continueWatching");
    QUrlQuery cwq; cwq.addQueryItem("limit","1");
    cwUrl.setQuery(cwq);
    auto *cwReply = plexGet(cwUrl, token);
    connect(cwReply, &QNetworkReply::finished, this, [this, cwReply, uri, token]() {
        cwReply->deleteLater();
        bool hasCw = false;
        if (cwReply->error() == QNetworkReply::NoError) {
            QJsonArray hubs = QJsonDocument::fromJson(cwReply->readAll())
                              .object()["MediaContainer"].toObject()["Hub"].toArray();
            hasCw = !hubs.isEmpty() && !hubs[0].toObject()["Metadata"].toArray().isEmpty();
        }

        auto *secReply = plexGet(QUrl(uri + "/library/sections"), token);
        connect(secReply, &QNetworkReply::finished, this, [this, secReply, hasCw]() {
            secReply->deleteLater();
            int secStatus = secReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (secReply->error() != QNetworkReply::NoError) {
                if (secStatus == 498) {
                    handle498([this]{ load_libraries_impl(); });
                } else if (secStatus == 401) {
                    // 401 from PMS means this server rejected our token — the plex.tv account
                    // auth is NOT invalidated. Do not delete plex_auth.json. The server may be
                    // running an older version that doesn't accept JWTs yet; the user should
                    // try selecting a different server or update their Plex Media Server.
                    emit errorOccurred("SERVER AUTHENTICATION FAILED. TRY SELECTING A DIFFERENT SERVER OR UPDATE YOUR PLEX MEDIA SERVER.");
                } else {
                    emit errorOccurred("LOAD LIBRARIES FAILED: " + secReply->errorString());
                }
                return;
            }
            QJsonArray sections = QJsonDocument::fromJson(secReply->readAll())
                                  .object()["MediaContainer"].toObject()["Directory"].toArray();

            QJsonObject auth = loadAuth();
            QString machineId = auth["active_server_machine_id"].toString();
            QJsonObject libEnabled = loadConfig()["modules"].toObject()
                                     ["com.240mp.plex"].toObject()["libraries"].toObject();

            QVariantList items;
            if (hasCw)
                items.append(QVariantMap{{"key","continue_watching"},{"title","CONTINUE WATCHING"},
                                         {"sectionId",QVariant()},{"sectionType",QVariant()}});

            for (const auto &sv : sections) {
                QJsonObject s = sv.toObject();
                if (!kSupportedLibraryTypes.contains(s["type"].toString())) continue;
                QString key = s["key"].toString();
                QString libKey = machineId.isEmpty() ? key : machineId + "_" + key;
                if (!libEnabled.isEmpty() && !libEnabled[libKey].toBool(true)) continue;
                items.append(QVariantMap{
                    {"key",         key},
                    {"title",       s["title"].toString().toUpper()},
                    {"sectionId",   key},
                    {"sectionType", s["type"].toString()},
                });
            }
            emit librariesLoaded(items);
        });
    });
}

void PlexBackend::load_continue_watching() {
    QString uri = serverUrl(), token = serverToken();
    auto *reply = plexGet(QUrl(uri + "/hubs/continueWatching"), token);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this]{ load_continue_watching(); }); return;
            }
            emit errorOccurred("CONTINUE WATCHING FAILED: " + reply->errorString()); return;
        }
        QJsonArray hubs = QJsonDocument::fromJson(reply->readAll())
                          .object()["MediaContainer"].toObject()["Hub"].toArray();
        QVariantList items;
        for (const auto &hv : hubs)
            for (const auto &mv : hv.toObject()["Metadata"].toArray())
                items.append(formatItem(mv.toObject()));
        flattenSeasons(items, [this](const QVariantList &flat) { emit continueWatchingLoaded(flat); });
    });
}

void PlexBackend::load_section_hubs(const QString &sectionId) {
    QString uri = serverUrl(), token = serverToken();
    QUrl url(uri + "/hubs/sections/" + sectionId);
    QUrlQuery q; q.addQueryItem("count","20"); url.setQuery(q);
    auto *reply = plexGet(url, token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, sectionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, sectionId]{ load_section_hubs(sectionId); }); return;
            }
            emit errorOccurred("LOAD HUBS FAILED: " + reply->errorString()); return;
        }
        QJsonArray hubs = QJsonDocument::fromJson(reply->readAll())
                          .object()["MediaContainer"].toObject()["Hub"].toArray();
        QVariantList result;
        for (const auto &hv : hubs) {
            QJsonObject h = hv.toObject();
            if (h["Metadata"].toArray().isEmpty() && h["size"].toInt() == 0) continue;
            result.append(QVariantMap{
                {"title",   h["title"].toString().toUpper()},
                {"key",     h["key"].toString()},
                {"hubKey",  h["hubKey"].toString()},
            });
        }
        emit hubsLoaded(result);
    });
}

void PlexBackend::load_items_for_hub(const QString &hubKey) {
    QString uri = serverUrl(), token = serverToken();
    QUrl url = hubKey.startsWith("http") ? QUrl(hubKey) : QUrl(uri + hubKey);
    auto *reply = plexGet(url, token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, hubKey]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, hubKey]{ load_items_for_hub(hubKey); }); return;
            }
            emit errorOccurred("LOAD HUB ITEMS FAILED: " + reply->errorString()); return;
        }
        QJsonObject container = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonObject mc = container["MediaContainer"].toObject();
        QJsonArray metadata = mc.isEmpty() ? container["Metadata"].toArray() : mc["Metadata"].toArray();
        QVariantList items;
        for (const auto &mv : metadata) items.append(formatItem(mv.toObject()));
        flattenSeasons(items, [this](const QVariantList &flat) { emit itemsLoaded(flat); });
    });
}

void PlexBackend::load_library_all(const QString &sectionId) {
    QString uri = serverUrl(), token = serverToken();
    QUrl url(uri + "/library/sections/" + sectionId + "/all");
    QUrlQuery q; q.addQueryItem("sort","titleSort"); url.setQuery(q);
    auto *reply = plexGet(url, token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, sectionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, sectionId]{ load_library_all(sectionId); }); return;
            }
            emit errorOccurred("LOAD LIBRARY FAILED: " + reply->errorString()); return;
        }
        QJsonArray metadata = QJsonDocument::fromJson(reply->readAll())
                              .object()["MediaContainer"].toObject()["Metadata"].toArray();
        QVariantList items;
        for (const auto &mv : metadata) items.append(formatItem(mv.toObject()));
        flattenSeasons(items, [this](const QVariantList &flat) { emit itemsLoaded(flat); });
    });
}

void PlexBackend::load_collections(const QString &sectionId) {
    QString uri = serverUrl(), token = serverToken();
    auto *reply = plexGet(QUrl(uri + "/library/sections/" + sectionId + "/collections"), token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, sectionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, sectionId]{ load_collections(sectionId); }); return;
            }
            emit errorOccurred("LOAD COLLECTIONS FAILED: " + reply->errorString()); return;
        }
        QJsonArray metadata = QJsonDocument::fromJson(reply->readAll())
                              .object()["MediaContainer"].toObject()["Metadata"].toArray();
        QVariantList items;
        for (const auto &mv : metadata) {
            QJsonObject m = mv.toObject();
            items.append(QVariantMap{{"ratingKey",m["ratingKey"].toString()},
                                     {"title",m["title"].toString().toUpper()},{"type","collection"}});
        }
        emit collectionsLoaded(items);
    });
}

void PlexBackend::load_collection_items(const QString &ratingKey) {
    QString uri = serverUrl(), token = serverToken();
    auto *reply = plexGet(QUrl(uri + "/library/collections/" + ratingKey + "/items"), token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ratingKey]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, ratingKey]{ load_collection_items(ratingKey); }); return;
            }
            emit errorOccurred("LOAD COLLECTION ITEMS FAILED: " + reply->errorString()); return;
        }
        QJsonArray metadata = QJsonDocument::fromJson(reply->readAll())
                              .object()["MediaContainer"].toObject()["Metadata"].toArray();
        QVariantList items;
        for (const auto &mv : metadata) items.append(formatItem(mv.toObject()));
        flattenSeasons(items, [this](const QVariantList &flat) { emit itemsLoaded(flat); });
    });
}

void PlexBackend::load_playlists(const QString &sectionId) {
    QString uri = serverUrl(), token = serverToken();
    QUrl url(uri + "/playlists");
    QUrlQuery q; q.addQueryItem("sectionID",sectionId); q.addQueryItem("playlistType","video"); url.setQuery(q);
    auto *reply = plexGet(url, token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, sectionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, sectionId]{ load_playlists(sectionId); }); return;
            }
            emit errorOccurred("LOAD PLAYLISTS FAILED: " + reply->errorString()); return;
        }
        QJsonArray metadata = QJsonDocument::fromJson(reply->readAll())
                              .object()["MediaContainer"].toObject()["Metadata"].toArray();
        QVariantList items;
        for (const auto &mv : metadata) {
            QJsonObject m = mv.toObject();
            items.append(QVariantMap{{"ratingKey",m["ratingKey"].toString()},
                                     {"title",m["title"].toString().toUpper()},{"type","playlist"}});
        }
        emit playlistsLoaded(items);
    });
}

void PlexBackend::load_playlist_items(const QString &ratingKey) {
    QString uri = serverUrl(), token = serverToken();
    auto *reply = plexGet(QUrl(uri + "/playlists/" + ratingKey + "/items"), token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ratingKey]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, ratingKey]{ load_playlist_items(ratingKey); }); return;
            }
            emit errorOccurred("LOAD PLAYLIST ITEMS FAILED: " + reply->errorString()); return;
        }
        QJsonArray metadata = QJsonDocument::fromJson(reply->readAll())
                              .object()["MediaContainer"].toObject()["Metadata"].toArray();
        QVariantList items;
        for (const auto &mv : metadata) items.append(formatItem(mv.toObject()));
        flattenSeasons(items, [this](const QVariantList &flat) { emit itemsLoaded(flat); });
    });
}

void PlexBackend::load_categories(const QString &sectionId) {
    QString uri = serverUrl(), token = serverToken();
    auto *reply = plexGet(QUrl(uri + "/library/sections/" + sectionId + "/filters"), token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, sectionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, sectionId]{ load_categories(sectionId); }); return;
            }
            emit errorOccurred("LOAD CATEGORIES FAILED: " + reply->errorString()); return;
        }
        QJsonArray dirs = QJsonDocument::fromJson(reply->readAll())
                          .object()["MediaContainer"].toObject()["Directory"].toArray();
        QVariantList items;
        for (const auto &fv : dirs) {
            QJsonObject f = fv.toObject();
            items.append(QVariantMap{
                {"key",        f["filter"].toString()},
                {"title",      f["title"].toString().toUpper()},
                {"sectionId",  sectionId},
                {"filterType", f["filterType"].toString("string")},
            });
        }
        emit categoriesLoaded(items);
    });
}

void PlexBackend::load_category_items(const QString &sectionId, const QString &filterKey) {
    QString uri = serverUrl(), token = serverToken();
    if (filterKey.contains('=')) {
        // Filtered items: /library/sections/{id}/all?genre=50553
        QStringList parts = filterKey.split('=', Qt::KeepEmptyParts);
        QUrl url(uri + "/library/sections/" + sectionId + "/all");
        QUrlQuery q; q.addQueryItem(parts[0], parts[1]); q.addQueryItem("sort","titleSort"); url.setQuery(q);
        auto *reply = plexGet(url, token);
        connect(reply, &QNetworkReply::finished, this, [this, reply, sectionId, filterKey]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                    handle498([this, sectionId, filterKey]{ load_category_items(sectionId, filterKey); }); return;
                }
                emit errorOccurred("LOAD CATEGORY ITEMS FAILED: " + reply->errorString()); return;
            }
            QJsonArray metadata = QJsonDocument::fromJson(reply->readAll())
                                  .object()["MediaContainer"].toObject()["Metadata"].toArray();
            QVariantList items;
            for (const auto &mv : metadata) items.append(formatItem(mv.toObject()));
            flattenSeasons(items, [this](const QVariantList &flat) { emit itemsLoaded(flat); });
        });
    } else {
        // Filter values: /library/sections/{id}/genre
        auto *reply = plexGet(QUrl(uri + "/library/sections/" + sectionId + "/" + filterKey), token);
        connect(reply, &QNetworkReply::finished, this, [this, reply, filterKey, sectionId]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                    handle498([this, sectionId, filterKey]{ load_category_items(sectionId, filterKey); }); return;
                }
                emit errorOccurred("LOAD CATEGORY ITEMS FAILED: " + reply->errorString()); return;
            }
            QJsonArray dirs = QJsonDocument::fromJson(reply->readAll())
                              .object()["MediaContainer"].toObject()["Directory"].toArray();
            QVariantList items;
            for (const auto &dv : dirs) {
                QJsonObject d = dv.toObject();
                items.append(QVariantMap{
                    {"ratingKey", d["key"].toString()},
                    {"title",     d["title"].toString().toUpper()},
                    {"year",      QVariant()},
                    {"duration",  QVariant()},
                    {"viewOffset",0},
                    {"summary",   QString{}},
                    {"type",      "genre_item"},
                    {"_filterKey",filterKey},
                    {"_sectionId",sectionId},
                });
            }
            emit itemsLoaded(items);
        });
    }
}

void PlexBackend::check_section_capabilities(const QString &sectionId) {
    QString uri = serverUrl(), token = serverToken();

    auto *caps = new QVariantMap{{"recommended",false},{"collections",false},{"playlists",false}};
    auto *remaining = new int(3);
    auto done = [this, caps, remaining]() {
        (*remaining)--;
        if (*remaining == 0) {
            emit capabilitiesLoaded(*caps);
            delete caps;
            delete remaining;
        }
    };

    // Hubs
    QUrl hubsUrl(uri + "/hubs/sections/" + sectionId);
    QUrlQuery hq; hq.addQueryItem("count","1"); hubsUrl.setQuery(hq);
    auto *hubReply = plexGet(hubsUrl, token);
    connect(hubReply, &QNetworkReply::finished, this, [this, hubReply, caps, done, sectionId]() {
        hubReply->deleteLater();
        if (hubReply->error() == QNetworkReply::NoError) {
            QJsonArray hubs = QJsonDocument::fromJson(hubReply->readAll())
                              .object()["MediaContainer"].toObject()["Hub"].toArray();
            (*caps)["recommended"] = !hubs.isEmpty();
        } else if (hubReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
            handle498([this, sectionId]{ check_section_capabilities(sectionId); });
        }
        done();
    });

    // Collections
    QUrl collUrl(uri + "/library/sections/" + sectionId + "/collections");
    QUrlQuery cq; cq.addQueryItem("X-Plex-Container-Size","0"); collUrl.setQuery(cq);
    auto *collReply = plexGet(collUrl, token);
    connect(collReply, &QNetworkReply::finished, this, [this, collReply, caps, done, sectionId]() {
        collReply->deleteLater();
        if (collReply->error() == QNetworkReply::NoError) {
            int total = QJsonDocument::fromJson(collReply->readAll())
                        .object()["MediaContainer"].toObject()["totalSize"].toInt();
            (*caps)["collections"] = total > 0;
        } else if (collReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
            handle498([this, sectionId]{ check_section_capabilities(sectionId); });
        }
        done();
    });

    // Playlists
    QUrl plUrl(uri + "/playlists");
    QUrlQuery pq; pq.addQueryItem("sectionID",sectionId); pq.addQueryItem("playlistType","video");
    pq.addQueryItem("X-Plex-Container-Size","0"); plUrl.setQuery(pq);
    auto *plReply = plexGet(plUrl, token);
    connect(plReply, &QNetworkReply::finished, this, [this, plReply, caps, done, sectionId]() {
        plReply->deleteLater();
        if (plReply->error() == QNetworkReply::NoError) {
            int total = QJsonDocument::fromJson(plReply->readAll())
                        .object()["MediaContainer"].toObject()["totalSize"].toInt();
            (*caps)["playlists"] = total > 0;
        } else if (plReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
            handle498([this, sectionId]{ check_section_capabilities(sectionId); });
        }
        done();
    });
}

// ---------------------------------------------------------------------------
// Item detail & playback
// ---------------------------------------------------------------------------

void PlexBackend::load_item_detail(const QString &ratingKey) {
    QString uri = serverUrl(), token = serverToken();
    auto *reply = plexGet(QUrl(uri + "/library/metadata/" + ratingKey), token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ratingKey, uri, token]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, ratingKey]{ load_item_detail(ratingKey); }); return;
            }
            emit errorOccurred("LOAD DETAIL FAILED: " + reply->errorString()); return;
        }
        QJsonArray metaArr = QJsonDocument::fromJson(reply->readAll())
                             .object()["MediaContainer"].toObject()["Metadata"].toArray();
        if (metaArr.isEmpty()) { emit errorOccurred("LOAD DETAIL FAILED: empty metadata"); return; }
        QJsonObject meta  = metaArr[0].toObject();
        QJsonObject media = meta["Media"].toArray().first().toObject();
        QJsonObject part  = media["Part"].toArray().first().toObject();
        QJsonArray streams = part["Stream"].toArray();

        static const QSet<QString> IMAGE_SUB_CODECS = {
            "pgssub","dvdsub","dvbsub","hdmv_pgs_subtitle","dvd_subtitle"
        };

        QVariantList audioStreams;
        QVariantList subtitleStreams;
        subtitleStreams.append(QVariantMap{{"id","0"},{"displayTitle","OFF"},
                                           {"selected",false},{"imageSubtitle",false}});
        QString selectedAudio, selectedSubtitle = "0";
        QString videoCodec;

        for (const auto &sv : streams) {
            QJsonObject s = sv.toObject();
            int st  = s["streamType"].toInt();
            QString sid   = QString::number(s["id"].toInt());
            QString title = s["displayTitle"].toString(s["language"].toString("UNKNOWN")).toUpper();
            QString codec = s["codec"].toString().toLower();
            if (st == 1) {
                videoCodec = codec;
            } else if (st == 2) {
                audioStreams.append(QVariantMap{{"id",sid},{"displayTitle",title}});
                if (s["selected"].toBool() && selectedAudio.isEmpty())
                    selectedAudio = sid;
            } else if (st == 3) {
                bool isImage = IMAGE_SUB_CODECS.contains(codec);
                QString subKey = s["key"].toString();
                QString subUrl = subKey.isEmpty() ? "" : uri + subKey;
                subtitleStreams.append(QVariantMap{{"id",sid},{"displayTitle",title},{"imageSubtitle",isImage},{"subUrl",subUrl}});
                if (s["selected"].toBool() && selectedSubtitle == "0")
                    selectedSubtitle = sid;
            }
        }
        if (selectedAudio.isEmpty() && !audioStreams.isEmpty())
            selectedAudio = audioStreams[0].toMap()["id"].toString();

        bool forceTranscode = (videoQuality() != "auto");
        qDebug() << "[Plex] Item detail loaded — codec:" << videoCodec
                 << "| quality:" << videoQuality()
                 << "| playback:" << (forceTranscode ? "transcode" : "direct play");
        int duration = meta["duration"].toInt();

        emit itemLoaded(QVariantMap{
            {"ratingKey",        ratingKey},
            {"title",            meta["title"].toString().toUpper()},
            {"editionTitle",     meta["editionTitle"].toString()},
            {"year",             meta["year"].toVariant()},
            {"duration",         duration},
            {"viewOffset",       meta["viewOffset"].toInt()},
            {"summary",          meta["summary"].toString()},
            {"partKey",          part["key"].toString()},
            {"partId",           QString::number(part["id"].toInt())},
            {"audioStreams",     audioStreams},
            {"subtitleStreams",  subtitleStreams},
            {"selectedAudioId",  selectedAudio},
            {"selectedSubtitleId", selectedSubtitle},
            {"durationDisplay",  msToDisplay(duration)},
            {"forceTranscode",   forceTranscode},
            {"type",             meta["type"].toString()},
            {"index",            meta["index"].toInt()},
            {"parentIndex",      meta["parentIndex"].toInt()},
            {"grandparentTitle", meta["grandparentTitle"].toString()},
            {"parentTitle",      meta["parentTitle"].toString()},
        });
    });
}

void PlexBackend::load_children(const QString &ratingKey) {
    QString uri = serverUrl(), token = serverToken();
    auto *reply = plexGet(QUrl(uri + "/library/metadata/" + ratingKey + "/children"), token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ratingKey]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, ratingKey]{ load_children(ratingKey); }); return;
            }
            emit errorOccurred("LOAD CHILDREN FAILED: " + reply->errorString()); return;
        }
        QJsonArray metadata = QJsonDocument::fromJson(reply->readAll())
                              .object()["MediaContainer"].toObject()["Metadata"].toArray();
        QVariantList items;
        for (const auto &mv : metadata) items.append(formatItem(mv.toObject()));
        emit childrenLoaded(items);
    });
}

void PlexBackend::load_on_deck_for(const QString &ratingKey) {
    QString uri = serverUrl(), token = serverToken();
    auto *reply = plexGet(QUrl(uri + "/library/metadata/" + ratingKey + "/onDeck"), token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ratingKey]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, ratingKey]{ load_on_deck_for(ratingKey); }); return;
            }
            emit inProgressEpisodeLoaded(QVariantMap{}); return;
        }
        QJsonArray metadata = QJsonDocument::fromJson(reply->readAll())
                              .object()["MediaContainer"].toObject()["Metadata"].toArray();
        for (const auto &mv : metadata) {
            QJsonObject m = mv.toObject();
            if (m["viewOffset"].toInt() > 0) {
                emit inProgressEpisodeLoaded(formatItem(m));
                return;
            }
        }
        emit inProgressEpisodeLoaded(QVariantMap{});
    });
}

void PlexBackend::request_transcode(const QString &ratingKey, const QString &partKey,
                                    const QString &sessionId,
                                    const QString &audioId, const QString &subtitleId,
                                    int offsetMs) {
    QString uri   = serverUrl();
    QString token = serverToken();
    QString quality = videoQuality();

    qDebug() << "[Plex] Playback: TRANSCODE — full re-encode, quality cap:" << quality << "kbps";
    QUrl url(uri + "/video/:/transcode/universal/start.m3u8");
    QUrlQuery q;
    q.addQueryItem("hasMDE",      "1");
    q.addQueryItem("path",        "/library/metadata/" + ratingKey);
    q.addQueryItem("mediaIndex",  "0");
    q.addQueryItem("partIndex",   "0");
    q.addQueryItem("protocol",    "hls");
    q.addQueryItem("fastSeek",    "1");
    q.addQueryItem("copyts",      "1");
    q.addQueryItem("directPlay",  "0");
    q.addQueryItem("directStream","0");
    q.addQueryItem("maxVideoBitrate", quality);
    q.addQueryItem("subtitleSize","100");
    q.addQueryItem("audioBoost",  "100");
    q.addQueryItem("session",     sessionId);
    // "Chrome" selects a well-known server-side transcode profile that defines
    // target codecs/containers. This is separate from the X-Plex-Platform header
    // (which identifies the calling application for auth/logging purposes).
    q.addQueryItem("X-Plex-Platform", "Chrome");
    q.addQueryItem("X-Plex-Client-Identifier", clientId());
    if (offsetMs > 0)
        q.addQueryItem("offset", QString::number(offsetMs / 1000));
    if (!audioId.isEmpty())
        q.addQueryItem("audioStreamID", audioId);
    if (!subtitleId.isEmpty() && subtitleId != "0") {
        q.addQueryItem("subtitleStreamID", subtitleId);
        q.addQueryItem("subtitles", "burn");
    }
    url.setQuery(q);

    // Build the request with Chrome as the platform in the header too, so it
    // matches the query param. Plex uses the Chrome profile for transcoding.
    QNetworkRequest req = plexRequest(url, token);
    req.setRawHeader("X-Plex-Platform", "Chrome");
    auto *reply = m_nam->get(req);
    ignoreSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, token, ratingKey, partKey, sessionId, audioId, subtitleId, offsetMs]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 498) {
                handle498([this, ratingKey, partKey, sessionId, audioId, subtitleId, offsetMs]{
                    request_transcode(ratingKey, partKey, sessionId, audioId, subtitleId, offsetMs);
                }); return;
            }
            emit errorOccurred("TRANSCODE FAILED: " + reply->errorString()); return;
        }
        // The server returns a master m3u8 with a relative variant URL.
        // Parse it to find the first non-comment line, then resolve to absolute.
        QByteArray body = reply->readAll();
        QString variantPath;
        for (const QString &line : QString::fromUtf8(body).split('\n')) {
            QString t = line.trimmed();
            if (!t.isEmpty() && !t.startsWith('#')) { variantPath = t; break; }
        }
        QString streamUrl;
        if (!variantPath.isEmpty()) {
            QUrl base = reply->url();
            base.setQuery(QString());
            QUrl resolved = base.resolved(QUrl(variantPath));
            streamUrl = resolved.toString();
        } else {
            streamUrl = reply->url().toString();
        }
        qDebug() << "[Plex] Transcode stream URL for mpv:" << streamUrl;
        emit streamUrlReady(streamUrl, token);
    });
}

void PlexBackend::update_timeline(const QString &ratingKey, const QString &partKey,
                                  const QString &state, int timeMs, int durationMs) {
    QString uri = serverUrl(), token = serverToken();
    QUrl url(uri + "/:/timeline");
    QUrlQuery q;
    q.addQueryItem("ratingKey", ratingKey);
    q.addQueryItem("key",       "/library/metadata/" + ratingKey);
    q.addQueryItem("state",     state);
    q.addQueryItem("time",      QString::number(timeMs));
    q.addQueryItem("duration",  QString::number(durationMs));
    q.addQueryItem("hasMDE",    "1");
    q.addQueryItem("X-Plex-Client-Identifier", clientId());
    url.setQuery(q);
    // Fire-and-forget
    auto *reply = plexGet(url, token);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void PlexBackend::set_audio_stream(const QString &streamId, const QString &partId) {
    QString uri = serverUrl(), token = serverToken();
    QUrl url(uri + "/library/parts/" + partId);
    QUrlQuery q; q.addQueryItem("audioStreamID", streamId); url.setQuery(q);
    auto *reply = plexPut(url, token);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void PlexBackend::set_subtitle_stream(const QString &streamId, const QString &partId) {
    QString uri = serverUrl(), token = serverToken();
    QUrl url(uri + "/library/parts/" + partId);
    QUrlQuery q; q.addQueryItem("subtitleStreamID", streamId); url.setQuery(q);
    auto *reply = plexPut(url, token);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

// ---------------------------------------------------------------------------
// Settings dynamic options
// ---------------------------------------------------------------------------

void PlexBackend::getUsers() {
    QJsonObject auth = loadAuth();
    QVariantList options;
    for (const auto &v : auth["users"].toArray()) {
        QJsonObject u = v.toObject();
        options.append(QVariantMap{{"id",u["id"].toString()},{"label",u["title"].toString()}});
    }
    emit dynamicOptionsReady("current_user_id", options);
}

void PlexBackend::getServers() {
    QJsonObject auth = loadAuth();
    QJsonObject managed = auth["managed_user_tokens"].toObject();
    bool managedActive = !managed.isEmpty();
    QVariantList options;
    for (const auto &v : auth["servers"].toArray()) {
        QJsonObject s = v.toObject();
        QString mid = s["machineId"].toString();
        if (managedActive && !managed.contains(mid)) continue;
        options.append(QVariantMap{{"id", mid}, {"label", s["name"].toString()}});
    }
    emit dynamicOptionsReady("server_machine_id", options);
}

void PlexBackend::getVideoQualities() {
    QVariantList options = {
        QVariantMap{{"id","auto"}, {"label","Direct Play"}},
        QVariantMap{{"id","8000"}, {"label","8 Mbps (1080p)"}},
        QVariantMap{{"id","4000"}, {"label","4 Mbps (720p)"}},
        QVariantMap{{"id","2000"}, {"label","2 Mbps (480p)"}},
    };
    emit dynamicOptionsReady("video_quality", options);
}

void PlexBackend::get_resume_playback_options() {
    QVariantList options;
    QVariantMap ask; ask["id"] = "ask"; ask["label"] = "Ask";
    QVariantMap yes; yes["id"] = "yes"; yes["label"] = "Always";
    options << ask << yes;
    emit dynamicOptionsReady("resume_playback", options);
}

void PlexBackend::getLibraries() {
    QString uri = serverUrl(), token = serverToken();
    if (uri.isEmpty()) { emit dynamicOptionsReady("libraries", QVariantList{}); return; }
    QString machineId = loadAuth()["active_server_machine_id"].toString();
    auto *reply = plexGet(QUrl(uri + "/library/sections"), token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, machineId]() {
        reply->deleteLater();
        QVariantList options;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonArray sections = QJsonDocument::fromJson(reply->readAll())
                                  .object()["MediaContainer"].toObject()["Directory"].toArray();
            for (const auto &sv : sections) {
                QJsonObject s = sv.toObject();
                if (!kSupportedLibraryTypes.contains(s["type"].toString())) continue;
                QString key = s["key"].toString();
                QString id = machineId.isEmpty() ? key : machineId + "_" + key;
                options.append(QVariantMap{{"id",id},{"label",s["title"].toString().toUpper()}});
            }
        }
        emit dynamicOptionsReady("libraries", options);
    });
}

void PlexBackend::applyCurrentUserSetting() {
    QString userId = loadConfig()["modules"].toObject()
                     ["com.240mp.plex"].toObject()["current_user_id"].toString();
    if (userId.isEmpty()) return;

    activateUser(userId, [this](const QVariantList &accessibleServers) {
        QJsonObject a = loadAuth();
        QString curMid = a["active_server_machine_id"].toString();
        bool curAccessible = false;
        for (const auto &sv : accessibleServers)
            if (sv.toMap()["machineId"].toString() == curMid) { curAccessible = true; break; }

        if (!accessibleServers.isEmpty() && !curAccessible) {
            QVariantMap ns = accessibleServers[0].toMap();
            QString newMid = ns["machineId"].toString();
            a["active_server_uri"]        = ns["uri"].toString();
            a["active_server_machine_id"] = newMid;
            saveAuth(a);

            QJsonObject cfg = loadConfig();
            QJsonObject mods = cfg["modules"].toObject();
            QJsonObject plexCfg = mods["com.240mp.plex"].toObject();
            plexCfg["server_machine_id"] = newMid;
            mods["com.240mp.plex"] = plexCfg;
            cfg["modules"] = mods;
            saveConfig(cfg);
        }

        QVariantList serverOptions;
        for (const auto &sv : accessibleServers)
            serverOptions.append(QVariantMap{{"id", sv.toMap()["machineId"]},
                                             {"label", sv.toMap()["name"]}});
        emit dynamicOptionsReady("server_machine_id", serverOptions);
    });
}

void PlexBackend::applyCurrentServerSetting() {
    QString machineId = loadConfig()["modules"].toObject()
                        ["com.240mp.plex"].toObject()["server_machine_id"].toString();
    if (machineId.isEmpty()) return;
    QJsonObject auth = loadAuth();
    QJsonObject server;
    for (const auto &v : auth["servers"].toArray()) {
        if (v.toObject()["machineId"].toString() == machineId) { server = v.toObject(); break; }
    }
    if (server.isEmpty()) return;
    auth["active_server_uri"]        = server["uri"].toString();
    auth["active_server_machine_id"] = machineId;
    saveAuth(auth);
}

// ---------------------------------------------------------------------------
// reauth_select_user
// ---------------------------------------------------------------------------

void PlexBackend::reauth_select_user(const QString &userId) {
    activateUser(userId, [this, userId](const QVariantList &accessibleServers) {
        QJsonObject a = loadAuth();
        QJsonObject cfg = loadConfig();
        QJsonObject mods = cfg["modules"].toObject();
        QJsonObject plexCfg = mods["com.240mp.plex"].toObject();
        plexCfg["current_user_id"] = userId;

        // Apply saved server, falling back to first accessible if current is unreachable
        QString machineId = plexCfg["server_machine_id"].toString();
        if (!machineId.isEmpty()) {
            bool found = false;
            for (const auto &sv : accessibleServers)
                if (sv.toMap()["machineId"].toString() == machineId) { found = true; break; }
            if (!found && !accessibleServers.isEmpty())
                machineId = accessibleServers[0].toMap()["machineId"].toString();
        }
        if (!machineId.isEmpty()) {
            for (const auto &sv : a["servers"].toArray()) {
                QJsonObject s = sv.toObject();
                if (s["machineId"].toString() == machineId) {
                    a["active_server_uri"]        = s["uri"].toString();
                    a["active_server_machine_id"] = machineId;
                    plexCfg["server_machine_id"]  = machineId;
                    break;
                }
            }
            saveAuth(a);
        }

        mods["com.240mp.plex"] = plexCfg;
        cfg["modules"] = mods;
        saveConfig(cfg);
        emit authSuccess();
    });
}
