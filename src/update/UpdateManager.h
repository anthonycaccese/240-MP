#pragma once
#include <QObject>
#include <QString>
#include <QFile>
#include <QCryptographicHash>
#include <QNetworkAccessManager>

class QNetworkReply;

// In-app self-update: checks GitHub Releases for a newer version, downloads the
// platform asset into DATA_ROOT/updates, and applies it.
//   - Linux (RPi): download stages the tarball; choosing Apply writes the
//     staged.sha256 marker that arms it — the launcher (/usr/local/bin/240mp,
//     written by scripts/install.sh) verifies and swaps an armed stage into
//     /opt/240mp before the next exec. Under the autostart service, "Apply &
//     Restart" exits with code 11 so systemd relaunches immediately (see
//     240mp-stop); otherwise the update applies on the next manual launch.
//   - macOS: spawns a detached helper script that waits for the app to quit,
//     mounts the DMG, swaps /Applications/240mp.app, and relaunches.
class UpdateManager : public QObject {
    Q_OBJECT
    // idle | checking | upToDate | updateAvailable | downloading | readyToApply | error
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY infoChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY infoChanged)
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY progressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(bool canApply READ canApply CONSTANT)
    Q_PROPERTY(QString applyHint READ applyHint CONSTANT)
public:
    explicit UpdateManager(const QString &appRoot, const QString &dataRoot,
                           QObject *parent = nullptr);

    QString state() const { return m_state; }
    QString currentVersion() const;
    QString latestVersion() const { return m_latestVersion; }
    QString releaseNotes() const { return m_releaseNotes; }
    qreal downloadProgress() const { return m_downloadProgress; }
    QString errorMessage() const { return m_errorMessage; }
    bool canApply() const { return m_canApply; }
    QString applyHint() const { return m_applyHint; }

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void applyAndRestart();
    Q_INVOKABLE void discardStagedUpdate();

signals:
    void stateChanged();
    void infoChanged();
    void progressChanged();

private:
    void setState(const QString &state, const QString &error = QString());
    void evaluateApplyCapability();
    void reconcileStagingDir();
    void handleReleaseInfo(const QByteArray &json);
    void fetchChecksums(const QString &url);
    void startAssetDownload();
    void finishDownload();
    void applyLinux();
    void applyMacos();
    QString updatesDir() const;
    QString stagedJsonPath() const;
    QString stagedSha256Path() const;
    void writeStagedMarkers(const QString &sha256Hex);
    void clearStagingFiles();

    QNetworkAccessManager m_nam;
    QString m_appRoot;
    QString m_dataRoot;

    QString m_state = "idle";
    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_errorMessage;
    qreal m_downloadProgress = 0.0;
    bool m_canApply = false;
    QString m_applyHint;

    // Latest-release asset picked for this platform
    QString m_assetName;
    QString m_assetUrl;
    qint64 m_assetSize = 0;
    QString m_expectedSha256;   // from the release's SHA256SUMS asset, if present

    QNetworkReply *m_downloadReply = nullptr;
    QFile m_downloadFile;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
};
