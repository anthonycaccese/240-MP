#include "DlnaBackend.h"
#include <QUdpSocket>
#include <QTimer>
#include <QNetworkInterface>
#include <QDebug>
#include <QXmlStreamReader>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>

// Note: some distributions ship a qtlogging.ini that disables *.debug. If
// these never appear, run with QT_LOGGING_RULES="*.debug=true".
#define DLNA_LOG qDebug() << "[DLNA]"

DlnaBackend::DlnaBackend(const QString &appRoot, const QString &dataRoot)
    : QObject(nullptr)
    , m_udpSocket(nullptr)
    , m_discoveryTimer(nullptr)
    , m_networkManager(nullptr)
    , m_pendingBrowseRequest(nullptr)
{
    Q_UNUSED(appRoot);
    m_dataRoot = dataRoot;
    m_networkManager = new QNetworkAccessManager(this);
    loadPins();
    DLNA_LOG << "backend ready -" << m_pins.size() << "pinned shortcut(s)";
}

DlnaBackend::~DlnaBackend() = default;

// ---------------------------------------------------------------- discovery

void DlnaBackend::startDiscovery()
{
    DLNA_LOG << "discovery starting";
    emit discoveryStarted();

    if (!m_udpSocket) {
        m_udpSocket = new QUdpSocket(this);
        connect(m_udpSocket, &QUdpSocket::readyRead, this, &DlnaBackend::onUdpReadyRead);
    }

    // A bound socket is required before joining the multicast group. Port 1900
    // is often already held by another SSDP listener, so fall back to an
    // ephemeral port: M-SEARCH replies are unicast back to the source port.
    if (m_udpSocket->state() != QAbstractSocket::BoundState) {
        bool bound = m_udpSocket->bind(QHostAddress::AnyIPv4, 1900,
                                       QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
        if (!bound) {
            DLNA_LOG << "port 1900 unavailable (" << m_udpSocket->errorString()
                     << ") - falling back to an ephemeral port";
            bound = m_udpSocket->bind(QHostAddress::AnyIPv4, 0);
        }
        if (!bound) {
            DLNA_LOG << "ERROR: could not bind UDP socket:" << m_udpSocket->errorString();
            emit connectionError("Could not open a network socket for discovery");
            emit discoveryFinished();
            return;
        }
        DLNA_LOG << "socket bound to port" << m_udpSocket->localPort();

        const QHostAddress group("239.255.255.250");
        if (!m_udpSocket->joinMulticastGroup(group))
            DLNA_LOG << "note: joinMulticastGroup failed:" << m_udpSocket->errorString()
                     << "(unicast M-SEARCH replies still work)";
    }

    sendSsdpMSearchRequest();

    if (!m_discoveryTimer) {
        m_discoveryTimer = new QTimer(this);
        m_discoveryTimer->setSingleShot(true);
        connect(m_discoveryTimer, &QTimer::timeout, this, &DlnaBackend::onDiscoveryTimeout);
    }
    m_discoveryTimer->start(5000);
}

void DlnaBackend::stopDiscovery()
{
    if (m_discoveryTimer)
        m_discoveryTimer->stop();
}

void DlnaBackend::sendSsdpMSearchRequest()
{
    const QHostAddress group("239.255.255.250");

    // Ask for MediaServers specifically, then rootdevices as a fallback for
    // servers that only answer the generic search.
    const QByteArray targets[] = {
        "urn:schemas-upnp-org:device:MediaServer:1",
        "upnp:rootdevice"
    };

    for (const QByteArray &st : targets) {
        QByteArray req = "M-SEARCH * HTTP/1.1\r\n"
                         "HOST: 239.255.255.250:1900\r\n"
                         "MAN: \"ssdp:discover\"\r\n"
                         "MX: 2\r\n"
                         "ST: " + st + "\r\n"
                         "USER-AGENT: 240-MP/1.0\r\n"
                         "\r\n";
        if (m_udpSocket->writeDatagram(req, group, 1900) < 0)
            DLNA_LOG << "M-SEARCH send failed:" << m_udpSocket->errorString();
    }
    DLNA_LOG << "M-SEARCH sent";
}

void DlnaBackend::onUdpReadyRead()
{
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_udpSocket->pendingDatagramSize()));
        QHostAddress remote;
        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &remote);
        parseSsdpResponse(QString::fromUtf8(datagram), remote.toString());
    }
}

void DlnaBackend::onDiscoveryTimeout()
{
    DLNA_LOG << "discovery finished -" << m_discoveredServers.size() << "device(s) known";

    // A pinned row that is still unresolved means its server never answered.
    if (!m_pendingPinUdn.isEmpty()) {
        DLNA_LOG << "pinned shortcut unresolved - server not on the network";
        m_pendingPinUdn.clear();
        m_pendingPinObjectId.clear();
        emit pinnedResolveFailed("That server is not responding on this network");
    }

    emit discoveryFinished();
}

void DlnaBackend::parseSsdpResponse(const QString &response, const QString &remoteAddress)
{
    QString descriptionUrl, usn;

    const QStringList lines = response.split("\r\n");
    for (const QString &line : lines) {
        if (line.startsWith("LOCATION:", Qt::CaseInsensitive))
            descriptionUrl = line.mid(9).trimmed();
        else if (line.startsWith("USN:", Qt::CaseInsensitive))
            usn = line.mid(4).trimmed();
    }

    if (descriptionUrl.isEmpty() || usn.isEmpty())
        return;

    // UUIDs are commonly uppercase and may contain any hex digit, so match
    // case-insensitively up to the "::" separator.
    static const QRegularExpression uuidRegex(
        "uuid:([0-9a-f\\-]+)", QRegularExpression::CaseInsensitiveOption);
    const QString udn = uuidRegex.match(usn).captured(1);
    if (udn.isEmpty())
        return;

    if (m_seenUdns.contains(udn))
        return;
    m_seenUdns.insert(udn);

    DLNA_LOG << "found device at" << remoteAddress << "->" << descriptionUrl;

    DlnaServer srv;
    srv.udn = udn;
    srv.address = remoteAddress;
    srv.descriptionUrl = descriptionUrl;
    srv.name = descriptionUrl;
    m_discoveredServers.insert(srv.udn, srv);

    fetchDeviceDescription(srv);
}

void DlnaBackend::fetchDeviceDescription(const DlnaServer &server)
{
    QNetworkRequest request{QUrl(server.descriptionUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "240-MP/1.0");

    QNetworkReply *reply = m_networkManager->get(request);
    m_pendingDeviceRequests.insert(reply, server.udn);

    connect(reply, &QNetworkReply::errorOccurred, this, &DlnaBackend::onDeviceDescriptionError);
    connect(reply, &QNetworkReply::finished, this, &DlnaBackend::onDeviceDescriptionFinished);
}

void DlnaBackend::onDeviceDescriptionFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    const QString udn = m_pendingDeviceRequests.take(reply);
    reply->deleteLater();

    if (!m_discoveredServers.contains(udn))
        return;

    if (reply->error() != QNetworkReply::NoError) {
        DLNA_LOG << "device.xml fetch failed:" << reply->errorString();
        return;
    }

    parseDeviceDescription(m_discoveredServers[udn], QString::fromUtf8(reply->readAll()));
}

void DlnaBackend::onDeviceDescriptionError(QNetworkReply::NetworkError error)
{
    DLNA_LOG << "device.xml network error:" << error;
}

void DlnaBackend::parseDeviceDescription(DlnaServer &server, const QString &xml)
{
    const QUrl base(server.descriptionUrl);
    QXmlStreamReader reader(xml);

    while (!reader.atEnd() && !reader.hasError()) {
        if (reader.readNext() != QXmlStreamReader::StartElement)
            continue;

        const QString tag = reader.name().toString().toLower();

        if (tag == "friendlyname")      server.name         = reader.readElementText();
        else if (tag == "manufacturer") server.manufacturer = reader.readElementText();
        else if (tag == "modelname")    server.modelName    = reader.readElementText();
        else if (tag == "modelnumber")  server.modelNumber  = reader.readElementText();
        else if (tag == "serialnumber") server.serialNumber = reader.readElementText();
        else if (tag == "service") {
            QString serviceType, controlUrl;

            while (!reader.atEnd()) {
                const QXmlStreamReader::TokenType t = reader.readNext();
                if (t == QXmlStreamReader::StartElement) {
                    const QString e = reader.name().toString().toLower();
                    if (e == "servicetype")     serviceType = reader.readElementText();
                    else if (e == "controlurl") controlUrl  = reader.readElementText();
                } else if (t == QXmlStreamReader::EndElement
                           && reader.name().toString().compare("service", Qt::CaseInsensitive) == 0) {
                    break;
                }
            }

            // SOAP Browse actions are POSTed to controlURL. SCPDURL only serves
            // the service's own description document and will reject them.
            if (serviceType.contains("ContentDirectory", Qt::CaseInsensitive) && !controlUrl.isEmpty()) {
                server.contentDirectoryUrl = base.resolved(QUrl(controlUrl)).toString();
                DLNA_LOG << "ContentDirectory control URL:" << server.contentDirectoryUrl;
            }
        }
    }

    if (reader.hasError())
        DLNA_LOG << "device.xml parse error:" << reader.errorString();

    if (server.contentDirectoryUrl.isEmpty()) {
        DLNA_LOG << server.name << "has no ContentDirectory service - not a media server, skipping";
        m_discoveredServers.remove(server.udn);
        return;
    }

    server.parsed = true;
    DLNA_LOG << "server ready:" << server.name << "(" << server.manufacturer << server.modelName << ")";
    emit serverDiscovered(server.udn, server.name);
    resolvePendingPin();
}

void DlnaBackend::selectServer(const QString &udn)
{
    if (m_discoveredServers.contains(udn)) {
        m_currentServer = m_discoveredServers[udn];
        DLNA_LOG << "selected server:" << m_currentServer.name;
    } else {
        DLNA_LOG << "selectServer: unknown udn" << udn;
    }
}

// ----------------------------------------------------------------- browsing

void DlnaBackend::browseRoot()
{
    browseContentDirectory("0");
}

void DlnaBackend::browseContainer(const QString &objectId)
{
    browseContentDirectory(objectId);
}

void DlnaBackend::browseContentDirectory(const QString &objectId, int startingIndex, int requestedCount)
{
    if (m_currentServer.contentDirectoryUrl.isEmpty()) {
        DLNA_LOG << "ERROR: no ContentDirectory URL for the selected server";
        emit browseError("This server did not advertise a ContentDirectory service");
        return;
    }

    // Abandon any in-flight browse so its reply cannot overwrite this one.
    if (m_pendingBrowseRequest) {
        m_pendingBrowseRequest->disconnect(this);
        m_pendingBrowseRequest->abort();
        m_pendingBrowseRequest->deleteLater();
        m_pendingBrowseRequest = nullptr;
    }

    m_currentContainerId = objectId;

    const QString soapBody = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<ObjectID>%1</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>%2</StartingIndex>"
        "<RequestedCount>%3</RequestedCount>"
        "<SortCriteria></SortCriteria>"
        "</u:Browse>"
        "</s:Body>"
        "</s:Envelope>").arg(objectId).arg(startingIndex).arg(requestedCount);

    QNetworkRequest request{QUrl(m_currentServer.contentDirectoryUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml; charset=\"utf-8\"");
    request.setRawHeader("SOAPACTION", "\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"");
    request.setRawHeader("User-Agent", "240-MP/1.0");

    DLNA_LOG << "Browse container" << objectId << "->" << m_currentServer.contentDirectoryUrl;

    m_pendingBrowseRequest = m_networkManager->post(request, soapBody.toUtf8());
    connect(m_pendingBrowseRequest, &QNetworkReply::errorOccurred, this, &DlnaBackend::onBrowseError);
    connect(m_pendingBrowseRequest, &QNetworkReply::finished, this, &DlnaBackend::onBrowseFinished);
}

void DlnaBackend::onBrowseFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    if (reply == m_pendingBrowseRequest)
        m_pendingBrowseRequest = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        DLNA_LOG << "browse failed:" << reply->errorString();
        emit browseError(reply->errorString());
        return;
    }

    parseContentDirectoryResponse(QString::fromUtf8(reply->readAll()));
}

void DlnaBackend::onBrowseError(QNetworkReply::NetworkError error)
{
    DLNA_LOG << "browse network error:" << error;
}

void DlnaBackend::parseContentDirectoryResponse(const QString &xml)
{
    // A Browse response wraps the DIDL-Lite payload as *escaped* text inside
    // <Result>. It has to be pulled out and parsed as its own document —
    // scanning the SOAP envelope directly finds no <item>/<container> nodes.
    QString didl;
    QXmlStreamReader envelope(xml);
    while (!envelope.atEnd()) {
        if (envelope.readNext() == QXmlStreamReader::StartElement
            && envelope.name().toString().compare("Result", Qt::CaseInsensitive) == 0) {
            didl = envelope.readElementText();
            break;
        }
    }

    if (didl.isEmpty()) {
        DLNA_LOG << "no <Result> payload in browse response";
        DLNA_LOG << "response head:" << xml.left(400);
        emit browseError("Server returned an unreadable response");
        return;
    }

    m_currentItems.clear();

    QXmlStreamReader reader(didl);
    while (!reader.atEnd() && !reader.hasError()) {
        if (reader.readNext() != QXmlStreamReader::StartElement)
            continue;

        const QString tag = reader.name().toString().toLower();
        if (tag != "container" && tag != "item")
            continue;

        MediaItem entry;
        entry.isContainer = (tag == "container");
        entry.id = reader.attributes().value("id").toString();

        // Thumbnails are only a sensible choice if nothing playable shows up,
        // so they are held aside rather than claiming the resource slot.
        QString imageFallback, imageFallbackMime;

        while (!reader.atEnd()) {
            const QXmlStreamReader::TokenType t = reader.readNext();

            if (t == QXmlStreamReader::StartElement) {
                // QXmlStreamReader reports local names, so a <dc:title> element
                // arrives here as "title" — never as "dc:title".
                const QString e = reader.name().toString().toLower();

                if (e == "title")            entry.title       = reader.readElementText();
                else if (e == "date")        entry.date        = reader.readElementText();
                else if (e == "description") entry.description = reader.readElementText();
                else if (e == "res") {
                    // Attributes must be read before readElementText() advances
                    // the reader onto the closing tag.
                    const QXmlStreamAttributes attrs = reader.attributes();
                    const QString protocolInfo = attrs.value("protocolInfo").toString();
                    const QString resolution   = attrs.value("resolution").toString();
                    const QString duration     = attrs.value("duration").toString();
                    const qint64 size          = attrs.value("size").toLongLong();
                    const int channels         = attrs.value("nrAudioChannels").toInt();
                    const int bitrate          = attrs.value("bitrate").toInt();
                    const int sampleRate       = attrs.value("sampleFrequency").toInt();

                    const QString uri = reader.readElementText();

                    // protocolInfo looks like: http-get:*:video/mp4:DLNA.ORG_PN=...
                    const QStringList parts = protocolInfo.split(':');
                    const QString mime = parts.size() > 2 ? parts.at(2) : QString();

                    if (uri.isEmpty())
                        continue;

                    // A video item also advertises its thumbnail as a <res>,
                    // and nothing says it comes second. Whichever order the
                    // server used, the JPEG must never win over the video.
                    if (mime.startsWith("image/")) {
                        if (imageFallback.isEmpty()) {
                            imageFallback = uri;
                            imageFallbackMime = mime;
                        }
                        continue;
                    }

                    // Sidecar subtitles ride along as their own resource.
                    if (mime.startsWith("text/") || mime.contains("srt", Qt::CaseInsensitive)
                        || mime.contains("subtitle", Qt::CaseInsensitive)) {
                        entry.subtitleUri = uri;
                        continue;
                    }

                    if (entry.resourceUri.isEmpty()) {
                        entry.resourceUri = uri;
                        entry.size = size;
                        entry.mimeType = mime;
                        if (!resolution.isEmpty()) entry.resolution = resolution;
                        if (!duration.isEmpty())   entry.duration   = duration;
                        if (channels > 0)   entry.audioChannels = channels;
                        if (bitrate > 0)    entry.bitrate       = bitrate;
                        if (sampleRate > 0) entry.sampleRate    = sampleRate;
                    }
                }
                else if (e == "captioninfoex" || e == "captioninfo") {
                    // Samsung's subtitle extension, emitted by MiniDLNA and others.
                    const QString uri = reader.readElementText();
                    if (!uri.isEmpty())
                        entry.subtitleUri = uri;
                }
            } else if (t == QXmlStreamReader::EndElement
                       && reader.name().toString().toLower() == tag) {
                break;
            }
        }

        // Genuinely an image item: nothing else was advertised.
        if (entry.resourceUri.isEmpty() && !imageFallback.isEmpty()) {
            entry.resourceUri = imageFallback;
            entry.mimeType    = imageFallbackMime;
        }

        // Some servers percent-encode dc:title, so "10%20a%C3%B1os" needs
        // decoding before it is fit to show.
        if (entry.title.contains('%'))
            entry.title = QUrl::fromPercentEncoding(entry.title.toUtf8());

        if (entry.title.isEmpty())
            entry.title = entry.isContainer ? QStringLiteral("Folder") : QStringLiteral("Untitled");

        // Containers have no resource of their own; items need one to play.
        if (entry.isContainer || !entry.resourceUri.isEmpty())
            m_currentItems.append(entry);
    }

    if (reader.hasError())
        DLNA_LOG << "DIDL parse error:" << reader.errorString();

    DLNA_LOG << "loaded" << m_currentItems.size() << "entries";

    emit itemsLoaded();
}

// ---------------------------------------------------------------- playback

void DlnaBackend::playItem(const QString &objectId, const QString &resourceUri)
{
    if (resourceUri.isEmpty()) {
        emit browseError("This item has no playable resource");
        return;
    }
    DLNA_LOG << "play" << objectId << resourceUri;
    emit playbackRequested(resourceUri);
}

QString DlnaBackend::historyFilePath() const
{
    return m_dataRoot + "/dlna_history.json";
}

QVariantMap DlnaBackend::loadHistory() const
{
    QFile f(historyFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

void DlnaBackend::saveHistory(const QVariantMap &history)
{
    if (m_dataRoot.isEmpty())
        return;
    QDir().mkpath(m_dataRoot);

    QFile f(historyFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        DLNA_LOG << "could not write resume history:" << f.errorString();
        return;
    }
    f.write(QJsonDocument(QJsonObject::fromVariantMap(history)).toJson(QJsonDocument::Compact));
}

void DlnaBackend::saveResumePosition(const QString &objectId, qint64 positionMs)
{
    if (m_currentServer.udn.isEmpty() || objectId.isEmpty())
        return;

    const QString key = m_currentServer.udn + "/" + objectId;
    QVariantMap history = loadHistory();

    // A zero position means "watched to the end". Dropping the key keeps the
    // file from accumulating a dead entry for everything ever played.
    if (positionMs <= 0)
        history.remove(key);
    else
        history[key] = QVariantMap{{"pos", positionMs}};

    saveHistory(history);
}

qint64 DlnaBackend::getResumePosition(const QString &objectId)
{
    if (m_currentServer.udn.isEmpty() || objectId.isEmpty())
        return 0;
    return loadHistory().value(m_currentServer.udn + "/" + objectId)
                        .toMap().value("pos").toLongLong();
}

// --------------------------------------------------------- pinned shortcuts

void DlnaBackend::loadPins()
{
    m_pins.clear();
    if (m_dataRoot.isEmpty())
        return;

    QFile f(m_dataRoot + "/dlna_pins.json");
    if (!f.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (o.value("serverUdn").toString().isEmpty())
            continue;
        m_pins.append(o.toVariantMap());
    }
}

void DlnaBackend::savePins()
{
    if (m_dataRoot.isEmpty()) {
        DLNA_LOG << "no data directory - pins cannot be saved";
        return;
    }
    QDir().mkpath(m_dataRoot);

    QJsonArray arr;
    for (const QVariant &p : m_pins)
        arr.append(QJsonObject::fromVariantMap(p.toMap()));

    QFile f(m_dataRoot + "/dlna_pins.json");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        DLNA_LOG << "could not write pins:" << f.errorString();
        return;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

int DlnaBackend::indexOfPin(const QString &serverUdn, const QString &objectId) const
{
    for (int i = 0; i < m_pins.size(); ++i) {
        const QVariantMap p = m_pins.at(i).toMap();
        if (p.value("serverUdn").toString() == serverUdn
            && p.value("objectId").toString() == objectId)
            return i;
    }
    return -1;
}

bool DlnaBackend::isPinned(const QString &serverUdn, const QString &objectId) const
{
    return indexOfPin(serverUdn, objectId) >= 0;
}

void DlnaBackend::togglePin(const QString &serverUdn, const QString &serverName,
                            const QString &objectId, const QString &title)
{
    if (serverUdn.isEmpty() || objectId.isEmpty())
        return;

    const int at = indexOfPin(serverUdn, objectId);
    if (at >= 0) {
        m_pins.removeAt(at);
        DLNA_LOG << "unpinned" << title;
    } else {
        QVariantMap p;
        p["serverUdn"] = serverUdn;
        p["serverName"] = serverName;
        p["objectId"] = objectId;
        p["title"] = title;
        m_pins.append(p);
        DLNA_LOG << "pinned" << title;
    }

    savePins();
    emit pinsChanged();
}

void DlnaBackend::clearPins()
{
    if (m_pins.isEmpty())
        return;
    DLNA_LOG << "removing all" << m_pins.size() << "pinned shortcut(s)";
    m_pins.clear();
    savePins();
    emit pinsChanged();
}

QVariantList DlnaBackend::get_menu_entries()
{
    QVariantList rows;
    for (const QVariant &v : m_pins) {
        const QVariantMap p = v.toMap();
        const QString objectId = p.value("objectId").toString();

        QVariantMap row;
        // A whole-server pin is the server's own name; a folder keeps its title.
        row["name"] = (objectId == "0") ? p.value("serverName").toString()
                                        : p.value("title").toString();
        row["params"] = p;
        rows.append(row);
    }
    return rows;
}

void DlnaBackend::openPinned(const QString &serverUdn, const QString &objectId)
{
    // The server may already be known from an earlier scan this session.
    if (m_discoveredServers.contains(serverUdn)
        && !m_discoveredServers[serverUdn].contentDirectoryUrl.isEmpty()) {
        DLNA_LOG << "pinned shortcut resolved from cache";
        selectServer(serverUdn);
        browseContainer(objectId);
        return;
    }

    // Otherwise it has to be found first — the caller shows a waiting state
    // until either the browse lands or discovery gives up.
    DLNA_LOG << "pinned shortcut waiting on discovery for" << serverUdn;
    m_pendingPinUdn = serverUdn;
    m_pendingPinObjectId = objectId;
    startDiscovery();
}

void DlnaBackend::resolvePendingPin()
{
    if (m_pendingPinUdn.isEmpty())
        return;
    if (!m_discoveredServers.contains(m_pendingPinUdn))
        return;
    if (m_discoveredServers[m_pendingPinUdn].contentDirectoryUrl.isEmpty())
        return;

    const QString udn = m_pendingPinUdn;
    const QString objectId = m_pendingPinObjectId;
    m_pendingPinUdn.clear();
    m_pendingPinObjectId.clear();

    // Discovery is left running so the server list is warm if the user backs out.
    DLNA_LOG << "pinned shortcut resolved - browsing" << objectId;
    selectServer(udn);
    browseContainer(objectId);
}

// ------------------------------------------------------------ track probing

void DlnaBackend::probeTracks(const QString &url)
{
    if (url.isEmpty())
        return;

    auto *proc = new QProcess(this);
    proc->setProgram("ffprobe");
    proc->setArguments({
        "-v", "error",
        "-show_entries", "stream=index,codec_type,codec_name,channels:stream_tags=language,title",
        "-of", "json",
        url
    });

    // ffprobe is optional: on a build without it, track languages simply are
    // not shown rather than the detail view failing.
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        DLNA_LOG << "ffprobe unavailable - track languages will not be listed";
        proc->deleteLater();
    });

    connect(proc, &QProcess::finished, this,
            [this, proc](int code, QProcess::ExitStatus) {
        const QByteArray out = proc->readAllStandardOutput();
        proc->deleteLater();
        if (code != 0) {
            DLNA_LOG << "ffprobe exited" << code;
            return;
        }
        parseProbeOutput(out);
    });

    // Only the header is read, so this should be near-instant; the guard is for
    // a server that stops responding mid-request.
    QTimer::singleShot(10000, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });

    proc->start();
}

void DlnaBackend::parseProbeOutput(const QByteArray &json)
{
    const QJsonArray streams = QJsonDocument::fromJson(json).object().value("streams").toArray();

    QVariantList audio, subs;
    for (const QJsonValue &v : streams) {
        const QJsonObject s = v.toObject();
        const QString type = s.value("codec_type").toString();
        const QJsonObject tags = s.value("tags").toObject();

        QVariantMap track;
        track["language"] = tags.value("language").toString();
        track["title"]    = tags.value("title").toString();
        track["codec"]    = s.value("codec_name").toString();

        if (type == "audio") {
            track["channels"] = s.value("channels").toInt();
            audio.append(track);
        } else if (type == "subtitle") {
            subs.append(track);
        }
    }

    DLNA_LOG << "probed" << audio.size() << "audio and" << subs.size() << "subtitle track(s)";

    QVariantMap result;
    result["audio"] = audio;
    result["subtitles"] = subs;
    emit tracksProbed(result);
}

// ----------------------------------------------------------------- getters

QVariantList DlnaBackend::getDiscoveredServers() const
{
    QVariantList list;
    for (const DlnaServer &server : m_discoveredServers)
        list.append(variantFromServer(server));
    return list;
}

QVariantList DlnaBackend::getCurrentItems() const
{
    QVariantList list;
    for (const MediaItem &item : m_currentItems)
        list.append(variantFromItem(item));
    return list;
}

QVariant DlnaBackend::variantFromServer(const DlnaServer &server) const
{
    QVariantMap map;
    map["udn"] = server.udn;
    map["name"] = server.name;
    map["address"] = server.address;
    map["manufacturer"] = server.manufacturer;
    map["modelName"] = server.modelName;
    map["modelNumber"] = server.modelNumber;
    map["serialNumber"] = server.serialNumber;
    map["parsed"] = server.parsed;
    return map;
}

QVariant DlnaBackend::variantFromItem(const MediaItem &item) const
{
    QVariantMap map;
    map["id"] = item.id;
    map["title"] = item.title;
    map["description"] = item.description;
    map["resourceUri"] = item.resourceUri;
    map["mimeType"] = item.mimeType;
    map["resolution"] = item.resolution;
    map["date"] = item.date;
    map["duration"] = item.duration;
    map["size"] = item.size;
    map["isContainer"] = item.isContainer;
    map["audioChannels"] = item.audioChannels;
    map["bitrate"] = item.bitrate;
    map["sampleRate"] = item.sampleRate;
    map["subtitleUri"] = item.subtitleUri;
    return map;
}
