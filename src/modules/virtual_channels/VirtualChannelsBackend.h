#pragma once
#include <functional>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "ChannelSchedule.h"
#include "LocalLibrary.h"
#include "ChannelTuner.h"
#include "DurationProbe.h"
#include "MediaServerSource.h"
#include "ScheduleGenerator.h"

class QTimer;

class VirtualChannelsBackend : public QObject {
    Q_OBJECT
public:
    explicit VirtualChannelsBackend(const QString &appRoot,
                                    const QString &dataRoot,
                                    QObject *plexBackend = nullptr,
                                    QObject *jellyfinBackend = nullptr,
                                    QObject *embyBackend = nullptr,
                                    QObject *parent = nullptr);

    Q_INVOKABLE QVariantList get_channels();

    Q_INVOKABLE int current_slot(int channelNumber);

    Q_INVOKABLE QVariantMap preview_source(int channelNumber,
                                           int artWidth = 480, int artHeight = 270);
    Q_INVOKABLE void preview_stream(int channelNumber);
    Q_INVOKABLE void cancel_preview_stream();

    Q_INVOKABLE QVariantMap now_next(int channelNumber);

    Q_INVOKABLE QVariantList upcoming(int channelNumber, int count = 40);

    static constexpr qint64 kMaxGuideSpanMs = 7LL * 24 * 3600 * 1000;
    Q_INVOKABLE QVariantList guide_grid(double fromMs, double spanMs);

    Q_INVOKABLE double now_ms() const;

    Q_INVOKABLE QVariantMap tune(int channelNumber);

    Q_INVOKABLE QVariantMap after_playback(int channelNumber,
                                           const QString &reason,
                                           int slotIndex,
                                           int consecutiveFailures);

    Q_INVOKABLE void regenerate(int channelNumber);
    Q_INVOKABLE bool is_generating() const { return m_genActive; }

    Q_INVOKABLE QString media_root() const { return m_mediaRoot; }
    Q_INVOKABLE QString prober_name() const { return m_probe.proberName(); }

    Q_INVOKABLE void get_resume_options();
    Q_INVOKABLE void get_schedule_days_options();
    Q_INVOKABLE void rebuild_all();

    static constexpr int kMaxBookingsPerChannel = 48;

    static constexpr int kMinScheduleDays     = 1;
    static constexpr int kMaxScheduleDays     = 7;
    static constexpr int kDefaultScheduleDays = 2;
    static constexpr int kStartupSweepDelayMs = 20000;
    Q_INVOKABLE int schedule_days() const;

    Q_INVOKABLE void top_up_schedules();
    Q_INVOKABLE void release_tuner();

    static constexpr int kMinGridMinutes = 5;
    static constexpr int kMaxGridMinutes = 120;

    Q_INVOKABLE QVariantMap channel_timing(int channelNumber);
    Q_INVOKABLE bool set_channel_grid(int channelNumber, int minutes);
    Q_INVOKABLE bool set_channel_ads(int channelNumber, int adsPerBreak);
    Q_INVOKABLE bool set_channel_order(int channelNumber, const QString &order);
    // "tv" or "movies". A movie channel airs films as its programmes, has no
    // movie slots and no grid, and takes its running order from where the films
    // come from rather than from a setting.
    Q_INVOKABLE bool set_channel_kind(int channelNumber, const QString &kind);
    // "playlist" or "selection", on a movie channel: a playlist airs in its own
    // order, a selection is shuffled.
    Q_INVOKABLE bool set_channel_films_from(int channelNumber, const QString &from);

    Q_INVOKABLE QVariantList list_logos();
    Q_INVOKABLE QString logos_dir() const { return m_dataRoot + QStringLiteral("/logos"); }
    Q_INVOKABLE QString logo_path(const QString &file) const;
    Q_INVOKABLE QString channel_logo(int channelNumber);
    Q_INVOKABLE bool    set_channel_logo(int channelNumber, const QString &file);

    Q_INVOKABLE int create_channel(const QString &name);

    Q_INVOKABLE QVariantList channel_interstitials(int channelNumber);
    Q_INVOKABLE QVariantList media_folders(int maxDepth = 3);

    Q_INVOKABLE QVariantList channel_bookings(int channelNumber);
    Q_INVOKABLE int  add_booking(int channelNumber);
    Q_INVOKABLE bool delete_booking(int channelNumber, int index);
    Q_INVOKABLE bool set_booking_name(int channelNumber, int index, const QString &name);
    Q_INVOKABLE bool set_booking_time(int channelNumber, int index, const QString &hhmm);
    Q_INVOKABLE bool set_booking_days(int channelNumber, int index, const QStringList &days);
    Q_INVOKABLE QStringList booking_list(int channelNumber, int index,
                                         const QString &field);
    Q_INVOKABLE bool set_booking_list(int channelNumber, int index,
                                      const QString &field, const QStringList &values);
    Q_INVOKABLE bool set_booking_any_film(int channelNumber, int index, bool any);
    Q_INVOKABLE bool set_booking_folder(int channelNumber, int index, const QString &folder);

    Q_INVOKABLE QStringList available_sources() const { return availableSources(); }
    // Asked per source rather than per channel: a break pool can draw on any
    // server that is connected, not only the one the channel is sourced from.
    Q_INVOKABLE bool source_supports_playlists(const QString &source) const;

    Q_INVOKABLE void browse_source(int channelNumber, const QString &kind,
                                   const QString &parentKey = QString());
    Q_INVOKABLE bool browse_busy() const;
    void browse_local(const QString &kind, const QString &parentKey);
    Q_INVOKABLE void browse_from(const QString &source, const QString &kind,
                                 const QString &parentKey = QString());

    Q_INVOKABLE QVariantList channel_pool(int channelNumber, const QString &pool);
    Q_INVOKABLE bool set_channel_pool(int channelNumber, const QString &pool,
                                      const QVariantList &entries);

    Q_INVOKABLE QVariantMap channel_source_config(int channelNumber);
    Q_INVOKABLE bool set_channel_source(int channelNumber, const QString &source);
    // `refs` runs alongside `values`: the id of each picked series on its
    // source, where the picker knew it. A blank entry leaves whatever id is
    // already stored for that name in place, so editing the list from a screen
    // that never saw the ids cannot throw them away.
    Q_INVOKABLE bool set_channel_list(int channelNumber, const QString &field,
                                      const QStringList &values,
                                      const QStringList &refs = {});
    // `seasonKey` says which season an episode belongs to, so that switching a
    // season back on can put all of its episodes on with it. Ignored for
    // seasons, which have no parent above the series.
    Q_INVOKABLE bool set_channel_excluded(int channelNumber, const QString &kind,
                                          const QString &itemKey, bool excluded,
                                          const QString &seasonKey = QString());
    // Every episode of one season airs again.
    Q_INVOKABLE bool clear_episode_exclusions(int channelNumber, const QString &seasonKey);

    Q_INVOKABLE QVariantList list_channels();

    Q_INVOKABLE int guide_channel_number() const;
    Q_INVOKABLE int weather_channel_number() const;
    Q_INVOKABLE bool weather_channel_enabled() const;
    Q_INVOKABLE bool rename_channel(int number, const QString &name);
    Q_INVOKABLE bool move_channel(int number, int direction);
    Q_INVOKABLE bool delete_channel(int number);

    Q_INVOKABLE QVariantList get_menu_entries() { return {}; }

signals:
    void sourceBrowseReady(const QString &kind, const QVariantList &items);
    void sourceBrowseFailed(const QString &kind, const QString &reason);

    void dynamicOptionsReady(const QString &key, const QVariant &options);

    void playDescriptorReady(const QVariantMap &descriptor);
    void previewStreamReady(int channelNumber, const QString &url, double positionMs);

    void generationProgress(int channelNumber, int done, int total);
    void generationFinished(int channelNumber, bool ok, const QString &message);

    void actionStatus(const QString &message);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

private slots:
    void onPlexStreamUrlReady(const QString &url, const QString &plexToken);
    void onServerStreamUrlReady(const QString &url);
    void onPlexItemLoaded(const QVariant &detail);
    void onPlexLibrariesLoaded(const QVariant &libraries);
    void onPlexItemsLoaded(const QVariant &items);
    void onPlexChildrenLoaded(const QVariant &items);
    void onPlexCollectionsLoaded(const QVariant &collections);
    void onPlexPlaylistsLoaded(const QVariant &playlists);
    void onGenerationTick();
    void onUrlTimeout();
    void onPlexEnumTimeout();

private:
    QString m_appRoot;
    QString m_dataRoot;
    QString m_mediaRoot;
    // Local files presented the way the servers present theirs, so one
    // browser can drive all four sources.
    mutable vchan::LocalLibrary m_localLibrary;

    QObject *m_plex = nullptr;
    QObject *m_jellyfin = nullptr;
    QObject *m_emby = nullptr;

    vchan::SlotSource channelSource(int channelNumber) const;
    static vchan::SlotSource sourceOf(const QJsonObject &channel);
    static bool    isMovieChannel(const QJsonObject &channel);
    static bool    playsAPlaylist(const QJsonObject &channel);
    static bool usesEntryPools(const QJsonObject &channel);
    static QString    sourceBlockName(vchan::SlotSource src);
    QStringList       availableSources() const;
    bool              sourceSignedIn(QObject *backend) const;

    vchan::DurationProbe m_probe;

    struct CachedSchedule {
        vchan::ChannelSchedule schedule;
        qint64                 mtimeMs = 0;
        QString                error;
    };
    QHash<int, CachedSchedule> m_cache;

    QTimer *m_urlTimer = nullptr;
    bool    m_urlPending = false;
    QVariantMap m_urlDescriptor;
    QString m_plexAwaitingDetailFor;

    enum class PlexStage { Idle, Libraries, Shows, Seasons, Episodes,
                           ApptLibraries, ApptItems,
                           ApptCollections, ApptCollectionItems,
                           ApptPlaylists, ApptPlaylistItems,
                           Collections, CollectionItems,
                           Playlists, PlaylistItems,
                           BrowseLibraries, BrowseShows, BrowseMovies,
                           BrowseCollections, BrowsePlaylists, BrowseChildren };
    PlexStage   m_pgStage = PlexStage::Idle;
    QTimer     *m_pgTimer = nullptr;
    int         m_pgChannel = -1;
    vchan::ChannelDef m_pgDef;
    QString     m_pgLibraryName;
    QStringList m_pgMatch;
    QStringList m_pgShowIds;
    // How many things carry each label being browsed, so a picker can show it.
    QHash<QString, int> m_browseTally;
    QSet<QString> m_pgExcludeSeasons;
    QSet<QString> m_pgExcludeEpisodes;
    QStringList m_pgCollections;
    QStringList m_pgPlaylists;
    QStringList m_pgPendingCollections;
    QStringList m_pgPendingPlaylists;
    bool m_pgAskedCollections = false;
    bool m_pgAskedPlaylists   = false;
    QStringList m_pgSections;
    QStringList m_pgShows;
    QStringList m_pgSeasons;
    QVector<vchan::MediaItem> m_pgEpisodes;

    struct PoolJob {
        vchan::SlotKind pool = vchan::SlotKind::Programme;
        int  apptIndex = -1;
        int  pack = -1;
        vchan::MediaServerSource::Request::Wants wants =
            vchan::MediaServerSource::Request::Wants::Episodes;

        QString library;
        QStringList titles;
        QStringList genres;
        QStringList collections;
        QStringList playlists;
        bool anyFilm = true;
        QStringList match;
        // The picked series' own ids on their source. Preferred over the name:
        // a show renamed on the server keeps its id.
        QStringList showIds;
        QSet<QString> excludeSeasons;
        QSet<QString> excludeEpisodes;
        vchan::SlotSource src = vchan::SlotSource::Plex;
    };
    static bool bookingWants(const PoolJob &job, const QString &title,
                             const QStringList &genres = QStringList());
    void appendToPool(const PoolJob &job, const QVector<vchan::MediaItem> &items);
    QVector<PoolJob> readPools(const QJsonObject &channel, vchan::ChannelDef &def) const;
    static QVector<QPair<vchan::SlotKind, const char *>> poolFields();
    QVector<PoolJob> m_pgApptJobs;
    int              m_pgApptCursor = 0;
    QStringList      m_pgApptSections;
    int              m_pgApptAdded = 0;
    enum class ApptPhase { Items, Collections, Playlists, Done };
    ApptPhase        m_pgApptPhase = ApptPhase::Items;
    QStringList      m_pgApptAllSections;
    QStringList      m_pgApptSetKeys;
    QSet<QString>    m_pgApptSeen;
    void plexApptAdvance();
    int  plexApptCollect(const QVariantList &items, bool applyCriteria);

    void browse_plex_shows();
    void browse_plex_movies(const QString &kind);
    void browse_plex_collections();
    void browse_plex_playlists();
    void browse_plex_seasons(const QString &showRatingKey);
    void browse_plex_episodes(const QString &seasonRatingKey);

    void parseBookings(const QJsonObject &o, int channelNumber,
                       vchan::ChannelDef &def,
                       QVector<PoolJob> &plexJobs,
                       QVector<PoolJob> &serverJobs,
                       QVector<QPair<int, QStringList>> *folderPools,
                       QVector<QPair<int, QStringList>> *localTitles = nullptr);

    QJsonArray bookingsOf(int channelNumber);
    bool       editBooking(int channelNumber, int index,
                           const std::function<void(QJsonObject &)> &edit);
    QString    bookingPoolKey(int channelNumber) const;

    void ensurePlexTimer();

    void armNightlySweep();
    QTimer *m_sweepTimer = nullptr;
    QVector<int> m_topUpQueue;
    int  m_tunedChannel = -1;

    void plexApptNext();
    QVector<PoolJob> m_serverApptJobs;
    int              m_serverApptCursor = 0;
    PoolJob          m_serverJob;
    bool             m_serverJobActive = false;
    void serverApptNext();
    QString m_browseKind;
    QVariantList m_browseItems;
    void browseStart(const QString &kind);
    void browseFail(const QString &reason);
    void finishLocalGeneration();

    void plexEnumStart(int channelNumber, const vchan::ChannelDef &def,
                       const QString &library, const QStringList &match,
                       const QStringList &showIds = {});
    bool plexExcluded(const QString &ratingKey, bool isSeason) const;
    bool plexItemToMedia(const QVariantMap &m, vchan::MediaItem *out) const;
    void plexEnumNext();
    void plexEnumFail(const QString &why);
    void plexEnumFinish();

    QList<int> m_rebuildQueue;

    QVariant settingValue(const QString &key) const;
    int      nextFreeChannelNumber() const;
    bool     appendChannel(const QJsonObject &channel, QString *error);
    QJsonArray readChannels() const;
    bool     writeChannels(const QJsonArray &channels);
    bool     moveScheduleFile(int fromNumber, int toNumber);
    void     setSpecialNumber(const QString &which, int number);
    bool     renumberDial();

    QTimer *m_genTimer = nullptr;
    bool    m_genActive = false;
    int     m_genChannel = -1;
    vchan::ChannelDef m_genDef;
    struct PendingItem { QString absPath; QString rel; vchan::SlotKind kind;
                         int apptIndex = -1;
                         int pack = -1;
                         // Known only for library items. A folder of clips has
                         // none of this and falls back to its directory name.
                         QString series = {};
                         qint64  airMs = 0;
                         int     seasonNo = -1;
                         int     episodeNo = -1; };
    QVector<PendingItem> m_genQueue;
    int     m_genCursor = 0;

    QString channelsFilePath() const;
    QString scheduleFilePath(int channelNumber) const;
    QString resolveMediaRoot() const;
    // Creates series/ and movies/ inside an existing media root.
    void ensureLibraryFolders() const;

    const vchan::ChannelSchedule &scheduleFor(int channelNumber);
    void invalidateCache(int channelNumber) { m_cache.remove(channelNumber); }

    QVariantMap  channelObject(int channelNumber) const;
    qint64       rotationAt(int channelNumber, qint64 t);
    void         marksAt(int channelNumber, qint64 t,
                         QHash<QString, QString> *perSeries, QString *overall);
    QStringList  mediaFilesUnder(const QString &relDir) const;
    QString      relativeMediaFolder(const QString &folder, QString *why = nullptr) const;

    QVariantMap buildPlayDescriptor(int channelNumber,
                                    const vchan::ChannelSchedule &schedule,
                                    qint64 nowMs,
                                    vchan::Decision start);
    QString resolveLocalSlot(const vchan::Slot &s) const;
    bool    requestPlexUrl(const vchan::Slot &s, qint64 offsetMs,
                           bool transcodeAllowed);
    bool    askPlexForStream(const QString &ratingKey, const QString &partKey,
                             const QString &sessionId, qint64 offsetMs,
                             bool transcodeAllowed);

    QString plexVideoQuality() const;

    QString m_plexTranscodeSession;
    qint64  m_plexPendingOffsetMs = 0;
    bool    m_plexPendingTranscodeOk = false;
    bool    requestServerUrl(const vchan::Slot &s);
    bool    deliverPreviewUrl(const QString &url, const QString &token = QString());
    int     m_previewChannel  = -1;
    int     m_previewSlot     = -1;
    qint64  m_previewPosition = 0;
    QObject *serverBackend(vchan::SlotSource src) const;
    vchan::SlotSource m_serverPendingSrc = vchan::SlotSource::Local;
    vchan::MediaServerSource *m_server = nullptr;
    void    onServerEnumerationFinished(const QVector<vchan::MediaItem> &items);
    void    onServerEnumerationFailed(const QString &reason);
    void    onServerBrowseReady(const QString &kind, const QVariantList &items);
    void    onServerBrowseFailed(const QString &kind, const QString &reason);

    void finishGeneration(bool ok, const QString &message);

    void generationEnded(int channelNumber, bool ok, const QString &message);

    void rebuildNextOrFinish();

    int m_rebuildTotal   = 0;
    int m_rebuildOk      = 0;
    int m_rebuildFailed  = 0;

    static QVariantMap offAirDescriptor(const QString &message, bool needsRegeneration);
};
