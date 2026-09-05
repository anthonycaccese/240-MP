#include "VirtualChannelsBackend.h"

#include "ChannelTuner.h"
#include "PathGuard.h"

#include <QDateTime>
#include <QTimeZone>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

using namespace vchan;

namespace {

constexpr const char *kModuleId = "com.240mp.virtual_channels";

constexpr int kUrlTimeoutMs = 12000;

constexpr int kGuideSliverDivisor = 16;

constexpr int kProbeBatchPerTick = 4;

constexpr qint64 kProbeBudgetPerTickMs = 2000;

const QStringList kInterstitialKinds = {
    QStringLiteral("intros"), QStringLiteral("bumps"),
    QStringLiteral("commercials"), QStringLiteral("outros")
};

constexpr int kMaxFolderChoices = 400;

const QStringList kVideoExts = {
    "mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv", "flv", "f4v",
    "mpg", "mpeg", "vob", "ts", "m2ts"
};

bool isSafeLogoName(const QString &file) {
    static const QRegularExpression re(QStringLiteral(R"(^[\p{L}\p{N}][\p{L}\p{N} ._()-]*$)"));
    return re.match(file).hasMatch();
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// Episode exclusions are stored under the season they belong to, so that
// switching a season on can clear its own and no one else's. An older file
// holds a flat array instead; it is read as season-less rather than migrated on
// sight, the way every other legacy shape here is.
static QStringList excludedEpisodesIn(const QJsonObject &excl) {
    QStringList out;
    const QJsonValue eps = excl.value(QLatin1String("episodes"));
    if (eps.isArray()) {
        for (const QJsonValue &v : eps.toArray())
            if (v.isString()) out << v.toString();
        return out;
    }
    const QJsonObject bySeason = eps.toObject();
    for (auto it = bySeason.constBegin(); it != bySeason.constEnd(); ++it)
        for (const QJsonValue &v : it.value().toArray())
            if (v.isString()) out << v.toString();
    return out;
}

// What a picker says under a collection, playlist or genre: how much is in it.
// Picking blind is how a channel ends up airing one film over and over.
QString countedAs(int n, const char *singular, const char *plural) {
    if (n <= 0) return QString();
    return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? singular : plural));
}

QStringList stringListOf(const QJsonObject &o, const char *key) {
    QStringList out;
    for (const QJsonValue &v : o.value(QLatin1String(key)).toArray())
        if (v.isString()) out << v.toString();
    return out;
}
}

VirtualChannelsBackend::VirtualChannelsBackend(const QString &appRoot,
                                               const QString &dataRoot,
                                               QObject *plexBackend,
                                               QObject *jellyfinBackend,
                                               QObject *embyBackend,
                                               QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot),
      m_mediaRoot(dataRoot + "/media"), m_plex(plexBackend),
      m_jellyfin(jellyfinBackend), m_emby(embyBackend), m_probe(dataRoot)
{
    const QString configured = resolveMediaRoot();
    if (!configured.isEmpty())
        m_mediaRoot = configured;
    m_localLibrary.setMediaRoot(m_mediaRoot);
    ensureLibraryFolders();

    if (m_plex) {
        const int sig = m_plex->metaObject()->indexOfSignal("streamUrlReady(QString,QString)");
        if (sig >= 0) {
            connect(m_plex, SIGNAL(streamUrlReady(QString,QString)),
                    this,   SLOT(onPlexStreamUrlReady(QString,QString)));
            const QMetaObject *mo = m_plex->metaObject();
            if (mo->indexOfSignal("itemLoaded(QVariant)") >= 0)
                connect(m_plex, SIGNAL(itemLoaded(QVariant)), this, SLOT(onPlexItemLoaded(QVariant)));
            if (mo->indexOfSignal("librariesLoaded(QVariant)") >= 0)
                connect(m_plex, SIGNAL(librariesLoaded(QVariant)), this, SLOT(onPlexLibrariesLoaded(QVariant)));
            if (mo->indexOfSignal("itemsLoaded(QVariant)") >= 0)
                connect(m_plex, SIGNAL(itemsLoaded(QVariant)), this, SLOT(onPlexItemsLoaded(QVariant)));
            if (mo->indexOfSignal("childrenLoaded(QVariant)") >= 0)
                connect(m_plex, SIGNAL(childrenLoaded(QVariant)), this, SLOT(onPlexChildrenLoaded(QVariant)));
            if (mo->indexOfSignal("collectionsLoaded(QVariant)") >= 0)
                connect(m_plex, SIGNAL(collectionsLoaded(QVariant)), this, SLOT(onPlexCollectionsLoaded(QVariant)));
            if (mo->indexOfSignal("playlistsLoaded(QVariant)") >= 0)
                connect(m_plex, SIGNAL(playlistsLoaded(QVariant)), this, SLOT(onPlexPlaylistsLoaded(QVariant)));
        } else {
            qWarning("[VirtualChannels] Plex backend has no streamUrlReady signal — Plex slots will not play");
            m_plex = nullptr;
        }
    }

    m_server = new MediaServerSource(this);
    m_server->setBackend(SlotSource::Jellyfin, m_jellyfin);
    m_server->setBackend(SlotSource::Emby, m_emby);
    if (!m_server->available(SlotSource::Jellyfin)) m_jellyfin = nullptr;
    if (!m_server->available(SlotSource::Emby))     m_emby = nullptr;
    connect(m_server, &MediaServerSource::enumerationFinished,
            this, &VirtualChannelsBackend::onServerEnumerationFinished);
    connect(m_server, &MediaServerSource::enumerationFailed,
            this, &VirtualChannelsBackend::onServerEnumerationFailed);
    connect(m_server, &MediaServerSource::browseReady,
            this, &VirtualChannelsBackend::onServerBrowseReady);
    connect(m_server, &MediaServerSource::browseFailed,
            this, &VirtualChannelsBackend::onServerBrowseFailed);

    for (QObject *b : { m_jellyfin, m_emby }) {
        if (!b) continue;
        if (b->metaObject()->indexOfSignal("streamUrlReady(QString)") >= 0)
            connect(b, SIGNAL(streamUrlReady(QString)), this, SLOT(onServerStreamUrlReady(QString)));
        else
            qWarning("[VirtualChannels] a media server backend has no streamUrlReady — its slots will not play");
    }

    m_urlTimer = new QTimer(this);
    m_urlTimer->setSingleShot(true);
    m_urlTimer->setInterval(kUrlTimeoutMs);
    connect(m_urlTimer, &QTimer::timeout, this, &VirtualChannelsBackend::onUrlTimeout);

    m_genTimer = new QTimer(this);
    m_genTimer->setInterval(0);
    connect(m_genTimer, &QTimer::timeout, this, &VirtualChannelsBackend::onGenerationTick);

    QTimer::singleShot(kStartupSweepDelayMs, this, [this] { top_up_schedules(); });
    armNightlySweep();

    qDebug("[VirtualChannels] mediaRoot = %s  prober = %s  plex = %s  jellyfin = %s  emby = %s",
           qPrintable(m_mediaRoot), qPrintable(m_probe.proberName()),
           m_plex     ? "available" : "unavailable",
           m_jellyfin ? "available" : "unavailable",
           m_emby     ? "available" : "unavailable");
}

// The two folders the local library reads, made so a fresh install has
// somewhere obvious to put things. Every other module that owns a directory
// does this on construction; this one did it for channels/ and logos/ but not
// for the media it tells people to use.
//
// Only ever inside a media root that already exists. Creating the root itself
// would be worse than useless when it is a drive that has not mounted yet: the
// empty directories would sit on top of the mount point and hide the media the
// moment it appeared.
void VirtualChannelsBackend::ensureLibraryFolders() const {
    const QString absRoot = QFileInfo(m_mediaRoot).canonicalFilePath();
    if (absRoot.isEmpty() || !QFileInfo(absRoot).isDir())
        return;
    for (const QString &name : vchan::LocalLibrary::seriesDirNames()
                                   + vchan::LocalLibrary::moviesDirNames()) {
        const QString path = absRoot + QLatin1Char('/') + name;
        if (!QFileInfo::exists(path) && !QDir().mkpath(path))
            qWarning("[VirtualChannels] could not create %s", qPrintable(path));
    }
}

QString VirtualChannelsBackend::resolveMediaRoot() const {
    QFile f(m_dataRoot + "/config.json");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject modules = QJsonDocument::fromJson(f.readAll()).object()
                                    .value(QLatin1String("modules")).toObject();

    // This module's own setting wins, so a viewer can point channels somewhere
    // separate. Failing that, follow Local Files: it is the same question asked
    // once, and someone who has moved their library to a connected drive should
    // not have to say so twice for the channels to find it.
    const QString own = modules.value(QLatin1String(kModuleId)).toObject()
                            .value(QLatin1String("media_directory")).toString();
    if (!own.trimmed().isEmpty())
        return own;

    return modules.value(QStringLiteral("com.240mp.local_files")).toObject()
               .value(QLatin1String("media_directory")).toString();
}

void VirtualChannelsBackend::onSettingChanged(const QString &moduleId,
                                              const QString &key,
                                              const QVariant &value) {
    // Local Files' media directory matters here too, because this module falls
    // back to it. Ignoring it would leave the channels pointed at the old place
    // until the next restart.
    const bool ours  = (moduleId == QLatin1String(kModuleId));
    const bool local = (moduleId == QLatin1String("com.240mp.local_files"));
    if (!ours && !local)
        return;
    if (key == QLatin1String("media_directory")) {
        const QString resolved = resolveMediaRoot();
        m_mediaRoot = resolved.trimmed().isEmpty() ? m_dataRoot + "/media" : resolved;
        m_localLibrary.setMediaRoot(m_mediaRoot);
        ensureLibraryFolders();
        qInfo("[VirtualChannels] mediaRoot = %s", qPrintable(m_mediaRoot));
    }
}

QString VirtualChannelsBackend::channelsFilePath() const {
    return m_dataRoot + "/channels/channels.json";
}

QString VirtualChannelsBackend::scheduleFilePath(int channelNumber) const {
    return m_dataRoot + QStringLiteral("/channels/schedule/%1.json").arg(channelNumber);
}

// ---------------------------------------------------------------------------
// Schedule cache
// ---------------------------------------------------------------------------

const ChannelSchedule &VirtualChannelsBackend::scheduleFor(int channelNumber) {
    const QString  path = scheduleFilePath(channelNumber);
    const QFileInfo fi(path);
    const qint64 mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0;

    auto it = m_cache.find(channelNumber);
    if (it != m_cache.end() && it->mtimeMs == mtime)
        return it->schedule;

    CachedSchedule entry;
    entry.mtimeMs = mtime;
    int dropped = 0;
    entry.schedule = ChannelSchedule::load(path, &entry.error, &dropped);

    if (!entry.schedule.isValid())
        qWarning("[VirtualChannels] channel %d: %s", channelNumber, qPrintable(entry.error));
    else if (dropped > 0)
        qWarning("[VirtualChannels] channel %d: %d unusable slot(s) dropped", channelNumber, dropped);

    it = m_cache.insert(channelNumber, entry);
    return it->schedule;
}

QVariantMap VirtualChannelsBackend::channelObject(int channelNumber) const {
    QFile f(channelsFilePath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &v : root.value(QLatin1String("channels")).toArray()) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("number")).toInt(-1) == channelNumber)
            return o.toVariantMap();
    }
    return {};
}

// ---------------------------------------------------------------------------
// Channel list / guide
// ---------------------------------------------------------------------------

QVariantList VirtualChannelsBackend::get_channels() {
    QVariantList out;

    QFile f(channelsFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("[VirtualChannels] no channels file at %s", qPrintable(channelsFilePath()));
        return out;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("[VirtualChannels] channels.json unreadable: %s", qPrintable(perr.errorString()));
        return out;
    }

    const qint64 t = nowMs();
    for (const QJsonValue &v : doc.object().value(QLatin1String("channels")).toArray()) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();

        const int number = o.value(QLatin1String("number")).toInt(-1);
        if (number < 0) continue;

        const ChannelSchedule &sched = scheduleFor(number);
        const Status st = sched.statusAt(t);

        QString onNow;
        if (st == Status::Ok) {
            const int idx = sched.indexAt(t);
            if (idx >= 0) {
                const Slot &s = sched.slotList()[idx];
                onNow = s.series.isEmpty() ? s.title
                                           : QStringLiteral("%1 — %2").arg(s.series, s.title);
            }
        }

        QVariantMap m;
        m["number"]   = number;
        m["name"]     = o.value(QLatin1String("name")).toString(
                            QStringLiteral("Channel %1").arg(number));
        m["onNow"]    = onNow;
        m["playable"] = (st == Status::Ok);
        m["message"]  = offAirMessage(st);
        out.append(m);
    }

    return out;
}

int VirtualChannelsBackend::current_slot(int channelNumber) {
    const ChannelSchedule &sched = scheduleFor(channelNumber);
    const qint64 t = nowMs();
    if (sched.statusAt(t) != Status::Ok) return -1;
    return sched.indexAt(t);
}

QVariantMap VirtualChannelsBackend::preview_source(int channelNumber,
                                                   int artWidth, int artHeight) {
    QVariantMap m;
    m["valid"] = false;
    m["image"] = QString();

    const ChannelSchedule &sched = scheduleFor(channelNumber);
    const qint64 t = nowMs();
    if (sched.statusAt(t) != Status::Ok) return m;
    const int idx = sched.indexAt(t);
    if (idx < 0) return m;

    ChannelSchedule::Block b;
    const int artIdx = sched.blockAt(t, &b) ? b.slotIndex : idx;
    const Slot &art  = sched.slotList()[artIdx];

    const Slot &s = sched.slotList()[idx];
    m["positionMs"] = double(sched.offsetInto(idx, t));
    m["slotIndex"]  = idx;

    if (!art.art.isEmpty()) {
        QObject *ab = (art.src == SlotSource::Plex) ? m_plex : m_server->backend(art.src);
        if (ab && ab->metaObject()->indexOfMethod(
                      "image_url(QString,QString,int,int)") >= 0) {
            QString url;
            QMetaObject::invokeMethod(ab, "image_url", Q_RETURN_ARG(QString, url),
                                      Q_ARG(QString, art.ref), Q_ARG(QString, art.art),
                                      Q_ARG(int, artWidth), Q_ARG(int, artHeight));
            m["image"] = url;
        } else if (ab && ab->metaObject()->indexOfMethod("image_url(QString,int,int)") >= 0) {
            QString url;
            QMetaObject::invokeMethod(ab, "image_url", Q_RETURN_ARG(QString, url),
                                      Q_ARG(QString, art.art),
                                      Q_ARG(int, artWidth), Q_ARG(int, artHeight));
            m["image"] = url;
        }
    }

    if (!isServerSource(s.src)) {
        PathVerdict verdict = PathVerdict::Ok;
        const QString abs = resolveMediaRef(m_mediaRoot, s.ref, &verdict);
        if (!abs.isEmpty()) {
            m["valid"] = true;
            m["url"]   = QUrl::fromLocalFile(abs).toString();
        }
    }
    return m;
}

QVariantMap VirtualChannelsBackend::now_next(int channelNumber) {
    QVariantMap m;
    const ChannelSchedule &sched = scheduleFor(channelNumber);
    const qint64 t = nowMs();

    const Status st = sched.statusAt(t);
    m["valid"]   = (st == Status::Ok);
    m["message"] = offAirMessage(st);
    if (st != Status::Ok)
        return m;

    const int idx = sched.indexAt(t);
    if (idx < 0) { m["valid"] = false; return m; }

    ChannelSchedule::Block b;
    if (!sched.blockAt(t, &b)) { m["valid"] = false; return m; }

    const Slot &prog = sched.slotList()[b.slotIndex];
    m["title"]    = b.title;
    m["series"]   = b.series;
    m["ep"]       = b.ep;
    m["desc"]     = prog.desc;
    m["kind"]     = slotKindToString(prog.kind);
    m["startMs"]  = b.start;
    m["durMs"]    = b.dur;
    m["offsetMs"] = qBound(qint64(0), t - b.start, b.dur);

    const Slot &onScreen = sched.slotList()[idx];
    m["inBreak"]      = (onScreen.kind != SlotKind::Programme);
    m["onScreenKind"] = slotKindToString(onScreen.kind);

    ChannelSchedule::Block nb;
    if (sched.blockAt(b.end() + 1, &nb)) {
        m["nextTitle"]   = nb.series.isEmpty() ? nb.title
                                               : nb.series + QStringLiteral(": ") + nb.title;
        m["nextStartMs"] = nb.start;
    }
    return m;
}

QVariantList VirtualChannelsBackend::upcoming(int channelNumber, int count) {
    QVariantList out;
    if (count <= 0) return out;

    const ChannelSchedule &sched = scheduleFor(channelNumber);
    if (!sched.isValid()) return out;

    const qint64 t = nowMs();
    int start = sched.indexAt(t);
    if (start < 0) {
        if (t < sched.slotList().first().start) start = 0;
        else return out;
    }

    for (int i = start; i < sched.slotCount() && out.size() < count; ++i) {
        const Slot &s = sched.slotList()[i];
        QVariantMap m;
        m["startMs"] = s.start;
        m["durMs"]   = s.dur;
        m["kind"]    = slotKindToString(s.kind);
        m["title"]   = s.title;
        m["series"]  = s.series;
        m["ep"]      = s.ep;
        m["onNow"]   = (i == start);
        out.append(m);
    }
    return out;
}

double VirtualChannelsBackend::now_ms() const { return double(nowMs()); }

QVariantList VirtualChannelsBackend::guide_grid(double fromMs, double spanMs) {
    QVariantList out;
    if (!(spanMs > 0) || !(fromMs > 0)) return out;
    const qint64 from = qint64(fromMs);
    const qint64 to   = from + qMin(qint64(spanMs), kMaxGuideSpanMs);

    QFile f(channelsFilePath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return out;

    const qint64 t = nowMs();
    for (const QJsonValue &v : doc.object().value(QLatin1String("channels")).toArray()) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        const int number = o.value(QLatin1String("number")).toInt(-1);
        if (number < 0) continue;

        const ChannelSchedule &sched = scheduleFor(number);

        const qint64 minBlock = (to - from) / kGuideSliverDivisor;

        QVariantList blocks;
        for (const ChannelSchedule::Block &b : sched.programmeBlocks(from, to, minBlock)) {
            QVariantMap m;
            m["startMs"] = double(b.start);
            m["durMs"]   = double(b.dur);
            m["title"]   = b.title;
            m["series"]  = b.series;
            m["ep"]      = b.ep;
            m["count"]   = b.count;
            m["onNow"]   = (t >= b.start && t < b.end());
            blocks.append(m);
        }

        QVariantMap row;
        row["number"] = number;
        row["name"]   = o.value(QLatin1String("name")).toString(
                            QStringLiteral("Channel %1").arg(number));
        row["blocks"] = blocks;
        out.append(row);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

QVariantMap VirtualChannelsBackend::offAirDescriptor(const QString &message,
                                                     bool needsRegeneration) {
    QVariantMap m;
    m["play"]              = false;
    m["pending"]           = false;
    m["message"]           = message;
    m["slotIndex"]         = -1;
    m["needsRegeneration"] = needsRegeneration;
    return m;
}

QString VirtualChannelsBackend::resolveLocalSlot(const Slot &s) const {
    PathVerdict v = PathVerdict::Ok;
    const QString abs = resolveMediaRef(m_mediaRoot, s.ref, &v);
    if (v != PathVerdict::Ok) {
        qWarning("[VirtualChannels] slot unplayable (%s): %s",
                 qPrintable(pathVerdictToString(v)), qPrintable(s.ref));
        return {};
    }
    return abs;
}

QObject *VirtualChannelsBackend::serverBackend(SlotSource src) const {
    if (src == SlotSource::Jellyfin) return m_jellyfin;
    if (src == SlotSource::Emby)     return m_emby;
    return nullptr;
}

bool VirtualChannelsBackend::requestServerUrl(const Slot &s) {
    QObject *b = serverBackend(s.src);
    if (!b) return false;
    if (b->metaObject()->indexOfMethod("get_playback_url(QString,QString,int,int,bool)") < 0) {
        qWarning("[VirtualChannels] %s backend has no get_playback_url — cannot resolve its slots",
                 qPrintable(MediaServerSource::providerName(s.src)));
        return false;
    }

    const QString mediaSourceId = s.partKey.isEmpty() ? s.ref : s.partKey;

    m_serverPendingSrc = s.src;
    const bool ok = QMetaObject::invokeMethod(
        b, "get_playback_url",
        Q_ARG(QString, s.ref), Q_ARG(QString, mediaSourceId),
        Q_ARG(int, -1), Q_ARG(int, -1), Q_ARG(bool, false));
    if (ok) m_urlTimer->start();
    return ok;
}

void VirtualChannelsBackend::onServerStreamUrlReady(const QString &url) {
    if (m_previewChannel >= 0) {
        QString token;
        if (QObject *b = serverBackend(m_serverPendingSrc)) {
            if (b->metaObject()->indexOfMethod("get_access_token()") >= 0)
                QMetaObject::invokeMethod(b, "get_access_token", Q_RETURN_ARG(QString, token));
        }
        deliverPreviewUrl(url, token);
        return;
    }
    if (!m_urlPending)
        return;
    m_urlPending = false;
    m_urlTimer->stop();

    QVariantMap m = m_urlDescriptor;
    if (url.isEmpty()) {
        emit playDescriptorReady(offAirDescriptor(
            QStringLiteral("%1 item unavailable")
                .arg(MediaServerSource::providerName(m_serverPendingSrc)), false));
        return;
    }
    QString token;
    if (QObject *b = serverBackend(m_serverPendingSrc)) {
        if (b->metaObject()->indexOfMethod("get_access_token()") >= 0)
            QMetaObject::invokeMethod(b, "get_access_token", Q_RETURN_ARG(QString, token));
    }
    m["play"]           = true;
    m["pending"]        = false;
    m["url"]            = url;
    m["jellyfinToken"]  = token;
    emit playDescriptorReady(m);
}

QString VirtualChannelsBackend::plexVideoQuality() const {
    if (!m_plex) return QStringLiteral("auto");
    if (m_plex->metaObject()->indexOfMethod("video_quality()") < 0)
        return QStringLiteral("auto");
    QString q;
    if (!QMetaObject::invokeMethod(m_plex, "video_quality", Qt::DirectConnection,
                                   Q_RETURN_ARG(QString, q)))
        return QStringLiteral("auto");
    return q.isEmpty() ? QStringLiteral("auto") : q;
}

bool VirtualChannelsBackend::requestPlexUrl(const Slot &s, qint64 offsetMs,
                                            bool transcodeAllowed) {
    if (!m_plex) return false;
    if (m_plex->metaObject()->indexOfMethod("build_stream_url(QString,QString,QString)") < 0) {
        qWarning("[VirtualChannels] Plex backend has no build_stream_url — cannot resolve Plex slots");
        return false;
    }

    if (s.partKey.isEmpty()) {
        if (m_plex->metaObject()->indexOfMethod("load_item_detail(QString)") < 0) {
            qWarning("[VirtualChannels] Plex backend cannot resolve a part key");
            return false;
        }
        m_plexAwaitingDetailFor = s.ref;
        m_plexPendingOffsetMs   = offsetMs;
        m_plexPendingTranscodeOk = transcodeAllowed;
        m_serverPendingSrc = SlotSource::Plex;
        const bool asked = QMetaObject::invokeMethod(m_plex, "load_item_detail",
                                                     Q_ARG(QString, s.ref));
        if (asked) m_urlTimer->start();
        return asked;
    }

    m_plexAwaitingDetailFor.clear();
    m_serverPendingSrc = SlotSource::Plex;
    const QString sessionId = QStringLiteral("vchan-%1").arg(nowMs());
    const bool ok = askPlexForStream(s.ref, s.partKey, sessionId, offsetMs,
                                     transcodeAllowed);
    if (ok) m_urlTimer->start();
    return ok;
}

bool VirtualChannelsBackend::askPlexForStream(const QString &ratingKey,
                                             const QString &partKey,
                                             const QString &sessionId,
                                             qint64 offsetMs,
                                             bool transcodeAllowed) {
    if (!m_plexTranscodeSession.isEmpty()
        && m_plex->metaObject()->indexOfMethod("stop_transcode(QString)") >= 0) {
        QMetaObject::invokeMethod(m_plex, "stop_transcode",
                                  Q_ARG(QString, m_plexTranscodeSession));
    }
    m_plexTranscodeSession.clear();

    const QString quality = plexVideoQuality();
    const bool willTranscode =
        transcodeAllowed && quality != QLatin1String("auto")
        && m_plex->metaObject()->indexOfMethod(
               "request_transcode(QString,QString,QString,QString,QString,int)") >= 0;

    // Which road a programme takes, and why. A channel joins part-way through
    // and allows transcoding; a guide preview starts at nothing and does not.
    // When one of the two plays and the other does not, this is the line that
    // says which was which.
    qInfo("[VirtualChannels] %s for %s: offset %lld s, quality %s, transcode %s",
          willTranscode ? "TRANSCODE" : "DIRECT", qPrintable(ratingKey),
          static_cast<long long>(offsetMs / 1000), qPrintable(quality),
          transcodeAllowed ? "allowed" : "not allowed");

    if (willTranscode) {
        m_plexTranscodeSession = sessionId;
        return QMetaObject::invokeMethod(
            m_plex, "request_transcode",
            Q_ARG(QString, ratingKey), Q_ARG(QString, partKey),
            Q_ARG(QString, sessionId), Q_ARG(QString, QString()),
            Q_ARG(QString, QStringLiteral("0")),
            Q_ARG(int, int(qBound<qint64>(0LL, offsetMs, qint64(INT_MAX)))));
    }

    return QMetaObject::invokeMethod(
        m_plex, "build_stream_url",
        Q_ARG(QString, ratingKey), Q_ARG(QString, partKey), Q_ARG(QString, sessionId));
}

void VirtualChannelsBackend::onPlexItemLoaded(const QVariant &detail) {
    if (!m_urlPending || m_plexAwaitingDetailFor.isEmpty())
        return;

    const QVariantMap d = detail.toMap();
    if (d.value("ratingKey").toString() != m_plexAwaitingDetailFor)
        return;

    const QString partKey = d.value("partKey").toString();
    m_plexAwaitingDetailFor.clear();
    if (partKey.isEmpty()) {
        m_urlPending = false;
        m_urlTimer->stop();
        emit playDescriptorReady(offAirDescriptor(QStringLiteral("Plex item has no playable part"), false));
        return;
    }

    const QString sessionId = QStringLiteral("vchan-%1").arg(nowMs());
    m_urlTimer->start();
    askPlexForStream(d.value("ratingKey").toString(), partKey, sessionId,
                     m_plexPendingOffsetMs, m_plexPendingTranscodeOk);
}

void VirtualChannelsBackend::preview_stream(int channelNumber) {
    cancel_preview_stream();

    const ChannelSchedule &sched = scheduleFor(channelNumber);
    const qint64 t = nowMs();
    if (sched.statusAt(t) != Status::Ok) return;
    const int idx = sched.indexAt(t);
    if (idx < 0) return;

    const Slot &s = sched.slotList()[idx];
    if (!isServerSource(s.src)) return;
    if (m_urlPending) return;

    m_previewChannel  = channelNumber;
    m_previewSlot     = idx;
    m_previewPosition = sched.offsetInto(idx, t);
    m_urlPending      = true;
    m_urlDescriptor   = QVariantMap{};

    const bool asked = (s.src == SlotSource::Plex)
                       ? requestPlexUrl(s, 0, /*transcodeAllowed*/ false)
                       : requestServerUrl(s);
    if (!asked) cancel_preview_stream();
}

void VirtualChannelsBackend::cancel_preview_stream() {
    if (m_previewChannel < 0) return;
    m_previewChannel  = -1;
    m_previewSlot     = -1;
    m_previewPosition = 0;
    m_urlPending      = false;
    m_plexAwaitingDetailFor.clear();
    if (m_urlTimer) m_urlTimer->stop();
}

bool VirtualChannelsBackend::deliverPreviewUrl(const QString &url, const QString &token) {
    if (m_previewChannel < 0) return false;
    const int ch = m_previewChannel;
    const qint64 pos = m_previewPosition;
    m_previewChannel  = -1;
    m_previewSlot     = -1;
    m_previewPosition = 0;
    m_urlPending      = false;
    if (m_urlTimer) m_urlTimer->stop();
    if (url.isEmpty()) return true;

    QString full = url;
    if (!token.isEmpty()) {
        QUrl u(url);
        QUrlQuery q(u.query());
        const QString key = (m_serverPendingSrc == SlotSource::Plex)
                            ? QStringLiteral("X-Plex-Token") : QStringLiteral("api_key");
        if (!q.hasQueryItem(key)) {
            q.addQueryItem(key, token);
            u.setQuery(q);
            full = u.toString();
        }
    }
    emit previewStreamReady(ch, full, double(pos));
    return true;
}

QVariantMap VirtualChannelsBackend::tune(int channelNumber) {
    cancel_preview_stream();
    m_tunedChannel = channelNumber;
    const ChannelSchedule &sched = scheduleFor(channelNumber);
    const qint64 t = nowMs();
    return buildPlayDescriptor(channelNumber, sched, t, decideTuneIn(sched, t));
}

QVariantMap VirtualChannelsBackend::after_playback(int channelNumber,
                                                   const QString &reason,
                                                   int slotIndex,
                                                   int consecutiveFailures) {
    const ChannelSchedule &sched = scheduleFor(channelNumber);
    const qint64 t = nowMs();

    const Decision d = decideAfterPlayback(sched, t, endReasonFromString(reason),
                                           slotIndex, consecutiveFailures);
    if (!d.play)
        return offAirDescriptor(offAirMessage(d.reason), d.needsRegeneration);

    return buildPlayDescriptor(channelNumber, sched, t, d);
}

QVariantMap VirtualChannelsBackend::buildPlayDescriptor(int channelNumber,
                                                        const ChannelSchedule &schedule,
                                                        qint64 t,
                                                        Decision start) {
    m_urlPending = false;
    m_plexAwaitingDetailFor.clear();
    m_urlTimer->stop();

    Decision d = start;
    int skipped = 0;

    while (d.play && skipped < kMaxConsecutiveFailures) {
        const Slot &s = schedule.slotList()[d.slotIndex];

        QVariantMap m;
        m["play"]              = true;
        m["pending"]           = false;
        m["message"]           = QString();
        m["slotIndex"]         = d.slotIndex;
        m["startSeconds"]      = double(d.offsetMs) / 1000.0;
        m["title"]             = s.title;
        m["series"]            = s.series;
        m["ep"]                = s.ep;
        m["kind"]              = slotKindToString(s.kind);
        m["needsRegeneration"] = false;
        m["plexToken"]         = QString();
        m["jellyfinToken"]     = QString();
        m["filler"]            = false;

        if (s.kind == SlotKind::Filler) {
            const qint64 left = qMax(qint64(0), s.end() - t);
            m["play"]    = false;
            m["filler"]  = true;
            m["seconds"] = double(left) / 1000.0;
            if (s.title.isEmpty()) {
                const QJsonObject co = QJsonObject::fromVariantMap(channelObject(channelNumber));
                m["title"] = co.value(QLatin1String("name"))
                               .toString(QStringLiteral("Channel %1").arg(channelNumber));
            }
            m["logo"]    = logo_path(channel_logo(channelNumber));
            m["extraUrls"] = QStringList{};
            return m;
        }

        if (isServerSource(s.src)) {
            if (s.src == SlotSource::Plex
                    ? requestPlexUrl(s, d.offsetMs, /*transcodeAllowed*/ true)
                    : requestServerUrl(s)) {
                m["play"]      = false;
                m["pending"]   = true;
                m["extraUrls"] = QStringList{};
                m_urlDescriptor = m;
                m_urlPending = true;
                return m;
            }
            d = decideAfterPlayback(schedule, t, EndReason::Failed, d.slotIndex, skipped);
            ++skipped;
            continue;
        }

        const QString url = resolveLocalSlot(s);
        if (url.isEmpty()) {
            d = decideAfterPlayback(schedule, t, EndReason::Failed, d.slotIndex, skipped);
            ++skipped;
            continue;
        }
        m["url"] = url;

        QStringList extras;
        for (int i = 1; i < d.window.size(); ++i) {
            const Slot &next = schedule.slotList()[d.window[i]];
            if (isServerSource(next.src)) break;
            if (next.kind == SlotKind::Filler) break;
            const QString extra = resolveLocalSlot(next);
            if (extra.isEmpty()) break;
            extras << extra;
        }
        m["extraUrls"] = extras;
        return m;
    }

    if (skipped >= kMaxConsecutiveFailures) {
        qWarning("[VirtualChannels] channel %d: gave up after %d unplayable slots in a row",
                 channelNumber, skipped);
        return offAirDescriptor(QStringLiteral("Nothing playable on this channel"), true);
    }
    return offAirDescriptor(offAirMessage(d.reason), d.needsRegeneration);
}

void VirtualChannelsBackend::onPlexStreamUrlReady(const QString &url, const QString &plexToken) {
    if (deliverPreviewUrl(url, plexToken)) return;
    if (!m_urlPending)
        return;
    m_urlPending = false;
    m_urlTimer->stop();

    QVariantMap m = m_urlDescriptor;
    if (url.isEmpty()) {
        emit playDescriptorReady(offAirDescriptor(QStringLiteral("Plex item unavailable"), false));
        return;
    }
    m["play"]      = true;
    m["pending"]   = false;
    m["url"]       = url;
    m["plexToken"] = plexToken;
    // A transcode is started at the join offset, so the stream mpv is handed is
    // meant to begin there. Logged because that assumption is exactly what a
    // programme that never starts calls into question.
    if (!m_plexTranscodeSession.isEmpty()) {
        qInfo("[VirtualChannels] handing mpv a transcode stream, telling it to start at 0 "
              "(was going to start at %.1f s)", m.value(QStringLiteral("startSeconds")).toDouble());
        m["startSeconds"] = 0.0;
    }
    emit playDescriptorReady(m);
}

void VirtualChannelsBackend::release_tuner() {
    m_tunedChannel = -1;
    if (!m_plexTranscodeSession.isEmpty() && m_plex
        && m_plex->metaObject()->indexOfMethod("stop_transcode(QString)") >= 0) {
        QMetaObject::invokeMethod(m_plex, "stop_transcode",
                                  Q_ARG(QString, m_plexTranscodeSession));
    }
    m_plexTranscodeSession.clear();
}

void VirtualChannelsBackend::onUrlTimeout() {
    if (!m_urlPending) return;
    m_urlPending = false;
    const QString who = MediaServerSource::providerName(m_serverPendingSrc);
    qWarning("[VirtualChannels] %s did not return a stream URL within %d ms",
             qPrintable(who), kUrlTimeoutMs);
    emit playDescriptorReady(
        offAirDescriptor(QStringLiteral("%1 did not respond").arg(who), false));
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

QStringList VirtualChannelsBackend::mediaFilesUnder(const QString &relDir) const {
    QStringList out;

    const QString absRoot = QFileInfo(m_mediaRoot).canonicalFilePath();
    if (absRoot.isEmpty()) return out;

    const QString abs = QDir(absRoot).filePath(relDir);
    const QString canon = QFileInfo(abs).canonicalFilePath();
    if (canon.isEmpty() || !isWithinRoot(canon, absRoot)) {
        qWarning("[VirtualChannels] refusing source directory outside media root: %s", qPrintable(relDir));
        return out;
    }

    const QFileInfo canonInfo(canon);
    if (canonInfo.isFile()) {
        if (kVideoExts.contains(canonInfo.suffix().toLower()))
            out << QDir(absRoot).relativeFilePath(canon);
        return out;
    }

    QDirIterator it(canon, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        if (!kVideoExts.contains(QFileInfo(f).suffix().toLower())) continue;
        out << QDir(absRoot).relativeFilePath(f);
    }
    out.sort();
    return out;
}

QVector<QPair<SlotKind, const char *>> VirtualChannelsBackend::poolFields() {
    return {
        { SlotKind::Programme,  "programmes"  },
        { SlotKind::Intro,      "intros"      },
        { SlotKind::Outro,      "outros"      },
        { SlotKind::Commercial, "commercials" },
        { SlotKind::Bump,       "bumps"       },
    };
}

QVector<VirtualChannelsBackend::PoolJob>
VirtualChannelsBackend::readPools(const QJsonObject &channel, ChannelDef &def) const {
    QVector<PoolJob> jobs;

    const auto packFor = [&](const QJsonObject &o, const QString &label) -> int {
        const QJsonArray in  = o.value(QLatin1String("intros")).toArray();
        const QJsonArray out = o.value(QLatin1String("outros")).toArray();
        if (in.isEmpty() && out.isEmpty()) return -1;

        const int index = int(def.packs.size());
        def.packs.append(BreakPack{ label, {}, {} });
        const auto add = [&](const QJsonArray &from, SlotKind kind) {
            for (const QJsonValue &f : from) {
                const QString folder = f.toString().trimmed();
                if (folder.isEmpty()) continue;
                PoolJob j;
                j.pool    = kind;
                j.wants   = MediaServerSource::Request::Wants::Anything;
                j.src     = SlotSource::Local;
                j.library = folder;
                j.pack    = index;
                jobs.append(j);
            }
        };
        add(in,  SlotKind::Intro);
        add(out, SlotKind::Outro);
        return index;
    };

    for (const auto &field : poolFields()) {
        const SlotKind pool = field.first;
        const QJsonArray arr = channel.value(QLatin1String(field.second)).toArray();

        for (const QJsonValue &v : arr) {
            PoolJob job;
            job.pool = pool;
            job.wants = (pool == SlotKind::Programme)
                            ? MediaServerSource::Request::Wants::Episodes
                            : MediaServerSource::Request::Wants::Anything;

            if (v.isString()) {
                job.src = SlotSource::Local;
                job.library = v.toString();
                if (job.library.trimmed().isEmpty()) continue;
                jobs.append(job);
                continue;
            }
            if (!v.isObject()) continue;

            const QJsonObject o = v.toObject();
            if (pool == SlotKind::Programme) {
                const QString label = o.value(QLatin1String("name")).toString().isEmpty()
                                          ? o.value(QLatin1String("folder")).toString()
                                          : o.value(QLatin1String("name")).toString();
                job.pack = packFor(o, label);
            }
            job.src = slotSourceFromString(o.value(QLatin1String("src")).toString());
            if (job.src == SlotSource::Local) {
                // A local entry is either a library item -- a series or a film
                // chosen from series/ or movies/ -- or a bare folder, which is
                // what break pools point at and what every entry written before
                // the library existed looks like. Both keep working.
                const QString localKind = o.value(QLatin1String("kind")).toString();
                const QString localName = o.value(QLatin1String("name")).toString();

                if (localKind == QLatin1String("series") && !localName.trimmed().isEmpty()) {
                    job.match << localName;
                } else if (localKind == QLatin1String("movie") && !localName.trimmed().isEmpty()) {
                    job.titles << localName;
                } else {
                    job.library = o.value(QLatin1String("folder")).toString();
                    if (job.library.trimmed().isEmpty()) continue;
                }

                // Seasons and episodes switched off are kept in the channel's
                // own source block, not on the entry, so both are read here.
                for (const QJsonObject &lexcl :
                     { o.value(QLatin1String("exclude")).toObject(),
                       channel.value(sourceBlockName(SlotSource::Local)).toObject()
                              .value(QLatin1String("exclude")).toObject() }) {
                    for (const QJsonValue &v : lexcl.value(QLatin1String("seasons")).toArray())
                        if (v.isString()) job.excludeSeasons.insert(v.toString());
                    for (const QString &ep : excludedEpisodesIn(lexcl))
                        job.excludeEpisodes.insert(ep);
                }

                jobs.append(job);
                continue;
            }

            const QString kind = o.value(QLatin1String("kind")).toString();
            const QString name = o.value(QLatin1String("name")).toString();
            if (name.trimmed().isEmpty()) continue;

            // A movie channel reads one of the two ways of choosing films, not
            // both: a playlist airs in its own order, a selection is shuffled,
            // and the sources screen shows only the rows for whichever is in
            // force. Reading both would air films from a row that is not on the
            // screen. What is not read stays in the file, so flipping back
            // brings it round again exactly as it was.
            if (isMovieChannel(channel)
                && playsAPlaylist(channel) != (kind == QLatin1String("playlist")))
                continue;

            // A collection or a playlist is looked for among shows or among
            // films depending on what the channel airs. The two enumerations
            // scan different libraries, so a film collection asked for down the
            // episode path is simply never found -- which is what "No episodes
            // matched" meant on a channel pointed at a collection of films.
            if (kind == QLatin1String("collection") || kind == QLatin1String("playlist")) {
                if (kind == QLatin1String("collection")) job.collections << name;
                else                                     job.playlists   << name;
                if (isMovieChannel(channel))
                    job.wants = MediaServerSource::Request::Wants::Films;
            }
            else if (kind == QLatin1String("series")) {
                // Both, and either will do. The id survives the show being
                // renamed on the server; the name survives it being removed and
                // added back under a new id. Neither can over-reach now that a
                // name is matched in full.
                job.match << name;
                const QString ref = o.value(QLatin1String("ref")).toString().trimmed();
                if (!ref.isEmpty()) job.showIds << ref;
            }
            else if (kind == QLatin1String("movie") || kind == QLatin1String("genre")) {
                // A film is not an episode: it is asked for by title or by
                // genre, the same two ways a movie slot asks. anyFilm is left
                // to the line below, which already says only a library takes
                // everything.
                job.wants = MediaServerSource::Request::Wants::Films;
                if (kind == QLatin1String("movie")) job.titles << name;
                else                                job.genres << name;
            }
            else if (kind == QLatin1String("library"))  job.library      = name;
            else {
                qWarning("[VirtualChannels] ignoring pool entry of unknown kind '%s'",
                         qPrintable(kind));
                continue;
            }
            job.anyFilm = (kind == QLatin1String("library"));

            // Seasons and episodes switched off live in the channel's own
            // source block, keyed by ids only the server can explain, so they
            // stay there rather than being copied onto each entry. An entry
            // still has to honour them: without this, picking a show through
            // the new entry list would quietly re-air every season the viewer
            // had already switched off.
            const QJsonObject entryExcl =
                channel.value(sourceBlockName(job.src)).toObject()
                       .value(QLatin1String("exclude")).toObject();
            for (const QJsonValue &ev : entryExcl.value(QLatin1String("seasons")).toArray())
                if (ev.isString()) job.excludeSeasons.insert(ev.toString());
            for (const QString &ep : excludedEpisodesIn(entryExcl))
                job.excludeEpisodes.insert(ep);

            jobs.append(job);
        }
    }

    bool haveProgrammes = false;
    for (const PoolJob &j : std::as_const(jobs))
        if (j.pool == SlotKind::Programme) { haveProgrammes = true; break; }

    // Collections and playlists picked in the interface are still written to
    // the channel's source block, so they have to be read whether or not the
    // pool also holds entries. Only `match` there is legacy: reading that
    // beside the entries would air the same shows twice. Without this a
    // collection showed as ticked on the sources screen and never aired.
    if (haveProgrammes) {
        for (const SlotSource src : { SlotSource::Plex, SlotSource::Jellyfin,
                                      SlotSource::Emby }) {
            const QString block = sourceBlockName(src);
            if (!channel.contains(block)) continue;
            const QJsonObject cfg = channel.value(block).toObject();

            PoolJob job;
            job.pool    = SlotKind::Programme;
            job.src     = src;
            job.wants   = isMovieChannel(channel)
                              ? MediaServerSource::Request::Wants::Films
                              : MediaServerSource::Request::Wants::Episodes;
            job.anyFilm = false;
            job.library = cfg.value(QLatin1String("library")).toString();
            const bool onlyPlaylists = isMovieChannel(channel) && playsAPlaylist(channel);
            const bool noPlaylists    = isMovieChannel(channel) && !playsAPlaylist(channel);
            if (!onlyPlaylists)
                for (const QJsonValue &v : cfg.value(QLatin1String("collections")).toArray())
                    if (v.isString()) job.collections << v.toString();
            if (!noPlaylists)
                for (const QJsonValue &v : cfg.value(QLatin1String("playlists")).toArray())
                    if (v.isString()) job.playlists << v.toString();
            if (job.collections.isEmpty() && job.playlists.isEmpty()) continue;

            const QJsonObject excl = cfg.value(QLatin1String("exclude")).toObject();
            for (const QJsonValue &v : excl.value(QLatin1String("seasons")).toArray())
                if (v.isString()) job.excludeSeasons.insert(v.toString());
            for (const QString &ep : excludedEpisodesIn(excl))
                job.excludeEpisodes.insert(ep);
            jobs.append(job);
        }
    }

    if (!haveProgrammes) {
        for (const SlotSource src : { SlotSource::Local, SlotSource::Plex,
                                     SlotSource::Jellyfin, SlotSource::Emby }) {
            const QString block = sourceBlockName(src);
            if (!channel.contains(block)) continue;
            const QJsonObject cfg = channel.value(block).toObject();

            PoolJob job;
            job.pool  = SlotKind::Programme;
            job.src   = src;
            job.wants = isMovieChannel(channel)
                            ? MediaServerSource::Request::Wants::Films
                            : MediaServerSource::Request::Wants::Episodes;
            job.library = cfg.value(QLatin1String("library")).toString();
            for (const QJsonValue &v : cfg.value(QLatin1String("match")).toArray())
                if (v.isString()) job.match << v.toString();
            for (const QJsonValue &v : cfg.value(QLatin1String("collections")).toArray())
                if (v.isString()) job.collections << v.toString();
            for (const QJsonValue &v : cfg.value(QLatin1String("playlists")).toArray())
                if (v.isString()) job.playlists << v.toString();
            const QJsonObject excl = cfg.value(QLatin1String("exclude")).toObject();
            for (const QJsonValue &v : excl.value(QLatin1String("seasons")).toArray())
                if (v.isString()) job.excludeSeasons.insert(v.toString());
            for (const QString &ep : excludedEpisodesIn(excl))
                job.excludeEpisodes.insert(ep);
            jobs.prepend(job);
            break;
        }
    }
    return jobs;
}

void VirtualChannelsBackend::appendToPool(const PoolJob &job,
                                          const QVector<MediaItem> &items) {
    if (items.isEmpty()) return;

    if (job.apptIndex >= 0) {
        if (job.apptIndex < m_genDef.appointments.size())
            m_genDef.appointments[job.apptIndex].pool.append(items);
        return;
    }

    if (job.pack >= 0 && job.pack < m_genDef.packs.size()
        && (job.pool == SlotKind::Intro || job.pool == SlotKind::Outro)) {
        if (job.pool == SlotKind::Intro) m_genDef.packs[job.pack].intros.append(items);
        else                             m_genDef.packs[job.pack].outros.append(items);
        return;
    }

    switch (job.pool) {
    case SlotKind::Programme: {
        QVector<MediaItem> tagged = items;
        if (job.pack >= 0 && job.pack < m_genDef.packs.size())
            for (MediaItem &m : tagged) m.pack = job.pack;
        m_genDef.programmes.append(tagged);
        return;
    }
    case SlotKind::Intro:      m_genDef.intros.append(items);      return;
    case SlotKind::Outro:      m_genDef.outros.append(items);      return;
    case SlotKind::Commercial: m_genDef.commercials.append(items); return;
    case SlotKind::Bump:       m_genDef.bumps.append(items);       return;
    case SlotKind::Filler:
    case SlotKind::Unknown:    return;
    }
}

bool VirtualChannelsBackend::bookingWants(const PoolJob &job, const QString &title,
                                          const QStringList &genres) {
    if (job.titles.isEmpty() && job.genres.isEmpty() && job.match.isEmpty()
        && job.collections.isEmpty() && job.playlists.isEmpty())
        return job.anyFilm;

    for (const QString &t : job.titles)
        if (title.compare(t, Qt::CaseInsensitive) == 0) return true;
    for (const QString &g : job.genres)
        for (const QString &has : genres)
            if (has.compare(g, Qt::CaseInsensitive) == 0) return true;
    for (const QString &needle : job.match)
        if (!needle.isEmpty() && title.contains(needle, Qt::CaseInsensitive)) return true;
    return false;
}

void VirtualChannelsBackend::parseBookings(const QJsonObject &o, int channelNumber,
                                           ChannelDef &def,
                                           QVector<PoolJob> &plexJobs,
                                           QVector<PoolJob> &serverJobs,
                                           QVector<QPair<int, QStringList>> *folderPools,
                                           QVector<QPair<int, QStringList>> *localTitles) {
    for (const QJsonValue &av : o.value(QLatin1String("appointments")).toArray()) {
        if (!av.isObject()) continue;
        const QJsonObject ao = av.toObject();

        Appointment appt;
        appt.name        = ao.value(QLatin1String("name")).toString();
        appt.minuteOfDay = minuteOfDayFromString(ao.value(QLatin1String("at")).toString());
        if (appt.minuteOfDay < 0) {
            qWarning("[VirtualChannels] channel %d: booking \"%s\" has no usable time, ignored",
                     channelNumber, qPrintable(appt.name));
            continue;
        }
        for (const QJsonValue &dv : ao.value(QLatin1String("days")).toArray()) {
            const int day = dayOfWeekFromString(dv.toString());
            if (day > 0) appt.days.append(day);
        }

        def.appointments.append(appt);
        const int idx = def.appointments.size() - 1;

        QStringList folders;
        const QJsonValue folder = ao.value(QLatin1String("folder"));
        if (folder.isString()) folders << folder.toString();
        for (const QJsonValue &fv : ao.value(QLatin1String("folders")).toArray())
            if (fv.isString()) folders << fv.toString();
        if (!folders.isEmpty()) {
            if (folderPools) {
                folderPools->append(qMakePair(idx, folders));
            } else {
                qWarning("[VirtualChannels] channel %d: booking \"%s\" draws on local folders, "
                         "which a server-sourced channel cannot use; that part is ignored",
                         channelNumber, qPrintable(appt.name));
            }
        }

        if (localTitles) {
            QStringList picks;
            for (const QJsonValue &tv :
                 ao.value(sourceBlockName(SlotSource::Local)).toObject()
                   .value(QLatin1String("titles")).toArray())
                if (tv.isString() && !tv.toString().trimmed().isEmpty()) picks << tv.toString();
            if (!picks.isEmpty()) localTitles->append(qMakePair(idx, picks));
        }

        const QJsonObject apptPlex = ao.value(QLatin1String("plex")).toObject();
        if (!apptPlex.isEmpty()) {
            if (!m_plex) {
                qWarning("[VirtualChannels] channel %d: booking \"%s\" wants Plex, which is unavailable",
                         channelNumber, qPrintable(appt.name));
            } else {
                PoolJob job;
                job.apptIndex  = idx;
                job.wants      = MediaServerSource::Request::Wants::Films;
                job.src     = SlotSource::Plex;
                job.anyFilm = ao.value(QLatin1String("any_film")).toBool(true);
                job.library = apptPlex.value(QLatin1String("library")).toString();
                for (const QJsonValue &tv : apptPlex.value(QLatin1String("titles")).toArray())
                    if (tv.isString()) job.titles << tv.toString();
                for (const QJsonValue &gv : apptPlex.value(QLatin1String("genres")).toArray())
                    if (gv.isString()) job.genres << gv.toString();
                for (const QJsonValue &cv : apptPlex.value(QLatin1String("collections")).toArray())
                    if (cv.isString()) job.collections << cv.toString();
                for (const QJsonValue &pv : apptPlex.value(QLatin1String("playlists")).toArray())
                    if (pv.isString()) job.playlists << pv.toString();
                for (const QJsonValue &mv : apptPlex.value(QLatin1String("match")).toArray())
                    if (mv.isString()) job.match << mv.toString();
                plexJobs.append(job);
            }
        }
        for (const SlotSource src : { SlotSource::Jellyfin, SlotSource::Emby }) {
            const QJsonObject apptCfg = ao.value(sourceBlockName(src)).toObject();
            if (apptCfg.isEmpty()) continue;
            if (!m_server->available(src)) {
                qWarning("[VirtualChannels] channel %d: booking \"%s\" wants %s, which is unavailable",
                         channelNumber, qPrintable(appt.name),
                         qPrintable(MediaServerSource::providerName(src)));
                continue;
            }
            PoolJob job;
            job.apptIndex  = idx;
            job.wants      = MediaServerSource::Request::Wants::Films;
            job.src     = src;
            job.anyFilm = ao.value(QLatin1String("any_film")).toBool(true);
            job.library = apptCfg.value(QLatin1String("library")).toString();
            for (const QJsonValue &tv : apptCfg.value(QLatin1String("titles")).toArray())
                if (tv.isString()) job.titles << tv.toString();
            for (const QJsonValue &gv : apptCfg.value(QLatin1String("genres")).toArray())
                if (gv.isString()) job.genres << gv.toString();
            for (const QJsonValue &cv : apptCfg.value(QLatin1String("collections")).toArray())
                if (cv.isString()) job.collections << cv.toString();
            for (const QJsonValue &mv : apptCfg.value(QLatin1String("match")).toArray())
                if (mv.isString()) job.match << mv.toString();
            serverJobs.append(job);
        }
    }
}

qint64 VirtualChannelsBackend::rotationAt(int channelNumber, qint64 t) {
    const ChannelSchedule &old = scheduleFor(channelNumber);
    if (!old.isValid()) return 0;

    qint64 aired = 0;
    for (const Slot &s : old.slotList()) {
        if (s.start >= t) break;
        if (s.kind == SlotKind::Programme) ++aired;
    }
    return old.rotation() + aired;
}

void VirtualChannelsBackend::marksAt(int channelNumber, qint64 t,
                                     QHash<QString, QString> *perSeries, QString *overall) {
    const ChannelSchedule &old = scheduleFor(channelNumber);
    if (!old.isValid()) return;

    // Carry forward what the last build was told, then replay everything that
    // has aired since, so the marks describe this moment rather than the moment
    // the window was written.
    if (perSeries) *perSeries = old.marks();
    if (overall)   *overall   = old.mark();

    for (const Slot &s : old.slotList()) {
        if (s.start >= t) break;
        if (s.kind != SlotKind::Programme || s.ref.isEmpty()) continue;
        if (overall) *overall = s.ref;
        if (perSeries) {
            const QString name = s.series.trimmed();
            perSeries->insert(name.isEmpty() ? unnamedSeriesKey() : name.toLower(),
                              s.ref);
        }
    }
}

void VirtualChannelsBackend::regenerate(int channelNumber) {
    if (m_genActive) {
        qWarning("[VirtualChannels] generation already running for channel %d", m_genChannel);

        if (m_rebuildTotal > 0) {
            m_rebuildQueue.prepend(channelNumber);
            return;
        }

        emit generationFinished(channelNumber, false,
                                QStringLiteral("Another channel is building"));
        return;
    }

    const QVariantMap raw = channelObject(channelNumber);
    if (raw.isEmpty()) {
        generationEnded(channelNumber, false, QStringLiteral("No such channel"));
        return;
    }
    const QJsonObject o = QJsonObject::fromVariantMap(raw);

    QVector<PoolJob> apptJobs;
    QVector<PoolJob> serverApptJobs;

    ChannelDef def;
    def.number       = channelNumber;
    def.name         = o.value(QLatin1String("name")).toString();
    def.seed         = static_cast<quint32>(o.value(QLatin1String("seed")).toDouble(channelNumber));
    def.horizonHours = double(schedule_days()) * 24.0;
    def.order        = orderingFromString(o.value(QLatin1String("order")).toString());
    def.rotation     = rotationAt(channelNumber, nowMs());
    marksAt(channelNumber, nowMs(), &def.marks, &def.mark);
    def.adsPerBreak  = o.value(QLatin1String("ads_per_break")).toInt(0);
    {
        const int grid = o.value(QLatin1String("grid_minutes")).toInt(0);
        def.gridMinutes = (grid >= kMinGridMinutes && grid <= kMaxGridMinutes) ? grid : 0;
        if (grid != 0 && def.gridMinutes == 0)
            qWarning("[VirtualChannels] channel %d: ignoring unusable grid of %d minutes",
                     channelNumber, grid);
    }

    // A movie channel decides these itself, so the sources screen carries no
    // ordering or timing row to disagree with. Its running order follows where
    // the films come from: a playlist airs in the order it was written, a
    // selection is shuffled. Films do not sit on a clock -- a two-hour film on
    // a half-hour grid is a card holding the remainder every time.
    if (isMovieChannel(o)) {
        def.order       = playsAPlaylist(o) ? Ordering::AsListed : Ordering::Shuffle;
        def.gridMinutes = 0;
    }

    QVector<PoolJob> jobs = readPools(o, def);

    QVector<QPair<int, QStringList>> folderPools;
    QVector<QPair<int, QStringList>> localApptTitles;
    // Movie slots are read for every channel but a movie one, where booking a
    // film to a time means nothing because every programme is already a film.
    // The slots stay in the file untouched, so switching the channel back to TV
    // brings them back exactly as they were -- but nothing airs from them while
    // the sources screen is not showing them.
    if (!isMovieChannel(o))
        parseBookings(o, channelNumber, def, apptJobs, serverApptJobs, &folderPools,
                      &localApptTitles);

    m_genQueue.clear();
    m_genCursor = 0;
    const QString absRoot = QFileInfo(m_mediaRoot).canonicalFilePath();
    const auto enqueue = [&](const QStringList &dirs, SlotKind kind,
                             int apptIndex = -1, int pack = -1) {
        for (const QString &d : dirs)
            for (const QString &rel : mediaFilesUnder(d))
                m_genQueue.push_back({ QDir(absRoot).filePath(rel), rel, kind, apptIndex, pack });
    };
    m_localLibrary.setMediaRoot(m_mediaRoot);
    // The between-build check lists series/ and movies/ only, so it cannot see
    // an episode added inside a show it already knows.
    m_localLibrary.refresh();
    for (const auto &pool : std::as_const(localApptTitles)) {
        for (const QString &title : pool.second) {
            const QString rel = m_localLibrary.movieRefFor(title);
            if (rel.isEmpty()) {
                qWarning("[VirtualChannels] channel %d: slot film \"%s\" is not in the library",
                         channelNumber, qPrintable(title));
                continue;
            }
            m_genQueue.push_back({ QDir(absRoot).filePath(rel), rel,
                                   SlotKind::Programme, pool.first, -1 });
        }
    }

    // Films only here. Every other folder is taken as it is -- a bump named
    // "trailer" is a bump.
    for (const auto &pool : std::as_const(folderPools)) {
        for (const QString &d : pool.second)
            for (const QString &rel : mediaFilesUnder(d)) {
                if (vchan::LocalLibrary::isExtraPath(rel)) continue;
                m_genQueue.push_back({ QDir(absRoot).filePath(rel), rel,
                                       SlotKind::Programme, pool.first, -1 });
            }
    }

    QVector<PoolJob> plexJobs   = apptJobs;
    QVector<PoolJob> serverJobs = serverApptJobs;
    for (const PoolJob &job : std::as_const(jobs)) {
        switch (job.src) {
        case SlotSource::Local:
            if (!job.match.isEmpty() || !job.titles.isEmpty()) {
                // Library items resolve through the same scan the picker
                // browsed, so what was ticked is what airs.
                m_localLibrary.setMediaRoot(m_mediaRoot);
                QStringList refs;
                for (const QString &showName : job.match) {
                    const auto eps = m_localLibrary.episodesFor(
                        showName,
                        QStringList(job.excludeSeasons.cbegin(), job.excludeSeasons.cend()),
                        QStringList(job.excludeEpisodes.cbegin(), job.excludeEpisodes.cend()));
                    if (eps.isEmpty())
                        qWarning("[VirtualChannels] channel %d: local series '%s' matched nothing",
                                 channelNumber, qPrintable(showName));

                    // The show, not the season folder the file happens to sit
                    // in: grouping keys on this, and every "Season 1" would
                    // otherwise be treated as one programme.
                    QString display = showName;
                    qint64  showAir = 0;
                    for (const vchan::LocalShow &sh : m_localLibrary.shows()) {
                        if (!vchan::LocalLibrary::matchesName(sh.name, sh.year, sh.folder, showName))
                            continue;
                        display = sh.name;
                        showAir = airedAtMs(QString(), sh.year > 0 ? QVariant(sh.year) : QVariant());
                        break;
                    }
                    for (const vchan::LocalEpisode &ep : eps)
                        m_genQueue.push_back({ QDir(absRoot).filePath(ep.ref), ep.ref,
                                               job.pool, job.apptIndex, job.pack,
                                               display, showAir, ep.season, ep.number });
                }
                for (const QString &film : job.titles) {
                    if (job.excludeEpisodes.contains(film)) continue;
                    const QString ref = m_localLibrary.movieRefFor(film);
                    if (ref.isEmpty()) {
                        qWarning("[VirtualChannels] channel %d: local film '%s' matched nothing",
                                 channelNumber, qPrintable(film));
                        continue;
                    }
                    // A film's year, so broadcast order can place it among the
                    // episodes rather than in the undated pile at the end.
                    qint64 filmAir = 0;
                    for (const vchan::LocalMovie &mv : m_localLibrary.movies()) {
                        if (mv.ref != ref) continue;
                        filmAir = airedAtMs(QString(), mv.year > 0 ? QVariant(mv.year) : QVariant());
                        break;
                    }
                    m_genQueue.push_back({ QDir(absRoot).filePath(ref), ref,
                                           job.pool, job.apptIndex, job.pack,
                                           QString(), filmAir, -1, -1 });
                }
                for (const QString &rel : std::as_const(refs))
                    m_genQueue.push_back({ QDir(absRoot).filePath(rel), rel,
                                           job.pool, job.apptIndex, job.pack });
            } else {
                enqueue({ job.library }, job.pool, job.apptIndex, job.pack);
            }
            break;
        case SlotSource::Plex:
            if (!m_plex) {
                qWarning("[VirtualChannels] channel %d: Plex is not available for one of its pools",
                         channelNumber);
                break;
            }
            plexJobs.append(job);
            break;
        case SlotSource::Jellyfin:
        case SlotSource::Emby:
            if (!m_server->available(job.src)) {
                qWarning("[VirtualChannels] channel %d: %s is not available for one of its pools",
                         channelNumber, qPrintable(MediaServerSource::providerName(job.src)));
                break;
            }
            serverJobs.append(job);
            break;
        }
    }

    if (m_genQueue.isEmpty() && plexJobs.isEmpty() && serverJobs.isEmpty()) {
        generationEnded(channelNumber, false,
                        QStringLiteral("No media found for this channel"));
        return;
    }
    if (!m_genQueue.isEmpty() && !m_probe.isUsable()) {
        generationEnded(channelNumber, false,
                        QStringLiteral("No ffprobe or mpv available to read durations"));
        return;
    }

    if (!def.appointments.isEmpty())
        qDebug("[VirtualChannels] channel %d: %lld appointment(s) declared",
               channelNumber, static_cast<long long>(def.appointments.size()));

    m_genActive        = true;
    m_genChannel       = channelNumber;
    m_genDef           = def;
    m_pgApptJobs       = plexJobs;
    m_pgApptCursor     = 0;
    m_serverApptJobs   = serverJobs;
    m_serverApptCursor = 0;
    m_serverJobActive  = false;

    if (!m_genQueue.isEmpty()) {
        emit generationProgress(channelNumber, 0, m_genQueue.size());
        m_genTimer->start();
        return;
    }
    emit generationProgress(channelNumber, 0, 0);
    serverApptNext();
}

void VirtualChannelsBackend::onGenerationTick() {
    if (!m_genActive) { m_genTimer->stop(); return; }

    QElapsedTimer tick;
    tick.start();
    const int start = m_genCursor;
    const int end   = qMin(m_genCursor + kProbeBatchPerTick, m_genQueue.size());
    for (; m_genCursor < end; ++m_genCursor) {
        if (m_genCursor > start && tick.elapsed() > kProbeBudgetPerTickMs) break;

        const PendingItem &p = m_genQueue[m_genCursor];
        const qint64 dur = m_probe.durationMs(p.absPath);
        if (dur <= 0) {
            qWarning("[VirtualChannels] no usable duration, skipping: %s", qPrintable(p.rel));
            continue;
        }
        MediaItem m;
        m.ref   = p.rel;
        m.src   = SlotSource::Local;
        m.durMs = dur;
        m.title = QFileInfo(p.rel).completeBaseName();
        m.airMs     = p.airMs;
        m.seasonNo  = p.seasonNo;
        m.episodeNo = p.episodeNo;
        if (!p.series.isEmpty()) {
            m.series = p.series;
        } else {
            const QString parent = QFileInfo(p.rel).dir().dirName();
            if (!parent.isEmpty() && parent != QLatin1String(".")) m.series = parent;
        }

        if (p.apptIndex >= 0 && p.apptIndex < m_genDef.appointments.size()) {
            m_genDef.appointments[p.apptIndex].pool.append(m);
            continue;
        }

        const bool hasPack = p.pack >= 0 && p.pack < m_genDef.packs.size();
        if (hasPack && p.kind == SlotKind::Intro) {
            m_genDef.packs[p.pack].intros.append(m);
            continue;
        }
        if (hasPack && p.kind == SlotKind::Outro) {
            m_genDef.packs[p.pack].outros.append(m);
            continue;
        }
        if (hasPack && p.kind == SlotKind::Programme) m.pack = p.pack;

        switch (p.kind) {
        case SlotKind::Programme:  m_genDef.programmes.append(m);  break;
        case SlotKind::Intro:      m_genDef.intros.append(m);      break;
        case SlotKind::Outro:      m_genDef.outros.append(m);      break;
        case SlotKind::Commercial: m_genDef.commercials.append(m); break;
        case SlotKind::Bump:       m_genDef.bumps.append(m);       break;
        case SlotKind::Filler:
        case SlotKind::Unknown:    break;
        }
    }

    emit generationProgress(m_genChannel, m_genCursor, m_genQueue.size());
    if (m_genCursor < m_genQueue.size())
        return;

    m_genTimer->stop();
    m_probe.save();

    serverApptNext();
}

void VirtualChannelsBackend::finishLocalGeneration() {
    // One episode can reach the pool by more than one road -- picked as a
    // series and again inside a collection that holds the same show. A pool
    // holding it twice airs it twice as often as everything beside it, and
    // the second copy sits one place along, so a rebuild resuming after the
    // first lands straight back on it. The first arrival wins.
    {
        QSet<QString> seen;
        QVector<MediaItem> once;
        once.reserve(m_genDef.programmes.size());
        for (const MediaItem &m : std::as_const(m_genDef.programmes)) {
            if (!m.ref.isEmpty() && seen.contains(m.ref)) continue;
            if (!m.ref.isEmpty()) seen.insert(m.ref);
            once.append(m);
        }
        if (once.size() != m_genDef.programmes.size())
            qInfo("[VirtualChannels] channel %d: %lld programme(s) reached the pool "
                  "more than once and were kept once",
                  m_genChannel,
                  static_cast<long long>(m_genDef.programmes.size() - once.size()));
        m_genDef.programmes = once;
    }

    if (!m_genDef.isPlayable()) {
        finishGeneration(false, QStringLiteral("No programmes with a usable duration"));
        return;
    }

    const qint64 start = nowMs();
    const QVector<Slot> placed = generateSlots(m_genDef, start);
    if (placed.isEmpty()) {
        finishGeneration(false, QStringLiteral("Generated an empty timeline"));
        return;
    }

    const QString path = scheduleFilePath(m_genChannel);
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        finishGeneration(false, QStringLiteral("Could not write the schedule"));
        return;
    }
    f.write(serializeSchedule(m_genDef, start - 1000, placed));
    if (!f.commit()) {
        finishGeneration(false, QStringLiteral("Could not commit the schedule"));
        return;
    }

    invalidateCache(m_genChannel);

    const double hours = double(placed.last().end() - start) / 3600000.0;
    QString summary = QStringLiteral("%1 slots over %2 hours")
                          .arg(placed.size())
                          .arg(hours, 0, 'f', 1);
    if (placed.size() >= kMaxSlotsPerChannel)
        summary += QStringLiteral(" (slot limit reached before %1 day(s) — "
                                  "this channel's programmes are very short)")
                       .arg(schedule_days());

    finishGeneration(true, summary);
}

void VirtualChannelsBackend::generationEnded(int channelNumber, bool ok,
                                            const QString &message) {
    emit generationFinished(channelNumber, ok, message);

    if (m_rebuildTotal > 0) {
        if (ok) ++m_rebuildOk;
        else    ++m_rebuildFailed;
    }
    rebuildNextOrFinish();
}

void VirtualChannelsBackend::rebuildNextOrFinish() {
    if (!m_rebuildQueue.isEmpty()) {
        const int next = m_rebuildQueue.takeFirst();
        if (m_rebuildTotal > 0) {
            const int done = m_rebuildOk + m_rebuildFailed;
            emit actionStatus(QStringLiteral("Rebuilding channel %1 — %2 of %3")
                                  .arg(next).arg(done + 1).arg(m_rebuildTotal));
        }
        QMetaObject::invokeMethod(this, [this, next] { regenerate(next); },
                                  Qt::QueuedConnection);
        return;
    }

    if (m_rebuildTotal <= 0) return;

    QString summary;
    if (m_rebuildFailed == 0)
        summary = QStringLiteral("Rebuilt %1 channel%2")
                      .arg(m_rebuildOk).arg(m_rebuildOk == 1 ? "" : "s");
    else if (m_rebuildOk == 0)
        summary = QStringLiteral("Could not rebuild any of %1 channels")
                      .arg(m_rebuildFailed);
    else
        summary = QStringLiteral("Rebuilt %1 of %2 channels — %3 had nothing to build")
                      .arg(m_rebuildOk).arg(m_rebuildOk + m_rebuildFailed).arg(m_rebuildFailed);

    qInfo("[VirtualChannels] rebuild all: %s", qPrintable(summary));
    emit actionStatus(summary);
    m_rebuildTotal = m_rebuildOk = m_rebuildFailed = 0;
}

void VirtualChannelsBackend::finishGeneration(bool ok, const QString &message) {
    const int ch = m_genChannel;
    m_genActive = false;
    m_genChannel = -1;
    m_genQueue.clear();
    m_genDef = ChannelDef{};
    m_genTimer->stop();
    if (ok) qInfo("[VirtualChannels] channel %d regenerated: %s", ch, qPrintable(message));
    else    qWarning("[VirtualChannels] channel %d generation failed: %s", ch, qPrintable(message));

    generationEnded(ch, ok, message);
}

// ---------------------------------------------------------------------------
// Plex enumeration
//
// Plex answers with one signal per kind (librariesLoaded / itemsLoaded /
// childrenLoaded) carrying no reference to the request that produced it. There
// is therefore no way to run several lookups at once and tell the replies
// apart, so enumeration is strictly serialised: issue one call, wait, advance.
// For a handful of series that is a few dozen round trips, which is fine as a
// one-off at generation time.
// ---------------------------------------------------------------------------

namespace {

constexpr int kPlexEnumTimeoutMs = 20000;
}

void VirtualChannelsBackend::ensurePlexTimer() {
    if (m_pgTimer) return;
    m_pgTimer = new QTimer(this);
    m_pgTimer->setSingleShot(true);
    m_pgTimer->setInterval(kPlexEnumTimeoutMs);
    connect(m_pgTimer, &QTimer::timeout, this, &VirtualChannelsBackend::onPlexEnumTimeout);
}

bool VirtualChannelsBackend::plexExcluded(const QString &ratingKey, bool isSeason) const {
    return isSeason ? m_pgExcludeSeasons.contains(ratingKey)
                    : m_pgExcludeEpisodes.contains(ratingKey);
}

bool VirtualChannelsBackend::plexItemToMedia(const QVariantMap &m, MediaItem *out) const {
    if (!out) return false;
    const QString key = m.value("ratingKey").toString();
    const qint64  dur = qint64(m.value("duration").toLongLong());
    if (key.isEmpty() || dur <= 0) return false;

    out->src    = SlotSource::Plex;
    out->ref    = key;
    out->durMs  = dur;
    out->title  = m.value("title").toString();
    out->series = m.value("grandparentTitle").toString();
    out->desc   = m.value("summary").toString();
    for (const char *k : { "thumb", "parentThumb", "grandparentThumb" }) {
        const QString v = m.value(QLatin1String(k)).toString();
        if (!v.isEmpty()) { out->art = v; break; }
    }
    const int season  = m.value("parentIndex").toInt();
    const int episode = m.value("index").toInt();
    out->seasonNo  = season  > 0 ? season  : -1;
    out->episodeNo = episode > 0 ? episode : -1;
    if (season > 0 && episode > 0)
        out->ep = QStringLiteral("S%1E%2")
                      .arg(season, 2, 10, QLatin1Char('0'))
                      .arg(episode, 2, 10, QLatin1Char('0'));

    QVariant year = m.value("year");
    if (!year.isValid() || year.toInt() <= 0) year = m.value("parentYear");
    out->airMs = airedAtMs(m.value("originallyAvailableAt").toString(), year);
    return true;
}

void VirtualChannelsBackend::plexEnumStart(int channelNumber,
                                           const ChannelDef &def,
                                           const QString &library,
                                           const QStringList &match,
                                           const QStringList &showIds) {
    if (!m_plex) { plexEnumFail(QStringLiteral("Plex is not available")); return; }

    m_pgChannel     = channelNumber;
    m_pgDef         = def;
    m_pgLibraryName = library;
    m_pgMatch       = match;
    m_pgShowIds     = showIds;
    m_pgSections.clear();
    m_pgShows.clear();
    m_pgSeasons.clear();
    m_pgEpisodes.clear();
    m_pgPendingCollections.clear();
    m_pgPendingPlaylists.clear();
    m_pgAskedCollections = false;
    m_pgAskedPlaylists   = false;

    ensurePlexTimer();

    m_pgStage = PlexStage::Libraries;
    m_pgTimer->start();
    emit generationProgress(channelNumber, 0, 0);
    QMetaObject::invokeMethod(m_plex, "load_libraries");
}

void VirtualChannelsBackend::plexEnumNext() {
    m_pgTimer->start();

    if (!m_pgPendingCollections.isEmpty()) {
        m_pgStage = PlexStage::CollectionItems;
        QMetaObject::invokeMethod(m_plex, "load_collection_items",
                                  Q_ARG(QString, m_pgPendingCollections.takeFirst()));
        return;
    }
    if (!m_pgPendingPlaylists.isEmpty()) {
        m_pgStage = PlexStage::PlaylistItems;
        QMetaObject::invokeMethod(m_plex, "load_playlist_items",
                                  Q_ARG(QString, m_pgPendingPlaylists.takeFirst()));
        return;
    }

    if (!m_pgShows.isEmpty()) {
        m_pgStage = PlexStage::Seasons;
        const QString key = m_pgShows.takeFirst();
        QMetaObject::invokeMethod(m_plex, "load_children", Q_ARG(QString, key));
        return;
    }
    if (!m_pgSeasons.isEmpty()) {
        m_pgStage = PlexStage::Episodes;
        const QString key = m_pgSeasons.takeFirst();
        QMetaObject::invokeMethod(m_plex, "load_children", Q_ARG(QString, key));
        return;
    }
    if (!m_pgSections.isEmpty()) {
        const QString sec = m_pgSections.takeFirst();
        if (!m_pgCollections.isEmpty() && !m_pgAskedCollections) {
            m_pgStage = PlexStage::Collections;
            m_pgSections.prepend(sec);
            m_pgAskedCollections = true;
            QMetaObject::invokeMethod(m_plex, "load_collections", Q_ARG(QString, sec));
            return;
        }
        if (!m_pgPlaylists.isEmpty() && !m_pgAskedPlaylists) {
            m_pgStage = PlexStage::Playlists;
            m_pgSections.prepend(sec);
            m_pgAskedPlaylists = true;
            QMetaObject::invokeMethod(m_plex, "load_playlists", Q_ARG(QString, sec));
            return;
        }
        m_pgStage = PlexStage::Shows;
        QMetaObject::invokeMethod(m_plex, "load_library_all", Q_ARG(QString, sec));
        return;
    }
    m_pgTimer->stop();
    plexEnumFinish();
}

void VirtualChannelsBackend::onPlexLibrariesLoaded(const QVariant &libraries) {
    if (m_pgStage == PlexStage::BrowseLibraries) {
        m_browseItems.clear();
        m_pgSections.clear();
        const bool showsOnly  = (m_browseKind == QLatin1String("shows"));
        const bool moviesOnly  = m_browseKind.startsWith(QLatin1String("movie"));
        for (const QVariant &v : libraries.toList()) {
            const QVariantMap m = v.toMap();
            const QString type = m.value("sectionType").toString();
            const bool keep = showsOnly  ? (type == QLatin1String("show"))
                            : moviesOnly ? (type == QLatin1String("movie"))
                            : (type == QLatin1String("show") || type == QLatin1String("movie"));
            if (!keep) continue;
            QString id = m.value("sectionId").toString();
            if (id.isEmpty()) id = m.value("key").toString();
            if (!id.isEmpty()) m_pgSections << id;
        }
        if (m_pgSections.isEmpty()) { browseFail(QStringLiteral("No matching libraries")); return; }

        const QString sec = m_pgSections.takeFirst();
        m_pgTimer->start();
        if (m_browseKind == QLatin1String("shows")) {
            m_pgStage = PlexStage::BrowseShows;
            QMetaObject::invokeMethod(m_plex, "load_library_all", Q_ARG(QString, sec));
        } else if (m_browseKind == QLatin1String("movies")
                   || m_browseKind == QLatin1String("moviegenres")) {
            m_pgStage = PlexStage::BrowseMovies;
            QMetaObject::invokeMethod(m_plex, "load_library_all", Q_ARG(QString, sec));
        } else if (m_browseKind == QLatin1String("collections")
                   || m_browseKind == QLatin1String("moviecollections")) {
            m_pgStage = PlexStage::BrowseCollections;
            QMetaObject::invokeMethod(m_plex, "load_collections", Q_ARG(QString, sec));
        } else {
            m_pgStage = PlexStage::BrowsePlaylists;
            QMetaObject::invokeMethod(m_plex, "load_playlists", Q_ARG(QString, sec));
        }
        return;
    }

    if (m_pgStage == PlexStage::ApptLibraries) {
        const PoolJob &job = m_pgApptJobs[m_pgApptCursor];
        for (const QVariant &v : libraries.toList()) {
            const QVariantMap m = v.toMap();
            if (m.value("sectionType").toString() != QLatin1String("movie")) continue;
            if (!job.library.isEmpty()
                && m.value("title").toString().compare(job.library, Qt::CaseInsensitive) != 0)
                continue;
            QString id = m.value("sectionId").toString();
            if (id.isEmpty()) id = m.value("key").toString();
            if (!id.isEmpty()) m_pgApptSections << id;
        }
        if (m_pgApptSections.isEmpty()) {
            qWarning("[VirtualChannels] no movie library found for a booking; it will not air");
            ++m_pgApptCursor;
            plexApptNext();
            return;
        }
        m_pgApptAllSections = m_pgApptSections;
        m_pgApptPhase = ApptPhase::Items;
        plexApptAdvance();
        return;
    }

    if (m_pgStage != PlexStage::Libraries) return;

    for (const QVariant &v : libraries.toList()) {
        const QVariantMap m = v.toMap();
        if (m.value("sectionType").toString() != QLatin1String("show")) continue;
        if (!m_pgLibraryName.isEmpty()
            && m.value("title").toString().compare(m_pgLibraryName, Qt::CaseInsensitive) != 0)
            continue;
        QString id = m.value("sectionId").toString();
        if (id.isEmpty()) id = m.value("key").toString();
        if (!id.isEmpty()) m_pgSections << id;
    }

    if (m_pgSections.isEmpty()) {
        plexEnumFail(m_pgLibraryName.isEmpty()
                         ? QStringLiteral("No TV libraries found on Plex")
                         : QStringLiteral("No TV library named %1").arg(m_pgLibraryName));
        return;
    }
    plexEnumNext();
}

void VirtualChannelsBackend::onPlexItemsLoaded(const QVariant &items) {
    if (m_pgStage == PlexStage::BrowseMovies) {
        const QString field = m_browseKind == QLatin1String("moviegenres")
                                  ? QStringLiteral("genres") : QString();

        for (const QVariant &v : items.toList()) {
            const QVariantMap m = v.toMap();
            if (m.value("type").toString() != QLatin1String("movie")) continue;

            QStringList values;
            if (field.isEmpty()) values << m.value("title").toString();
            else                 values = m.value(field).toStringList();

            for (const QString &value : std::as_const(values)) {
                if (value.isEmpty()) continue;
                // Tallied rather than de-duplicated by scanning what is already
                // there: the picker says how many films carry a genre, and a
                // genre with one film in it is worth seeing before you pick it.
                const int seen = ++m_browseTally[value];
                if (seen > 1) continue;
                m_browseItems.append(QVariantMap{{"id", value}, {"label", value},
                                                 {"sub", QString()}});
            }
        }
        if (!m_pgSections.isEmpty()) {
            m_pgTimer->start();
            QMetaObject::invokeMethod(m_plex, "load_library_all",
                                      Q_ARG(QString, m_pgSections.takeFirst()));
            return;
        }
        m_pgStage = PlexStage::Idle;
        m_pgTimer->stop();
        if (m_browseKind == QLatin1String("moviegenres")) {
            for (QVariant &e : m_browseItems) {
                QVariantMap row = e.toMap();
                row["sub"] = countedAs(m_browseTally.value(row.value("label").toString()),
                                       "FILM", "FILMS");
                e = row;
            }
        }
        if (m_browseKind != QLatin1String("movies")) {
            std::sort(m_browseItems.begin(), m_browseItems.end(),
                      [](const QVariant &a, const QVariant &b) {
                          return a.toMap().value("label").toString().compare(
                                     b.toMap().value("label").toString(),
                                     Qt::CaseInsensitive) < 0;
                      });
        }
        const QString kind = m_browseKind; m_browseKind.clear();
        emit sourceBrowseReady(kind, m_browseItems);
        m_browseItems.clear();
        return;
    }

    if (m_pgStage == PlexStage::BrowseShows) {
        for (const QVariant &v : items.toList()) {
            const QVariantMap m = v.toMap();
            const QString key = m.value("ratingKey").toString();
            const QString title = m.value("title").toString();
            if (key.isEmpty() || title.isEmpty()) continue;
            m_browseItems.append(QVariantMap{{"id", key}, {"label", title}, {"sub", QString()}});
        }
        if (!m_pgSections.isEmpty()) {
            m_pgTimer->start();
            QMetaObject::invokeMethod(m_plex, "load_library_all",
                                      Q_ARG(QString, m_pgSections.takeFirst()));
            return;
        }
        m_pgStage = PlexStage::Idle;
        m_pgTimer->stop();
        const QString kind = m_browseKind; m_browseKind.clear();
        emit sourceBrowseReady(kind, m_browseItems);
        m_browseItems.clear();
        return;
    }

    if (m_pgStage == PlexStage::ApptItems) {
        plexApptCollect(items.toList(), /*applyCriteria*/ true);
        plexApptAdvance();
        return;
    }

    if (m_pgStage == PlexStage::ApptCollectionItems
        || m_pgStage == PlexStage::ApptPlaylistItems) {
        plexApptCollect(items.toList(), /*applyCriteria*/ false);
        plexApptAdvance();
        return;
    }

    if (m_pgStage == PlexStage::CollectionItems || m_pgStage == PlexStage::PlaylistItems) {
        for (const QVariant &v : items.toList()) {
            const QVariantMap m = v.toMap();
            const QString type = m.value("type").toString();
            const QString key  = m.value("ratingKey").toString();
            if (key.isEmpty()) continue;

            if (type == QLatin1String("show")) {
                m_pgShows << key;
                continue;
            }
            if (plexExcluded(key, /*isSeason*/ false)) continue;
            MediaItem item;
            if (plexItemToMedia(m, &item)) m_pgEpisodes.push_back(item);
        }
        emit generationProgress(m_pgChannel, m_pgEpisodes.size(),
                                m_pgEpisodes.size() + m_pgShows.size());
        plexEnumNext();
        return;
    }

    if (m_pgStage != PlexStage::Shows) return;

    // Naming nothing at all means the whole library, which is what a channel
    // pointed at a library asks for. A pass scoped to a collection or a playlist
    // has named something: its shows arrive from that listing, so taking them
    // from here as well would sweep the library in behind them.
    const bool takesWholeLibrary =
        m_pgMatch.isEmpty() && m_pgShowIds.isEmpty()
        && m_pgCollections.isEmpty() && m_pgPlaylists.isEmpty();

    for (const QVariant &v : items.toList()) {
        const QVariantMap m = v.toMap();
        const QString title = m.value("title").toString();
        const QString key   = m.value("ratingKey").toString();

        // A show is taken by its id where the picker stored one, and otherwise
        // by its whole name. Matching a name as a substring made one entry
        // claim every show whose title began the same way, so "STAR TREK" took
        // the franchise. Jellyfin and Emby already compare a series in full.
        bool wanted = takesWholeLibrary;
        if (!wanted && !key.isEmpty()) wanted = m_pgShowIds.contains(key);
        for (const QString &want : m_pgMatch) {
            if (wanted) break;
            wanted = title.compare(want, Qt::CaseInsensitive) == 0;
        }
        if (!wanted) continue;
        if (!key.isEmpty()) m_pgShows << key;
    }

    emit generationProgress(m_pgChannel, 0, m_pgShows.size());
    plexEnumNext();
}

void VirtualChannelsBackend::onPlexChildrenLoaded(const QVariant &items) {
    if (m_pgStage == PlexStage::BrowseChildren) {
        QVariantList out;
        for (const QVariant &v : items.toList()) {
            const QVariantMap m = v.toMap();
            const QString key = m.value("ratingKey").toString();
            if (key.isEmpty()) continue;
            QString label = m.value("title").toString();
            QString sub;
            const int season  = m.value("parentIndex").toInt();
            const int episode = m.value("index").toInt();
            if (m.value("type").toString() == QLatin1String("episode")
                && season > 0 && episode > 0)
                sub = QStringLiteral("S%1E%2").arg(season, 2, 10, QLatin1Char('0'))
                                              .arg(episode, 2, 10, QLatin1Char('0'));
            if (label.isEmpty()) label = key;
            out.append(QVariantMap{{"id", key}, {"label", label}, {"sub", sub}});
        }
        m_pgStage = PlexStage::Idle;
        m_pgTimer->stop();
        const QString kind = m_browseKind; m_browseKind.clear();
        emit sourceBrowseReady(kind, out);
        return;
    }

    if (m_pgStage != PlexStage::Seasons && m_pgStage != PlexStage::Episodes) return;

    int skippedSeasons = 0, skippedEpisodes = 0;
    for (const QVariant &v : items.toList()) {
        const QVariantMap m = v.toMap();
        const QString type = m.value("type").toString();
        const QString key  = m.value("ratingKey").toString();
        if (key.isEmpty()) continue;

        if (type == QLatin1String("season")) {
            if (plexExcluded(key, /*isSeason*/ true)) { ++skippedSeasons; continue; }
            m_pgSeasons << key;
            continue;
        }

        if (plexExcluded(key, /*isSeason*/ false)) { ++skippedEpisodes; continue; }

        MediaItem item;
        if (plexItemToMedia(m, &item)) m_pgEpisodes.push_back(item);
    }

    if (skippedSeasons || skippedEpisodes)
        qDebug("[VirtualChannels] skipped %d season(s) and %d episode(s) switched off for this channel",
               skippedSeasons, skippedEpisodes);

    emit generationProgress(m_pgChannel, m_pgEpisodes.size(),
                            m_pgEpisodes.size() + m_pgSeasons.size() + m_pgShows.size());
    plexEnumNext();
}

static bool matchesAny(const QString &title, const QStringList &wanted) {
    if (wanted.isEmpty()) return true;
    for (const QString &w : wanted)
        if (title.contains(w, Qt::CaseInsensitive)) return true;
    return false;
}

static void queueNamedSets(const QVariant &listing, const QStringList &wanted,
                           QStringList *keys) {
    for (const QVariant &v : listing.toList()) {
        const QVariantMap m = v.toMap();
        const QString title = m.value("title").toString();
        const QString key   = m.value("ratingKey").toString();
        if (title.isEmpty() || key.isEmpty()) continue;
        for (const QString &w : wanted) {
            if (title.compare(w, Qt::CaseInsensitive) == 0) {
                if (!keys->contains(key)) keys->append(key);
                break;
            }
        }
    }
}

void VirtualChannelsBackend::onPlexCollectionsLoaded(const QVariant &collections) {
    if (m_pgStage == PlexStage::ApptCollections) {
        if (m_pgApptCursor < m_pgApptJobs.size())
            queueNamedSets(collections, m_pgApptJobs[m_pgApptCursor].collections,
                           &m_pgApptSetKeys);
        plexApptAdvance();
        return;
    }

    if (m_pgStage == PlexStage::BrowseCollections) {
        for (const QVariant &v : collections.toList()) {
            const QVariantMap m = v.toMap();
            const QString title = m.value("title").toString();
            if (title.isEmpty()) continue;
            m_browseItems.append(QVariantMap{
                {"id", title}, {"label", title},
                {"sub", countedAs(m.value("childCount").toInt(), "ITEM", "ITEMS")}});
        }
        if (!m_pgSections.isEmpty()) {
            m_pgTimer->start();
            QMetaObject::invokeMethod(m_plex, "load_collections",
                                      Q_ARG(QString, m_pgSections.takeFirst()));
            return;
        }
        m_pgStage = PlexStage::Idle;
        m_pgTimer->stop();
        const QString kind = m_browseKind; m_browseKind.clear();
        emit sourceBrowseReady(kind, m_browseItems);
        m_browseItems.clear();
        return;
    }

    if (m_pgStage != PlexStage::Collections) return;
    int matched = 0;
    for (const QVariant &v : collections.toList()) {
        const QVariantMap m = v.toMap();
        const QString key = m.value("ratingKey").toString();
        if (key.isEmpty()) continue;
        if (!matchesAny(m.value("title").toString(), m_pgCollections)) continue;
        m_pgPendingCollections << key;
        ++matched;
    }
    qDebug("[VirtualChannels] %d collection(s) matched", matched);
    plexEnumNext();
}

void VirtualChannelsBackend::onPlexPlaylistsLoaded(const QVariant &playlists) {
    if (m_pgStage == PlexStage::ApptPlaylists) {
        if (m_pgApptCursor < m_pgApptJobs.size())
            queueNamedSets(playlists, m_pgApptJobs[m_pgApptCursor].playlists,
                           &m_pgApptSetKeys);
        plexApptAdvance();
        return;
    }

    if (m_pgStage == PlexStage::BrowsePlaylists) {
        for (const QVariant &v : playlists.toList()) {
            const QVariantMap m = v.toMap();
            const QString title = m.value("title").toString();
            if (title.isEmpty()) continue;
            m_browseItems.append(QVariantMap{
                {"id", title}, {"label", title},
                {"sub", countedAs(m.value("leafCount").toInt(), "ITEM", "ITEMS")}});
        }
        if (!m_pgSections.isEmpty()) {
            m_pgTimer->start();
            QMetaObject::invokeMethod(m_plex, "load_playlists",
                                      Q_ARG(QString, m_pgSections.takeFirst()));
            return;
        }
        m_pgStage = PlexStage::Idle;
        m_pgTimer->stop();
        const QString kind = m_browseKind; m_browseKind.clear();
        emit sourceBrowseReady(kind, m_browseItems);
        m_browseItems.clear();
        return;
    }

    if (m_pgStage != PlexStage::Playlists) return;
    int matched = 0;
    for (const QVariant &v : playlists.toList()) {
        const QVariantMap m = v.toMap();
        const QString key = m.value("ratingKey").toString();
        if (key.isEmpty()) continue;
        if (!matchesAny(m.value("title").toString(), m_pgPlaylists)) continue;
        m_pgPendingPlaylists << key;
        ++matched;
    }
    qDebug("[VirtualChannels] %d playlist(s) matched", matched);
    plexEnumNext();
}

void VirtualChannelsBackend::onPlexEnumTimeout() {
    if (m_pgStage == PlexStage::Idle) return;
    if (!m_browseKind.isEmpty()) { browseFail(QStringLiteral("Plex did not respond")); return; }
    plexEnumFail(QStringLiteral("Plex stopped responding while listing episodes"));
}

void VirtualChannelsBackend::serverApptNext() {
    while (m_serverApptCursor < m_serverApptJobs.size()) {
        const PoolJob job = m_serverApptJobs[m_serverApptCursor];

        MediaServerSource::Request req;
        req.src = job.src;
        req.wants = job.wants;
        if (!job.library.isEmpty()) req.libraries << job.library;
        req.anyFilm     = job.anyFilm;
        req.titles      = job.titles;
        req.genres      = job.genres;
        req.collections = job.collections;
        req.showIds     = job.showIds;
        req.match       = job.match;

        m_serverJob       = job;
        m_serverJobActive = true;
        if (m_server->enumerate(req)) return;
        m_serverJobActive = false;

        qWarning("[VirtualChannels] channel %d: could not ask %s for a booking's films",
                 m_genChannel, qPrintable(MediaServerSource::providerName(job.src)));
        ++m_serverApptCursor;
    }

    m_serverJobActive = false;
    m_serverApptJobs.clear();
    m_serverApptCursor = 0;
    if (!m_pgApptJobs.isEmpty() && m_plex) {
        plexApptNext();
        return;
    }
    finishLocalGeneration();
}

void VirtualChannelsBackend::onServerEnumerationFinished(const QVector<MediaItem> &items) {
    if (!m_genActive) return;

    if (m_serverJobActive) {
        appendToPool(m_serverJob, items);
        m_serverJobActive = false;
        ++m_serverApptCursor;
        serverApptNext();
        return;
    }

    m_genDef.programmes = items;
    if (!m_serverApptJobs.isEmpty()) { serverApptNext(); return; }
    if (!m_pgApptJobs.isEmpty() && m_plex) { plexApptNext(); return; }
    finishLocalGeneration();
}

void VirtualChannelsBackend::onServerEnumerationFailed(const QString &reason) {
    if (!m_genActive) return;

    if (m_serverJobActive && !(m_serverJob.apptIndex < 0
                               && m_serverJob.pool == SlotKind::Programme)) {
        qWarning("[VirtualChannels] channel %d: a pool could not be listed: %s",
                 m_genChannel, qPrintable(reason));
        m_serverJobActive = false;
        ++m_serverApptCursor;
        serverApptNext();
        return;
    }
    m_serverJobActive = false;

    const int ch = m_genChannel;
    m_genActive  = false;
    m_genChannel = -1;
    m_genDef     = ChannelDef{};
    m_serverApptJobs.clear();
    m_serverApptCursor = 0;
    generationEnded(ch, false, reason);
}

int VirtualChannelsBackend::plexApptCollect(const QVariantList &items, bool applyCriteria) {
    if (m_pgApptCursor >= m_pgApptJobs.size()) return 0;
    const PoolJob &job = m_pgApptJobs[m_pgApptCursor];

    int added = 0;
    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        const QString title = m.value("title").toString();
        const QString key   = m.value("ratingKey").toString();
        const qint64 dur    = qint64(m.value("duration").toLongLong());
        if (key.isEmpty() || dur <= 0) continue;

        if (m.value("type").toString() != QLatin1String("movie")) continue;

        if (applyCriteria
            && !bookingWants(job, title, m.value("genres").toStringList()))
            continue;

        if (m_pgApptSeen.contains(key)) continue;
        m_pgApptSeen.insert(key);

        MediaItem mi;
        mi.src   = SlotSource::Plex;
        mi.ref   = key;
        mi.durMs = dur;
        mi.title = title;
        mi.desc  = m.value("summary").toString();
        mi.art   = m.value("thumb").toString();
        appendToPool(job, { mi });
        ++added;
        ++m_pgApptAdded;
    }
    emit generationProgress(m_genChannel, m_pgApptAdded, m_pgApptAdded);
    return added;
}

void VirtualChannelsBackend::plexApptAdvance() {
    if (m_pgApptCursor >= m_pgApptJobs.size()) { plexApptNext(); return; }
    const PoolJob &job = m_pgApptJobs[m_pgApptCursor];
    m_pgTimer->start();

    if (m_pgApptPhase == ApptPhase::Items) {
        const bool filtersLibrary = !job.titles.isEmpty() || !job.genres.isEmpty()
                                    || !job.match.isEmpty();
        const bool takesEverything = job.anyFilm && job.titles.isEmpty()
                                     && job.genres.isEmpty() && job.match.isEmpty()
                                     && job.collections.isEmpty() && job.playlists.isEmpty();
        if (filtersLibrary || takesEverything) {
            if (!m_pgApptSections.isEmpty()) {
                m_pgStage = PlexStage::ApptItems;
                QMetaObject::invokeMethod(m_plex, "load_library_all",
                                          Q_ARG(QString, m_pgApptSections.takeFirst()));
                return;
            }
        }
        m_pgApptPhase = ApptPhase::Collections;
        m_pgApptSections = m_pgApptAllSections;
        m_pgApptSetKeys.clear();
    }

    if (m_pgApptPhase == ApptPhase::Collections) {
        if (!job.collections.isEmpty()) {
            if (!m_pgApptSetKeys.isEmpty()) {
                m_pgStage = PlexStage::ApptCollectionItems;
                QMetaObject::invokeMethod(m_plex, "load_collection_items",
                                          Q_ARG(QString, m_pgApptSetKeys.takeFirst()));
                return;
            }
            if (!m_pgApptSections.isEmpty()) {
                m_pgStage = PlexStage::ApptCollections;
                QMetaObject::invokeMethod(m_plex, "load_collections",
                                          Q_ARG(QString, m_pgApptSections.takeFirst()));
                return;
            }
        }
        m_pgApptPhase = ApptPhase::Playlists;
        m_pgApptSections = m_pgApptAllSections;
        m_pgApptSetKeys.clear();
    }

    if (m_pgApptPhase == ApptPhase::Playlists) {
        if (!job.playlists.isEmpty()) {
            if (!m_pgApptSetKeys.isEmpty()) {
                m_pgStage = PlexStage::ApptPlaylistItems;
                QMetaObject::invokeMethod(m_plex, "load_playlist_items",
                                          Q_ARG(QString, m_pgApptSetKeys.takeFirst()));
                return;
            }
            if (!m_pgApptSections.isEmpty()) {
                m_pgStage = PlexStage::ApptPlaylists;
                QMetaObject::invokeMethod(m_plex, "load_playlists",
                                          Q_ARG(QString, m_pgApptSections.takeFirst()));
                return;
            }
        }
        m_pgApptPhase = ApptPhase::Done;
    }

    m_pgTimer->stop();
    if (job.apptIndex >= 0)
        qDebug("[VirtualChannels] booking %d filled with %d film(s)",
               job.apptIndex, m_pgApptAdded);
    else
        qDebug("[VirtualChannels] %s filled with %d item(s) from Plex",
               qPrintable(slotKindToString(job.pool)), m_pgApptAdded);
    ++m_pgApptCursor;
    plexApptNext();
}

void VirtualChannelsBackend::plexApptNext() {
    if (m_pgApptCursor >= m_pgApptJobs.size()) {
        m_pgStage = PlexStage::Idle;
        if (m_pgTimer) m_pgTimer->stop();
        m_pgApptJobs.clear();
        finishLocalGeneration();
        return;
    }
    const PoolJob &job = m_pgApptJobs[m_pgApptCursor];

    if (job.wants == MediaServerSource::Request::Wants::Episodes) {
        m_pgCollections     = job.collections;
        m_pgPlaylists       = job.playlists;
        m_pgExcludeSeasons  = job.excludeSeasons;
        m_pgExcludeEpisodes = job.excludeEpisodes;
        plexEnumStart(m_genChannel, m_genDef, job.library, job.match, job.showIds);
        return;
    }

    ensurePlexTimer();
    m_pgApptSections.clear();
    m_pgApptAllSections.clear();
    m_pgApptSetKeys.clear();
    m_pgApptSeen.clear();
    m_pgApptAdded = 0;
    m_pgApptPhase = ApptPhase::Items;
    m_pgStage = PlexStage::ApptLibraries;
    m_pgTimer->start();
    QMetaObject::invokeMethod(m_plex, "load_libraries");
}

void VirtualChannelsBackend::plexEnumFail(const QString &why) {
    const int ch = m_pgChannel;
    m_pgStage = PlexStage::Idle;
    if (m_pgTimer) m_pgTimer->stop();
    m_pgEpisodes.clear();
    m_pgShows.clear();
    m_pgSeasons.clear();
    m_pgSections.clear();
    m_pgApptJobs.clear();
    m_pgApptCursor = 0;
    m_pgApptSetKeys.clear();
    m_pgApptAllSections.clear();
    m_pgApptSeen.clear();

    // Only a generation can be failed, and only if one is running: a browse
    // timing out must not end an unrelated build.
    if (!m_genActive) {
        qWarning("[VirtualChannels] Plex enumeration failed with nothing being built: %s",
                 qPrintable(why));
        return;
    }

    m_serverApptJobs.clear();
    m_serverApptCursor = 0;
    m_serverJobActive  = false;
    m_genActive = false;
    qWarning("[VirtualChannels] channel %d Plex enumeration failed: %s", ch, qPrintable(why));
    generationEnded(ch, false, why);
}

void VirtualChannelsBackend::plexEnumFinish() {
    m_pgStage = PlexStage::Idle;

    const bool isProgrammes = m_pgApptCursor < m_pgApptJobs.size()
                              && m_pgApptJobs[m_pgApptCursor].apptIndex < 0
                              && m_pgApptJobs[m_pgApptCursor].pool == SlotKind::Programme;

    if (m_pgEpisodes.isEmpty()) {
        if (isProgrammes) { plexEnumFail(QStringLiteral("No episodes matched")); return; }
        qWarning("[VirtualChannels] channel %d: a pool drew nothing from Plex", m_genChannel);
        ++m_pgApptCursor;
        plexApptNext();
        return;
    }

    // Stable, because films carry none of these: they compare equal on every
    // one and would otherwise come out in whatever order the sort happened to
    // leave them, throwing away the running order a playlist was written in.
    std::stable_sort(m_pgEpisodes.begin(), m_pgEpisodes.end(),
                     [](const MediaItem &a, const MediaItem &b) {
                         if (a.series != b.series) return a.series < b.series;
                         if (a.seasonNo  != b.seasonNo)  return a.seasonNo  < b.seasonNo;
                         if (a.episodeNo != b.episodeNo) return a.episodeNo < b.episodeNo;
                         return a.ep < b.ep;
                     });

    if (m_pgApptCursor < m_pgApptJobs.size())
        appendToPool(m_pgApptJobs[m_pgApptCursor], m_pgEpisodes);
    m_pgEpisodes.clear();

    ++m_pgApptCursor;
    plexApptNext();
}

// ---------------------------------------------------------------------------
// Settings-driven channel builder
//
// The settings system offers toggles, pickers, a directory browser and action
// buttons — but no text entry, because everything has to be reachable from a
// remote control. A channel is therefore assembled by choosing a source and
// pressing a button; its name comes from the folder or series it was built
// from, and its number is the next one free.
// ---------------------------------------------------------------------------

QVariant VirtualChannelsBackend::settingValue(const QString &key) const {
    QFile f(m_dataRoot + "/config.json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject cfg = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject mod = cfg["modules"].toObject()[kModuleId].toObject();

    const QStringList parts = key.split(QLatin1Char('.'));
    QJsonValue v = mod;
    for (const QString &p : parts) {
        if (!v.isObject()) return {};
        v = v.toObject().value(p);
    }
    return v.toVariant();
}

int VirtualChannelsBackend::nextFreeChannelNumber() const {
    int highest = -1;
    QFile f(channelsFilePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue &v : root.value(QLatin1String("channels")).toArray())
            highest = qMax(highest, v.toObject().value(QLatin1String("number")).toInt(0));
    }
    highest = qMax(highest, guide_channel_number());
    if (weather_channel_enabled()) highest = qMax(highest, weather_channel_number());
    return highest + 1;
}

bool VirtualChannelsBackend::appendChannel(const QJsonObject &channel, QString *error) {
    QJsonObject root;
    QJsonArray channels;

    QFile in(channelsFilePath());
    if (in.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(in.readAll()).object();
        channels = root.value(QLatin1String("channels")).toArray();
        in.close();
    }
    channels.append(channel);
    root["channels"] = channels;

    QDir().mkpath(QFileInfo(channelsFilePath()).absolutePath());
    QSaveFile out(channelsFilePath());
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not open channels.json for writing");
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        if (error) *error = QStringLiteral("Could not save channels.json");
        return false;
    }
    return true;
}

int VirtualChannelsBackend::schedule_days() const {
    bool ok = false;
    const int d = settingValue(QStringLiteral("schedule.days")).toString().toInt(&ok);
    if (!ok) return kDefaultScheduleDays;
    return qBound(kMinScheduleDays, d, kMaxScheduleDays);
}

void VirtualChannelsBackend::get_schedule_days_options() {
    QVariantList opts;
    for (int d = kMinScheduleDays; d <= kMaxScheduleDays; ++d)
        opts.append(QVariantMap{{"id", QString::number(d)},
                                {"label", d == 1 ? QStringLiteral("1 DAY")
                                                 : QStringLiteral("%1 DAYS").arg(d)}});
    emit dynamicOptionsReady(QStringLiteral("schedule.days"), opts);
}

// ---------------------------------------------------------------------------
// Keeping the dial ahead of the clock
// ---------------------------------------------------------------------------

void VirtualChannelsBackend::top_up_schedules() {
    if (m_genActive) {
        qDebug("[VirtualChannels] top-up skipped: channel %d is already building", m_genChannel);
        return;
    }
    if (m_rebuildTotal > 0) {
        qDebug("[VirtualChannels] top-up skipped: a rebuild of every channel is running");
        return;
    }

    const qint64 target = qint64(schedule_days()) * 24LL * 3600LL * 1000LL;
    const qint64 floorMs = qMin(target, 24LL * 3600LL * 1000LL);
    const qint64 now     = nowMs();
    m_topUpQueue.clear();

    QFile f(channelsFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray channels = QJsonDocument::fromJson(f.readAll())
                                    .object().value(QLatin1String("channels")).toArray();
    f.close();

    QSet<int> preCached;
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) preCached.insert(it.key());

    for (const QJsonValue &v : channels) {
        const int n = v.toObject().value(QLatin1String("number")).toInt(-1);
        if (n < 0) continue;
        if (n == m_tunedChannel) continue;

        const ChannelSchedule &sched = scheduleFor(n);
        const qint64 reach = sched.isValid() ? sched.horizonEnd() - now : 0;
        if (reach < floorMs) m_topUpQueue.append(n);
        if (!preCached.contains(n)) invalidateCache(n);
    }

    if (m_topUpQueue.isEmpty()) {
        qDebug("[VirtualChannels] top-up: nothing is close to running dry");
        return;
    }

    qDebug("[VirtualChannels] top-up: %lld channel(s) within a day of running dry "
           "(building %d day(s) ahead)",
           static_cast<long long>(m_topUpQueue.size()), schedule_days());
    m_rebuildQueue = m_topUpQueue;
    m_topUpQueue.clear();
    regenerate(m_rebuildQueue.takeFirst());
}

void VirtualChannelsBackend::armNightlySweep() {
    if (!m_sweepTimer) {
        m_sweepTimer = new QTimer(this);
        m_sweepTimer->setSingleShot(true);
        connect(m_sweepTimer, &QTimer::timeout, this, [this] {
            top_up_schedules();
            armNightlySweep();
        });
    }

    const QDateTime now = QDateTime::currentDateTime();
    QDateTime next(now.date(), QTime(4, 0));
    if (next <= now) next = next.addDays(1);

    const qint64 wait = now.msecsTo(next);
    m_sweepTimer->start(int(qBound<qint64>(60000, wait, 24LL * 3600 * 1000)));
    qDebug("[VirtualChannels] next schedule top-up at %s", qPrintable(next.toString(Qt::ISODate)));
}

void VirtualChannelsBackend::get_resume_options() {
    QVariantList opts;
    for (int s : {5, 10, 15, 30, 60})
        opts.append(QVariantMap{{"id", QString::number(s)},
                                {"label", QStringLiteral("%1 SECONDS").arg(s)}});
    opts.append(QVariantMap{{"id", "0"}, {"label", "NEVER"}});
    emit dynamicOptionsReady(QStringLiteral("guide.resume_seconds"), opts);
}

void VirtualChannelsBackend::onServerBrowseReady(const QString &kind, const QVariantList &items) {
    emit sourceBrowseReady(kind, items);
}

void VirtualChannelsBackend::onServerBrowseFailed(const QString &kind, const QString &reason) {
    emit sourceBrowseFailed(kind, reason);
}

void VirtualChannelsBackend::rebuild_all() {
    if (m_genActive || m_rebuildTotal > 0) {
        emit actionStatus(QStringLiteral("A rebuild is already running"));
        emit generationFinished(-1, false, QStringLiteral("A rebuild is already running"));
        return;
    }

    QFile f(channelsFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        emit actionStatus(QStringLiteral("No channels to rebuild"));
        emit generationFinished(-1, false, QStringLiteral("No channels to rebuild"));
        return;
    }
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll())
                               .object().value(QLatin1String("channels")).toArray();
    if (arr.isEmpty()) {
        emit actionStatus(QStringLiteral("No channels to rebuild"));
        emit generationFinished(-1, false, QStringLiteral("No channels to rebuild"));
        return;
    }
    m_rebuildQueue.clear();
    for (const QJsonValue &v : arr) {
        const int n = v.toObject().value(QLatin1String("number")).toInt(-1);
        if (n >= 0) m_rebuildQueue.append(n);
    }
    if (m_rebuildQueue.isEmpty()) {
        emit actionStatus(QStringLiteral("No channels have a number"));
        emit generationFinished(-1, false, QStringLiteral("No channels have a number"));
        return;
    }

    m_rebuildTotal  = int(m_rebuildQueue.size());
    m_rebuildOk     = 0;
    m_rebuildFailed = 0;
    qInfo("[VirtualChannels] rebuild all: %d channels", m_rebuildTotal);

    rebuildNextOrFinish();
}

// ---------------------------------------------------------------------------
// Channel management
//
// A channel's number is its identity: its timeline lives at schedule/<n>.json.
// Renumbering therefore has to move the schedule file too, or the channel
// arrives at its new number with someone else's programming — or none.
// ---------------------------------------------------------------------------

QJsonArray VirtualChannelsBackend::readChannels() const {
    QFile f(channelsFilePath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object()
               .value(QLatin1String("channels")).toArray();
}

bool VirtualChannelsBackend::writeChannels(const QJsonArray &channels) {
    QJsonObject root;
    root["channels"] = channels;
    QDir().mkpath(QFileInfo(channelsFilePath()).absolutePath());
    QSaveFile out(channelsFilePath());
    if (!out.open(QIODevice::WriteOnly)) return false;
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return out.commit();
}

QVariantList VirtualChannelsBackend::list_channels() {
    QVector<QVariantMap> rows;

    for (const QJsonValue &v : readChannels()) {
        const QJsonObject o = v.toObject();
        const int n = o.value(QLatin1String("number")).toInt(-1);
        if (n < 0) continue;
        QVariantMap m;
        m["number"]      = n;
        m["name"]        = o.value(QLatin1String("name")).toString(
                               QStringLiteral("Channel %1").arg(n));
        m["source"]      = slotSourceToString(sourceOf(o));
        m["hasSchedule"] = QFileInfo::exists(scheduleFilePath(n));
        m["special"]     = QString();
        rows.push_back(m);
    }

    QVariantMap guide;
    guide["number"]      = guide_channel_number();
    guide["name"]        = QStringLiteral("Guide");
    guide["source"]      = QStringLiteral("built-in");
    guide["hasSchedule"] = true;
    guide["special"]     = QStringLiteral("guide");
    rows.push_back(guide);

    if (weather_channel_enabled()) {
        QVariantMap wx;
        wx["number"]      = weather_channel_number();
        wx["name"]        = QStringLiteral("Weather");
        wx["source"]      = QStringLiteral("built-in");
        wx["hasSchedule"] = true;
        wx["special"]     = QStringLiteral("weather");
        rows.push_back(wx);
    }

    std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("number").toInt() < b.value("number").toInt();
    });

    QVariantList out;
    for (const QVariantMap &m : rows) out.append(m);
    return out;
}

bool VirtualChannelsBackend::rename_channel(int number, const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return false;

    QJsonArray channels = readChannels();
    bool found = false;
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != number) continue;
        o["name"] = trimmed;
        channels[i] = o;
        found = true;
        break;
    }
    if (!found) return false;
    if (!writeChannels(channels)) return false;
    qDebug("[VirtualChannels] channel %d renamed", number);
    return true;
}

bool VirtualChannelsBackend::move_channel(int number, int direction) {
    if (direction == 0) return false;

    if (m_genActive) {
        qWarning("[VirtualChannels] refused to move channel %d: channel %d is still building",
                 number, m_genChannel);
        return false;
    }

    const QVariantList dial = list_channels();
    int at = -1, sharing = 0;
    for (int i = 0; i < dial.size(); ++i)
        if (dial[i].toMap().value("number").toInt() == number) {
            if (at < 0) at = i;
            ++sharing;
        }
    if (at < 0) return false;

    // Two things on one dial position: the guide or the weather station landing
    // on the same number as a channel. Whichever is found first would be moved,
    // which is not necessarily the one the viewer is pointing at, and the swap
    // would leave the pair still sharing a number. Refuse and say so, rather
    // than move the wrong one.
    if (sharing > 1) {
        qWarning("[VirtualChannels] dial position %d is held by %d entries; "
                 "move refused until they are apart", number, sharing);
        return false;
    }

    const int other = at + (direction < 0 ? -1 : 1);
    if (other < 0 || other >= dial.size()) return false;

    const QVariantMap a = dial[at].toMap();
    const QVariantMap b = dial[other].toMap();
    const int na = a.value("number").toInt();
    const int nb = b.value("number").toInt();
    const QString sa = a.value("special").toString();
    const QString sb = b.value("special").toString();

    const QString tmp = scheduleFilePath(na) + QStringLiteral(".swap");
    QFile::remove(tmp);
    const bool aHasFile = sa.isEmpty() && QFileInfo::exists(scheduleFilePath(na));
    const bool bHasFile = sb.isEmpty() && QFileInfo::exists(scheduleFilePath(nb));

    if (aHasFile && !QFile::rename(scheduleFilePath(na), tmp)) return false;
    if (bHasFile && !QFile::rename(scheduleFilePath(nb), scheduleFilePath(na))) {
        if (aHasFile) QFile::rename(tmp, scheduleFilePath(na));
        return false;
    }
    if (aHasFile && !QFile::rename(tmp, scheduleFilePath(nb))) {
        if (bHasFile) QFile::rename(scheduleFilePath(na), scheduleFilePath(nb));
        QFile::rename(tmp, scheduleFilePath(na));
        return false;
    }

    if (!sa.isEmpty()) setSpecialNumber(sa, nb);
    if (!sb.isEmpty()) setSpecialNumber(sb, na);

    if (sa.isEmpty() || sb.isEmpty()) {
        QJsonArray channels = readChannels();
        for (int i = 0; i < channels.size(); ++i) {
            QJsonObject o = channels[i].toObject();
            const int n = o.value(QLatin1String("number")).toInt(-1);
            if (sa.isEmpty() && n == na)      o["number"] = nb;
            else if (sb.isEmpty() && n == nb) o["number"] = na;
            else continue;
            channels[i] = o;
        }
        QVector<QJsonObject> sorted;
        for (const QJsonValue &v : channels) sorted.push_back(v.toObject());
        std::sort(sorted.begin(), sorted.end(), [](const QJsonObject &x, const QJsonObject &y) {
            return x.value(QLatin1String("number")).toInt() < y.value(QLatin1String("number")).toInt();
        });
        QJsonArray reordered;
        for (const QJsonObject &o : sorted) reordered.append(o);
        if (!writeChannels(reordered)) return false;
    }

    invalidateCache(na);
    invalidateCache(nb);
    qDebug("[VirtualChannels] swapped dial positions %d and %d", na, nb);
    return true;
}

bool VirtualChannelsBackend::delete_channel(int number) {
    if (m_genActive) {
        qWarning("[VirtualChannels] refused to delete channel %d: channel %d is still building",
                 number, m_genChannel);
        return false;
    }

    QJsonArray channels = readChannels();
    QJsonArray kept;
    bool found = false;
    for (const QJsonValue &v : channels) {
        if (v.toObject().value(QLatin1String("number")).toInt(-1) == number) { found = true; continue; }
        kept.append(v);
    }
    if (!found) return false;
    if (!writeChannels(kept)) return false;

    QFile::remove(scheduleFilePath(number));
    invalidateCache(number);

    if (!renumberDial())
        qWarning("[VirtualChannels] channel %d deleted but the dial could not be renumbered", number);

    qDebug("[VirtualChannels] deleted channel %d", number);
    return true;
}

// ---------------------------------------------------------------------------
// The dial, including the built-in stops
//
// The guide and the weather station are positions on the dial rather than
// schedules, but they still have to sit somewhere in the order — and the viewer
// should be able to move them like anything else. Their numbers therefore live
// in settings rather than being fixed at 0 and 1.
// ---------------------------------------------------------------------------

int VirtualChannelsBackend::guide_channel_number() const {
    const QVariant v = settingValue(QStringLiteral("channels.guide_number"));
    bool ok = false;
    const int n = v.toString().toInt(&ok);
    return ok ? n : 0;
}

int VirtualChannelsBackend::weather_channel_number() const {
    const QVariant v = settingValue(QStringLiteral("channels.weather_number"));
    bool ok = false;
    const int n = v.toString().toInt(&ok);
    return ok ? n : 1;
}

bool VirtualChannelsBackend::weather_channel_enabled() const {
    const QVariant v = settingValue(QStringLiteral("channels.weather"));
    if (!v.isValid()) return false;
    if (v.typeId() == QMetaType::QString)
        return v.toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
    return v.toBool();
}

void VirtualChannelsBackend::setSpecialNumber(const QString &which, int number) {
    QFile f(m_dataRoot + "/config.json");
    QJsonObject cfg;
    if (f.open(QIODevice::ReadOnly)) { cfg = QJsonDocument::fromJson(f.readAll()).object(); f.close(); }

    QJsonObject modules = cfg["modules"].toObject();
    QJsonObject mine    = modules[kModuleId].toObject();
    QJsonObject channels = mine["channels"].toObject();
    channels[which + "_number"] = QString::number(number);
    mine["channels"] = channels;
    modules[kModuleId] = mine;
    cfg["modules"] = modules;

    QSaveFile out(m_dataRoot + "/config.json");
    if (!out.open(QIODevice::WriteOnly)) return;
    out.write(QJsonDocument(cfg).toJson(QJsonDocument::Indented));
    out.commit();
}

bool VirtualChannelsBackend::moveScheduleFile(int fromNumber, int toNumber) {
    if (fromNumber == toNumber) return true;
    const QString from = scheduleFilePath(fromNumber);
    if (!QFileInfo::exists(from)) return true;
    const QString to = scheduleFilePath(toNumber);
    QFile::remove(to);
    return QFile::rename(from, to);
}

bool VirtualChannelsBackend::renumberDial() {
    const QVariantList dial = list_channels();
    if (dial.isEmpty()) return true;

    bool contiguous = true;
    for (int i = 0; i < dial.size(); ++i)
        if (dial[i].toMap().value("number").toInt() != i) { contiguous = false; break; }
    if (contiguous) return true;

    struct Staged { QString tmp; int newNumber; };
    QVector<Staged> staged;
    QJsonArray channels = readChannels();

    bool ok = true;
    for (int i = 0; i < dial.size() && ok; ++i) {
        const QVariantMap row = dial[i].toMap();
        const int oldNumber = row.value("number").toInt();
        if (oldNumber == i) continue;
        if (!row.value("special").toString().isEmpty()) continue;

        const QString from = scheduleFilePath(oldNumber);
        if (!QFileInfo::exists(from)) continue;
        const QString tmp = from + QStringLiteral(".tidy");
        QFile::remove(tmp);
        if (QFile::rename(from, tmp)) staged.push_back({ tmp, i });
        else                          ok = false;
    }

    if (!ok) {
        for (const Staged &st : staged) {
            const QString back = st.tmp;
            QFile::rename(back, back.left(back.size() - 5));
        }
        qWarning("[VirtualChannels] tidy aborted: could not stage a schedule file");
        return false;
    }

    for (int i = 0; i < dial.size(); ++i) {
        const QVariantMap row = dial[i].toMap();
        const int oldNumber   = row.value("number").toInt();
        const QString special = row.value("special").toString();
        if (oldNumber == i) continue;

        if (!special.isEmpty()) {
            setSpecialNumber(special, i);
            continue;
        }
        for (int c = 0; c < channels.size(); ++c) {
            QJsonObject o = channels[c].toObject();
            if (o.value(QLatin1String("number")).toInt(-1) != oldNumber) continue;
            o["number"] = i;
            channels[c] = o;
            break;
        }
    }

    QVector<QJsonObject> sorted;
    for (const QJsonValue &v : channels) sorted.push_back(v.toObject());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonObject &x, const QJsonObject &y) {
        return x.value(QLatin1String("number")).toInt() < y.value(QLatin1String("number")).toInt();
    });
    QJsonArray reordered;
    for (const QJsonObject &o : sorted) reordered.append(o);
    if (!writeChannels(reordered)) {
        for (const Staged &st : staged)
            QFile::rename(st.tmp, st.tmp.left(st.tmp.size() - 5));
        qWarning("[VirtualChannels] tidy aborted: could not write channels.json");
        return false;
    }

    for (const Staged &st : staged) {
        const QString dest = scheduleFilePath(st.newNumber);
        QFile::remove(dest);
        if (!QFile::rename(st.tmp, dest))
            qWarning("[VirtualChannels] tidy: a schedule is left at %s", qPrintable(st.tmp));
    }

    m_cache.clear();
    qDebug("[VirtualChannels] dial renumbered 0..%lld", static_cast<long long>(dial.size() - 1));
    return true;
}

// ---------------------------------------------------------------------------
// Plex browsing for the builder
//
// The builder needs to show what is in the library so the viewer can pick from
// it rather than type names. These share the serialised machine with generation
// but use their own stages, so a browse can never be mistaken for a step in a
// build — and a browse is refused outright while a build is running, because
// both would be reading the same replies with no way to tell them apart.
// ---------------------------------------------------------------------------

void VirtualChannelsBackend::browseStart(const QString &kind) {
    m_browseKind = kind;
    m_browseTally.clear();
    ensurePlexTimer();
    m_pgTimer->start();
}

void VirtualChannelsBackend::browseFail(const QString &reason) {
    const QString kind = m_browseKind;
    m_browseKind.clear();
    m_pgStage = PlexStage::Idle;
    if (m_pgTimer) m_pgTimer->stop();
    qWarning("[VirtualChannels] browse %s failed: %s", qPrintable(kind), qPrintable(reason));
    emit sourceBrowseFailed(kind, reason);
}

// ---------------------------------------------------------------------------
// Movie slots
// ---------------------------------------------------------------------------

QVariantList VirtualChannelsBackend::list_logos() {
    QVariantList out;
    QDir dir(logos_dir());
    if (!dir.exists()) {
        QDir().mkpath(logos_dir());
        return out;
    }

    static const QStringList kPatterns = { QStringLiteral("*.png"),  QStringLiteral("*.gif"),
                                           QStringLiteral("*.jpg"),  QStringLiteral("*.jpeg"),
                                           QStringLiteral("*.webp"), QStringLiteral("*.bmp") };
    const QFileInfoList files = dir.entryInfoList(kPatterns, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        if (!isSafeLogoName(fi.fileName())) {
            qWarning("[VirtualChannels] skipping logo '%s' — letters, digits, spaces and "
                     "._()- only", qPrintable(fi.fileName()));
            continue;
        }
        out.append(QVariantMap{
            {"name",     fi.completeBaseName()},
            {"file",     fi.fileName()},
            {"animated", fi.suffix().compare(QLatin1String("gif"), Qt::CaseInsensitive) == 0}});
    }
    return out;
}

QString VirtualChannelsBackend::logo_path(const QString &file) const {
    if (file.trimmed().isEmpty()) return QString();
    if (!isSafeLogoName(file)) {
        qWarning("[VirtualChannels] refused logo name '%s' — letters, digits, spaces and "
                 "._()- only", qPrintable(file));
        return QString();
    }
    const QString full = logos_dir() + QLatin1Char('/') + file;
    if (!QFileInfo(full).isFile()) return QString();
    return full;
}

// ---------------------------------------------------------------------------
// Interstitials
// ---------------------------------------------------------------------------

QVariantList VirtualChannelsBackend::channel_interstitials(int channelNumber) {
    const QJsonObject o = QJsonObject::fromVariantMap(channelObject(channelNumber));
    QVariantList out;
    for (const QString &kind : kInterstitialKinds) {
        QStringList folders;
        int fromServer = 0;
        // Both shapes: the bare strings older channel files hold, and the entry
        // objects every pool is written as now.
        for (const QJsonValue &v : o.value(kind).toArray()) {
            if (v.isString()) { folders << v.toString(); continue; }
            if (!v.isObject()) continue;
            const QJsonObject e = v.toObject();
            const QString folder = e.value(QLatin1String("folder")).toString();
            if (!folder.isEmpty()) folders << folder;
            else if (!e.value(QLatin1String("name")).toString().isEmpty()) ++fromServer;
        }

        int count = 0;
        for (const QString &f : std::as_const(folders)) count += mediaFilesUnder(f).size();

        out.append(QVariantMap{{"kind", kind}, {"folders", folders},
                               {"count", count},
                               // A server's clips are not on disk to be counted,
                               // so they are reported as sources instead of
                               // vanishing from the total.
                               {"sources", folders.size() + fromServer}});
    }
    return out;
}

QString VirtualChannelsBackend::relativeMediaFolder(const QString &folder,
                                                    QString *why) const {
    const auto fail = [&](const QString &reason) {
        if (why) *why = reason;
        return QString();
    };
    if (why) why->clear();

    const QString wanted = folder.trimmed();
    if (wanted.isEmpty()) return fail(QStringLiteral("No folder given"));

    const QString absRoot = QFileInfo(m_mediaRoot).canonicalFilePath();
    if (absRoot.isEmpty())
        return fail(QStringLiteral("The media folder is not set or does not exist"));

    const QString canon = QFileInfo(QDir(absRoot).filePath(wanted)).canonicalFilePath();
    if (canon.isEmpty() || !isWithinRoot(canon, absRoot))
        return fail(QStringLiteral("Folder must be inside the media directory"));
    if (!QFileInfo(canon).isDir())
        return fail(QStringLiteral("That is a file, not a folder"));

    return (canon == absRoot) ? QStringLiteral(".")
                              : QDir(absRoot).relativeFilePath(canon);
}

int VirtualChannelsBackend::create_channel(const QString &name) {
    const QString wanted = name.trimmed();
    if (wanted.isEmpty()) {
        qWarning("[VirtualChannels] refused to create a channel with no name");
        return -1;
    }

    const int number = nextFreeChannelNumber();
    QFile::remove(scheduleFilePath(number));

    QJsonObject ch;
    ch["number"]        = number;
    ch["name"]          = wanted;
    ch["seed"]          = number;
    ch["order"]         = QStringLiteral("broadcast");
    ch["ads_per_break"] = 0;

    const QString absRoot = QFileInfo(m_mediaRoot).canonicalFilePath();
    if (!absRoot.isEmpty()) {
        const QVector<QPair<const char *, const char *>> conventional = {
            { "commercials", "interstitials/commercial" },
            { "bumps",       "interstitials/bump" },
            { "intros",      "interstitials/intro" },
            { "outros",      "interstitials/outro" },
        };
        for (const auto &c : conventional)
            if (QFileInfo::exists(absRoot + QLatin1Char('/') + QLatin1String(c.second)))
                ch[QLatin1String(c.first)] = QJsonArray{ QLatin1String(c.second) };
    }

    QString err;
    if (!appendChannel(ch, &err)) {
        qWarning("[VirtualChannels] could not create channel: %s", qPrintable(err));
        return -1;
    }
    qDebug("[VirtualChannels] created channel %d \"%s\"", number, qPrintable(wanted));
    return number;
}

QVariantList VirtualChannelsBackend::media_folders(int maxDepth) {
    QVariantList out;
    const QString absRoot = QFileInfo(m_mediaRoot).canonicalFilePath();
    if (absRoot.isEmpty()) return out;

    const int depth = qBound(1, maxDepth, 4);

    QDirIterator it(absRoot, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    QStringList rels;
    while (it.hasNext() && rels.size() < kMaxFolderChoices) {
        const QString rel = QDir(absRoot).relativeFilePath(it.next());
        if (rel.isEmpty() || rel.startsWith(QLatin1String(".."))) continue;
        if (rel.count(QLatin1Char('/')) + 1 > depth) continue;
        rels << rel;
    }
    rels.sort();

    for (const QString &rel : std::as_const(rels)) {
        out.append(QVariantMap{{"path", rel},
                               {"depth", rel.count(QLatin1Char('/'))},
                               {"name", rel.section(QLatin1Char('/'), -1)},
                               {"count", mediaFilesUnder(rel).size()}});
    }
    return out;
}

QVariantMap VirtualChannelsBackend::channel_timing(int channelNumber) {
    const QJsonObject o = QJsonObject::fromVariantMap(channelObject(channelNumber));
    const int grid = o.value(QLatin1String("grid_minutes")).toInt(0);
    return QVariantMap{
        {"gridMinutes", (grid >= kMinGridMinutes && grid <= kMaxGridMinutes) ? grid : 0},
        {"adsPerBreak", o.value(QLatin1String("ads_per_break")).toInt(0)},
        {"order",       orderingToString(orderingFromString(
                            o.value(QLatin1String("order")).toString()))}};
}

// One writer for both of the movie channel's switches: each takes a small set
// of words and refuses anything else rather than writing a value the generator
// would have to guess at later.
static bool writeChannelWord(QJsonArray &channels, int channelNumber,
                             const char *field, const QString &value,
                             const QStringList &allowed) {
    const QString clean = value.trimmed().toLower();
    if (!allowed.contains(clean)) return false;
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;
        o[QLatin1String(field)] = clean;
        channels[i] = o;
        return true;
    }
    return false;
}

bool VirtualChannelsBackend::set_channel_kind(int channelNumber, const QString &kind) {
    QJsonArray channels = readChannels();
    if (!writeChannelWord(channels, channelNumber, "kind", kind,
                          { QStringLiteral("tv"), QStringLiteral("movies") })) {
        qWarning("[VirtualChannels] refused channel kind '%s'", qPrintable(kind));
        return false;
    }
    return writeChannels(channels);
}

bool VirtualChannelsBackend::set_channel_films_from(int channelNumber, const QString &from) {
    QJsonArray channels = readChannels();
    if (!writeChannelWord(channels, channelNumber, "films_from", from,
                          { QStringLiteral("playlist"), QStringLiteral("selection") })) {
        qWarning("[VirtualChannels] refused films-from '%s'", qPrintable(from));
        return false;
    }
    return writeChannels(channels);
}

bool VirtualChannelsBackend::set_channel_order(int channelNumber, const QString &order) {
    const QString clean = orderingToString(orderingFromString(order));
    if (clean.compare(order.trimmed(), Qt::CaseInsensitive) != 0) {
        qWarning("[VirtualChannels] refused ordering '%s'", qPrintable(order));
        return false;
    }

    QJsonArray channels = readChannels();
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;
        o["order"] = clean;
        channels[i] = o;
        return writeChannels(channels);
    }
    return false;
}

bool VirtualChannelsBackend::set_channel_grid(int channelNumber, int minutes) {
    if (minutes != 0 && (minutes < kMinGridMinutes || minutes > kMaxGridMinutes)) {
        qWarning("[VirtualChannels] refused a grid of %d minutes", minutes);
        return false;
    }
    QJsonArray channels = readChannels();
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;
        if (minutes == 0) o.remove(QLatin1String("grid_minutes"));
        else              o["grid_minutes"] = minutes;
        channels[i] = o;
        return writeChannels(channels);
    }
    return false;
}

bool VirtualChannelsBackend::set_channel_ads(int channelNumber, int adsPerBreak) {
    if (adsPerBreak < 0 || adsPerBreak > 8) {
        qWarning("[VirtualChannels] refused %d ads per break", adsPerBreak);
        return false;
    }
    QJsonArray channels = readChannels();
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;
        o["ads_per_break"] = adsPerBreak;
        channels[i] = o;
        return writeChannels(channels);
    }
    return false;
}

QString VirtualChannelsBackend::channel_logo(int channelNumber) {
    const QJsonObject o = QJsonObject::fromVariantMap(channelObject(channelNumber));
    return o.value(QLatin1String("logo")).toString();
}

bool VirtualChannelsBackend::set_channel_logo(int channelNumber, const QString &file) {
    const QString wanted = file.trimmed();
    if (!wanted.isEmpty() && logo_path(wanted).isEmpty()) {
        qWarning("[VirtualChannels] refused logo '%s' for channel %d", qPrintable(wanted), channelNumber);
        return false;
    }

    QJsonArray channels = readChannels();
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;
        if (wanted.isEmpty()) o.remove(QLatin1String("logo"));
        else                  o["logo"] = wanted;
        channels[i] = o;
        return writeChannels(channels);
    }
    return false;
}

QString VirtualChannelsBackend::bookingPoolKey(int channelNumber) const {
    return sourceBlockName(channelSource(channelNumber));
}

QJsonArray VirtualChannelsBackend::bookingsOf(int channelNumber) {
    const QJsonObject o = QJsonObject::fromVariantMap(channelObject(channelNumber));
    return o.value(QLatin1String("appointments")).toArray();
}

bool VirtualChannelsBackend::editBooking(int channelNumber, int index,
                                         const std::function<void(QJsonObject &)> &edit) {
    if (index < 0) return false;

    QJsonArray channels = readChannels();
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;

        QJsonArray bookings = o.value(QLatin1String("appointments")).toArray();
        if (index >= bookings.size()) return false;

        QJsonObject b = bookings[index].toObject();
        edit(b);
        bookings[index] = b;
        o["appointments"] = bookings;
        channels[i] = o;
        return writeChannels(channels);
    }
    return false;
}

QVariantList VirtualChannelsBackend::channel_bookings(int channelNumber) {
    QVariantList out;
    const QString poolKey = bookingPoolKey(channelNumber);
    const bool local = (poolKey == QLatin1String("local"));

    const QJsonArray bookings = bookingsOf(channelNumber);
    for (const QJsonValue &bv : bookings) {
        const QJsonObject b = bv.toObject();
        const QString at = b.value(QLatin1String("at")).toString();
        const int minuteOfDay = minuteOfDayFromString(at);

        QStringList days;
        for (const QJsonValue &dv : b.value(QLatin1String("days")).toArray())
            if (dv.isString()) days << dv.toString().toLower();

        QStringList titles, genres, collections, playlists, match;
        QString folder;
        if (local) {
            folder = b.value(QLatin1String("folder")).toString();
            for (const QJsonValue &v : b.value(QLatin1String("local")).toObject()
                                        .value(QLatin1String("titles")).toArray())
                if (v.isString()) titles << v.toString();
        } else {
            const QJsonObject pool = b.value(poolKey).toObject();
            const auto listOf = [&pool](const char *key) {
                QStringList l;
                for (const QJsonValue &v : pool.value(QLatin1String(key)).toArray())
                    if (v.isString()) l << v.toString();
                return l;
            };
            titles      = listOf("titles");
            genres      = listOf("genres");
            collections = listOf("collections");
            playlists   = listOf("playlists");
            match       = listOf("match");
        }

        out.append(QVariantMap{
            {"anyFilm",  b.value(QLatin1String("any_film")).toBool(true)},
            {"name",     b.value(QLatin1String("name")).toString()},
            {"at",       at},
            {"hour",     minuteOfDay >= 0 ? minuteOfDay / 60 : 20},
            {"minute",   minuteOfDay >= 0 ? minuteOfDay % 60 : 0},
            {"valid",    minuteOfDay >= 0},
            {"days",     days},
            {"everyDay", days.isEmpty()},
            {"titles",      titles},
            {"genres",      genres},
            {"collections", collections},
            {"playlists",   playlists},
            {"match",       match},
            {"films",       titles.size()},
            {"criteria",    titles.size() + genres.size() + collections.size()
                            + playlists.size() + match.size()},
            {"source",   poolKey},
            {"folder",   folder}});
    }
    return out;
}

int VirtualChannelsBackend::add_booking(int channelNumber) {
    QJsonArray channels = readChannels();
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;

        QJsonArray bookings = o.value(QLatin1String("appointments")).toArray();
        if (bookings.size() >= kMaxBookingsPerChannel) {
            qWarning("[VirtualChannels] channel %d already has the most movie slots it can take",
                     channelNumber);
            return -1;
        }

        QJsonObject b;
        b["name"] = QStringLiteral("Movie Slot");
        b["at"]   = QStringLiteral("20:00");
        b["any_film"] = false;
        bookings.append(b);
        o["appointments"] = bookings;
        channels[i] = o;
        if (!writeChannels(channels)) return -1;
        qDebug("[VirtualChannels] channel %d: added movie slot %lld",
               channelNumber, static_cast<long long>(bookings.size() - 1));
        return int(bookings.size()) - 1;
    }
    return -1;
}

bool VirtualChannelsBackend::delete_booking(int channelNumber, int index) {
    if (index < 0) return false;
    QJsonArray channels = readChannels();
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;

        QJsonArray bookings = o.value(QLatin1String("appointments")).toArray();
        if (index >= bookings.size()) return false;
        bookings.removeAt(index);
        if (bookings.isEmpty()) o.remove(QLatin1String("appointments"));
        else                    o["appointments"] = bookings;
        channels[i] = o;
        return writeChannels(channels);
    }
    return false;
}

bool VirtualChannelsBackend::set_booking_name(int channelNumber, int index,
                                              const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return false;
    return editBooking(channelNumber, index, [&](QJsonObject &b) { b["name"] = trimmed; });
}

bool VirtualChannelsBackend::set_booking_time(int channelNumber, int index,
                                              const QString &hhmm) {
    if (minuteOfDayFromString(hhmm) < 0) {
        qWarning("[VirtualChannels] refused movie slot time '%s'", qPrintable(hhmm));
        return false;
    }
    return editBooking(channelNumber, index, [&](QJsonObject &b) { b["at"] = hhmm; });
}

bool VirtualChannelsBackend::set_booking_days(int channelNumber, int index,
                                              const QStringList &days) {
    QJsonArray arr;
    for (const QString &d : days) {
        if (dayOfWeekFromString(d) <= 0) {
            qWarning("[VirtualChannels] refused movie slot day '%s'", qPrintable(d));
            return false;
        }
        arr.append(d.toLower());
    }
    return editBooking(channelNumber, index, [&](QJsonObject &b) {
        if (arr.isEmpty()) b.remove(QLatin1String("days"));
        else               b["days"] = arr;
    });
}

static const QStringList kBookingFields = { QStringLiteral("titles"),
                                            QStringLiteral("genres"),
                                            QStringLiteral("collections"),
                                            QStringLiteral("playlists") };

QStringList VirtualChannelsBackend::booking_list(int channelNumber, int index,
                                                 const QString &field) {
    if (!kBookingFields.contains(field)) return {};
    const QJsonArray bookings = bookingsOf(channelNumber);
    if (index < 0 || index >= bookings.size()) return {};
    const QJsonObject pool = bookings[index].toObject()
                                 .value(bookingPoolKey(channelNumber)).toObject();
    QStringList out;
    for (const QJsonValue &v : pool.value(field).toArray())
        if (v.isString()) out << v.toString();
    return out;
}

bool VirtualChannelsBackend::set_booking_list(int channelNumber, int index,
                                              const QString &field,
                                              const QStringList &values) {
    if (!kBookingFields.contains(field)) {
        qWarning("[VirtualChannels] refused unknown movie slot field '%s'", qPrintable(field));
        return false;
    }
    const QString poolKey = bookingPoolKey(channelNumber);
    if (poolKey == QLatin1String("local") && field != QLatin1String("titles")) {
        qWarning("[VirtualChannels] channel %d draws on local files, which have no %s",
                 channelNumber, qPrintable(field));
        return false;
    }
    QJsonArray arr;
    for (const QString &v : values) if (!v.trimmed().isEmpty()) arr.append(v);

    return editBooking(channelNumber, index, [&](QJsonObject &b) {
        QJsonObject pool = b.value(poolKey).toObject();
        if (arr.isEmpty()) pool.remove(field);
        else               pool[field] = arr;
        b[poolKey] = pool;
    });
}

bool VirtualChannelsBackend::set_booking_any_film(int channelNumber, int index, bool any) {
    return editBooking(channelNumber, index,
                       [any](QJsonObject &b) { b["any_film"] = any; });
}

bool VirtualChannelsBackend::set_booking_folder(int channelNumber, int index,
                                                const QString &folder) {
    // Checked now: an unresolvable folder is otherwise dropped in silence when
    // the schedule is built, hours later.
    QString rel;
    if (!folder.trimmed().isEmpty()) {
        QString why;
        rel = relativeMediaFolder(folder, &why);
        if (rel.isEmpty()) {
            qWarning("[VirtualChannels] refused slot folder '%s': %s",
                     qPrintable(folder), qPrintable(why));
            return false;
        }
    }

    return editBooking(channelNumber, index, [&](QJsonObject &b) {
        if (rel.isEmpty()) b.remove(QLatin1String("folder"));
        else               b["folder"] = rel;
    });
}

QString VirtualChannelsBackend::sourceBlockName(SlotSource src) {
    switch (src) {
    case SlotSource::Plex:     return QStringLiteral("plex");
    case SlotSource::Jellyfin: return QStringLiteral("jellyfin");
    case SlotSource::Emby:     return QStringLiteral("emby");
    case SlotSource::Local:    break;
    }
    return QStringLiteral("local");
}

SlotSource VirtualChannelsBackend::sourceOf(const QJsonObject &o) {
    // What the channel was last set to, when it says so. The two rules below
    // are guesses -- a channel moved off a server still holds that server's
    // entries -- and are kept only for channels written before this key.
    const QString stated = o.value(QLatin1String("source")).toString().trimmed().toLower();
    if (!stated.isEmpty()) {
        const SlotSource src = slotSourceFromString(stated);
        if (slotSourceToString(src) == stated) return src;
        qWarning("[VirtualChannels] channel names an unknown source '%s'; working it out instead",
                 qPrintable(stated));
    }

    for (const SlotSource src : { SlotSource::Plex, SlotSource::Jellyfin, SlotSource::Emby })
        if (o.contains(sourceBlockName(src))) return src;

    for (const QJsonValue &v : o.value(QLatin1String("programmes")).toArray()) {
        if (!v.isObject()) continue;
        const SlotSource src =
            slotSourceFromString(v.toObject().value(QLatin1String("src")).toString());
        if (src != SlotSource::Local) return src;
    }
    return SlotSource::Local;
}

bool VirtualChannelsBackend::usesEntryPools(const QJsonObject &o) {
    for (const QJsonValue &v : o.value(QLatin1String("programmes")).toArray())
        if (v.isObject()) return true;
    return false;
}

bool VirtualChannelsBackend::isMovieChannel(const QJsonObject &channel) {
    return channel.value(QLatin1String("kind")).toString().trimmed().toLower()
           == QLatin1String("movies");
}

// A selection is the default: a channel that has never been told otherwise
// shuffles what it was given rather than waiting for a playlist to be set.
bool VirtualChannelsBackend::playsAPlaylist(const QJsonObject &channel) {
    return isMovieChannel(channel)
           && channel.value(QLatin1String("films_from")).toString().trimmed().toLower()
              == QLatin1String("playlist");
}

SlotSource VirtualChannelsBackend::channelSource(int channelNumber) const {
    return sourceOf(QJsonObject::fromVariantMap(channelObject(channelNumber)));
}

bool VirtualChannelsBackend::sourceSignedIn(QObject *backend) const {
    if (!backend) return false;
    if (backend->metaObject()->indexOfMethod("get_auth_state()") < 0) return true;
    QString state;
    if (!QMetaObject::invokeMethod(backend, "get_auth_state", Q_RETURN_ARG(QString, state)))
        return true;
    return state == QLatin1String("authed");
}

QStringList VirtualChannelsBackend::availableSources() const {
    QStringList out{ QStringLiteral("local") };
    if (sourceSignedIn(m_plex))     out << QStringLiteral("plex");
    if (sourceSignedIn(m_jellyfin)) out << QStringLiteral("jellyfin");
    if (sourceSignedIn(m_emby))     out << QStringLiteral("emby");
    return out;
}

bool VirtualChannelsBackend::browse_busy() const {
    return m_pgStage != PlexStage::Idle || (m_server && m_server->busy());
}

void VirtualChannelsBackend::browse_source(int channelNumber, const QString &kind,
                                           const QString &parentKey) {
    browse_from(slotSourceToString(channelSource(channelNumber)), kind, parentKey);
}

void VirtualChannelsBackend::browse_from(const QString &source, const QString &kind,
                                         const QString &parentKey) {
    static const QStringList kKinds = { QStringLiteral("shows"),
                                        QStringLiteral("movies"),
                                        QStringLiteral("moviegenres"),
                                        QStringLiteral("moviecollections"),
                                        QStringLiteral("movieplaylists"),
                                        QStringLiteral("collections"),
                                        QStringLiteral("playlists"),
                                        QStringLiteral("seasons"),
                                        QStringLiteral("episodes") };
    if (!kKinds.contains(kind)) {
        qWarning("[VirtualChannels] refused unknown browse kind '%s'", qPrintable(kind));
        emit sourceBrowseFailed(kind, QStringLiteral("Unknown"));
        return;
    }
    if (browse_busy()) { emit sourceBrowseFailed(kind, QStringLiteral("Busy")); return; }

    const SlotSource src = slotSourceFromString(source);
    if (src == SlotSource::Plex) {
        if (kind == QLatin1String("shows"))            browse_plex_shows();
        else if (kind.startsWith(QLatin1String("movie"))) browse_plex_movies(kind);
        else if (kind == QLatin1String("collections")) browse_plex_collections();
        else if (kind == QLatin1String("playlists"))   browse_plex_playlists();
        else if (kind == QLatin1String("seasons"))     browse_plex_seasons(parentKey);
        else                                           browse_plex_episodes(parentKey);
        return;
    }
    if (src == SlotSource::Jellyfin || src == SlotSource::Emby) {
        if (kind == QLatin1String("playlists")) {
            emit sourceBrowseFailed(kind, QStringLiteral("Playlists are not available here"));
            return;
        }
        if (!m_server->browse(src, kind, parentKey))
            emit sourceBrowseFailed(kind,
                QStringLiteral("%1 unavailable").arg(MediaServerSource::providerName(src)));
        return;
    }
    browse_local(kind, parentKey);
}

// Local files, browsed exactly like a server's library.
//
// Synchronous, unlike the server paths: reading a directory is immediate, so
// there is nothing to wait for and no busy state to hold. The rows carry the
// same two fields the browser reads from every other source, which is what lets
// one view drive all four.
void VirtualChannelsBackend::browse_local(const QString &kind, const QString &parentKey) {
    m_localLibrary.setMediaRoot(m_mediaRoot);

    if (!m_localLibrary.hasLibraryFolders()) {
        // Normally impossible -- the folders are made at startup -- but a media
        // root on a drive that has not mounted lands here, and saying which
        // folder is missing beats an empty list.
        emit sourceBrowseFailed(kind,
            QStringLiteral("No series or movies folder in %1").arg(m_mediaRoot));
        return;
    }

    QVariantList rows;

    if (kind == QLatin1String("shows")) {
        for (const vchan::LocalShow &show : m_localLibrary.shows()) {
            QVariantMap m;
            m["id"]    = vchan::LocalLibrary::showKey(show);
            m["label"] = show.year > 0
                             ? QStringLiteral("%1 (%2)").arg(show.name).arg(show.year)
                             : show.name;
            rows.append(m);
        }
    } else if (kind.startsWith(QLatin1String("movie"))) {
        // Local files have no genres, collections or playlists to group films
        // by, so every movie kind resolves to the one flat list rather than
        // failing and leaving the viewer at a dead end.
        for (const vchan::LocalMovie &mv : m_localLibrary.movies()) {
            QVariantMap m;
            m["id"]    = mv.ref;
            m["label"] = mv.year > 0
                             ? QStringLiteral("%1 (%2)").arg(mv.name).arg(mv.year)
                             : mv.name;
            rows.append(m);
        }
    } else if (kind == QLatin1String("seasons")) {
        const vchan::LocalShow show = m_localLibrary.showByKey(parentKey);
        for (const vchan::LocalSeason &season : show.seasons) {
            QVariantMap m;
            m["id"]    = vchan::LocalLibrary::seasonKey(show.folder, season.number);
            m["count"] = season.episodes.size();
            m["label"] = season.episodes.size() == 1
                             ? QStringLiteral("%1 (1 episode)").arg(season.label)
                             : QStringLiteral("%1 (%2 episodes)")
                                   .arg(season.label).arg(season.episodes.size());
            rows.append(m);
        }
    } else if (kind == QLatin1String("episodes")) {
        QString showFolder;
        int season = -1;
        if (!vchan::LocalLibrary::splitSeasonKey(parentKey, &showFolder, &season)) {
            emit sourceBrowseFailed(kind, QStringLiteral("Unreadable season"));
            return;
        }
        for (const vchan::LocalEpisode &ep : m_localLibrary.episodesOf(showFolder, season)) {
            QVariantMap m;
            m["id"] = ep.ref;
            // Numbered when the name said so, plain otherwise: inventing a
            // number for a file that never carried one would misrepresent it.
            m["label"] = ep.number >= 0
                             ? QStringLiteral("%1. %2").arg(ep.number).arg(ep.title)
                             : ep.title;
            rows.append(m);
        }
    } else {
        // collections and playlists have no local equivalent.
        emit sourceBrowseFailed(kind, QStringLiteral("Not available for local files"));
        return;
    }

    if (rows.isEmpty()) {
        // The folder exists and is empty, which is what a fresh install looks
        // like. Name the layout rather than leaving someone to guess it.
        emit sourceBrowseFailed(kind,
            kind.startsWith(QLatin1String("movie"))
                ? QStringLiteral("Nothing in %1/movies yet — add films as "
                                 "movies/Film Name (Year).mp4").arg(m_mediaRoot)
                : QStringLiteral("Nothing in %1/series yet — add shows as "
                                 "series/Show Name/Season 1/Show S01E01.mp4").arg(m_mediaRoot));
        return;
    }

    emit sourceBrowseReady(kind, rows);
}

// ---------------------------------------------------------------------------
// Pools
// ---------------------------------------------------------------------------

QVariantList VirtualChannelsBackend::channel_pool(int channelNumber, const QString &pool) {
    QVariantList out;
    const QJsonObject o = QJsonObject::fromVariantMap(channelObject(channelNumber));

    const char *field = nullptr;
    for (const auto &f : poolFields())
        if (slotKindToString(f.first) + QLatin1Char('s') == pool
            || QLatin1String(f.second) == pool) { field = f.second; break; }
    if (!field) return out;

    for (const QJsonValue &v : o.value(QLatin1String(field)).toArray()) {
        QVariantMap e;
        if (v.isString()) {
            e["src"]    = slotSourceToString(SlotSource::Local);
            e["kind"]   = QStringLiteral("folder");
            e["name"]   = v.toString();
            e["count"]  = mediaFilesUnder(v.toString()).size();
        } else if (v.isObject()) {
            const QJsonObject j = v.toObject();
            const SlotSource src = slotSourceFromString(j.value(QLatin1String("src")).toString());
            e["src"] = slotSourceToString(src);
            // A local entry is a folder of clips or a show from the library.
            const QString localFolder = j.value(QLatin1String("folder")).toString();
            if (src == SlotSource::Local && !localFolder.isEmpty()) {
                e["kind"]  = QStringLiteral("folder");
                e["name"]  = localFolder;
                e["count"] = mediaFilesUnder(localFolder).size();
            } else {
                e["kind"] = j.value(QLatin1String("kind")).toString();
                e["name"] = j.value(QLatin1String("name")).toString();
                e["count"] = -1;
            }
            // Carried out and back so that a screen editing the pool returns
            // the show's id rather than dropping it.
            const QString ref = j.value(QLatin1String("ref")).toString();
            if (!ref.isEmpty()) e["ref"] = ref;
            for (const char *k : { "intros", "outros" }) {
                QStringList folders;
                for (const QJsonValue &f : j.value(QLatin1String(k)).toArray())
                    if (f.isString()) folders << f.toString();
                if (folders.isEmpty()) continue;
                e[QLatin1String(k)] = folders;
                int clips = 0;
                for (const QString &f : std::as_const(folders))
                    clips += mediaFilesUnder(f).size();
                e[QLatin1String(k) + QStringLiteral("_count")] = clips;
            }
        } else {
            continue;
        }
        out.append(e);
    }
    return out;
}

bool VirtualChannelsBackend::set_channel_pool(int channelNumber, const QString &pool,
                                              const QVariantList &entries) {
    const char *field = nullptr;
    for (const auto &f : poolFields())
        if (QLatin1String(f.second) == pool) { field = f.second; break; }
    if (!field) {
        qWarning("[VirtualChannels] refused unknown pool '%s'", qPrintable(pool));
        return false;
    }

    QJsonArray arr;
    for (const QVariant &v : entries) {
        const QVariantMap m = v.toMap();
        const SlotSource src = slotSourceFromString(m.value(QStringLiteral("src")).toString());
        QJsonObject e;
        e["src"] = slotSourceToString(src);

        const QString localKind = m.value(QStringLiteral("kind")).toString();
        const bool localIsFolder = (src == SlotSource::Local)
                                   && (localKind.isEmpty()
                                       || localKind == QLatin1String("folder"));

        if (localIsFolder) {
            QString why;
            const QString raw = m.value(QStringLiteral("name")).toString();
            const QString rel = relativeMediaFolder(raw, &why);
            if (rel.isEmpty()) {
                // Kept rather than refused: failing the list would stop every
                // other row being edited, and say only "could not save".
                qWarning("[VirtualChannels] channel %d keeps an unresolved pool "
                         "folder '%s': %s", channelNumber, qPrintable(raw), qPrintable(why));
                if (raw.trimmed().isEmpty()) continue;
                e["folder"] = raw;
            } else {
                e["folder"] = rel;
            }
        } else {
            // Must agree with what the picker offers and the generator plays.
            static const QStringList kKinds = { QStringLiteral("library"),
                                                QStringLiteral("series"),
                                                QStringLiteral("movie"),
                                                QStringLiteral("genre"),
                                                QStringLiteral("collection"),
                                                QStringLiteral("playlist") };
            const QString kind = m.value(QStringLiteral("kind")).toString();
            const QString name = m.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty()) {
                qWarning("[VirtualChannels] dropped a nameless pool entry of kind '%s'",
                         qPrintable(kind));
                continue;
            }
            if (!kKinds.contains(kind)) {
                // Kept, not refused, for the same reason as an unresolved
                // folder above.
                qWarning("[VirtualChannels] channel %d keeps a pool entry '%s' of "
                         "unknown kind '%s'", channelNumber, qPrintable(name), qPrintable(kind));
            }
            e["kind"] = kind;
            e["name"] = name;
            const QString ref = m.value(QStringLiteral("ref")).toString().trimmed();
            if (!ref.isEmpty()) e["ref"] = ref;
        }

        for (const char *k : { "intros", "outros" }) {
            QJsonArray folders;
            for (const QVariant &fv : m.value(QLatin1String(k)).toList()) {
                QString why;
                const QString rel = relativeMediaFolder(fv.toString(), &why);
                if (rel.isEmpty()) {
                    qWarning("[VirtualChannels] refused ident folder for channel %d: %s",
                             channelNumber, qPrintable(why));
                    return false;
                }
                folders.append(rel);
            }
            if (!folders.isEmpty()) e[QLatin1String(k)] = folders;
        }
        arr.append(e);
    }

    QJsonArray channels = readChannels();
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;
        if (arr.isEmpty()) o.remove(QLatin1String(field));
        else               o[QLatin1String(field)] = arr;
        // Writing entries retires only the legacy whole-channel series list,
        // which would otherwise air the same shows a second time. The rest of
        // the block stays: it holds the seasons and episodes switched off, and
        // the collections and playlists, none of which the entries carry.
        // Removing the block outright discarded all of it.
        if (QLatin1String(field) == QLatin1String("programmes") && !arr.isEmpty())
            for (const SlotSource sb : { SlotSource::Plex, SlotSource::Jellyfin, SlotSource::Emby }) {
                const QString block = sourceBlockName(sb);
                if (!o.contains(block)) continue;
                QJsonObject b = o.value(block).toObject();
                b.remove(QLatin1String("match"));
                if (b.isEmpty()) o.remove(block);
                else             o[block] = b;
            }
        channels[i] = o;
        return writeChannels(channels);
    }
    return false;
}

void VirtualChannelsBackend::browse_plex_shows() {
    if (!m_plex)            { emit sourceBrowseFailed("shows", "Plex unavailable"); return; }
    if (browse_busy())      { emit sourceBrowseFailed("shows", "Plex is busy"); return; }
    m_pgSections.clear();
    browseStart(QStringLiteral("shows"));
    m_pgStage = PlexStage::BrowseLibraries;
    QMetaObject::invokeMethod(m_plex, "load_libraries");
}

void VirtualChannelsBackend::browse_plex_movies(const QString &kind) {
    if (!m_plex)       { emit sourceBrowseFailed(kind, "Plex unavailable"); return; }
    if (browse_busy()) { emit sourceBrowseFailed(kind, "Plex is busy"); return; }
    m_pgSections.clear();
    browseStart(kind);
    m_pgStage = PlexStage::BrowseLibraries;
    QMetaObject::invokeMethod(m_plex, "load_libraries");
}

void VirtualChannelsBackend::browse_plex_collections() {
    if (!m_plex)       { emit sourceBrowseFailed("collections", "Plex unavailable"); return; }
    if (browse_busy()) { emit sourceBrowseFailed("collections", "Plex is busy"); return; }
    m_pgSections.clear();
    browseStart(QStringLiteral("collections"));
    m_pgStage = PlexStage::BrowseLibraries;
    QMetaObject::invokeMethod(m_plex, "load_libraries");
}

void VirtualChannelsBackend::browse_plex_playlists() {
    if (!m_plex)       { emit sourceBrowseFailed("playlists", "Plex unavailable"); return; }
    if (browse_busy()) { emit sourceBrowseFailed("playlists", "Plex is busy"); return; }
    m_pgSections.clear();
    browseStart(QStringLiteral("playlists"));
    m_pgStage = PlexStage::BrowseLibraries;
    QMetaObject::invokeMethod(m_plex, "load_libraries");
}

void VirtualChannelsBackend::browse_plex_seasons(const QString &showRatingKey) {
    if (!m_plex)                 { emit sourceBrowseFailed("seasons", "Plex unavailable"); return; }
    if (browse_busy())           { emit sourceBrowseFailed("seasons", "Plex is busy"); return; }
    if (showRatingKey.isEmpty()) { emit sourceBrowseFailed("seasons", "No series given"); return; }
    browseStart(QStringLiteral("seasons"));
    m_pgStage = PlexStage::BrowseChildren;
    QMetaObject::invokeMethod(m_plex, "load_children", Q_ARG(QString, showRatingKey));
}

void VirtualChannelsBackend::browse_plex_episodes(const QString &seasonRatingKey) {
    if (!m_plex)                   { emit sourceBrowseFailed("episodes", "Plex unavailable"); return; }
    if (browse_busy())             { emit sourceBrowseFailed("episodes", "Plex is busy"); return; }
    if (seasonRatingKey.isEmpty()) { emit sourceBrowseFailed("episodes", "No season given"); return; }
    browseStart(QStringLiteral("episodes"));
    m_pgStage = PlexStage::BrowseChildren;
    QMetaObject::invokeMethod(m_plex, "load_children", Q_ARG(QString, seasonRatingKey));
}

// ---------------------------------------------------------------------------
// Reading and editing a channel's Plex sources
// ---------------------------------------------------------------------------

bool VirtualChannelsBackend::source_supports_playlists(const QString &source) const {
    // Only Plex serves playlists; browse_from refuses them for the others.
    return slotSourceFromString(source) == SlotSource::Plex;
}

QVariantMap VirtualChannelsBackend::channel_source_config(int channelNumber) {
    QVariantMap out;
    const QJsonObject o = QJsonObject::fromVariantMap(channelObject(channelNumber));
    const SlotSource src = channelSource(channelNumber);
    const QJsonObject plex = o.value(sourceBlockName(src)).toObject();

    const auto listOf = [](const QJsonObject &parent, const char *key) {
        QStringList l;
        for (const QJsonValue &v : parent.value(QLatin1String(key)).toArray())
            if (v.isString()) l << v.toString();
        return l;
    };

    out["source"]      = slotSourceToString(src);
    out["sourceName"]  = src == SlotSource::Local ? QStringLiteral("Local Files")
                                                  : MediaServerSource::providerName(src);
    QStringList choices = availableSources();
    const QString here = slotSourceToString(src);
    if (!choices.contains(here)) choices << here;
    out["available"]   = choices;
    out["supportsPlaylists"] = source_supports_playlists(slotSourceToString(src));
    out["usesEntryPools"] = usesEntryPools(o);
    out["kind"]        = isMovieChannel(o) ? QStringLiteral("movies") : QStringLiteral("tv");
    out["filmsFrom"]   = playsAPlaylist(o) ? QStringLiteral("playlist")
                                           : QStringLiteral("selection");
    const QStringList programmes = listOf(o, "programmes");
    out["folder"] = programmes.isEmpty() ? QString() : programmes.first();
    int folderCount = 0;
    for (const QString &f : programmes) folderCount += mediaFilesUnder(f).size();
    out["folderCount"] = folderCount;

    out["library"]     = plex.value(QLatin1String("library")).toString();
    // Picks live in the programmes array as entry objects, so each one can also
    // carry its own idents. The legacy per-source "match" array is still read
    // for channels written before that, and is retired the next time the list
    // is saved.
    // Split by kind, so the screen can count shows, films and genres apart. A
    // pool holding all three would otherwise report every entry as a show.
    QStringList picked, films, genres;
    for (const QJsonValue &v : o.value(QLatin1String("programmes")).toArray()) {
        if (!v.isObject()) continue;
        const QJsonObject e = v.toObject();
        if (slotSourceFromString(e.value(QLatin1String("src")).toString()) != src) continue;
        const QString name = e.value(QLatin1String("name")).toString().trimmed();
        if (name.isEmpty()) continue;
        const QString kind = e.value(QLatin1String("kind")).toString();
        if      (kind == QLatin1String("movie")) films  << name;
        else if (kind == QLatin1String("genre")) genres << name;
        else if (kind == QLatin1String("series")
                 || kind.isEmpty())              picked << name;
    }
    if (picked.isEmpty() && films.isEmpty() && genres.isEmpty())
        picked = listOf(plex, "match");
    out["match"]       = picked;
    out["films"]       = films;
    out["genres"]      = genres;
    out["collections"] = listOf(plex, "collections");
    out["playlists"]   = listOf(plex, "playlists");

    const QJsonObject excl = plex.value(QLatin1String("exclude")).toObject();
    out["excludedSeasons"]  = listOf(excl, "seasons");
    out["excludedEpisodes"] = excludedEpisodesIn(excl);

    QVariantMap bySeason;
    const QJsonObject epsBySeason = excl.value(QLatin1String("episodes")).toObject();
    for (auto it = epsBySeason.constBegin(); it != epsBySeason.constEnd(); ++it) {
        QStringList refs;
        for (const QJsonValue &v : it.value().toArray())
            if (v.isString()) refs << v.toString();
        if (!refs.isEmpty()) bySeason.insert(it.key(), refs);
    }
    out["excludedBySeason"] = bySeason;
    return out;
}

bool VirtualChannelsBackend::set_channel_source(int channelNumber, const QString &source) {
    const QString wanted = source.trimmed().toLower();

    if (wanted == slotSourceToString(channelSource(channelNumber)))
        return true;

    if (!availableSources().contains(wanted)) {
        qWarning("[VirtualChannels] refused to set channel %d to unavailable source '%s'",
                 channelNumber, qPrintable(wanted));
        return false;
    }

    QJsonArray channels = readChannels();
    bool found = false;
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;

        for (const SlotSource src : { SlotSource::Plex, SlotSource::Jellyfin, SlotSource::Emby })
            o.remove(sourceBlockName(src));
        if (wanted != QLatin1String("local"))
            o[wanted] = QJsonObject{};
        // Stated, so entries left by the previous source do not decide it.
        // They are kept: a round trip should not lose what the channel played.
        o[QStringLiteral("source")] = wanted;

        channels[i] = o;
        found = true;
        break;
    }
    if (!found) return false;
    if (!writeChannels(channels)) return false;
    qDebug("[VirtualChannels] channel %d source set to %s", channelNumber, qPrintable(wanted));
    return true;
}

bool VirtualChannelsBackend::set_channel_list(int channelNumber, const QString &field,
                                              const QStringList &values,
                                              const QStringList &refs) {
    // "match" is the shows list, kept under that name because that is what the
    // screen and the older channel files call it. Films and genres are the same
    // shape of list, differing only in the kind each entry is written as.
    static const QHash<QString, QString> kEntryKinds = {
        { QStringLiteral("match"),  QStringLiteral("series") },
        { QStringLiteral("films"),  QStringLiteral("movie")  },
        { QStringLiteral("genres"), QStringLiteral("genre")  },
    };
    static const QStringList kAllowed = { QStringLiteral("match"),
                                          QStringLiteral("films"),
                                          QStringLiteral("genres"),
                                          QStringLiteral("collections"),
                                          QStringLiteral("playlists") };
    if (!kAllowed.contains(field)) {
        qWarning("[VirtualChannels] refused to set unknown source field '%s'", qPrintable(field));
        return false;
    }

    QJsonArray channels = readChannels();
    bool found = false;
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;

        const QString block = sourceBlockName(channelSource(channelNumber));
        QJsonObject plex = o.value(block).toObject();

        if (kEntryKinds.contains(field)) {
            const QString entryKind = kEntryKinds.value(field);
            // Only this source's SERIES entries are ours to rewrite. Another
            // source's entries, collections, folders and the bare strings an
            // older channel file uses are carried across untouched.
            const SlotSource src = channelSource(channelNumber);
            QJsonArray kept;
            QHash<QString, QJsonObject> existing;
            for (const QJsonValue &v : o.value(QLatin1String("programmes")).toArray()) {
                if (!v.isObject()) { kept.append(v); continue; }
                const QJsonObject e = v.toObject();
                const bool mine =
                    slotSourceFromString(e.value(QLatin1String("src")).toString()) == src
                    && e.value(QLatin1String("kind")).toString() == entryKind;
                if (!mine) { kept.append(e); continue; }
                existing.insert(e.value(QLatin1String("name")).toString(), e);
            }

            QJsonArray out;
            for (const QJsonValue &v : kept) out.append(v);
            for (int n = 0; n < values.size(); ++n) {
                const QString name = values[n];
                if (name.trimmed().isEmpty()) continue;
                QJsonObject e = existing.value(name);
                e["name"] = name;
                e["kind"] = entryKind;
                e["src"]  = slotSourceToString(src);
                const QString ref = refs.value(n).trimmed();
                if (!ref.isEmpty()) e["ref"] = ref;
                out.append(e);
            }
            o[QLatin1String("programmes")] = out;
            // The legacy array is retired rather than left to disagree. Only
            // the shows list ever had one.
            if (field == QLatin1String("match")) plex.remove(QLatin1String("match"));
        } else {
            QJsonArray arr;
            for (const QString &v : values) if (!v.trimmed().isEmpty()) arr.append(v);
            if (arr.isEmpty()) plex.remove(field);
            else               plex[field] = arr;
        }

        o[block] = plex;

        channels[i] = o;
        found = true;
        break;
    }
    if (!found) return false;
    if (!writeChannels(channels)) return false;
    qDebug("[VirtualChannels] channel %d %s set to %lld entry(s)",
           channelNumber, qPrintable(field), static_cast<long long>(values.size()));
    return true;
}

bool VirtualChannelsBackend::clear_episode_exclusions(int channelNumber,
                                                     const QString &seasonKey) {
    if (seasonKey.trimmed().isEmpty()) return false;
    QJsonArray channels = readChannels();
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;

        const QString block = sourceBlockName(channelSource(channelNumber));
        QJsonObject plex = o.value(block).toObject();
        QJsonObject excl = plex.value(QLatin1String("exclude")).toObject();
        QJsonObject bySeason = excl.value(QLatin1String("episodes")).toObject();
        if (!bySeason.contains(seasonKey)) return true;

        bySeason.remove(seasonKey);
        if (bySeason.isEmpty()) excl.remove(QLatin1String("episodes"));
        else                    excl[QLatin1String("episodes")] = bySeason;
        if (excl.isEmpty()) plex.remove(QLatin1String("exclude"));
        else                plex[QLatin1String("exclude")] = excl;
        o[block] = plex;
        channels[i] = o;
        return writeChannels(channels);
    }
    return false;
}

bool VirtualChannelsBackend::set_channel_excluded(int channelNumber, const QString &kind,
                                                  const QString &itemKey, bool excluded,
                                                  const QString &seasonKey) {
    if (kind != QLatin1String("seasons") && kind != QLatin1String("episodes")) {
        qWarning("[VirtualChannels] refused unknown exclusion kind '%s'", qPrintable(kind));
        return false;
    }
    if (itemKey.trimmed().isEmpty()) return false;

    QJsonArray channels = readChannels();
    bool found = false;
    for (int i = 0; i < channels.size(); ++i) {
        QJsonObject o = channels[i].toObject();
        if (o.value(QLatin1String("number")).toInt(-1) != channelNumber) continue;

        const QString block = sourceBlockName(channelSource(channelNumber));
        QJsonObject plex = o.value(block).toObject();
        QJsonObject excl = plex.value(QLatin1String("exclude")).toObject();

        if (kind == QLatin1String("episodes")) {
            QJsonObject bySeason = excl.value(kind).toObject();
            const QString under = seasonKey.trimmed().isEmpty() ? QStringLiteral("")
                                                                : seasonKey;
            QStringList keys;
            for (const QJsonValue &v : bySeason.value(under).toArray())
                if (v.isString()) keys << v.toString();

            if (excluded) { if (!keys.contains(itemKey)) keys << itemKey; }
            else          { keys.removeAll(itemKey); }

            QJsonArray arr;
            for (const QString &k : keys) arr.append(k);
            if (arr.isEmpty()) bySeason.remove(under);
            else               bySeason[under] = arr;
            if (bySeason.isEmpty()) excl.remove(kind);
            else                    excl[kind] = bySeason;
        } else {
            QStringList keys;
            for (const QJsonValue &v : excl.value(kind).toArray())
                if (v.isString()) keys << v.toString();

            if (excluded) { if (!keys.contains(itemKey)) keys << itemKey; }
            else          { keys.removeAll(itemKey); }

            QJsonArray arr;
            for (const QString &k : keys) arr.append(k);
            if (arr.isEmpty()) excl.remove(kind);
            else               excl[kind] = arr;
        }

        if (excl.isEmpty()) plex.remove(QLatin1String("exclude"));
        else                plex["exclude"] = excl;

        o[block] = plex;

        channels[i] = o;
        found = true;
        break;
    }
    if (!found) return false;
    return writeChannels(channels);
}
