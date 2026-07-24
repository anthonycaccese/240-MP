#include "YtDlpLocator.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

namespace ytdlp {

QString locate(const QString &dataRoot) {
    // 1. User-updatable drop-in under the data dir. Preferred so the user can
    //    update one file with `yt-dlp -U` decoupled from app releases, even when
    //    it isn't on the global PATH (the SteamOS story).
    if (!dataRoot.isEmpty()) {
        const QFileInfo dropIn(dataRoot + QStringLiteral("/bin/yt-dlp"));
        if (dropIn.isExecutable())
            return dropIn.absoluteFilePath();
    }

    // 2. Sibling of the app binary — mirrors how MpvController finds bundled mpv.
    //    Harmless when absent (yt-dlp is not bundled today).
    const QFileInfo sibling(QCoreApplication::applicationDirPath() + QStringLiteral("/yt-dlp"));
    if (sibling.isExecutable())
        return sibling.absoluteFilePath();

    // 3. Fall back to PATH (brew / apt / ~/.local/bin).
#ifdef Q_OS_MACOS
    // .app bundles launched via double-click get a minimal PATH that excludes
    // Homebrew. Prepend known install locations so findExecutable works.
    const QStringList extraPaths = { QStringLiteral("/opt/homebrew/bin"),
                                     QStringLiteral("/usr/local/bin") };
    const QStringList currentPath = qEnvironmentVariable("PATH").split(QLatin1Char(':'));
    for (const QString &p : extraPaths) {
        if (!currentPath.contains(p))
            qputenv("PATH", (p + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
    }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
}

} // namespace ytdlp
