#include "FontconfigOverride.h"
#include <QDir>
#include <QFile>

namespace fcoverride {

QString write(const QString &fontsDir) {
    const QString path = QDir::tempPath() + "/240mp-fonts.conf";
    QFile f(path);
    if (!f.open(QFile::WriteOnly | QFile::Text))
        return {};
    f.write(QString(
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
        "<fontconfig>\n"
        "  <dir>%1</dir>\n"
        "  <include ignore_missing=\"yes\">/etc/fonts/fonts.conf</include>\n"
        "</fontconfig>\n"
    ).arg(fontsDir).toUtf8());
    return path;
}

} // namespace fcoverride
