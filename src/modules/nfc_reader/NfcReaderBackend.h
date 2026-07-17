#pragma once
#include <QObject>
#include <QVariant>
#include <QTimer>
#include <QThread>
#include <QHash>
#include <cstdint>

// Runs all PC/SC calls on a dedicated thread. On macOS, SCardConnect can block
// inside the ctkpcscd daemon for a minute or more (sometimes forever) after
// reader replugs or rapid card swaps; polling from the main thread would
// freeze the whole UI with it.
class NfcPollWorker : public QObject {
    Q_OBJECT
public:
    ~NfcPollWorker() override;

public slots:
    void start();

signals:
    void sampled(bool readerConnected, const QString &uid);

private:
    void poll();
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    QString findReader();
    bool cardPresent(const QString &readerName);
    QString readCardUid(const QString &readerName);
    uintptr_t m_context = 0;
#endif
};

class NfcReaderBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool readerConnected READ readerConnected NOTIFY readerConnectedChanged)
    Q_PROPERTY(QString cardState READ cardState NOTIFY cardStateChanged)
    Q_PROPERTY(QString cardUid READ cardUid NOTIFY cardStateChanged)
    Q_PROPERTY(QString videoTitle READ videoTitle NOTIFY cardStateChanged)
public:
    explicit NfcReaderBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);
    ~NfcReaderBackend() override;

    Q_INVOKABLE void reloadMapping();
    Q_INVOKABLE void resetAfterPlayback();

    // The module's Root.qml raises/lowers this on load/unload. Polling runs
    // for the whole app lifetime (the watchdog needs it), but card events must
    // not do anything — no state changes, no playback — unless the user is
    // inside the NFC module.
    Q_INVOKABLE void setModuleActive(bool active);

    Q_INVOKABLE QVariantMap getSavedPosition(const QString &videoPath);
    Q_INVOKABLE void        savePosition(const QString &videoPath, int positionMs, int playlistPos);
    Q_INVOKABLE void        clearPosition(const QString &videoPath);
    Q_INVOKABLE void        get_resume_playback_options();
    Q_INVOKABLE void        get_auto_subtitles_options();
    Q_INVOKABLE void        get_subtitle_languages();
    Q_INVOKABLE QString     ytdlFormatForResolution(const QString &resolution) const;

    bool available() const {
#ifdef MP240_NFC_READER_AVAILABLE
        return true;
#else
        return false;
#endif
    }
    bool readerConnected() const { return m_readerConnected; }
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
    void onSampled(bool readerConnected, const QString &uid);

private:
    struct MappingEntry {
        QString path;  // empty = known card with a tag file but no path yet
        QString title;
    };

    QString m_appRoot;
    QString m_dataRoot;
    QString m_tagsDir;
    QHash<QString, MappingEntry> m_mapping;
    QThread *m_workerThread = nullptr;
    NfcPollWorker *m_worker = nullptr;
    QTimer *m_watchdog = nullptr;
    qint64 m_lastSampleMs = 0;
    int m_respawnCount = 0;
    bool m_readerConnected = false;
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
    void startWorker();
    void abandonWorker(int waitMs);
    void setCardState(const QString &state, const QString &uid = {}, const QString &title = {});
    QString normalizeUid(const QString &uid) const;
    QString resolveVideoPath(const QString &path) const;
};
