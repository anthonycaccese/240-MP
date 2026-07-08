#include "UpdateManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTimer>

namespace {

const QString kDefaultFeedUrl =
    QStringLiteral("https://api.github.com/repos/anthonycaccese/240-mp/releases/latest");

#ifdef Q_OS_MAC
const QString kAssetSuffix = QStringLiteral("-macOS-arm64.dmg");
#else
const QString kAssetSuffix = QStringLiteral("-linux-arm64.tar.gz");
#endif

// "Apply & Restart" under the autostart service: 240mp-stop (scripts/install.sh)
// treats exit 11 as a no-op so Restart=on-failure relaunches through the
// launcher, which applies the staged tarball. Sibling of Qt.exit(10) ("Exit to
// Terminal") in views/Settings.qml.
constexpr int kExitCodeUpdateRestart = 11;

QString feedUrl() {
    const QString env = qEnvironmentVariable("MP240_UPDATE_FEED_URL");
    return env.isEmpty() ? kDefaultFeedUrl : env;
}

QNetworkRequest githubRequest(const QUrl &url) {
    QNetworkRequest req(url);
    // GitHub's API rejects requests without a User-Agent.
    req.setRawHeader("User-Agent",
                     QStringLiteral("240-MP/%1").arg(QCoreApplication::applicationVersion()).toUtf8());
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    return req;
}

#ifdef Q_OS_MAC
// Bundle root ("/Applications/240mp.app") when running from a bundle, else empty.
QString macBundlePath() {
    const QString binDir = QCoreApplication::applicationDirPath();
    if (!binDir.endsWith(QStringLiteral(".app/Contents/MacOS")))
        return QString();
    return QFileInfo(binDir + QStringLiteral("/../..")).canonicalFilePath();
}
#endif

} // namespace

UpdateManager::UpdateManager(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot) {
    evaluateApplyCapability();
    reconcileStagingDir();
}

QString UpdateManager::currentVersion() const {
    return QCoreApplication::applicationVersion();
}

QString UpdateManager::updatesDir() const { return m_dataRoot + QStringLiteral("/updates"); }
QString UpdateManager::stagedJsonPath() const { return updatesDir() + QStringLiteral("/staged.json"); }
QString UpdateManager::stagedSha256Path() const { return updatesDir() + QStringLiteral("/staged.sha256"); }

void UpdateManager::setState(const QString &state, const QString &error) {
    m_state = state;
    m_errorMessage = error;
    emit stateChanged();
}

void UpdateManager::evaluateApplyCapability() {
#ifdef Q_OS_MAC
    const QString bundle = macBundlePath();
    if (bundle.startsWith(QStringLiteral("/Applications/"))
        && QFileInfo(bundle).isWritable()
        && QFileInfo(QFileInfo(bundle).absolutePath()).isWritable()) {
        m_canApply = true;
    } else {
        m_canApply = false;
        m_applyHint = QStringLiteral("This copy of 240-MP is not in /Applications — "
                                     "240-MP will quit and open the disk image for manual install.");
    }
#else
    // The launcher exports MP240_LAUNCHER_API when it knows how to apply staged
    // updates (see scripts/install.sh). Older installs must re-run the installer
    // once; non-standard installs (dev builds, custom prefixes) are not managed.
    const bool standardInstall =
        QCoreApplication::applicationDirPath() == QStringLiteral("/opt/240mp/bin");
    if (!standardInstall) {
        m_canApply = false;
        m_applyHint = QStringLiteral("Not a standard install — update manually.");
    } else if (!qEnvironmentVariableIsSet("MP240_LAUNCHER_API")) {
        m_canApply = false;
        m_applyHint = QStringLiteral("This install predates in-app updates. Re-run the "
                                     "installer once:\nbash <(curl -fsSL https://github.com/"
                                     "anthonycaccese/240-mp/releases/latest/download/install.sh)");
    } else {
        m_canApply = true;
    }
#endif
}

// Startup pass over DATA_ROOT/updates: drop half-finished downloads, and restore
// readyToApply if a verified staged update is still pending from a previous run.
void UpdateManager::reconcileStagingDir() {
    QDir dir(updatesDir());
    dir.mkpath(QStringLiteral("."));
    const QStringList parts = dir.entryList({QStringLiteral("*.part")}, QDir::Files);
    for (const QString &p : parts)
        dir.remove(p);

    QFile marker(stagedJsonPath());
    if (!marker.open(QIODevice::ReadOnly)) {
        clearStagingFiles();   // sweep payloads orphaned by a discarded stage
        return;
    }
    const QJsonObject staged = QJsonDocument::fromJson(marker.readAll()).object();
    marker.close();

    const QString version = staged.value(QStringLiteral("version")).toString();
    const QString asset   = staged.value(QStringLiteral("asset")).toString();
    const qint64 size     = staged.value(QStringLiteral("size")).toVariant().toLongLong();
    const QFileInfo payload(dir.filePath(asset));

    // Version matching the running app means the update was already applied (or
    // installed another way); a size mismatch means a corrupt stage. The sha is
    // not recomputed here — the RPi launcher re-verifies it before swapping, and
    // hashing a full tarball on every boot is wasted Pi CPU.
    if (version.isEmpty() || asset.isEmpty() || version == currentVersion()
        || !payload.exists() || payload.size() != size) {
        clearStagingFiles();
        return;
    }

    m_latestVersion = version;
    m_assetName = asset;
    emit infoChanged();
    setState(QStringLiteral("readyToApply"));
}

void UpdateManager::checkForUpdates() {
    if (m_state == QStringLiteral("checking") || m_state == QStringLiteral("downloading"))
        return;
    setState(QStringLiteral("checking"));

    QNetworkReply *reply = m_nam.get(githubRequest(QUrl(feedUrl())));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 403 || status == 429) {
            setState(QStringLiteral("error"),
                     QStringLiteral("GitHub rate limit reached — try again later."));
            return;
        }
        if (status == 404) {
            setState(QStringLiteral("error"), QStringLiteral("No releases found."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            setState(QStringLiteral("error"),
                     QStringLiteral("Update check failed: %1").arg(reply->errorString()));
            return;
        }
        handleReleaseInfo(reply->readAll());
    });
}

void UpdateManager::handleReleaseInfo(const QByteArray &json) {
    const QJsonObject release = QJsonDocument::fromJson(json).object();
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty()) {
        setState(QStringLiteral("error"), QStringLiteral("Unexpected response from GitHub."));
        return;
    }

    m_latestVersion = tag;
    m_releaseNotes = release.value(QStringLiteral("body")).toString();
    emit infoChanged();

    if (tag == currentVersion()) {
        setState(QStringLiteral("upToDate"));
        return;
    }

#ifndef Q_OS_MAC
    if (QSysInfo::currentCpuArchitecture() != QStringLiteral("arm64")) {
        setState(QStringLiteral("error"),
                 QStringLiteral("No update package for this architecture (%1).")
                     .arg(QSysInfo::currentCpuArchitecture()));
        return;
    }
#endif

    m_assetName.clear();
    m_assetUrl.clear();
    m_assetSize = 0;
    m_expectedSha256.clear();
    QString checksumsUrl;
    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &v : assets) {
        const QJsonObject asset = v.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (name.endsWith(kAssetSuffix)) {
            m_assetName = name;
            m_assetUrl = asset.value(QStringLiteral("browser_download_url")).toString();
            m_assetSize = asset.value(QStringLiteral("size")).toVariant().toLongLong();
        } else if (name == QStringLiteral("SHA256SUMS")) {
            checksumsUrl = asset.value(QStringLiteral("browser_download_url")).toString();
        }
    }
    if (m_assetUrl.isEmpty()) {
        setState(QStringLiteral("error"),
                 QStringLiteral("Release %1 has no package for this platform.").arg(tag));
        return;
    }

    if (!checksumsUrl.isEmpty())
        fetchChecksums(checksumsUrl);   // sets updateAvailable when done
    else
        setState(QStringLiteral("updateAvailable"));
}

// Releases published before the checksums step (or a fetch hiccup) degrade to
// size-only verification rather than blocking the update.
void UpdateManager::fetchChecksums(const QString &url) {
    QNetworkReply *reply = m_nam.get(githubRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QString sums = QString::fromUtf8(reply->readAll());
            for (const QString &line : sums.split(QLatin1Char('\n'))) {
                if (line.endsWith(m_assetName)) {
                    m_expectedSha256 = line.section(QLatin1Char(' '), 0, 0).trimmed();
                    break;
                }
            }
        }
        setState(QStringLiteral("updateAvailable"));
    });
}

void UpdateManager::download() {
    if (m_state != QStringLiteral("updateAvailable"))
        return;
    if (currentVersion() == QStringLiteral("dev")) {
        setState(QStringLiteral("error"),
                 QStringLiteral("Dev build — self-update is disabled."));
        return;
    }
    // 3x headroom: tarball + extracted .new tree share the same filesystem.
    if (m_assetSize > 0
        && QStorageInfo(updatesDir()).bytesAvailable() < 3 * m_assetSize) {
        setState(QStringLiteral("error"),
                 QStringLiteral("Not enough free disk space to download the update."));
        return;
    }

    m_downloadFile.setFileName(updatesDir() + QStringLiteral("/") + m_assetName
                               + QStringLiteral(".part"));
    if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setState(QStringLiteral("error"), QStringLiteral("Could not write to the updates folder."));
        return;
    }
    m_hash.reset();
    m_downloadProgress = 0.0;
    emit progressChanged();
    setState(QStringLiteral("downloading"));

    m_downloadReply = m_nam.get(githubRequest(QUrl(m_assetUrl)));
    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this]() {
        const QByteArray chunk = m_downloadReply->readAll();
        m_downloadFile.write(chunk);
        m_hash.addData(chunk);
    });
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        m_downloadProgress = total > 0 ? qreal(received) / qreal(total) : 0.0;
        emit progressChanged();
    });
    connect(m_downloadReply, &QNetworkReply::finished, this, &UpdateManager::finishDownload);
}

void UpdateManager::cancelDownload() {
    if (m_downloadReply)
        m_downloadReply->abort();   // finishDownload cleans up and restores state
}

void UpdateManager::finishDownload() {
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->deleteLater();
    m_downloadFile.close();

    auto fail = [this](const QString &msg) {
        m_downloadFile.remove();
        // A user cancel is not an error — return to the offer.
        if (msg.isEmpty())
            setState(QStringLiteral("updateAvailable"));
        else
            setState(QStringLiteral("error"), msg);
    };

    if (reply->error() == QNetworkReply::OperationCanceledError)
        return fail(QString());
    if (reply->error() != QNetworkReply::NoError)
        return fail(QStringLiteral("Download failed: %1").arg(reply->errorString()));
    if (m_assetSize > 0 && m_downloadFile.size() != m_assetSize)
        return fail(QStringLiteral("Download incomplete — please try again."));

    const QString sha256 = QString::fromLatin1(m_hash.result().toHex());
    if (!m_expectedSha256.isEmpty() && sha256 != m_expectedSha256)
        return fail(QStringLiteral("Downloaded file failed verification — please try again."));

    const QString finalPath = updatesDir() + QStringLiteral("/") + m_assetName;
    QFile::remove(finalPath);
    if (!m_downloadFile.rename(finalPath))
        return fail(QStringLiteral("Could not save the downloaded update."));

    writeStagedMarkers(sha256);
    setState(QStringLiteral("readyToApply"));
}

// Download-time marker only. The launcher-facing staged.sha256 is deliberately
// NOT written here — the launcher applies any stage that file blesses, so it is
// only created at the commitment point in applyLinux(). Until then a downloaded
// update is inert: backing out and rebooting runs the old version, and the page
// just re-offers Install.
void UpdateManager::writeStagedMarkers(const QString &sha256Hex) {
    QJsonObject staged{
        {QStringLiteral("version"), m_latestVersion},
        {QStringLiteral("asset"), m_assetName},
        {QStringLiteral("sha256"), sha256Hex},
        {QStringLiteral("size"), QString::number(QFileInfo(updatesDir() + QStringLiteral("/") + m_assetName).size())},
    };
    QFile json(stagedJsonPath());
    if (json.open(QIODevice::WriteOnly | QIODevice::Truncate))
        json.write(QJsonDocument(staged).toJson());
}

void UpdateManager::clearStagingFiles() {
    QDir dir(updatesDir());
    if (!m_assetName.isEmpty())
        dir.remove(m_assetName);
    dir.remove(QStringLiteral("staged.json"));
    dir.remove(QStringLiteral("staged.sha256"));
    // Plus whatever payload an older run left behind
    const QStringList leftovers =
        dir.entryList({QStringLiteral("*.tar.gz"), QStringLiteral("*.dmg")}, QDir::Files);
    for (const QString &f : leftovers)
        dir.remove(f);
}

void UpdateManager::discardStagedUpdate() {
    if (m_state != QStringLiteral("readyToApply"))
        return;
    clearStagingFiles();
    setState(m_assetUrl.isEmpty() ? QStringLiteral("idle") : QStringLiteral("updateAvailable"));
}

void UpdateManager::applyAndRestart() {
    if (m_state != QStringLiteral("readyToApply"))
        return;
#ifdef Q_OS_MAC
    applyMacos();
#else
    applyLinux();
#endif
}

void UpdateManager::applyLinux() {
    // Commitment point: writing staged.sha256 (coreutils format, verified by
    // the launcher with `sha256sum -c`) is what arms the stage — the launcher
    // swaps it in on its next run. The sha comes from staged.json so this works
    // both right after a download and after an app restart. Under autostart,
    // exit 11 makes systemd relaunch immediately; a manual session just quits
    // and the update applies on the next launch.
    QFile marker(stagedJsonPath());
    if (!marker.open(QIODevice::ReadOnly)) {
        setState(QStringLiteral("error"), QStringLiteral("Staged update is missing — please download again."));
        return;
    }
    const QJsonObject staged = QJsonDocument::fromJson(marker.readAll()).object();
    const QString sha256 = staged.value(QStringLiteral("sha256")).toString();
    const QString asset  = staged.value(QStringLiteral("asset")).toString();
    if (sha256.isEmpty() || asset.isEmpty()) {
        clearStagingFiles();
        setState(QStringLiteral("error"), QStringLiteral("Staged update is invalid — please download again."));
        return;
    }
    QFile sums(stagedSha256Path());
    if (!sums.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setState(QStringLiteral("error"), QStringLiteral("Could not write to the updates folder."));
        return;
    }
    sums.write(QStringLiteral("%1  %2\n").arg(sha256, asset).toUtf8());
    sums.close();

    if (qEnvironmentVariableIsSet("MP240_AUTOSTART"))
        QTimer::singleShot(0, qApp, []() { QCoreApplication::exit(kExitCodeUpdateRestart); });
    else
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void UpdateManager::applyMacos() {
#ifdef Q_OS_MAC
    const QString dmgPath = updatesDir() + QStringLiteral("/") + m_assetName;
    if (!m_canApply) {
        // Manual fallback: hand the DMG to Finder and quit — the fullscreen
        // window otherwise sits over/behind Finder looking frozen. The user
        // drags the app into place and reopens it; the stage is cleaned up by
        // startup reconciliation once the running version matches.
        QProcess::startDetached(QStringLiteral("/usr/bin/open"), {dmgPath});
        QTimer::singleShot(200, qApp, &QCoreApplication::quit);
        return;
    }

    static const char kHelper[] = R"HELPER(#!/bin/bash
# apply-macos.sh <pid> <dmg> <bundle> — spawned detached by 240-MP before it quits.
exec >>"$(dirname "$0")/apply.log" 2>&1
echo "=== $(date) apply $2 -> $3"
PID="$1"; DMG="$2"; BUNDLE="$3"
for i in $(seq 1 150); do kill -0 "$PID" 2>/dev/null || break; sleep 0.2; done
MOUNT=$(hdiutil attach -nobrowse -readonly "$DMG" | awk -F'\t' '/\/Volumes\//{print $NF; exit}')
[ -d "$MOUNT" ] || { echo "mount failed"; open "$DMG"; exit 1; }
SRC=$(/bin/ls -d "$MOUNT"/*.app 2>/dev/null | head -1)
OK=0
if [ -d "$SRC" ]; then
    rm -rf "$BUNDLE.new"
    if ditto "$SRC" "$BUNDLE.new"; then
        rm -rf "$BUNDLE.old"
        mv "$BUNDLE" "$BUNDLE.old" && mv "$BUNDLE.new" "$BUNDLE" && rm -rf "$BUNDLE.old" && OK=1
    fi
fi
hdiutil detach "$MOUNT" -quiet
if [ "$OK" = "1" ]; then
    xattr -dr com.apple.quarantine "$BUNDLE" 2>/dev/null || true
    rm -f "$DMG" "$(dirname "$0")/staged.json"
    echo "swap ok, relaunching"
    open -n "$BUNDLE"
else
    echo "swap failed, opening DMG for manual install"
    open "$DMG"
fi
)HELPER";

    const QString scriptPath = updatesDir() + QStringLiteral("/apply-macos.sh");
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setState(QStringLiteral("error"), QStringLiteral("Could not write the update helper."));
        return;
    }
    script.write(kHelper);
    script.close();
    script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                          | QFile::ReadGroup | QFile::ExeGroup);

    QProcess::startDetached(QStringLiteral("/bin/bash"),
                            {scriptPath,
                             QString::number(QCoreApplication::applicationPid()),
                             dmgPath, macBundlePath()});
    // Give the detached helper a beat to start before the app exits.
    QTimer::singleShot(200, qApp, &QCoreApplication::quit);
#endif
}
