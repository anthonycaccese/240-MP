#include "NfcReaderBackend.h"

#include <QDir>
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
static const char *kTagsDirName = "nfc_tags";

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

    // Resolve the configured tags directory (falls back to the dataRoot/nfc_tags default).
    m_tagsDir = m_dataRoot + "/" + kTagsDirName;
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        const QString dir = QJsonDocument::fromJson(f.readAll()).object()
            ["modules"].toObject()["com.240mp.nfc_reader"].toObject()
            ["tags_directory"].toString();
        if (!dir.isEmpty())
            m_tagsDir = dir;
    }
    qDebug("[NfcReader] Tags dir: %s", qPrintable(tagsDirPath()));

    QDir().mkpath(tagsDirPath());
    scanTagsDir();
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

QString NfcReaderBackend::tagsDirPath() const {
    return m_tagsDir;
}

void NfcReaderBackend::setTagsDir(const QString &path) {
    // An empty (cleared) setting means back to the dataRoot/nfc_tags default.
    m_tagsDir = path.isEmpty() ? m_dataRoot + "/" + kTagsDirName : path;
    QDir().mkpath(m_tagsDir);
    qDebug("[NfcReader] Tags dir: %s", qPrintable(m_tagsDir));
    scanTagsDir();
}

void NfcReaderBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value) {
    if (moduleId == QLatin1String("com.240mp.nfc_reader") && key == QLatin1String("tags_directory"))
        setTagsDir(value.toString());
}

// One .txt file per card: the filename (minus .txt) is the display title, the
// first non-empty line is the card UID (any formatting — it's normalized), and
// the second non-empty line is the playback path (absolute, appRoot/dataRoot-
// relative, or a URL). A file with a UID but no path is a valid "known but
// unmapped" card. Lines past the second are ignored.
bool NfcReaderBackend::parseTagFile(const QString &filePath, QString &uidOut, QString &pathOut) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("[NfcReader] Cannot open tag file %s: %s",
                 qPrintable(filePath), qPrintable(file.errorString()));
        return false;
    }
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    QStringList lines;
    for (QString line : text.split(u'\n')) {
        line = line.trimmed();  // also strips the \r of CRLF files
        if (!line.isEmpty()) lines.append(line);
        if (lines.size() == 2) break;
    }
    if (lines.isEmpty()) return false;

    uidOut = normalizeUid(lines.at(0));
    if (uidOut.isEmpty()) return false;
    pathOut = lines.size() > 1 ? lines.at(1) : QString();
    return true;
}

void NfcReaderBackend::scanTagsDir() {
    m_mapping.clear();

    // QDir's default filter excludes hidden files (.DS_Store etc.). The .txt
    // suffix is checked manually because nameFilters are case-sensitive on
    // Linux. Alphabetical listing makes duplicate handling deterministic.
    const QDir dir(tagsDirPath());
    const QFileInfoList files =
        dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    int fileCount = 0;
    for (const QFileInfo &fi : files) {
        if (fi.suffix().compare(QLatin1String("txt"), Qt::CaseInsensitive) != 0)
            continue;

        QString uid, path;
        if (!parseTagFile(fi.absoluteFilePath(), uid, path)) {
            qWarning("[NfcReader] Skipping tag file with no usable UID: %s",
                     qPrintable(fi.fileName()));
            continue;
        }
        if (m_mapping.contains(uid)) {
            qWarning("[NfcReader] Duplicate UID %s in %s - keeping earlier file",
                     qPrintable(uid), qPrintable(fi.fileName()));
            continue;
        }
        m_mapping.insert(uid, MappingEntry{path, fi.completeBaseName()});
        fileCount++;
    }
    qDebug("[NfcReader] Scanned %d tag files from %s", fileCount, qPrintable(tagsDirPath()));
}

// A tapped card with no tag file gets a stub written for it so the user only
// has to rename the file and add a path line. NewOnly never overwrites: if a
// same-named file already exists (e.g. it holds a different UID), skip + warn.
void NfcReaderBackend::writeStubFile(const QString &normalizedUid) {
    QDir().mkpath(tagsDirPath());  // recreate if deleted at runtime
    QString name = normalizedUid;
    name.replace(QLatin1Char(':'), QLatin1Char('-'));
    const QString path = tagsDirPath() + "/" + name + ".txt";

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        qWarning("[NfcReader] Could not create stub tag file %s: %s",
                 qPrintable(path), qPrintable(file.errorString()));
        return;
    }
    file.write((normalizedUid + "\n").toUtf8());
    qDebug("[NfcReader] Created stub tag file: %s", qPrintable(path));
    // Register as known-unmapped so a lift-and-retap doesn't re-scan/re-write.
    m_mapping.insert(normalizedUid, MappingEntry{QString(), name});
}

void NfcReaderBackend::reloadMapping() {
    qDebug("[NfcReader] Rescanning tags dir");
    scanTagsDir();
}

void NfcReaderBackend::setModuleActive(bool active) {
    if (m_moduleActive == active) return;
    m_moduleActive = active;
    qDebug("[NfcReader] Module %s", active ? "active - card taps armed" : "inactive - card taps ignored");
    if (!active) {
        // Leaving the module drops any transient card state so the next visit
        // starts from a clean "tap a card" screen.
        m_playbackActive = false;
        setCardState("none");
    }
}

void NfcReaderBackend::resetAfterPlayback() {
    // Back to "tap a card" — but m_lastUid is kept so a card still sitting on
    // the reader doesn't immediately restart playback. It clears (and the card
    // becomes tappable again) once the card is physically removed.
    m_playbackActive = false;
    setCardState("none");
    qDebug("[NfcReader] Playback ended - ready for next card");
}

// Resume history — same shape as local_files: a JSON map of path → {pos, plPos}.
// plPos is the playlist item index for .m3u / YouTube-playlist mappings, -1 for
// single items. Keyed by the mapped video path (not the card UID) so remapping
// a card to a different video doesn't inherit the old video's resume point.
QString NfcReaderBackend::historyFilePath() const {
    return m_dataRoot + "/nfc_reader_history.json";
}

QVariantMap NfcReaderBackend::loadHistory() const {
    QFile file(historyFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

void NfcReaderBackend::saveHistory(const QVariantMap &history) {
    QFile file(historyFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(QJsonObject::fromVariantMap(history)).toJson(QJsonDocument::Compact));
}

QVariantMap NfcReaderBackend::getSavedPosition(const QString &videoPath) {
    const QVariant val = loadHistory().value(videoPath);
    if (!val.canConvert<QVariantMap>())
        return {};
    QVariantMap entry = val.toMap();
    if (!entry.contains("plPos")) entry["plPos"] = -1;
    return entry;
}

void NfcReaderBackend::savePosition(const QString &videoPath, int positionMs, int playlistPos) {
    QVariantMap history = loadHistory();
    QVariantMap entry;
    entry["pos"]   = positionMs;
    entry["plPos"] = playlistPos;
    history[videoPath] = entry;
    saveHistory(history);
}

void NfcReaderBackend::clearPosition(const QString &videoPath) {
    QVariantMap history = loadHistory();
    history.remove(videoPath);
    saveHistory(history);
}

// Same format cascade as the YouTube module; only applies to mapping entries
// that resolve through yt-dlp (YouTube page URLs) — local files and direct
// media URLs never reach the ytdl hook.
QString NfcReaderBackend::ytdlFormatForResolution(const QString &resolution) const {
    int height = 480;
    if (resolution == QLatin1String("720p"))
        height = 720;
    else if (resolution == QLatin1String("1080p"))
        height = 1080;
    // H.264 first (RPi hardware decode), then any codec at the cap, then best
    return QStringLiteral("bestvideo[height<=?%1][vcodec^=avc1]+bestaudio/"
                          "bestvideo[height<=?%1]+bestaudio/"
                          "best[height<=?%1]/best")
        .arg(height);
}

void NfcReaderBackend::get_resume_playback_options() {
    QVariantList options;
    QVariantMap ask; ask["id"] = "ask"; ask["label"] = "Ask";
    QVariantMap yes; yes["id"] = "yes"; yes["label"] = "Always";
    QVariantMap no;  no["id"]  = "no";  no["label"]  = "Never";
    options << ask << yes << no;
    emit dynamicOptionsReady("resume_playback", options);
}

void NfcReaderBackend::get_auto_subtitles_options() {
    QVariantList options;
    QVariantMap forced; forced["id"] = "forced"; forced["label"] = "Forced Only";
    QVariantMap on;     on["id"] = "on";         on["label"] = "On";
    QVariantMap off;    off["id"] = "off";       off["label"] = "Off";
    options << forced << on << off;
    emit dynamicOptionsReady("auto_subtitles", options);
}

void NfcReaderBackend::get_subtitle_languages() {
    QStringList addedLabels;
    QVariantList options;

    QFile file(m_appRoot + "/modules/nfc_reader/iso639-1.json");
    if (!file.open(QIODevice::ReadOnly))
        return;

    options.append(QVariantMap{{"id","-"},{"label","Any"}});

    QVariantList locList = QJsonDocument::fromJson(file.readAll()).toVariant().toList();
    for (const QVariant loc : locList)
    {
        QVariantMap langOption = QVariantMap{{"id",loc.toJsonObject()["id"].toString()},{"label",loc.toJsonObject()["label"].toString()}};
        if (langOption["label"].toString() == "" || addedLabels.contains(langOption["label"].toString())) continue;
        addedLabels.append(langOption["label"].toString());
        options.append(langOption);
    }

    emit dynamicOptionsReady("sub_lang", options);
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

    // Outside the module, card events must have no effect — but keep tracking
    // the UID silently so a card already resting on the reader when the module
    // opens is not treated as a fresh tap; it must be lifted and re-tapped,
    // same as a card left on the reader after playback ends.
    if (!m_moduleActive) {
        m_lastUid = uid;
        return;
    }

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

    auto it = m_mapping.constFind(normalizedUid);
    if (it == m_mapping.constEnd() || it->path.isEmpty()) {
        // Unknown or not-yet-mapped card: the user may have just added or
        // edited a tag file. Re-scan once before deciding. Cheap (tiny dir)
        // and naturally rate-limited by the uid == m_lastUid early return.
        scanTagsDir();
        it = m_mapping.constFind(normalizedUid);
    }

    if (it != m_mapping.constEnd() && !it->path.isEmpty()) {
        QString resolvedPath = resolveVideoPath(it->path);
        qDebug("[NfcReader] Mapping found: %s -> %s", qPrintable(normalizedUid), qPrintable(resolvedPath));
        m_playbackActive = true;
        setCardState("matched", normalizedUid, it->title);
        emit playbackRequested(resolvedPath);
    } else {
        if (it == m_mapping.constEnd()) {
            qWarning("[NfcReader] No tag file for UID %s - creating stub", qPrintable(normalizedUid));
            writeStubFile(normalizedUid);
        } else {
            qWarning("[NfcReader] Tag file for UID %s has no path line: %s",
                     qPrintable(normalizedUid), qPrintable(it->title));
        }
        setCardState("unmatched", normalizedUid);
    }
}
