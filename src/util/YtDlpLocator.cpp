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

    // 3. Fall back to PATH (brew / apt / ~/.local/bin). The macOS Homebrew PATH
    //    fixup this relies on is done once in main() by execpath::primeSystemPath().
    return QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
}

} // namespace ytdlp
