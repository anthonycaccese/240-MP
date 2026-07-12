#include "NfcReaderBackend.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>

#include <cstring>

#if defined(MP240_NFC_READER_AVAILABLE) && (defined(Q_OS_LINUX) || defined(Q_OS_MAC))
#include <PCSC/winscard.h>
// Not pulled in by winscard.h on macOS; provides the DWORD/LONG typedefs that
// keep the SCard* calls portable (pcsclite widens them to long on 64-bit Linux).
#include <PCSC/wintypes.h>
#define NFC_PCSC_AVAILABLE
#endif

// How long without a sample before the UI shows disconnected, and before we
// conclude the poll thread is wedged in ctkpcscd and replace it.
static constexpr qint64 kStallDisconnectMs = 3000;
static constexpr qint64 kStallRespawnMs = 10000;
static constexpr int kMaxRespawns = 5;
static const char *kMappingFileName = "nfc_mapping.json";

// ---------------------------------------------------------------------------
// NfcPollWorker — lives on its own QThread; owns all PC/SC state and calls.
// ---------------------------------------------------------------------------

NfcPollWorker::~NfcPollWorker() {
#ifdef NFC_PCSC_AVAILABLE
    if (m_context) {
        SCardReleaseContext(static_cast<SCARDCONTEXT>(m_context));
        m_context = 0;
    }
#endif
}

void NfcPollWorker::start() {
    // The timer must be created here (in the worker thread) so its events run
    // on this thread's event loop, not the main thread's.
    auto *timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, &NfcPollWorker::poll);
    timer->start();
}

void NfcPollWorker::poll() {
#ifdef NFC_PCSC_AVAILABLE
    QString reader = findReader();
    if (reader.isEmpty()) {
        emit sampled(false, {});
        return;
    }
    if (!cardPresent(reader)) {
        emit sampled(true, {});
        return;
    }
    emit sampled(true, readCardUid(reader));
#else
    // PC/SC not available — reader never connects
    emit sampled(false, {});
#endif
}

#ifdef NFC_PCSC_AVAILABLE

QString NfcPollWorker::findReader() {
    if (!m_context) {
        SCARDCONTEXT newCtx = 0;
        if (SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &newCtx) != SCARD_S_SUCCESS) {
            return {};
        }
        m_context = static_cast<uintptr_t>(newCtx);
    }

    // DWORD/LONG (not uint32_t/int32_t): pcsclite on 64-bit Linux types these
    // as unsigned long/long, while macOS's PCSC framework uses 32-bit types.
    SCARDCONTEXT ctx = static_cast<SCARDCONTEXT>(m_context);
    DWORD cchReaders = 0;
    LONG rv = SCardListReaders(ctx, nullptr, nullptr, &cchReaders);
    if (rv != SCARD_S_SUCCESS || cchReaders == 0) {
        // Covers both "no readers" and stale contexts (pcscd restart, last
        // reader unplugged); release so the next poll re-establishes.
        SCardReleaseContext(ctx);
        m_context = 0;
        return {};
    }

    char *mszReaders = new char[cchReaders];
    rv = SCardListReaders(ctx, nullptr, mszReaders, &cchReaders);
    if (rv != SCARD_S_SUCCESS) {
        delete[] mszReaders;
        SCardReleaseContext(ctx);
        m_context = 0;
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
    return targetReader;
}

bool NfcPollWorker::cardPresent(const QString &readerName) {
    // Ask for the reader's state instead of blindly calling SCardConnect every
    // poll: connecting while a card is mid-insertion/removal is what tends to
    // wedge ctkpcscd on macOS. MUTE means a card is present but unresponsive
    // (still settling); wait for a clean PRESENT before connecting.
    QByteArray name = readerName.toUtf8();
    SCARD_READERSTATE state;
    memset(&state, 0, sizeof(state));
    state.szReader = name.constData();
    state.dwCurrentState = SCARD_STATE_UNAWARE;

    LONG rv = SCardGetStatusChange(static_cast<SCARDCONTEXT>(m_context), 0, &state, 1);
    if (rv != SCARD_S_SUCCESS) return false;

    return (state.dwEventState & SCARD_STATE_PRESENT) &&
           !(state.dwEventState & SCARD_STATE_MUTE);
}

QString NfcPollWorker::readCardUid(const QString &readerName) {
    SCARDCONTEXT ctx = static_cast<SCARDCONTEXT>(m_context);

    SCARDHANDLE cardHandle = 0;
    DWORD dwActiveProtocol = 0;
    LONG rv = SCardConnect(ctx,
                              readerName.toUtf8().constData(),
                              SCARD_SHARE_SHARED,
                              SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                              &cardHandle,
                              &dwActiveProtocol);
    if (rv != SCARD_S_SUCCESS) return {};

    unsigned char sendBuffer[] = {0xFF, 0xCA, 0x00, 0x00, 0x00};
    unsigned char recvBuffer[256];
    DWORD recvLength = sizeof(recvBuffer);

    SCARD_IO_REQUEST ioRequest = {dwActiveProtocol, sizeof(SCARD_IO_REQUEST)};

    rv = SCardTransmit(cardHandle,
                       &ioRequest,
                       sendBuffer, sizeof(sendBuffer),
                       nullptr,
                       recvBuffer, &recvLength);

    SCardDisconnect(cardHandle, SCARD_LEAVE_CARD);

    if (rv != SCARD_S_SUCCESS || recvLength < 2) return {};

    unsigned char sw1 = recvBuffer[recvLength - 2];
    unsigned char sw2 = recvBuffer[recvLength - 1];
    if (sw1 != 0x90 || sw2 != 0x00) return {};

    QByteArray uidBytes(reinterpret_cast<const char*>(recvBuffer), recvLength - 2);
    return uidBytes.toHex(':').toUpper();
}

#endif // NFC_PCSC_AVAILABLE

// ---------------------------------------------------------------------------
// NfcReaderBackend — main-thread state machine + QML API.
// ---------------------------------------------------------------------------

NfcReaderBackend::NfcReaderBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
{
    qDebug("[NfcReader] Initializing NFC reader backend");
    qDebug("[NfcReader] Mapping file: %s", qPrintable(m_dataRoot + "/" + kMappingFileName));

    loadMapping();
    startWorker();

    // If the worker wedges inside a PC/SC call, first report the reader as
    // disconnected rather than showing a stale "tap a card" while taps go
    // nowhere; if it stays wedged, abandon that thread and start a fresh one.
    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(2000);
    connect(m_watchdog, &QTimer::timeout, this, [this]() {
        const qint64 stalledMs = QDateTime::currentMSecsSinceEpoch() - m_lastSampleMs;
        if (stalledMs < kStallDisconnectMs) return;

        if (m_readerConnected) {
            qWarning("[NfcReader] PC/SC polling stalled - marking reader disconnected");
            m_readerConnected = false;
            emit readerConnectedChanged();
            m_lastUid.clear();
            setCardState("none");
        }

        if (stalledMs > kStallRespawnMs) {
            if (m_respawnCount >= kMaxRespawns) return;
            m_respawnCount++;
            qWarning("[NfcReader] Poll thread wedged for %llds - restarting it (attempt %d/%d)",
                     static_cast<long long>(stalledMs / 1000), m_respawnCount, kMaxRespawns);
            abandonWorker(100);
            startWorker();
            m_lastSampleMs = QDateTime::currentMSecsSinceEpoch();
            if (m_respawnCount == kMaxRespawns) {
                qWarning("[NfcReader] Repeated PC/SC wedges - giving up until app restart");
            }
        }
    });
    m_watchdog->start();
}

NfcReaderBackend::~NfcReaderBackend() {
    abandonWorker(1500);
}

void NfcReaderBackend::startWorker() {
    m_lastSampleMs = QDateTime::currentMSecsSinceEpoch();
    m_workerThread = new QThread(this);
    m_worker = new NfcPollWorker;
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::started, m_worker, &NfcPollWorker::start);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &NfcPollWorker::sampled, this, &NfcReaderBackend::onSampled);
    m_workerThread->start();
}

void NfcReaderBackend::abandonWorker(int waitMs) {
    if (!m_workerThread) return;

    disconnect(m_worker, nullptr, this, nullptr);
    m_workerThread->quit();
    if (m_workerThread->wait(waitMs)) {
        delete m_workerThread;
    } else {
        // The thread is stuck in an uninterruptible PC/SC call: it can't be
        // terminated (mach_msg is not a cancellation point) and destroying a
        // running QThread aborts the process. Unparent and leak it instead;
        // if the call ever returns, the thread exits (quit() was already
        // requested) and deletes itself.
        m_workerThread->setParent(nullptr);
        connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    }
    m_workerThread = nullptr;
    m_worker = nullptr;
}

bool NfcReaderBackend::loadMapping() {
    const QString path = m_dataRoot + "/" + kMappingFileName;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("[NfcReader] Cannot open mapping file %s: %s", qPrintable(path), qPrintable(file.errorString()));
        if (m_mappingLoaded) {
            m_mappingLoaded = false;
            emit mappingLoadedChanged();
        }
        return false;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError) {
        qWarning("[NfcReader] JSON parse error in %s: %s", qPrintable(path), qPrintable(err.errorString()));
        if (m_mappingLoaded) {
            m_mappingLoaded = false;
            emit mappingLoadedChanged();
        }
        return false;
    }

    // Entries are either "UID": "path" or "UID": { "path": ..., "title": ... }.
    // Keys are normalized so the file's UID formatting doesn't have to match
    // the reader's byte formatting exactly.
    m_mapping.clear();
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        MappingEntry entry;
        if (it.value().isObject()) {
            const QJsonObject v = it.value().toObject();
            entry.path = v["path"].toString();
            entry.title = v["title"].toString();
        } else {
            entry.path = it.value().toString();
        }
        if (entry.path.isEmpty()) {
            qWarning("[NfcReader] Skipping mapping entry with no path: %s", qPrintable(it.key()));
            continue;
        }
        if (entry.title.isEmpty()) {
            const QString base = QFileInfo(entry.path).completeBaseName();
            entry.title = base.isEmpty() ? entry.path : base;
        }
        m_mapping.insert(normalizeUid(it.key()), entry);
    }

    qDebug("[NfcReader] Loaded mapping with %lld entries from %s",
           static_cast<long long>(m_mapping.size()), qPrintable(path));
    if (!m_mappingLoaded) {
        m_mappingLoaded = true;
        emit mappingLoadedChanged();
    }
    return true;
}

void NfcReaderBackend::reloadMapping() {
    qDebug("[NfcReader] Reloading mapping file");
    loadMapping();
}

void NfcReaderBackend::resetAfterPlayback() {
    // Back to "tap a card" — but m_lastUid is kept so a card still sitting on
    // the reader doesn't immediately restart playback. It clears (and the card
    // becomes tappable again) once the card is physically removed.
    m_playbackActive = false;
    setCardState("none");
    qDebug("[NfcReader] Playback ended - ready for next card");
}

void NfcReaderBackend::setCardState(const QString &state, const QString &uid, const QString &title) {
    if (m_cardState == state && m_cardUid == uid && m_videoTitle == title) return;
    m_cardState = state;
    m_cardUid = uid;
    m_videoTitle = title;
    emit cardStateChanged();
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

void NfcReaderBackend::onSampled(bool readerConnected, const QString &uid) {
    m_lastSampleMs = QDateTime::currentMSecsSinceEpoch();
    m_respawnCount = 0;

    if (readerConnected != m_readerConnected) {
        m_readerConnected = readerConnected;
        emit readerConnectedChanged();
        if (readerConnected) {
            qDebug("[NfcReader] Reader connected");
        } else {
            qDebug("[NfcReader] Reader disconnected");
            m_lastUid.clear();
            setCardState("none");
        }
    }
    if (!readerConnected) return;

    if (uid.isEmpty()) {
        if (!m_lastUid.isEmpty()) {
            m_lastUid.clear();
            // While mpv is up "matched" is still accurate; resetAfterPlayback
            // handles the return to idle.
            if (!m_playbackActive) setCardState("none");
        }
        return;
    }

    if (uid == m_lastUid) return;

    m_lastUid = uid;
    QString normalizedUid = normalizeUid(uid);
    qDebug("[NfcReader] Card detected: %s", qPrintable(normalizedUid));

    if (m_playbackActive) {
        qDebug("[NfcReader] Playback active - ignoring card");
        return;
    }

    const auto it = m_mapping.constFind(normalizedUid);
    if (it != m_mapping.constEnd()) {
        QString resolvedPath = resolveVideoPath(it->path);
        qDebug("[NfcReader] Mapping found: %s -> %s", qPrintable(normalizedUid), qPrintable(resolvedPath));
        m_playbackActive = true;
        setCardState("matched", normalizedUid, it->title);
        emit playbackRequested(resolvedPath);
    } else {
        qWarning("[NfcReader] No mapping for UID: %s", qPrintable(normalizedUid));
        setCardState("unmatched", normalizedUid);
    }
}
