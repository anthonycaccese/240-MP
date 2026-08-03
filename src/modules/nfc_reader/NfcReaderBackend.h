#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QVariant>
#include <QTimer>
#include <QThread>
#include <QHash>
#include <cstdint>
#include <memory>
#include <vector>

class NfcDriver;

// Runs all reader I/O on a dedicated thread. On macOS, SCardConnect can block
// inside the ctkpcscd daemon for a minute or more (sometimes forever) after
// reader replugs or rapid card swaps; polling from the main thread would
// freeze the whole UI with it. Serial reads are bounded, but they share the
// thread (and the backend's stall watchdog) for the same reason.
//
// Owns one instance of each NfcDriver and hands polling to whichever one finds
// a device first.
class NfcPollWorker : public QObject {
    Q_OBJECT
public:
    NfcPollWorker();
    ~NfcPollWorker() override;

public slots:
    void start();

signals:
    void sampled(bool readerConnected, const QString &uid, const QString &deviceName);

private:
    void poll();

    std::vector<std::unique_ptr<NfcDriver>> m_drivers;
    NfcDriver *m_active = nullptr;
    // Detection opens device nodes, so it runs on its own slower cadence than
    // the 500 ms poll tick rather than on every miss.
    QElapsedTimer m_sinceDetect;
};

class NfcReaderBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool pcscAvailable READ pcscAvailable CONSTANT)
    Q_PROPERTY(bool readerConnected READ readerConnected NOTIFY readerConnectedChanged)
    Q_PROPERTY(QString readerName READ readerName NOTIFY readerConnectedChanged)
    Q_PROPERTY(QString cardState READ cardState NOTIFY cardStateChanged)
    Q_PROPERTY(QString cardUid READ cardUid NOTIFY cardStateChanged)
    Q_PROPERTY(QString videoTitle READ videoTitle NOTIFY cardStateChanged)
public:
    explicit NfcReaderBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);
    ~NfcReaderBackend() override;

    Q_INVOKABLE void reloadMapping();
    Q_INVOKABLE void resetAfterPlayback();

    // The module's Root.qml raises/lowers this on load/unload. The configured
    // enabled setting owns polling lifetime; active view state only decides
    // whether card events may change state or request playback.
    Q_INVOKABLE void setModuleActive(bool active);

    Q_INVOKABLE QVariantMap getSavedPosition(const QString &videoPath);
    Q_INVOKABLE void        savePosition(const QString &videoPath, int positionMs, int playlistPos);
    Q_INVOKABLE void        clearPosition(const QString &videoPath);
    Q_INVOKABLE void        get_resume_playback_options();
    Q_INVOKABLE void        get_auto_subtitles_options();
    Q_INVOKABLE void        get_subtitle_languages();
    Q_INVOKABLE QString     ytdlFormatForResolution(const QString &resolution) const;

    // True when at least one reader driver is compiled in. The PN532 serial
    // driver links nothing, so this is really "is this a platform we support"
    // — unlike pcscAvailable(), which depends on libpcsclite being found.
    bool available() const {
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
        return true;
#else
        return false;
#endif
    }
    // Lets the UI tell "no reader plugged in" apart from "this build has no
    // PC/SC support, so only a PN532 will work".
    bool pcscAvailable() const;
    bool readerConnected() const { return m_readerConnected; }
    // e.g. "ACS ACR122U PICC Interface" or "PN532 v1.6 (/dev/ttyUSB0)".
    QString readerName() const { return m_readerName; }
    // "none" (no card / idle), "unmatched" (card with no tag file or no path yet), "matched" (playing)
    QString cardState() const { return m_cardState; }
    QString cardUid() const { return m_cardUid; }
    QString videoTitle() const { return m_videoTitle; }

signals:
    void readerConnectedChanged();
    void cardStateChanged();
    void playbackRequested(const QString &videoPath);
    void dynamicOptionsReady(const QString &key, const QVariant &options);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

private slots:
    void onSampled(bool readerConnected, const QString &uid, const QString &deviceName);

private:
    struct MappingEntry {
        QString path;  // empty = known card with a tag file but no path yet
        QString title;
    };

    QString m_appRoot;
    QString m_dataRoot;
    QString m_tagsDir;
    QHash<QString, MappingEntry> m_mapping;
    bool m_pollingEnabled = false;
    QThread *m_workerThread = nullptr;
    NfcPollWorker *m_worker = nullptr;
    QTimer *m_watchdog = nullptr;
    qint64 m_lastSampleMs = 0;
    int m_respawnCount = 0;
    bool m_readerConnected = false;
    QString m_readerName;
    QString m_cardState = "none";
    QString m_cardUid;
    QString m_videoTitle;
    QString m_lastUid;
    bool m_playbackActive = false;
    bool m_moduleActive = false;

    QString     historyFilePath() const;
    QVariantMap loadHistory() const;
    void        saveHistory(const QVariantMap &history);

    QString tagsDirPath() const;
    void setTagsDir(const QString &path);
    void scanTagsDir();
    bool parseTagFile(const QString &filePath, QString &uidOut, QString &pathOut) const;
    void writeStubFile(const QString &normalizedUid);
    void setPollingEnabled(bool enabled);
    void startPolling();
    void stopPolling();
    void startWorker();
    void abandonWorker(int waitMs);
    void setCardState(const QString &state, const QString &uid = {}, const QString &title = {});
    QString normalizeUid(const QString &uid) const;
    QString resolveVideoPath(const QString &path) const;
};
