#pragma once
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class FakeMediaServer : public QObject {
    Q_OBJECT
public:
    explicit FakeMediaServer(QObject *parent = nullptr) : QObject(parent) {}

    QStringList calls;
    QVariantList libraries;
    QHash<QString, QVariantList> itemsFor;
    QHash<QString, QVariantList> seasonsFor;
    QHash<QString, QVariantList> boxsetChildrenFor;

    QString errorOnCall;
    bool    silent = false;
    QString token = QStringLiteral("fake-token");
    QString streamUrl = QStringLiteral("http://server/stream.mkv");

    Q_INVOKABLE void load_libraries() {
        calls << QStringLiteral("load_libraries()");
        if (misbehave(QStringLiteral("load_libraries"))) return;
        emit librariesLoaded(QVariant(libraries));
    }

    Q_INVOKABLE void load_items(const QString &parentId, const QString &includeTypes,
                                const QString &sortBy) {
        Q_UNUSED(sortBy);
        calls << QStringLiteral("load_items(%1,%2)").arg(parentId, includeTypes);
        if (misbehave(QStringLiteral("load_items"))) return;
        if (includeTypes.isEmpty()) {
            QVariantList all;
            for (auto it = itemsFor.cbegin(); it != itemsFor.cend(); ++it)
                if (it.key().startsWith(parentId + QLatin1Char('|'))) all += it.value();
            emit itemsLoaded(QVariant(all));
            return;
        }
        emit itemsLoaded(QVariant(itemsFor.value(parentId + QLatin1Char('|') + includeTypes)));
    }

    Q_INVOKABLE void load_seasons(const QString &seriesId) {
        calls << QStringLiteral("load_seasons(%1)").arg(seriesId);
        if (misbehave(QStringLiteral("load_seasons"))) return;
        emit seasonsLoaded(QVariant(seasonsFor.value(seriesId)));
    }

    Q_INVOKABLE void load_episodes(const QString &seriesId, const QString &seasonId) {
        calls << QStringLiteral("load_episodes(%1,%2)").arg(seriesId, seasonId);
        if (misbehave(QStringLiteral("load_episodes"))) return;
        emit episodesLoaded(QVariant(QVariantList()));
    }

    Q_INVOKABLE void load_boxset_children(const QString &parentId) {
        calls << QStringLiteral("load_boxset_children(%1)").arg(parentId);
        if (misbehave(QStringLiteral("load_boxset_children"))) return;
        emit boxsetChildrenLoaded(QVariant(boxsetChildrenFor.value(parentId)));
    }

    Q_INVOKABLE void get_playback_url(const QString &itemId, const QString &mediaSourceId,
                                      int audioStreamIndex, int subtitleStreamIndex,
                                      bool forceTranscode = false) {
        calls << QStringLiteral("get_playback_url(%1,%2,%3,%4,%5)")
                     .arg(itemId, mediaSourceId)
                     .arg(audioStreamIndex).arg(subtitleStreamIndex)
                     .arg(forceTranscode ? 1 : 0);
        if (misbehave(QStringLiteral("get_playback_url"))) return;
        emit streamUrlReady(streamUrl);
    }

    Q_INVOKABLE QString get_access_token() const { return token; }

    static QVariantMap library(const QString &id, const QString &title, const QString &type) {
        return QVariantMap{{"key", id}, {"itemId", id},
                           {"title", title}, {"collectionType", type}};
    }
    static QVariantMap shelf(const QString &id, const QString &title) {
        return QVariantMap{{"key", id}, {"title", title}};
    }
    static QVariantMap episode(const QString &id, const QString &seriesId, const QString &series,
                               int season, int ep, double durationMs,
                               const QString &title = QString()) {
        return QVariantMap{{"itemId", id}, {"seriesId", seriesId}, {"type", "episode"},
                           {"title", title.isEmpty() ? QStringLiteral("Episode %1").arg(ep) : title},
                           {"grandparentTitle", series},
                           {"parentIndex", season}, {"index", ep},
                           {"duration", durationMs}};
    }
    static QVariantMap movie(const QString &id, const QString &title, double durationMs,
                             const QStringList &genres = QStringList()) {
        QVariantList g;
        for (const QString &one : genres) g.append(one);
        return QVariantMap{{"itemId", id}, {"type", "movie"}, {"title", title},
                           {"duration", durationMs}, {"genres", g}};
    }
    static QVariantMap series(const QString &id, const QString &title) {
        return QVariantMap{{"itemId", id}, {"type", "series"}, {"title", title}};
    }
    static QVariantMap boxset(const QString &id, const QString &title) {
        return QVariantMap{{"itemId", id}, {"type", "boxset"}, {"title", title}};
    }
    static QVariantMap season(const QString &id, const QString &seriesId, int number) {
        return QVariantMap{{"itemId", id}, {"seriesId", seriesId}, {"type", "season"},
                           {"title", QStringLiteral("Season %1").arg(number)},
                           {"index", number}};
    }

signals:
    void librariesLoaded(const QVariant &libraries);
    void itemsLoaded(const QVariant &items);
    void seasonsLoaded(const QVariant &seasons);
    void episodesLoaded(const QVariant &episodes);
    void boxsetChildrenLoaded(const QVariant &children);
    void streamUrlReady(const QString &url);
    void errorOccurred(const QString &message);

private:
    bool misbehave(const QString &call) {
        if (silent) return true;
        if (!errorOnCall.isEmpty() && call.contains(errorOnCall)) {
            emit errorOccurred(QStringLiteral("SERVER SAID NO"));
            return true;
        }
        return false;
    }
};
