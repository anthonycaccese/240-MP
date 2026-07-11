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
    Q_PROPERTY(bool mappingLoaded READ mappingLoaded NOTIFY mappingLoadedChanged)
    Q_PROPERTY(bool readerConnected READ readerConnected NOTIFY readerConnectedChanged)
    Q_PROPERTY(QString cardState READ cardState NOTIFY cardStateChanged)
    Q_PROPERTY(QString cardUid READ cardUid NOTIFY cardStateChanged)
    Q_PROPERTY(QString videoTitle READ videoTitle NOTIFY cardStateChanged)
public:
    explicit NfcReaderBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);
    ~NfcReaderBackend() override;

    Q_INVOKABLE void reloadMapping();
    Q_INVOKABLE void resetAfterPlayback();

    bool available() const {
#ifdef MP240_NFC_READER_AVAILABLE
        return true;
#else
        return false;
#endif
    }
    bool mappingLoaded() const { return m_mappingLoaded; }
    bool readerConnected() const { return m_readerConnected; }
    // "none" (no card / idle), "unmatched" (card with no mapping), "matched" (playing)
    QString cardState() const { return m_cardState; }
    QString cardUid() const { return m_cardUid; }
    QString videoTitle() const { return m_videoTitle; }

signals:
    void mappingLoadedChanged();
    void readerConnectedChanged();
    void cardStateChanged();
    void playbackRequested(const QString &videoPath);

private slots:
    void onSampled(bool readerConnected, const QString &uid);

private:
    struct MappingEntry {
        QString path;
        QString title;
    };

    QString m_appRoot;
    QString m_dataRoot;
    QHash<QString, MappingEntry> m_mapping;
    bool m_mappingLoaded = false;
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

    bool loadMapping();
    void startWorker();
    void abandonWorker(int waitMs);
    void setCardState(const QString &state, const QString &uid = {}, const QString &title = {});
    QString normalizeUid(const QString &uid) const;
    QString resolveVideoPath(const QString &path) const;
};
