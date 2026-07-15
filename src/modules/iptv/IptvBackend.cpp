#include "IptvBackend.h"

#include <QFile>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QDebug>

static const QString kModuleId = QStringLiteral("com.240mp.iptv");

IptvBackend::IptvBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_nam(new QNetworkAccessManager(this))
{
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

QJsonObject IptvBackend::loadConfig() const {
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    return {};
}

QJsonObject IptvBackend::moduleConfig() const {
    return loadConfig()["modules"].toObject()[kModuleId].toObject();
}

QString IptvBackend::sourceUrl() const {
    return moduleConfig()["m3u_url"].toString().trimmed();
}

bool IptvBackend::has_source() const {
    return !sourceUrl().isEmpty();
}

QString IptvBackend::get_source() const {
    return sourceUrl();
}

// ---------------------------------------------------------------------------
// Load + parse
// ---------------------------------------------------------------------------

void IptvBackend::load_groups(bool force) {
    if (m_loaded && !force) {
        emitGroups();
        return;
    }
    fetchAndParse([this]() { emitGroups(); });
}

void IptvBackend::load_channels(const QString &group) {
    if (m_loaded) {
        emitChannels(group);
        return;
    }
    // Playlist not parsed yet (e.g. Channels opened before Groups): fetch first,
    // then emit the requested group's channels.
    fetchAndParse([this, group]() { emitChannels(group); });
}

void IptvBackend::emitChannels(const QString &group) {
    const bool all = group.isEmpty() || group == QLatin1String("__all__");
    QVariantList result;
    for (const Channel &c : m_channels) {
        if (all || c.group == group) {
            result.append(QVariantMap{
                {"name",  c.name},
                {"url",   c.url},
                {"logo",  c.logo},
                {"group", c.group},
            });
        }
    }
    emit channelsLoaded(result);
}

void IptvBackend::fetchAndParse(std::function<void()> onDone) {
    const QString src = sourceUrl();
    if (src.isEmpty()) {
        emit playlistError("NO PLAYLIST CONFIGURED");
        return;
    }

    emit loadingChanged(true);

    const bool isUrl = src.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
                    || src.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);

    if (!isUrl) {
        // Local file path.
        QFile f(src);
        if (!f.open(QIODevice::ReadOnly)) {
            emit loadingChanged(false);
            emit playlistError("CANNOT OPEN PLAYLIST FILE");
            return;
        }
        parseM3U(f.readAll());
        emit loadingChanged(false);
        if (onDone) onDone();
        return;
    }

    QNetworkRequest req{QUrl(src)};
    req.setRawHeader("Accept", "*/*");
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, onDone]() {
        reply->deleteLater();
        emit loadingChanged(false);
        if (reply->error() != QNetworkReply::NoError) {
            emit playlistError("PLAYLIST DOWNLOAD FAILED: " + reply->errorString());
            return;
        }
        parseM3U(reply->readAll());
        if (onDone) onDone();
    });
}

void IptvBackend::parseM3U(const QByteArray &data) {
    m_channels.clear();
    m_loaded = true;

    static const QRegularExpression reLogo(
        QStringLiteral("tvg-logo=\"([^\"]*)\""));
    static const QRegularExpression reGroup(
        QStringLiteral("group-title=\"([^\"]*)\""));
    static const QRegularExpression reTvgId(
        QStringLiteral("tvg-id=\"([^\"]*)\""));

    const QString text = QString::fromUtf8(data);
    const QStringList lines = text.split(QRegularExpression("\r\n|\n|\r"));

    Channel pending;
    bool havePending = false;

    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1String("#EXTINF"))) {
            pending = Channel{};
            havePending = true;

            auto mLogo = reLogo.match(line);
            if (mLogo.hasMatch()) pending.logo = mLogo.captured(1);
            auto mGroup = reGroup.match(line);
            if (mGroup.hasMatch()) pending.group = mGroup.captured(1);
            auto mId = reTvgId.match(line);
            if (mId.hasMatch()) pending.tvgId = mId.captured(1);

            // Display name is everything after the last comma on the EXTINF line.
            const int comma = line.lastIndexOf(QLatin1Char(','));
            if (comma >= 0)
                pending.name = line.mid(comma + 1).trimmed();
        } else if (line.startsWith(QLatin1Char('#'))) {
            // Other directives (#EXTGRP, #EXTVLCOPT, etc.) — ignore for Phase 1.
            continue;
        } else if (havePending) {
            // First non-comment line after an #EXTINF is the stream URL.
            pending.url = line;
            if (pending.name.isEmpty())
                pending.name = line;
            m_channels.append(pending);
            havePending = false;
        }
    }

    qDebug("[Iptv] Parsed %d channels", int(m_channels.size()));
}

void IptvBackend::emitGroups() {
    // Preserve first-appearance order; count channels per group.
    QVariantList groups;
    QVector<QString> order;
    QHash<QString, int> counts;
    for (const Channel &c : m_channels) {
        if (!counts.contains(c.group))
            order.append(c.group);
        counts[c.group]++;
    }

    // Always offer an "all channels" shelf first.
    groups.append(QVariantMap{
        {"title", QStringLiteral("ALL CHANNELS")},
        {"key",   QStringLiteral("__all__")},
        {"count", m_channels.size()},
    });

    for (const QString &g : order) {
        groups.append(QVariantMap{
            {"title", g.isEmpty() ? QStringLiteral("UNGROUPED") : g.toUpper()},
            {"key",   g},
            {"count", counts.value(g)},
        });
    }

    emit groupsLoaded(groups);
}
