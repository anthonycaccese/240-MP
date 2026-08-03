#include "TvBackend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>

static const char *kModuleId = "com.240mp.tv_mode";

// "dragon_tales" / "dragon-tales" -> "Dragon Tales". Names that already carry
// capitals are left alone, so "The Magic School Bus" survives intact.
static QString prettifyName(const QString &folderName) {
    QString cleaned = folderName;
    cleaned.replace('_', ' ').replace('-', ' ');
    cleaned = cleaned.simplified();
    if (cleaned.isEmpty())
        return folderName;
    if (cleaned != cleaned.toLower())
        return cleaned;
    QStringList words = cleaned.split(' ', Qt::SkipEmptyParts);
    for (QString &w : words)
        w[0] = w[0].toUpper();
    return words.join(' ');
}

TvBackend::TvBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
{
    loadSettings();
    // NOTE: no buildLineup() here — see ensureLineup().

    // Kick off filler-clip generation in the background. Nothing waits on it:
    // transitions and colour bars simply start applying once the clips exist.
    m_filler = new FillerAssets(dataRoot, this);
    m_filler->ensureAsync();

    // Broadcast mode needs real episode lengths. Probing is background work and
    // schedules are rebuilt as better numbers arrive, so a large library never
    // blocks — it just starts out assuming a typical episode length.
    m_durations = new DurationCache(dataRoot, this);
    connect(m_durations, &DurationCache::updated, this, [this]() {
        buildSchedules();
        emit lineupChanged();
    });
}

QString TvBackend::transitionClip() const {
    if (!m_filler || m_transition == QLatin1String("none"))
        return QString();
    return m_transition == QLatin1String("glitch") ? m_filler->glitchPath()
                                                   : m_filler->staticPath();
}

QString TvBackend::colorbarsClip() const {
    return m_filler ? m_filler->colorbarsPath() : QString();
}

void TvBackend::loadSettings() {
    QFile f(m_dataRoot + "/config.json");
    if (!f.open(QFile::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject mod  = root.value("modules").toObject()
                                 .value(QString::fromLatin1(kModuleId)).toObject();

    m_mediaRoot = mod.value("media_directory").toString();
    if (m_mediaRoot.isEmpty())
        m_mediaRoot = m_dataRoot + "/tv";

    const QString mode = mod.value("tune_in").toString();
    if (mode == QLatin1String("resume") || mode == QLatin1String("random")
        || mode == QLatin1String("broadcast"))
        m_tuneInMode = mode;

    // Stored as a plain number of seconds; the range is min..min+4 so channel
    // changes land at varied points instead of always the same spot.
    if (mod.contains("start_offset")) {
        const double v = mod.value("start_offset").toVariant().toDouble();
        m_startOffsetMin = qMax(0.0, v);
        m_startOffsetMax = m_startOffsetMin > 0.0 ? m_startOffsetMin + 4.0 : 0.0;
    }
    if (mod.contains("initial_volume"))
        m_initialVolume = qBound(0, mod.value("initial_volume").toVariant().toInt(), 100);

    // OSD look. `safe_area` is the fraction of each edge kept clear of overscan
    // and is meant to be calibrated per set — see the plan, Phase 4.
    if (mod.contains("banner_seconds"))
        m_bannerMs = qBound(0, int(mod.value("banner_seconds").toVariant().toDouble() * 1000), 30000);
    const QString uiColor = mod.value("ui_color").toString();
    if (uiColor.startsWith('#') && uiColor.size() == 7)
        m_overlayStyle.color = uiColor;
    if (mod.contains("safe_area"))
        m_overlayStyle.safe = qBound(0.0, mod.value("safe_area").toVariant().toDouble(), 0.30);
    // Per-edge overrides for a tube whose overscan is not centred.
    auto edge = [&](const char *key, double &out) {
        if (mod.contains(QString::fromLatin1(key)))
            out = qBound(0.0, mod.value(QString::fromLatin1(key)).toVariant().toDouble(), 0.30);
    };
    edge("safe_left",   m_overlayStyle.safeLeft);
    edge("safe_right",  m_overlayStyle.safeRight);
    edge("safe_top",    m_overlayStyle.safeTop);
    edge("safe_bottom", m_overlayStyle.safeBottom);
    // Legibility knobs, all tunable from config.json so the OSD can be dialled in
    // against the actual tube without a rebuild.
    if (mod.contains("ui_bloom"))
        m_overlayStyle.bloom = mod.value("ui_bloom").toVariant().toBool();
    if (mod.contains("ui_border"))
        m_overlayStyle.border = qBound(0.0, mod.value("ui_border").toVariant().toDouble(), 8.0);
    if (mod.contains("ui_blur"))
        m_overlayStyle.blur = qBound(0.0, mod.value("ui_blur").toVariant().toDouble(), 8.0);
    if (mod.contains("ui_number_size"))
        m_overlayStyle.numberSize = qBound(8, mod.value("ui_number_size").toVariant().toInt(), 200);
    if (mod.contains("ui_name_size"))
        m_overlayStyle.nameSize = qBound(8, mod.value("ui_name_size").toVariant().toInt(), 200);
    if (mod.contains("ui_message_size"))
        m_overlayStyle.messageSize = qBound(8, mod.value("ui_message_size").toVariant().toInt(), 200);

    // Episode ordering: "random" (shuffle bag) or "sequential".
    const QString order = mod.value("episode_order").toString();
    if (order == QLatin1String("sequential") || order == QLatin1String("random"))
        m_episodeOrder = order;

    m_autoTune = mod.value("auto_tune").toVariant().toBool();
    const QJsonObject orders = mod.value("channel_orders").toObject();
    m_channelOrders.clear();
    for (auto it = orders.begin(); it != orders.end(); ++it)
        m_channelOrders.insert(it.key().toInt(), it.value().toString());
    if (mod.contains("last_channel"))
        m_lastChannelNum = mod.value("last_channel").toVariant().toInt();

    // Commercials. Empty directory = feature off.
    m_adDirectory = mod.value("commercials_directory").toString();
    if (mod.contains("commercial_min"))
        m_adMin = qBound(0, mod.value("commercial_min").toVariant().toInt(), 10);
    if (mod.contains("commercial_max"))
        m_adMax = qBound(0, mod.value("commercial_max").toVariant().toInt(), 10);
    if (m_adMax < m_adMin)
        m_adMax = m_adMin;
    // Guarded by contains() rather than read unconditionally: this one defaults
    // to ON, and an absent key must keep that default instead of reading back
    // as false. TvBackend parses config.json itself, so the manifest default
    // never reaches here.
    if (mod.contains("ads_on_manual_pick"))
        m_adsOnManualPick = mod.value("ads_on_manual_pick").toVariant().toBool();

    m_bumperDirectory = mod.value("bumpers_directory").toString();
    m_identDirectory  = mod.value("idents_directory").toString();

    // Channel-change transition.
    const QString effect = mod.value("transition").toString();
    if (effect == QLatin1String("static") || effect == QLatin1String("glitch")
        || effect == QLatin1String("none"))
        m_transition = effect;
    if (mod.contains("transition_seconds"))
        m_transitionSeconds = qBound(0.05, mod.value("transition_seconds").toVariant().toDouble(), 3.0);
    if (mod.contains("bridge_seconds"))
        m_bridgeMs = qBound(0, int(mod.value("bridge_seconds").toVariant().toDouble() * 1000), 5000);

    // Sleep timer. The ladder mirrors the TV's own so one SLEEP press sets both
    // to the same value; it is overridable in case another set counts differently.
    const QString action = mod.value("sleep_action").toString();
    if (action == QLatin1String("poweroff") || action == QLatin1String("standby"))
        m_sleepAction = action;
    const QJsonArray ladder = mod.value("sleep_ladder").toArray();
    if (!ladder.isEmpty()) {
        QVector<int> parsed;
        for (const QJsonValue &v : ladder)
            parsed.append(qMax(0, v.toInt()));
        m_sleepLadder = parsed;
    }
}

int TvBackend::startChannelIndex() {
    ensureLineup();
    if (m_lastChannelNum >= 0) {
        const QVector<Channel> &chans = m_lineup.channels();
        for (int i = 0; i < chans.size(); ++i)
            if (chans[i].number() == m_lastChannelNum)
                return i;
    }
    return 0;
}

QVariantList TvBackend::sleepLadder() const {
    QVariantList out;
    for (int m : m_sleepLadder)
        out.append(m);
    return out;
}

// ---------------------------------------------------------------------------
// On-screen display
// ---------------------------------------------------------------------------

QString TvBackend::channelBannerAss(int number, const QString &name) const {
    return TvOverlay::channelBanner(number, name, m_overlayStyle);
}

QString TvBackend::messageAss(const QString &text, const QString &position) const {
    const auto pos = (position == QLatin1String("left"))
                         ? TvOverlay::MessagePos::CentreLeft
                         : TvOverlay::MessagePos::TopCentre;
    return TvOverlay::message(text, m_overlayStyle, pos);
}

int TvBackend::canvasWidth()  const { return TvOverlay::CanvasW; }
int TvBackend::canvasHeight() const { return TvOverlay::CanvasH; }
int TvBackend::overlayIdChannel() const { return TvOverlay::IdChannel; }
int TvBackend::overlayIdMessage() const { return TvOverlay::IdMessage; }
int TvBackend::overlayIdCalibrate() const { return TvOverlay::IdCalibrate; }

QString TvBackend::calibrationAss() const {
    return TvOverlay::calibrationPattern(m_overlayStyle);
}

// "Show - S01E02 - Episode Name.mkv" -> "S01E02  EPISODE NAME". Falls back to
// the bare filename when it does not follow a recognisable convention.
static QString episodeTitle(const QString &path) {
    const QString base = QFileInfo(path).completeBaseName();
    static const QRegularExpression code(QStringLiteral("(s\\d{1,2}e\\d{1,3})"),
                                         QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = code.match(base);
    const QString ep = m.hasMatch() ? m.captured(1).toUpper() : QString();

    QString title = base;
    const QStringList parts = base.split(QStringLiteral(" - "));
    if (parts.size() > 1)
        title = parts.last().trimmed();
    if (!ep.isEmpty()) {
        if (title.compare(ep, Qt::CaseInsensitive) == 0)
            return ep;
        if (!title.contains(ep, Qt::CaseInsensitive))
            return ep + QStringLiteral("  ") + title.toUpper();
    }
    return title.toUpper();
}

int TvBackend::overlayIdGuide() const { return TvOverlay::IdGuide; }

QString TvBackend::guideAss(const QVariantMap &colors, int selRow, int selCol,
                            int firstRow, int visibleRows) {
    ensureLineup();

    TvOverlay::GuideTheme theme;
    theme.primary   = colors.value("primary",   "#FFFFFF").toString();
    theme.secondary = colors.value("secondary", "#AAAAAA").toString();
    theme.tertiary  = colors.value("tertiary",  "#777777").toString();
    theme.surface   = colors.value("surface",   "#000000").toString();
    theme.accent    = colors.value("accent",    "#4DFF5A").toString();
    if (colors.contains("font"))
        theme.font = colors.value("font").toString();

    const int nCols = guideColumns();
    QVector<TvOverlay::GuideRow> rows;
    const QVector<Channel> &chans = m_lineup.channels();
    rows.reserve(chans.size());
    for (const Channel &ch : chans) {
        TvOverlay::GuideRow r;
        r.number = ch.number();
        r.name   = ch.name().toUpper();
        const QStringList up = ch.upcoming(nCols);
        for (int i = 0; i < nCols; ++i)
            r.cells.append(i < up.size() ? episodeTitle(up[i]) : QStringLiteral("—"));
        rows.append(r);
    }

    // Detail panel describes whatever is highlighted, in full — that is what
    // makes truncating the grid cells acceptable.
    QString detailTitle = QStringLiteral("NO CHANNELS");
    QString detailSub;
    if (selRow >= 0 && selRow < rows.size()) {
        const TvOverlay::GuideRow &r = rows[selRow];
        const int c = qBound(0, selCol, nCols - 1);
        const QStringList labels{ "NOW", "NEXT", "THEN" };
        const QString title = c < r.cells.size() ? r.cells[c] : QString();
        // Reads as "S01E02  PLAYING NOW" — the slot belongs with the episode.
        detailTitle = QStringLiteral("%1   PLAYING %2").arg(title, labels.value(c));
        detailSub = QStringLiteral("CH %1   %2")
                        .arg(QString::number(r.number).rightJustified(2, QLatin1Char('0')),
                             r.name);
    }

    // The settings bar is the row after the last channel. It used to toggle the
    // channel's episode order in place; it now opens the settings page, so a
    // single press can no longer change anything by accident.
    const bool optionSelected = (selRow >= rows.size());
    const QString optionText = QStringLiteral("SETTINGS");
    if (optionSelected) {
        detailTitle = QStringLiteral("TV SETTINGS");
        detailSub   = QStringLiteral("ORDER, COMMERCIALS, OVERSCAN  ·  SELECT TO OPEN");
    }

    return TvOverlay::guideGrid(rows, QStringList{ "NOW", "NEXT", "THEN" },
                                firstRow, visibleRows, selRow, selCol,
                                detailTitle, detailSub,
                                optionText, optionSelected, theme,
                                m_overlayStyle);
}

QVariantMap TvBackend::playGuideSelection(int row, int col) {
    ensureLineup();
    clearBreak();
    Channel *c = m_lineup.selectIndex(row);
    if (!c)
        return QVariantMap{{"valid", false}};
    const QStringList up = c->upcoming(guideColumns());
    if (col < 0 || col >= up.size())
        return QVariantMap{{"valid", false}};
    return requestToMap(c->playSpecific(up[col]));
}

int TvBackend::overlayIdEpisodes() const { return TvOverlay::IdEpisodes; }

QStringList TvBackend::channelEpisodeTitles(int channelIndex) {
    ensureLineup();
    const QVector<Channel> &chans = m_lineup.channels();
    if (channelIndex < 0 || channelIndex >= chans.size())
        return {};
    QStringList out;
    const QStringList eps = chans[channelIndex].episodes();
    out.reserve(eps.size());
    for (const QString &p : eps)
        out << episodeTitle(p);
    return out;
}

QString TvBackend::channelLabel(int channelIndex) {
    ensureLineup();
    const QVector<Channel> &chans = m_lineup.channels();
    if (channelIndex < 0 || channelIndex >= chans.size())
        return QString();
    return QString::number(chans[channelIndex].number()).rightJustified(2, '0')
           + "  " + chans[channelIndex].name().toUpper();
}

QString TvBackend::episodeListAss(const QVariantMap &colors, int channelIndex,
                                  int selRow, int firstRow, int visibleRows) {
    ensureLineup();

    TvOverlay::GuideTheme theme;
    theme.primary   = colors.value("primary",   "#FFFFFF").toString();
    theme.secondary = colors.value("secondary", "#AAAAAA").toString();
    theme.tertiary  = colors.value("tertiary",  "#777777").toString();
    theme.surface   = colors.value("surface",   "#000000").toString();
    theme.accent    = colors.value("accent",    "#4DFF5A").toString();
    if (colors.contains("font"))
        theme.font = colors.value("font").toString();

    return TvOverlay::episodeList(channelLabel(channelIndex),
                                  channelEpisodeTitles(channelIndex),
                                  firstRow, visibleRows, selRow,
                                  theme, m_overlayStyle);
}

int TvBackend::overlayIdSettings() const { return TvOverlay::IdSettings; }

int TvBackend::optionRowCapacity() const {
    return TvOverlay::optionListCapacity(m_overlayStyle);
}

int TvBackend::episodeRowCapacity() const {
    return TvOverlay::episodeListCapacity(m_overlayStyle);
}

QString TvBackend::settingsAss(const QVariantMap &colors, const QString &title,
                               const QStringList &labels, const QStringList &values,
                               int selRow, int firstRow, int visibleRows,
                               const QString &hint, bool compact) {
    TvOverlay::GuideTheme theme;
    theme.primary   = colors.value("primary",   "#FFFFFF").toString();
    theme.secondary = colors.value("secondary", "#AAAAAA").toString();
    theme.tertiary  = colors.value("tertiary",  "#777777").toString();
    theme.surface   = colors.value("surface",   "#000000").toString();
    theme.accent    = colors.value("accent",    "#4DFF5A").toString();
    if (colors.contains("font"))
        theme.font = colors.value("font").toString();

    return TvOverlay::optionList(title, labels, values, selRow,
                                 firstRow, visibleRows, hint, compact,
                                 theme, m_overlayStyle);
}

double TvBackend::safeEdge(const QString &edge) const {
    if (edge == QLatin1String("left"))   return m_overlayStyle.leftFrac();
    if (edge == QLatin1String("right"))  return m_overlayStyle.rightFrac();
    if (edge == QLatin1String("top"))    return m_overlayStyle.topFrac();
    if (edge == QLatin1String("bottom")) return m_overlayStyle.bottomFrac();
    return m_overlayStyle.safe;
}

void TvBackend::setSafeEdge(const QString &edge, double fraction) {
    // Same ceiling loadSettings() clamps to, so a value dialled in here can
    // always be written back to config.json and read again unchanged.
    const double v = qBound(0.0, fraction, 0.30);
    if (edge == QLatin1String("left"))        m_overlayStyle.safeLeft   = v;
    else if (edge == QLatin1String("right"))  m_overlayStyle.safeRight  = v;
    else if (edge == QLatin1String("top"))    m_overlayStyle.safeTop    = v;
    else if (edge == QLatin1String("bottom")) m_overlayStyle.safeBottom = v;
}

QVariantMap TvBackend::playChannelEpisode(int channelIndex, int episodeIndex) {
    ensureLineup();
    ensurePools();
    clearBreak();
    Channel *c = m_lineup.selectIndex(channelIndex);
    if (!c)
        return QVariantMap{{"valid", false}};
    const QStringList eps = c->episodes();
    if (episodeIndex < 0 || episodeIndex >= eps.size())
        return QVariantMap{{"valid", false}};

    const PlayRequest programme = c->playSpecific(eps[episodeIndex]);
    if (!m_adsOnManualPick || !programme.isValid())
        return requestToMap(programme);

    // Ads first, episode held back — the same mechanism advanceCurrent() uses,
    // so Session.qml needs no special case: it plays what it is handed and asks
    // for the next thing when the file ends.
    const QStringList queue = buildBreakQueue(channelIndex);
    if (queue.isEmpty())
        return requestToMap(programme);

    m_afterInterstitials = programme;
    m_haveAfter          = true;
    m_interstitials      = queue;
    return interstitialToMap(m_interstitials.takeFirst());
}

QVector<ChannelConfig> TvBackend::readChannelsFile() const {
    QVector<ChannelConfig> out;
    QFile f(m_dataRoot + "/tv_channels.json");
    if (!f.open(QFile::ReadOnly))
        return out;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning("[tv_mode] tv_channels.json is not a valid JSON array: %s",
                 qPrintable(err.errorString()));
        return out;
    }

    const QJsonArray arr = doc.array();
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr[i].toObject();
        const QString path  = o.value("path").toString();
        if (path.isEmpty()) {
            qWarning("[tv_mode] tv_channels.json entry %d has no 'path' — skipped", i);
            continue;
        }
        ChannelConfig cfg;
        cfg.path   = path;
        cfg.number = o.contains("number") ? o.value("number").toInt() : i + 2;
        cfg.name   = o.value("name").toString();
        if (cfg.name.isEmpty())
            cfg.name = prettifyName(QFileInfo(path).fileName());
        for (const QJsonValue &v : o.value("exclude").toArray())
            cfg.exclude.append(v.toString());
        for (const QJsonValue &v : o.value("exclude_seasons").toArray())
            cfg.excludeSeasons.insert(v.toInt());
        cfg.commercials = o.value("commercials").toString();
        cfg.bumpers     = o.value("bumpers").toString();
        cfg.idents      = o.value("idents").toString();
        out.append(cfg);
    }
    return out;
}

QVector<ChannelConfig> TvBackend::discoverChannels() const {
    QVector<ChannelConfig> out;
    QDir root(m_mediaRoot);
    if (!root.exists()) {
        qWarning("[tv_mode] media directory does not exist: %s", qPrintable(m_mediaRoot));
        return out;
    }
    // Old sets started around channel 2, so the lineup does too.
    int number = 2;
    const QFileInfoList dirs =
        root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &d : dirs) {
        if (d.fileName().startsWith('.'))
            continue;
        ChannelConfig cfg;
        cfg.number = number++;
        cfg.name   = prettifyName(d.fileName());
        cfg.path   = d.absoluteFilePath();
        out.append(cfg);
    }
    return out;
}

void TvBackend::buildLineup() {
    QVector<ChannelConfig> configs = readChannelsFile();
    const bool explicitConfig = !configs.isEmpty();
    if (!explicitConfig)
        configs = discoverChannels();

    const QStringList exts = defaultVideoExtensions();
    QVector<Channel> channels;
    channels.reserve(configs.size());
    for (const ChannelConfig &cfg : configs) {
        const QStringList episodes = scanEpisodes(cfg.path, exts, m_recursive,
                                                  cfg.exclude, cfg.excludeSeasons);
        if (episodes.isEmpty()) {
            qWarning("[tv_mode] channel %d (%s) has no playable episodes in %s",
                     cfg.number, qPrintable(cfg.name), qPrintable(cfg.path));
        }
        channels.append(Channel(cfg, episodes, m_tuneInMode,
                                m_startOffsetMin, m_startOffsetMax, m_episodeOrder));
    }
    m_lineup = ChannelLineup(channels);
    // Re-apply any per-channel order overrides saved from the guide.
    for (Channel &c : m_lineup.mutableChannels()) {
        const auto it = m_channelOrders.constFind(c.number());
        if (it != m_channelOrders.constEnd())
            c.setOrder(it.value());
    }
    m_poolsScanned = false;   // pools are lineup-shaped

    if (m_tuneInMode == QLatin1String("broadcast") && m_durations) {
        QStringList all;
        for (const Channel &c : m_lineup.channels())
            all << c.episodes();
        buildSchedules();          // with whatever is cached (or defaults)
        m_durations->probeAsync(all);
    }

    qInfo("[tv_mode] lineup: %d channel(s) from %s", m_lineup.size(),
          explicitConfig ? "tv_channels.json" : qPrintable(m_mediaRoot));
    emit lineupChanged();
}

void TvBackend::ensureLineup() {
    if (m_lineupBuilt)
        return;
    m_lineupBuilt = true;   // set first: a failed scan must not retry forever
    buildLineup();
}

void TvBackend::rescan() {
    loadSettings();
    m_poolsScanned = false;
    clearBreak();
    m_lineupBuilt = true;
    buildLineup();
}

// ---------------------------------------------------------------------------
// QML-facing helpers
// ---------------------------------------------------------------------------

QVariantMap TvBackend::channelToMap(const Channel *channel) const {
    QVariantMap m;
    if (!channel) {
        m["valid"] = false;
        return m;
    }
    m["number"]   = channel->number();
    m["name"]     = channel->name();
    m["episodes"] = channel->episodeCount();
    m["index"]    = m_lineup.currentIndex();
    return m;
}

QVariantMap TvBackend::requestToMap(const PlayRequest &req) const {
    QVariantMap m = channelToMap(m_lineup.current());
    m["valid"] = req.isValid();
    m["path"]  = req.path;
    m["start"] = req.start;
    return m;
}

QVariantList TvBackend::channels() {
    ensureLineup();
    QVariantList out;
    const QVector<Channel> &list = m_lineup.channels();
    for (int i = 0; i < list.size(); ++i) {
        QVariantMap m;
        m["number"]   = list[i].number();
        m["name"]     = list[i].name();
        m["episodes"] = list[i].episodeCount();
        m["index"]    = i;
        out.append(m);
    }
    return out;
}

QVariantMap TvBackend::currentChannel() {
    ensureLineup();
    return channelToMap(m_lineup.current());
}

// Tuning to a channel: air its ident first, if it has one, then the programme.
QVariantMap TvBackend::tuneInCurrent() {
    ensureLineup();
    ensurePools();
    Channel *c = m_lineup.current();
    if (!c)
        return QVariantMap{{"valid", false}};

    PlayRequest programme = (m_tuneInMode == QLatin1String("broadcast") && c->hasSchedule())
        ? c->scheduledAt(QDateTime::currentSecsSinceEpoch())
        : c->tuneIn();

    const QString ident = m_idents.draw(m_lineup.currentIndex());
    if (ident.isEmpty() || !programme.isValid())
        return requestToMap(programme);

    m_afterInterstitials = programme;
    m_haveAfter = true;
    m_interstitials.clear();
    return interstitialToMap(ident);
}

// Scans the three clip pools on first use, like the lineup.
void TvBackend::ensurePools() {
    if (m_poolsScanned)
        return;
    m_poolsScanned = true;
    const QVector<Channel> &chans = m_lineup.channels();

    QStringList adDirs, bumperDirs, identDirs;
    for (const Channel &c : chans) {
        adDirs     << c.commercialsDir();
        bumperDirs << c.bumpersDir();
        identDirs  << c.identsDir();
    }
    m_ads.build(m_adDirectory, chans, adDirs, QStringLiteral("commercial(s)"));
    m_bumpers.build(m_bumperDirectory, chans, bumperDirs, QStringLiteral("bumper(s)"));
    m_idents.build(m_identDirectory, chans, identDirs, QStringLiteral("ident(s)"));
}

QVariantMap TvBackend::interstitialToMap(const QString &path) const {
    QVariantMap m = channelToMap(m_lineup.current());
    m["valid"] = !path.isEmpty();
    m["path"]  = path;
    m["start"] = 0.0;          // interstitials always play from the top
    m["commercial"] = true;    // Session.qml: no banner, no resume position
    return m;
}

QVariantMap TvBackend::advanceCurrent() {
    ensureLineup();
    ensurePools();
    Channel *c = m_lineup.current();
    if (!c)
        return QVariantMap{{"valid", false}};

    // Mid-break / mid-ident: keep rolling what is queued.
    if (!m_interstitials.isEmpty())
        return interstitialToMap(m_interstitials.takeFirst());

    // The queue just drained — now the programme it was holding back. In
    // broadcast mode the timeline has moved on while the break aired, so ask it
    // again rather than playing a request that is now stale.
    if (m_haveAfter) {
        m_haveAfter = false;
        if (m_tuneInMode == QLatin1String("broadcast") && c->hasSchedule())
            return requestToMap(nextProgramme(c));
        return requestToMap(m_afterInterstitials);
    }

    // An episode ended. Cut to a break before the next one, the way a real
    // station would, rather than running shows back to back.
    const QStringList queue = buildBreakQueue(m_lineup.currentIndex());

    if (queue.isEmpty())
        return requestToMap(nextProgramme(c));

    m_afterInterstitials = nextProgramme(c);
    m_haveAfter = true;
    m_interstitials = queue;
    return interstitialToMap(m_interstitials.takeFirst());
}

// One commercial break for channel `idx`: an opening bumper, a random number of
// ads within the configured min..max, and a closing bumper. Empty when nothing
// is configured, which callers read as "go straight to the programme".
//
// Shared by the natural end-of-episode break and the optional break in front of
// a manually picked episode, so both sound like the same station.
QStringList TvBackend::buildBreakQueue(int idx) {
    QStringList queue;
    const QString bumperIn = m_bumpers.draw(idx);
    if (!bumperIn.isEmpty())
        queue << bumperIn;
    if (m_adMax > 0) {
        const int span  = qMax(0, m_adMax - m_adMin);
        const int count = m_adMin + (span > 0 ? QRandomGenerator::global()->bounded(span + 1) : 0);
        for (int i = 0; i < count; ++i) {
            const QString ad = m_ads.draw(idx);
            if (!ad.isEmpty())
                queue << ad;
        }
    }
    // A closing bumper only makes sense if something aired between them.
    if (!queue.isEmpty()) {
        const QString bumperOut = m_bumpers.draw(idx);
        if (!bumperOut.isEmpty())
            queue << bumperOut;
    }
    return queue;
}

// Changing channel abandons anything queued — you don't return to the middle of
// someone else's ad slot.
void TvBackend::clearBreak() {
    m_interstitials.clear();
    m_haveAfter = false;
}

PlayRequest TvBackend::nextProgramme(Channel *c) const {
    if (!c)
        return PlayRequest();
    if (m_tuneInMode == QLatin1String("broadcast") && c->hasSchedule())
        return c->scheduledAt(QDateTime::currentSecsSinceEpoch());
    return c->advance();
}

// Broadcast mode: lay every channel's episodes on a shared, fixed timeline.
void TvBackend::buildSchedules() {
    if (m_tuneInMode != QLatin1String("broadcast") || !m_durations)
        return;
    // A fixed epoch (2000-01-01 UTC) rather than "now": the station has been on
    // the air since then, so restarting the box does not restart the schedule.
    const double epoch = 946684800.0;
    for (Channel &c : m_lineup.mutableChannels()) {
        QVector<double> durs;
        const QStringList eps = c.episodes();
        durs.reserve(eps.size());
        for (const QString &e : eps)
            durs.append(m_durations->durationFor(e));
        c.buildSchedule(durs, epoch);
    }
}

QVariantMap TvBackend::channelUp() {
    ensureLineup();
    clearBreak();
    m_lastIndex = m_lineup.currentIndex();
    Channel *c = m_lineup.up();
    if (!c)
        return QVariantMap{{"valid", false}};
    return requestToMap(c->tuneIn());
}

QVariantMap TvBackend::channelDown() {
    ensureLineup();
    clearBreak();
    m_lastIndex = m_lineup.currentIndex();
    Channel *c = m_lineup.down();
    if (!c)
        return QVariantMap{{"valid", false}};
    return requestToMap(c->tuneIn());
}

QVariantMap TvBackend::selectIndex(int index) {
    ensureLineup();
    clearBreak();
    // Entering the lineup is not a "channel change", so this deliberately does
    // not arm the last-channel flip — there is nothing to flip back to yet.
    Channel *c = m_lineup.selectIndex(index);
    if (!c)
        return QVariantMap{{"valid", false}};
    return tuneInCurrent();
}

QVariantMap TvBackend::lastChannel() {
    ensureLineup();
    clearBreak();
    if (m_lastIndex < 0 || m_lastIndex >= m_lineup.size())
        return QVariantMap{{"valid", false}};
    const int target = m_lastIndex;
    m_lastIndex = m_lineup.currentIndex();   // so pressing it again flips back
    Channel *c = m_lineup.selectIndex(target);
    if (!c)
        return QVariantMap{{"valid", false}};
    return requestToMap(c->tuneIn());
}

void TvBackend::rememberPosition(const QString &path, double positionSeconds) {
    if (m_tuneInMode != QLatin1String("resume"))
        return;
    if (Channel *c = m_lineup.current())
        c->remember(path, positionSeconds);
}

void TvBackend::setChannelOrder(int channelIndex, const QString &order) {
    if (order != QLatin1String("sequential") && order != QLatin1String("random"))
        return;
    ensureLineup();
    QVector<Channel> &chans = m_lineup.mutableChannels();
    if (channelIndex < 0 || channelIndex >= chans.size())
        return;
    chans[channelIndex].setOrder(order);
    m_channelOrders.insert(chans[channelIndex].number(), order);
}

QString TvBackend::channelOrder(int channelIndex) {
    ensureLineup();
    const QVector<Channel> &chans = m_lineup.channels();
    if (channelIndex < 0 || channelIndex >= chans.size())
        return m_episodeOrder;
    return chans[channelIndex].order();
}

void TvBackend::setEpisodeOrder(const QString &order) {
    if (order != QLatin1String("sequential") && order != QLatin1String("random"))
        return;
    m_episodeOrder = order;
    for (Channel &c : m_lineup.mutableChannels())
        c.setOrder(order);
}

void TvBackend::get_episode_order_options() {
    QVariantList opts;
    opts.append(QVariantMap{{"id", "sequential"}, {"label", "Sequential"}});
    opts.append(QVariantMap{{"id", "random"},     {"label", "Random"}});
    emit dynamicOptionsReady(QStringLiteral("episode_order"), opts);
}

void TvBackend::get_tune_in_options() {
    QVariantList opts;
    opts.append(QVariantMap{{"id", "random"}, {"label", "Random"}});
    opts.append(QVariantMap{{"id", "resume"}, {"label", "Resume"}});
    opts.append(QVariantMap{{"id", "broadcast"}, {"label", "Broadcast"}});
    emit dynamicOptionsReady(QStringLiteral("tune_in"), opts);
}

int TvBackend::channelCount() {
    ensureLineup();
    return m_lineup.size();
}

void TvBackend::onSettingChanged(const QString &moduleId, const QString &key,
                                 const QVariant &value) {
    Q_UNUSED(value)
    if (moduleId != QLatin1String(kModuleId))
        return;
    // Anything that changes what the lineup contains, or how it plays, needs a
    // rebuild. Volume is read at session start, so it needs no action here.
    if (key == QLatin1String("media_directory") || key == QLatin1String("tune_in")
        || key == QLatin1String("start_offset")) {
        rescan();
    } else {
        loadSettings();
    }
}
