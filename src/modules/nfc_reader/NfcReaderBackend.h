#pragma once
#include <QObject>
#include <QVariant>
#include <QTimer>
#include <QJsonObject>
#include <cstdint>

class NfcReaderBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool readerReady READ isReaderReady NOTIFY readerReadyChanged)
public:
    explicit NfcReaderBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);
    ~NfcReaderBackend() override;

    Q_INVOKABLE QString getStatus() const;
    Q_INVOKABLE QVariantList getMapping() const;
    Q_INVOKABLE void reloadMapping();
    Q_INVOKABLE void clearLastUid();
    bool isReaderReady() const { return m_readerReady; }

signals:
    void readerReady();
    void readerReadyChanged();
    void cardDetected(const QString &uid);
    void playbackRequested(const QString &videoPath);
    void statusChanged(const QString &status);
    void errorOccurred(const QString &message);

private slots:
    void pollForCard();

private:
    QString m_appRoot;
    QString m_dataRoot;
    QString m_mappingFile;
    QJsonObject m_mapping;
    QTimer *m_pollTimer = nullptr;
    bool m_readerReady = false;
    QString m_lastUid;
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    uintptr_t m_context = 0;
    uintptr_t m_cardHandle = 0;
#endif

    bool initializeReader();
    bool loadMapping();
    QString normalizeUid(const QString &uid) const;
    QString resolveVideoPath(const QString &path) const;
    bool checkReader();
    QString readCardUid();
};
