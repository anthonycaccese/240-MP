#include "MpvLocator.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

namespace mpvbin {

QString locate()
{
#ifdef Q_OS_MACOS
    // A double-clicked .app bundle inherits launchd's PATH, which has no
    // Homebrew. Prepend the known install locations so findExecutable works.
    {
        const QStringList extraPaths = { "/opt/homebrew/bin", "/usr/local/bin" };
        for (const QString &p : extraPaths) {
            if (!qEnvironmentVariable("PATH").split(":").contains(p))
                qputenv("PATH", (p + ":" + qEnvironmentVariable("PATH")).toUtf8());
        }
    }
#endif

    const QString siblingMpv = QCoreApplication::applicationDirPath() + "/mpv";
    if (QFileInfo(siblingMpv).isExecutable())
        return siblingMpv;

    return QStandardPaths::findExecutable("mpv");
}

} // namespace mpvbin
