#include "MpvLocator.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

namespace mpvbin {

QString locate()
{
    const QString siblingMpv = QCoreApplication::applicationDirPath() + "/mpv";
    if (QFileInfo(siblingMpv).isExecutable())
        return siblingMpv;

    return QStandardPaths::findExecutable("mpv");
}

} // namespace mpvbin
