#include "NfcReaderBackend.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QDebug>
#include <QDir>
#include <QRegularExpression>

#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
#include <PCSC/winscard.h>
#endif

NfcReaderBackend::NfcReaderBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_mappingFile(dataRoot + "/modules/nfc_reader/nfc_mapping.json")
{
    qDebug("[NfcReader] Initializing NFC reader backend");
    qDebug("[NfcReader] App root: %s", qPrintable(appRoot));
    qDebug("[NfcReader] Data root: %s", qPrintable(dataRoot));
    qDebug("[NfcReader] Mapping file: %s", qPrintable(m_mappingFile));

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &NfcReaderBackend::pollForCard);

    if (initializeReader()) {
        loadMapping();
        m_readerReady = true;
        emit readerReady();
        emit readerReadyChanged();
        emit statusChanged("Ready for NFC card");
        m_pollTimer->start();
        qDebug("[NfcReader] Reader initialized and polling started");
    } else {
        emit errorOccurred("NFC reader not found. Please connect ACS ACR122U reader.");
        emit statusChanged("Reader not found");
    }
}

NfcReaderBackend::~NfcReaderBackend() {
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    if (m_cardHandle) {
        SCardDisconnect(static_cast<SCARDHANDLE>(m_cardHandle), SCARD_LEAVE_CARD);
        m_cardHandle = 0;
    }
    if (m_context) {
        SCardReleaseContext(static_cast<SCARDCONTEXT>(m_context));
        m_context = 0;
    }
#endif
}

bool NfcReaderBackend::initializeReader() {
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    SCARDCONTEXT ctx = 0;
    int32_t rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &ctx);
    if (rv != SCARD_S_SUCCESS) {
        qWarning("[NfcReader] SCardEstablishContext failed: %d", rv);
        return false;
    }
    m_context = static_cast<uintptr_t>(ctx);

    uint32_t cchReaders = 0;
    rv = SCardListReaders(ctx, nullptr, nullptr, &cchReaders);
    if (rv != SCARD_S_SUCCESS) {
        qWarning("[NfcReader] SCardListReaders (size check) failed: %d", rv);
        SCardReleaseContext(ctx);
        m_context = 0;
        return false;
    }

    if (cchReaders == 0) {
        qWarning("[NfcReader] No readers found");
        SCardReleaseContext(ctx);
        m_context = 0;
        return false;
    }

    char *mszReaders = new char[cchReaders];
    rv = SCardListReaders(ctx, nullptr, mszReaders, &cchReaders);
    if (rv != SCARD_S_SUCCESS) {
        qWarning("[NfcReader] SCardListReaders failed: %d", rv);
        delete[] mszReaders;
        SCardReleaseContext(ctx);
        m_context = 0;
        return false;
    }

    bool foundReader = false;
    for (char *p = mszReaders; *p; p += strlen(p) + 1) {
        QString readerName = QString::fromUtf8(p);
        qDebug("[NfcReader] Found reader: %s", qPrintable(readerName));
        if (readerName.contains("ACR122", Qt::CaseInsensitive) ||
            readerName.contains("ACS", Qt::CaseInsensitive)) {
            foundReader = true;
            break;
        }
    }

    delete[] mszReaders;

    if (!foundReader) {
        qWarning("[NfcReader] No ACS ACR122U reader found");
        SCardReleaseContext(ctx);
        m_context = 0;
        return false;
    }

    qDebug("[NfcReader] ACS ACR122U reader found");
    return true;

#else
    qDebug("[NfcReader] Non-Linux/macOS platform - mock mode enabled");
    return true;
#endif
}

bool NfcReaderBackend::loadMapping() {
    QFile file(m_mappingFile);
    if (!file.exists()) {
        QString defaultPath = m_appRoot + "/modules/nfc_reader/nfc_mapping.json";
        QFile defaultFile(defaultPath);
        if (defaultFile.exists()) {
            if (!defaultFile.open(QIODevice::ReadOnly)) {
                qWarning("[NfcReader] Cannot open default mapping file: %s", qPrintable(defaultFile.errorString()));
                return false;
            }
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(defaultFile.readAll(), &err);
            defaultFile.close();
            if (err.error != QJsonParseError::NoError) {
                qWarning("[NfcReader] JSON parse error in default mapping: %s", qPrintable(err.errorString()));
                return false;
            }
            m_mapping = doc.object();
            qDebug("[NfcReader] Loaded default mapping with %lld entries", m_mapping.size());
            return true;
        }
        qWarning("[NfcReader] Mapping file not found: %s", qPrintable(m_mappingFile));
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("[NfcReader] Cannot open mapping file: %s", qPrintable(file.errorString()));
        return false;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError) {
        qWarning("[NfcReader] JSON parse error: %s", qPrintable(err.errorString()));
        return false;
    }

    m_mapping = doc.object();
    qDebug("[NfcReader] Loaded mapping with %lld entries", m_mapping.size());
    return true;
}

QString NfcReaderBackend::normalizeUid(const QString &uid) const {
    QString normalized = uid.toUpper();
    normalized.remove(QRegularExpression("[^0-9A-F]"));
    QStringList bytes;
    for (qsizetype i = 0; i < normalized.length(); i += 2) {
        bytes.append(normalized.mid(i, 2));
    }
    return bytes.join(":");
}

QString NfcReaderBackend::resolveVideoPath(const QString &path) const {
    if (path.isEmpty()) return QString();

    if (QFileInfo(path).isAbsolute()) {
        return path;
    }

    QString resolved = m_appRoot + "/" + path;
    if (QFileInfo(resolved).exists()) {
        return resolved;
    }

    resolved = m_dataRoot + "/" + path;
    if (QFileInfo(resolved).exists()) {
        return resolved;
    }

    return path;
}

bool NfcReaderBackend::checkReader() {
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    if (!m_context) return false;

    SCARDCONTEXT ctx = static_cast<SCARDCONTEXT>(m_context);
    uint32_t cchReaders = 0;
    int32_t rv = SCardListReaders(ctx, nullptr, nullptr, &cchReaders);
    if (rv != SCARD_S_SUCCESS) return false;
    if (cchReaders == 0) return false;

    char *mszReaders = new char[cchReaders];
    rv = SCardListReaders(ctx, nullptr, mszReaders, &cchReaders);
    if (rv != SCARD_S_SUCCESS) {
        delete[] mszReaders;
        return false;
    }

    bool foundReader = false;
    for (char *p = mszReaders; *p; p += strlen(p) + 1) {
        QString readerName = QString::fromUtf8(p);
        if (readerName.contains("ACR122", Qt::CaseInsensitive) ||
            readerName.contains("ACS", Qt::CaseInsensitive)) {
            foundReader = true;
            break;
        }
    }

    delete[] mszReaders;
    return foundReader;

#else
    return true;
#endif
}

QString NfcReaderBackend::readCardUid() {
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    if (!m_context) return {};

    SCARDCONTEXT ctx = static_cast<SCARDCONTEXT>(m_context);

    uint32_t cchReaders = 0;
    int32_t rv = SCardListReaders(ctx, nullptr, nullptr, &cchReaders);
    if (rv != SCARD_S_SUCCESS || cchReaders == 0) return {};

    char *mszReaders = new char[cchReaders];
    rv = SCardListReaders(ctx, nullptr, mszReaders, &cchReaders);
    if (rv != SCARD_S_SUCCESS) {
        delete[] mszReaders;
        return {};
    }

    QString targetReader;
    for (char *p = mszReaders; *p; p += strlen(p) + 1) {
        QString readerName = QString::fromUtf8(p);
        if (readerName.contains("ACR122", Qt::CaseInsensitive) ||
            readerName.contains("ACS", Qt::CaseInsensitive)) {
            targetReader = readerName;
            break;
        }
    }

    delete[] mszReaders;

    if (targetReader.isEmpty()) return {};

    SCARDHANDLE cardHandle = 0;
    uint32_t dwActiveProtocol = 0;
    rv = SCardConnect(ctx,
                      targetReader.toUtf8().constData(),
                      SCARD_SHARE_SHARED,
                      SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                      &cardHandle,
                      &dwActiveProtocol);
    if (rv != SCARD_S_SUCCESS) return {};
    m_cardHandle = static_cast<uintptr_t>(cardHandle);

    unsigned char sendBuffer[] = {0xFF, 0xCA, 0x00, 0x00, 0x00};
    unsigned char recvBuffer[256];
    uint32_t recvLength = sizeof(recvBuffer);

    SCARD_IO_REQUEST ioRequest = {dwActiveProtocol, sizeof(SCARD_IO_REQUEST)};

    rv = SCardTransmit(cardHandle,
                       &ioRequest,
                       sendBuffer, sizeof(sendBuffer),
                       nullptr,
                       recvBuffer, &recvLength);

    SCardDisconnect(cardHandle, SCARD_LEAVE_CARD);
    m_cardHandle = 0;

    if (rv != SCARD_S_SUCCESS || recvLength < 2) return {};

    unsigned char sw1 = recvBuffer[recvLength - 2];
    unsigned char sw2 = recvBuffer[recvLength - 1];
    if (sw1 != 0x90 || sw2 != 0x00) return {};

    QByteArray uidBytes(reinterpret_cast<const char*>(recvBuffer), recvLength - 2);
    return uidBytes.toHex(':').toUpper();

#else
    static int mockCounter = 0;
    mockCounter++;
    if (mockCounter % 100 == 0) {
        return "04:1A:2B:3C:4D:5E";
    }
    return {};
#endif
}

void NfcReaderBackend::pollForCard() {
    if (!m_readerReady) return;

    if (!checkReader()) {
        m_readerReady = false;
        emit readerReadyChanged();
        emit errorOccurred("NFC reader disconnected");
        emit statusChanged("Reader disconnected");
        m_pollTimer->stop();
        return;
    }

    QString uid = readCardUid();
    if (!uid.isEmpty() && uid != m_lastUid) {
        m_lastUid = uid;
        qDebug("[NfcReader] Card detected: %s", qPrintable(uid));
        emit cardDetected(uid);
        emit statusChanged(QString("Card detected: %1").arg(uid));

        QString normalizedUid = normalizeUid(uid);
        if (m_mapping.contains(normalizedUid)) {
            QString videoPath = m_mapping[normalizedUid].toString();
            QString resolvedPath = resolveVideoPath(videoPath);
            qDebug("[NfcReader] Mapping found: %s -> %s", qPrintable(normalizedUid), qPrintable(resolvedPath));
            emit playbackRequested(resolvedPath);
            emit statusChanged(QString("Playing: %1").arg(QFileInfo(resolvedPath).fileName()));
        } else {
            qWarning("[NfcReader] No mapping for UID: %s", qPrintable(normalizedUid));
            emit errorOccurred(QString("No video mapped for card: %1").arg(normalizedUid));
            emit statusChanged("Card not mapped");
        }
    } else if (uid.isEmpty()) {
        m_lastUid.clear();
    }
}

void NfcReaderBackend::clearLastUid() {
    m_lastUid.clear();
    qDebug("[NfcReader] Last UID cleared");
}

QString NfcReaderBackend::getStatus() const {
    if (!m_readerReady) return "Reader not found";
    return "Ready for NFC card";
}

QVariantList NfcReaderBackend::getMapping() const {
    QVariantList list;
    for (auto it = m_mapping.constBegin(); it != m_mapping.constEnd(); ++it) {
        QVariantMap entry;
        entry["uid"] = it.key();
        entry["path"] = it.value().toString();
        list.append(entry);
    }
    return list;
}

void NfcReaderBackend::reloadMapping() {
    qDebug("[NfcReader] Reloading mapping file");
    if (loadMapping()) {
        emit statusChanged("Mapping reloaded");
    } else {
        emit errorOccurred("Failed to reload mapping");
    }
}
