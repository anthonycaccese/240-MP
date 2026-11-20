#pragma once
#include "ChannelSchedule.h"
#include "ScheduleGenerator.h"

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVector>

class QTimer;

namespace vchan {

inline constexpr int kServerTimeoutMs = 20000;

inline constexpr int kMaxServerItems = 5000;

class MediaServerSource : public QObject {
    Q_OBJECT
public:
    struct Request {
        SlotSource    src = SlotSource::Jellyfin;
        QStringList   libraries;
        QStringList   match;
        // The picked series' ItemIds, where the picker stored them. A series
        // matched by id survives being renamed on the server.
        QStringList   showIds;
        QStringList   titles;
        QStringList   genres;
        QStringList   collections;
        QSet<QString> excludeSeasons;
        QSet<QString> excludeEpisodes;
        enum class Wants { Episodes, Films, Anything };
        Wants         wants = Wants::Episodes;
        bool          anyFilm = true;
    };

    explicit MediaServerSource(QObject *parent = nullptr);

    void setBackend(SlotSource src, QObject *backend);
    QObject *backend(SlotSource src) const;
    bool available(SlotSource src) const { return backend(src) != nullptr; }
    bool busy() const;

    bool enumerate(const Request &req);
    static bool wantsFilms(Request::Wants w) { return w == Request::Wants::Films; }

    bool browse(SlotSource src, const QString &kind, const QString &parentKey);

    void cancel();

    static QString providerName(SlotSource src);

    static QString seasonKey(const QString &seriesId, int seasonNumber);
    static bool parseSeasonKey(const QString &key, QString *seriesId, int *seasonNumber);

    static bool itemToMedia(const QVariantMap &m, SlotSource src, MediaItem *out);

signals:
    void enumerationFinished(const QVector<vchan::MediaItem> &items);
    void enumerationFailed(const QString &reason);
    void browseReady(const QString &kind, const QVariantList &items);
    void browseFailed(const QString &kind, const QString &reason);

private slots:
    void onLibrariesLoaded(const QVariant &libraries);
    void onItemsLoaded(const QVariant &items);
    void onSeasonsLoaded(const QVariant &seasons);
    void onBoxsetChildrenLoaded(const QVariant &children);
    void onBackendError(const QString &message);
    void onTimeout();

private:
    enum class Stage {
        Idle,
        GenLibraries,
        GenBoxsets,
        GenBoxsetChildren,
        GenItems,
        BrowseLibraries,
        BrowseSeries,
        BrowseMovies,
        BrowseCollections,
        BrowseSeasons,
        BrowseEpisodes
    };

    QObject *m_jellyfin = nullptr;
    QObject *m_emby     = nullptr;
    QObject *m_active   = nullptr;
    Stage    m_stage    = Stage::Idle;
    QTimer  *m_timer    = nullptr;

    Request     m_req;
    QStringList m_libs;
    QStringList m_pendingSeries;
    QStringList m_pendingBoxsets;
    QString     m_currentLib;
    bool        m_expandingSeries = false;
    bool        m_askedBoxsetsForLib = false;
    bool        m_truncated = false;
    int         m_rejected  = 0;
    QVector<MediaItem> m_collected;
    QSet<QString> m_seenIds;
    QSet<QString> m_expandedSeries;
    QSet<QObject *> m_wired;

    QString       m_browseKind;
    QString       m_browseParent;
    int           m_browseSeason = -1;
    QString       m_browseSeries;
    QVariantList  m_browseItems;

    bool  callBackend(const char *method, const QStringList &args = QStringList());
    void  genNext();
    void  genFinish();
    void  fail(const QString &reason);
    void  browseNextLibrary();
    void  finishBrowse();
    void  reset();
    bool  fromActive() const;
    QStringList eligibleLibraries(const QVariant &libraries, bool films,
                                  const QStringList &wanted) const;
};
}
