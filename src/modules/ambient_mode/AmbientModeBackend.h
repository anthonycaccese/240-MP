#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantList>

class AmbientModeBackend : public QObject {
    Q_OBJECT
public:
    explicit AmbientModeBackend(const QString &dataRoot, QObject *parent = nullptr);
    ~AmbientModeBackend() override;

    Q_INVOKABLE QVariantList getVideoFiles() const;
    Q_INVOKABLE QVariantList getAudioFiles() const;
    Q_INVOKABLE QString      mediaRoot() const;
    Q_INVOKABLE void         setMediaRoot(const QString &path);
    Q_INVOKABLE void         startAudio(const QStringList &paths, bool shuffle = false);
    Q_INVOKABLE void         stopAudio();

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

private:
    QVariantList scanFiles(const QStringList &extensions) const;

    QString   m_dataRoot;
    QString   m_mediaRoot;
    QProcess *m_audioProcess = nullptr;
};
