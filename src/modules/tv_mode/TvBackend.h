#pragma once
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QHash>
#include <QVariantMap>

#include "Channel.h"
#include "DurationCache.h"
#include "FillerAssets.h"
#include "MediaPool.h"
#include "TvOverlay.h"

// Backend for the TV Mode module: owns the channel lineup and answers "what
// plays next", but never talks to mpv itself.
//
// That split is deliberate and matches the rest of 240-MP: QML drives
// `mpvController`, backends supply data. Session.qml asks this class what to
// play and hands the answer to MpvController's session API.
class TvBackend : public QObject {
    Q_OBJECT
public:
    explicit TvBackend(const QString &appRoot, const QString &dataRoot,
                       QObject *parent = nullptr);

    // ---- lineup ----------------------------------------------------------
    // [{ number, name, index, episodes }] — for the guide screen.
    Q_INVOKABLE QVariantList channels();
    Q_INVOKABLE int          channelCount();
    Q_INVOKABLE QVariantMap  currentChannel();
    // Re-read the media directory / tv_channels.json and rebuild the lineup.
    Q_INVOKABLE void         rescan();

    // ---- playback decisions ---------------------------------------------
    // Each returns { valid, path, start, number, name, index, episodes }.
    // `valid` is false for a channel with no playable episodes — the caller
    // should show "no signal" rather than trying to play anything.
    Q_INVOKABLE QVariantMap tuneInCurrent();
    Q_INVOKABLE QVariantMap advanceCurrent();
    Q_INVOKABLE QVariantMap channelUp();
    Q_INVOKABLE QVariantMap channelDown();
    Q_INVOKABLE QVariantMap selectIndex(int index);
    // Flip back to the channel watched before this one. Returns an invalid map
    // when there is no previous channel yet (nothing to jump to).
    Q_INVOKABLE QVariantMap lastChannel();
    Q_INVOKABLE bool        hasLastChannel() const { return m_lastIndex >= 0; }

    // ---- commercials -----------------------------------------------------
    // How many clips are queued for the current break, 0 when not in one.
    Q_INVOKABLE int  pendingBreakCount() const { return m_interstitials.size(); }
    Q_INVOKABLE bool inInterstitial() const { return !m_interstitials.isEmpty(); }

    // Record where the viewer left a channel, for the "resume" tune-in mode.
    Q_INVOKABLE void rememberPosition(const QString &path, double positionSeconds);

    // ---- on-screen display ----------------------------------------------
    // Session.qml hands these strings to mpvController.setOverlay(). The backend
    // builds the ASS but never draws it, keeping "QML drives mpv" intact.
    Q_INVOKABLE QString channelBannerAss(int number, const QString &name) const;
    Q_INVOKABLE QString messageAss(const QString &text,
                                   const QString &position = QStringLiteral("top")) const;
    Q_INVOKABLE int     canvasWidth()  const;
    Q_INVOKABLE int     canvasHeight() const;
    Q_INVOKABLE int     overlayIdChannel() const;
    Q_INVOKABLE int     overlayIdMessage() const;
    // Overscan calibration pattern — see TvOverlay::calibrationPattern.
    Q_INVOKABLE QString calibrationAss() const;
    Q_INVOKABLE int     overlayIdCalibrate() const;
    // How long the channel banner lingers, in milliseconds.
    Q_INVOKABLE int     bannerDurationMs() const { return m_bannerMs; }

    // ---- TV guide --------------------------------------------------------
    // `colors` carries 240-MP's active scheme from QML (primary/secondary/
    // tertiary/surface/accent/font) — the themes are defined in Main.qml,
    // including user-defined ones, so the guide follows whatever is selected.
    Q_INVOKABLE QString guideAss(const QVariantMap &colors,
                                 int selRow, int selCol,
                                 int firstRow, int visibleRows);
    Q_INVOKABLE int     overlayIdGuide() const;
    // Columns shown per channel. "Now / Next / Then" — positions in the queue,
    // not clock times: a shuffle bag has no schedule to report.
    Q_INVOKABLE int     guideColumns() const { return 3; }
    // Tune to a guide cell and play that episode. Returns the usual play map.
    Q_INVOKABLE QVariantMap playGuideSelection(int row, int col);

    // ---- episode list ----------------------------------------------------
    // Every episode on one channel, in the channel's own sorted order — the
    // list behind the guide's channel-name cell. Titles only; the caller picks
    // by index, so the paths never have to cross into QML.
    Q_INVOKABLE QStringList channelEpisodeTitles(int channelIndex);
    Q_INVOKABLE QString     channelLabel(int channelIndex);
    Q_INVOKABLE QString     episodeListAss(const QVariantMap &colors,
                                           int channelIndex, int selRow,
                                           int firstRow, int visibleRows);
    Q_INVOKABLE int         overlayIdEpisodes() const;
    // Tune to `channelIndex` and play its `episodeIndex`th episode from the
    // start. When the "commercials before pick" setting is on, a break airs
    // first and the episode follows it, exactly as at a natural episode end.
    Q_INVOKABLE QVariantMap playChannelEpisode(int channelIndex, int episodeIndex);
    Q_INVOKABLE bool        adsOnManualPick() const { return m_adsOnManualPick; }
    Q_INVOKABLE void        setAdsOnManualPick(bool on) { m_adsOnManualPick = on; }

    // ---- settings page ---------------------------------------------------
    // Generic label/value list, so QML owns which rows exist and what they say
    // while the drawing stays here with the rest of the OSD.
    Q_INVOKABLE QString settingsAss(const QVariantMap &colors,
                                    const QString &title,
                                    const QStringList &labels,
                                    const QStringList &values,
                                    int selRow, int firstRow, int visibleRows,
                                    const QString &hint,
                                    bool compact);
    Q_INVOKABLE int     overlayIdSettings() const;
    // Rows that actually fit at the current safe area. QML must size its scroll
    // window from these rather than a constant — the overscan screen can change
    // the safe area underneath it, and a stale larger value would let the
    // selected row scroll off the bottom of the list.
    Q_INVOKABLE int     optionRowCapacity()  const;
    Q_INVOKABLE int     episodeRowCapacity() const;

    // Overscan safe area, as a fraction of each edge. Set live so the
    // calibration pattern reflects a nudge immediately; QML persists the value
    // through appCore so it survives a restart.
    Q_INVOKABLE double  safeEdge(const QString &edge) const;
    Q_INVOKABLE void    setSafeEdge(const QString &edge, double fraction);

    // ---- channel-change transitions --------------------------------------
    // The clip shown briefly while changing channel, or "" for a cut / when the
    // clip has not finished generating yet.
    Q_INVOKABLE QString transitionClip()    const;
    Q_INVOKABLE double  transitionSeconds() const { return m_transitionSeconds; }
    // Colour bars for an empty channel, or "" if unavailable.
    Q_INVOKABLE QString colorbarsClip()     const;
    // With no transition effect, keep the outgoing show playing this long while
    // the next channel preloads, then cut over — avoids a frozen frame. 0 = cut.
    Q_INVOKABLE int     bridgeMs()          const { return m_bridgeMs; }

    // ---- settings --------------------------------------------------------
    // What the sleep timer does when it expires:
    //   "standby"  — stop playback, leave the Pi running (default; the Pi cannot
    //                be woken from halt by the remote, only by GPIO3 or power)
    //   "poweroff" — quit the app, which under the autostart service triggers a
    //                clean shutdown via 240mp-stop's ExecStopPost
    Q_INVOKABLE QString sleepAction()    const { return m_sleepAction; }
    // Minutes per press, mirroring the TV's own ladder. 0 means "cancel".
    Q_INVOKABLE QVariantList sleepLadder() const;
    // Skip the guide on entry and tune straight in — "power on, already
    // watching", which is the whole point of a TV.
    Q_INVOKABLE bool    autoTune()       const { return m_autoTune; }
    // Lineup index for the last channel watched, or 0. Resolved from a saved
    // channel NUMBER rather than an index, so it survives the lineup changing.
    Q_INVOKABLE int     startChannelIndex();
    Q_INVOKABLE QString mediaRoot()      const { return m_mediaRoot; }
    Q_INVOKABLE QString tuneInMode()     const { return m_tuneInMode; }
    Q_INVOKABLE QString episodeOrder()   const { return m_episodeOrder; }
    // Applied to every channel immediately, so the guide can toggle it
    // without rebuilding the lineup or losing the current position.
    Q_INVOKABLE void    setEpisodeOrder(const QString &order);
    // Per-channel override. Toggling order in the guide affects only the channel
    // you are pointing at — different shows want different treatment.
    Q_INVOKABLE void    setChannelOrder(int channelIndex, const QString &order);
    Q_INVOKABLE QString channelOrder(int channelIndex);
    Q_INVOKABLE void    get_episode_order_options();
    Q_INVOKABLE int     initialVolume()  const { return m_initialVolume; }
    // Options for the manifest's dynamic list_single settings.
    Q_INVOKABLE void    get_tune_in_options();

signals:
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void lineupChanged();

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key,
                          const QVariant &value);

private:
    void        loadSettings();
    void        buildLineup();
    // Scans on FIRST USE, not at construction. The lineup may live on a
    // network share (a Plex library, say), where scanning hundreds of files
    // at startup would delay the whole app — or block outright if the server
    // is offline. Nothing touches the media path until the module is opened.
    void        ensureLineup();
    // Explicit per-channel config from $DATA_ROOT/tv_channels.json, or an empty
    // list when the file is absent (the common case — channels are discovered).
    QVector<ChannelConfig> readChannelsFile() const;
    // Every immediate sub-folder of the media root becomes a channel.
    QVector<ChannelConfig> discoverChannels() const;
    QVariantMap requestToMap(const PlayRequest &req) const;
    QVariantMap channelToMap(const Channel *channel) const;

    QString       m_appRoot;
    QString       m_dataRoot;
    QString       m_mediaRoot;
    QString       m_tuneInMode    = QStringLiteral("random");
    QString       m_episodeOrder  = QStringLiteral("sequential");
    double        m_startOffsetMin = 6.0;
    double        m_startOffsetMax = 10.0;
    // Unity. mpv's software volume is the first of two gain stages (ALSA's PCM
    // control is the second), and anything below 100 here throws away headroom
    // the tube's own amplifier then has to make up. Override with
    // `initial_volume` in config.json; setVolume() clamps to this same ceiling.
    int           m_initialVolume  = 100;
    bool          m_recursive      = true;
    // Index of the channel watched before the current one, for the last-channel
    // flip. -1 until the viewer has changed channel at least once.
    int           m_lastIndex      = -1;
    int           m_bannerMs       = 4000;
    bool          m_autoTune       = false;
    // Per-channel order overrides, keyed by channel NUMBER so they survive
    // the lineup changing shape.
    QHash<int, QString> m_channelOrders;
    int           m_lastChannelNum = -1;
    QString       m_sleepAction    = QStringLiteral("standby");
    // "static" | "glitch" | "none". Snow is the period-correct effect for a tube,
    // so it is the default here even though NostalgiaBox defaults to none.
    QString       m_transition       = QStringLiteral("static");
    double        m_transitionSeconds = 0.4;
    int           m_bridgeMs          = 800;
    FillerAssets *m_filler            = nullptr;
    QVector<int>  m_sleepLadder    { 15, 30, 45, 60, 90, 0 };
    TvOverlay::Style m_overlayStyle;
    // Three pools of short clips, all resolved the same way (see MediaPool):
    //   commercials — the break itself
    //   bumpers     — station idents bracketing a break ("we'll be right back")
    //   idents      — a network sting when you tune to the channel
    QString       m_adDirectory, m_bumperDirectory, m_identDirectory;
    int           m_adMin        = 1;
    int           m_adMax        = 3;
    // Whether deliberately picking an episode still gets a commercial break in
    // front of it. On by default: a real station never cut straight to the show
    // just because you knew what you wanted to watch.
    bool          m_adsOnManualPick = true;
    MediaPool     m_ads, m_bumpers, m_idents;
    bool          m_poolsScanned = false;
    void          ensurePools();

    // Interstitials queued ahead of a real programme. This replaces the earlier
    // break-only flag: idents, bumpers and ads all queue through one path, so
    // "what plays next" has a single shape. m_haveAfter is what stops a drained
    // queue from looking like "an episode ended" and starting another break.
    QStringList   m_interstitials;
    PlayRequest   m_afterInterstitials;
    bool          m_haveAfter    = false;
    void          clearBreak();
    QStringList   buildBreakQueue(int channelIndex);
    QVariantMap   interstitialToMap(const QString &path) const;

    // Broadcast mode: real episode lengths, probed in the background.
    DurationCache *m_durations   = nullptr;
    void          buildSchedules();
    // What this channel airs next, honouring broadcast mode. In broadcast the
    // answer comes from the timeline, not the rotation — otherwise a channel
    // drifts off its own schedule as soon as an episode ends.
    PlayRequest   nextProgramme(Channel *c) const;

    bool          m_lineupBuilt = false;
    ChannelLineup m_lineup;
};
