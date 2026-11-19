#pragma once

#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QString>
#include <QMap>
#include <QUdpSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <memory>

struct DlnaServer {
    QString udn;                    // Unique Device Name (uuid:...)
    QString deviceType;             // urn:schemas-upnp-org:device:MediaServer:1
    QString name;                   // Friendly name
    QString address;                // IP address
    QString descriptionUrl;         // URL to device.xml
    int port;                       // Usually 1900 for SSDP

    // Parsed from device.xml
    QString manufacturer;
    QString manufacturerUrl;
    QString modelName;
    QString modelNumber;
    QString modelDescription;
    QString serialNumber;
    QString upc;
    QString iconUrl;                // URL to device icon
    QString presentationUrl;        // Device web UI
    QString contentDirectoryUrl;    // ContentDirectory service URL
    bool parsed;                    // Whether device.xml was fetched & parsed

    DlnaServer() : port(1900), parsed(false) {}
};

struct MediaItem {
    QString id;            // DLNA object ID
    QString title;
    QString description;
    QString resourceUri;   // URL to actual media file
    QString mimeType;
    QString resolution;    // e.g. "1920x1080"
    QString date;
    QString duration;      // e.g. "1:54:41.875"
    qint64 size = 0;
    bool isContainer = false;  // true if folder/container, false if playable

    // From the <res> element, where the server bothers to supply them.
    int audioChannels = 0;
    int bitrate = 0;
    int sampleRate = 0;

    // Sidecar subtitles, if advertised (sec:CaptionInfoEx, or a subtitle <res>).
    QString subtitleUri;

    // Playback state
    qint64 resumePosition = 0; // milliseconds
    QString serverId;          // Which server this item came from
};

class DlnaBackend : public QObject {
    Q_OBJECT

public:
    explicit DlnaBackend(const QString &appRoot, const QString &dataRoot);
    ~DlnaBackend();

    // QML invokable methods
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();
    Q_INVOKABLE void selectServer(const QString &udn);

    // Navigation
    Q_INVOKABLE void browseRoot();
    Q_INVOKABLE void browseContainer(const QString &objectId);

    // Playback
    Q_INVOKABLE void playItem(const QString &objectId, const QString &resourceUri);
    Q_INVOKABLE void saveResumePosition(const QString &objectId, qint64 positionMs);
    Q_INVOKABLE qint64 getResumePosition(const QString &objectId);

    // Getters
    Q_INVOKABLE QVariantList getDiscoveredServers() const;
    Q_INVOKABLE QVariantList getCurrentItems() const;

    Q_INVOKABLE QString currentServerUdn() const { return m_currentServer.udn; }

    // Pinned shortcuts. AppCore probes for get_menu_entries() and appends
    // whatever it returns to the main menu (see AppCore::menuEntriesForModule).
    Q_INVOKABLE bool isPinned(const QString &serverUdn, const QString &objectId) const;
    Q_INVOKABLE void togglePin(const QString &serverUdn, const QString &serverName,
                               const QString &objectId, const QString &title);
    Q_INVOKABLE void clearPins();
    Q_INVOKABLE void openPinned(const QString &serverUdn, const QString &objectId);
    Q_INVOKABLE QVariantList get_menu_entries();

    // Track languages are not part of DLNA at all — they live inside the file.
    // ffprobe reads just the container header over HTTP, which is how other
    // DLNA clients manage to list them before playback starts.
    Q_INVOKABLE void probeTracks(const QString &url);

signals:
    // Discovery signals
    void discoveryStarted();
    void discoveryFinished();
    void serverDiscovered(const QString &udn, const QString &name);
    void connectionError(const QString &error);

    // Browsing signals
    void itemsLoaded();
    void browseError(const QString &error);

    // Track probing
    void tracksProbed(const QVariantMap &tracks);

    // Pinned shortcuts
    void pinsChanged();
    void pinnedResolveFailed(const QString &message);

    // Playback signals
    void playbackRequested(const QString &url);


private slots:
    void onUdpReadyRead();
    void onDiscoveryTimeout();
    void onDeviceDescriptionFinished();
    void onDeviceDescriptionError(QNetworkReply::NetworkError error);
    void onBrowseFinished();
    void onBrowseError(QNetworkReply::NetworkError error);

private:
    // Discovery methods
    void sendSsdpMSearchRequest();
    void parseSsdpResponse(const QString &response, const QString &remoteAddress);
    void fetchDeviceDescription(const DlnaServer &server);
        void parseDeviceDescription(DlnaServer &server, const QString &xml);

    // UPnP ContentDirectory Browse
    void browseContentDirectory(const QString &objectId, int startingIndex = 0, int requestedCount = 100);
    void parseContentDirectoryResponse(const QString &xml);
    QVariant variantFromServer(const DlnaServer &server) const;
    QVariant variantFromItem(const MediaItem &item) const;

    // Members
    QUdpSocket *m_udpSocket;
    QTimer *m_discoveryTimer;
    QNetworkAccessManager *m_networkManager;
    QMap<QString, DlnaServer> m_discoveredServers;
    QSet<QString> m_seenUdns;         // Avoid duplicates from multiple interfaces
    QMap<QNetworkReply*, QString> m_pendingDeviceRequests;  // Track pending device.xml requests
    QNetworkReply *m_pendingBrowseRequest;  // Track current browse request
    DlnaServer m_currentServer;
    QString m_currentContainerId;     // Current browsing container ID
    QVector<MediaItem> m_currentItems;

    void parseProbeOutput(const QByteArray &json);

    // Resume history, persisted as a flat map of "<serverUdn>/<objectId>" -> pos
    QString historyFilePath() const;
    QVariantMap loadHistory() const;
    void saveHistory(const QVariantMap &history);

    // Pinned shortcuts
    void loadPins();
    void savePins();
    int indexOfPin(const QString &serverUdn, const QString &objectId) const;
    void resolvePendingPin();

    QString m_dataRoot;
    QVariantList m_pins;              // [{serverUdn, serverName, objectId, title}]
    QString m_pendingPinUdn;          // set while a pinned row waits on discovery
    QString m_pendingPinObjectId;


};
