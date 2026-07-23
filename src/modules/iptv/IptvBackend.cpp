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
#include <QXmlStreamReader>
#include <QDateTime>
#include <QTimeZone>
#include <QDebug>
#include <algorithm>

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

QString IptvBackend::epgUrl() const {
    return moduleConfig()["epg_url"].toString().trimmed();
}

bool IptvBackend::has_source() const {
    return !sourceUrl().isEmpty();
}

QString IptvBackend::get_source() const {
    return sourceUrl();
}

QString IptvBackend::get_epg_source() const {
    return epgUrl();
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
    const qint64 nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    QVariantList result;
    for (const Channel &c : m_channels) {
        if (all || c.group == group) {
            QVariantMap m{
                {"name",  c.name},
                {"url",   c.url},
                {"logo",  c.logo},
                {"group", c.group},
                {"tvgId", c.tvgId},
            };
            // Merge in now/next guide info (empty fields when no EPG match).
            const QVariantMap nn = nowNextFor(c.tvgId, nowMs);
            for (auto it = nn.constBegin(); it != nn.constEnd(); ++it)
                m.insert(it.key(), it.value());
            result.append(m);
        }
    }
    emit channelsLoaded(result);
}

// Returns {nowTitle, nowStartMs, nowStopMs, nextTitle, nextStartMs} for the
// channel's guide at nowMs. Missing fields stay empty/0 when there's no match.
QVariantMap IptvBackend::nowNextFor(const QString &tvgId, qint64 nowMs) const {
    QVariantMap out{
        {"nowTitle",    QString()},
        {"nowStartMs",  QVariant::fromValue<qint64>(0)},
        {"nowStopMs",   QVariant::fromValue<qint64>(0)},
        {"nextTitle",   QString()},
        {"nextStartMs", QVariant::fromValue<qint64>(0)},
    };
    if (tvgId.isEmpty())
        return out;
    auto it = m_epg.constFind(tvgId);
    if (it == m_epg.constEnd())
        return out;

    const QVector<Programme> &progs = it.value();
    // Sorted ascending by start; find the airing programme and the one after.
    for (int i = 0; i < progs.size(); ++i) {
        const Programme &p = progs[i];
        if (nowMs >= p.startMs && nowMs < p.stopMs) {
            out["nowTitle"]   = p.title;
            out["nowStartMs"] = QVariant::fromValue<qint64>(p.startMs);
            out["nowStopMs"]  = QVariant::fromValue<qint64>(p.stopMs);
            if (i + 1 < progs.size()) {
                out["nextTitle"]   = progs[i + 1].title;
                out["nextStartMs"] = QVariant::fromValue<qint64>(progs[i + 1].startMs);
            }
            return out;
        }
        if (p.startMs > nowMs) {
            // Gap before the next programme — report it as "next" only.
            out["nextTitle"]   = p.title;
            out["nextStartMs"] = QVariant::fromValue<qint64>(p.startMs);
            return out;
        }
    }
    return out;
}

void IptvBackend::fetchAndParse(std::function<void()> onDone) {
    const QString src = sourceUrl();
    if (src.isEmpty()) {
        emit playlistError("NO PLAYLIST CONFIGURED");
        return;
    }

    emit loadingChanged(true);
    m_epgLoaded = false; // a (re)load of the playlist also refreshes the guide

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
        // Load the guide (if any) before signalling completion.
        fetchEpg([this, onDone]() {
            emit loadingChanged(false);
            if (onDone) onDone();
        });
        return;
    }

    QNetworkRequest req{QUrl(src)};
    req.setRawHeader("Accept", "*/*");
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, onDone]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit loadingChanged(false);
            emit playlistError("PLAYLIST DOWNLOAD FAILED: " + reply->errorString());
            return;
        }
        parseM3U(reply->readAll());
        fetchEpg([this, onDone]() {
            emit loadingChanged(false);
            if (onDone) onDone();
        });
    });
}

void IptvBackend::fetchEpg(std::function<void()> onDone) {
    // Guide is optional and only loaded once per session. A missing or broken
    // EPG never blocks channel browsing — it just means no now/next info.
    const QString url = epgUrl();
    if (url.isEmpty() || m_epgLoaded) {
        if (onDone) onDone();
        return;
    }

    const bool isUrl = url.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
                    || url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);

    if (!isUrl) {
        QFile f(url);
        if (f.open(QIODevice::ReadOnly))
            parseXmltv(f.readAll());
        else
            qWarning("[Iptv] Cannot open EPG file: %s", qPrintable(url));
        m_epgLoaded = true;
        if (onDone) onDone();
        return;
    }

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Accept", "*/*");
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, onDone]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            parseXmltv(reply->readAll());
        else
            qWarning("[Iptv] EPG download failed: %s", qPrintable(reply->errorString()));
        m_epgLoaded = true; // don't retry this session even on failure
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

// ---------------------------------------------------------------------------
// XMLTV guide
// ---------------------------------------------------------------------------

// Parse an XMLTV timestamp ("YYYYMMDDHHMMSS +ZZZZ", offset optional) to epoch
// milliseconds. Returns 0 on failure.
static qint64 parseXmltvTime(const QString &raw) {
    const QString s = raw.trimmed();
    if (s.size() < 14)
        return 0;

    QDateTime dt = QDateTime::fromString(s.left(14), QStringLiteral("yyyyMMddHHmmss"));
    if (!dt.isValid())
        return 0;
    // The 14-digit stamp is wall-clock time in the trailing offset; interpret it
    // as UTC first, then subtract the offset to get true UTC.
    dt.setTimeZone(QTimeZone::UTC);

    int offsetSec = 0;
    const QString rest = s.mid(14).trimmed(); // e.g. "+0000" / "-0500"
    if (rest.size() >= 5 && (rest[0] == QLatin1Char('+') || rest[0] == QLatin1Char('-'))) {
        const int hh = rest.mid(1, 2).toInt();
        const int mm = rest.mid(3, 2).toInt();
        offsetSec = (hh * 3600 + mm * 60) * (rest[0] == QLatin1Char('-') ? -1 : 1);
    }
    return dt.addSecs(-offsetSec).toMSecsSinceEpoch();
}

void IptvBackend::parseXmltv(const QByteArray &data) {
    m_epg.clear();

    QXmlStreamReader xml(data);
    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        if (xml.name() != QLatin1String("programme"))
            continue;

        const QString channel = xml.attributes().value(QLatin1String("channel")).toString();
        const qint64 startMs = parseXmltvTime(xml.attributes().value(QLatin1String("start")).toString());
        const qint64 stopMs  = parseXmltvTime(xml.attributes().value(QLatin1String("stop")).toString());
        QString title;

        // Read child elements until the matching </programme>.
        while (!(xml.tokenType() == QXmlStreamReader::EndElement
                 && xml.name() == QLatin1String("programme"))) {
            if (xml.readNext() == QXmlStreamReader::StartElement
                && xml.name() == QLatin1String("title") && title.isEmpty()) {
                title = xml.readElementText();
            }
            if (xml.atEnd())
                break;
        }

        if (channel.isEmpty() || startMs == 0)
            continue;
        m_epg[channel].append(Programme{startMs, stopMs, title});
    }

    // Sort each channel's programmes by start time so now/next lookups are a
    // single ordered scan.
    int total = 0;
    for (auto it = m_epg.begin(); it != m_epg.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [](const Programme &a, const Programme &b) { return a.startMs < b.startMs; });
        total += it.value().size();
    }
    qDebug("[Iptv] Parsed EPG: %d channels, %d programmes", int(m_epg.size()), total);
}
