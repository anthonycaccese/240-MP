#pragma once
#include <QObject>
#include <QVariant>
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <functional>

// Live TV / IPTV backend. Reads a user-supplied M3U playlist (a remote URL or a
// local file), parses its #EXTINF channel entries, and exposes them grouped by
// category. Streams are handed to mpv untouched (HLS/TS/UDP) — there is no auth,
// transcode, or progress reporting for live playback.
class IptvBackend : public QObject {
    Q_OBJECT
public:
    explicit IptvBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);

    // Source (the configured M3U URL / path lives in modules.<id>.m3u_url).
    Q_INVOKABLE bool has_source() const;
    Q_INVOKABLE QString get_source() const;

    // Fetch + parse (once), then emit groupsLoaded. Cheap to call repeatedly —
    // it only re-fetches when nothing is cached or force is true.
    Q_INVOKABLE void load_groups(bool force = false);
    // Emit channelsLoaded for a group ("" or "__all__" = every channel).
    Q_INVOKABLE void load_channels(const QString &group);

signals:
    // [{ title, key, count }] — key is the raw group name ("" for ungrouped).
    void groupsLoaded(const QVariant &groups);
    // [{ name, url, logo, group }]
    void channelsLoaded(const QVariant &channels);
    void playlistError(const QString &message);
    void loadingChanged(bool loading);

private:
    struct Channel {
        QString name;
        QString url;
        QString logo;
        QString group;
        QString tvgId;
    };

    QString m_appRoot;
    QString m_dataRoot;
    QNetworkAccessManager *m_nam;
    QVector<Channel> m_channels;
    bool m_loaded = false;

    QString sourceUrl() const;
    void fetchAndParse(std::function<void()> onDone);
    void parseM3U(const QByteArray &data);
    void emitGroups();
    void emitChannels(const QString &group);

    QJsonObject loadConfig() const;
    QJsonObject moduleConfig() const;
};
