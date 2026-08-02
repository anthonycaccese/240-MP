#include "ExecPath.h"

#include <QByteArray>
#include <QLatin1Char>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace execpath {

void primeSystemPath()
{
#ifdef Q_OS_MACOS
    const QStringList extraPaths = { QStringLiteral("/opt/homebrew/bin"),
                                     QStringLiteral("/usr/local/bin") };
    for (const QString &p : extraPaths) {
        const QString current = qEnvironmentVariable("PATH");
        if (!current.split(QLatin1Char(':')).contains(p))
            qputenv("PATH", (p + QLatin1Char(':') + current).toUtf8());
    }
#endif
}

} // namespace execpath
