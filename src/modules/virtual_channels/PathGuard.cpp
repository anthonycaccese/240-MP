#include "PathGuard.h"

#include <QDir>
#include <QFileInfo>

namespace vchan {

bool isWithinRoot(const QString &path, const QString &root) {
    if (path == root) return true;
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root
                                                           : root + QLatin1Char('/');
    return path.startsWith(prefix);
}

QString resolveMediaRef(const QString &mediaRoot,
                        const QString &ref,
                        PathVerdict *verdict) {
    const auto done = [&](PathVerdict v, const QString &p = QString()) {
        if (verdict) *verdict = v;
        return p;
    };

    const QString r = ref.trimmed();
    if (r.isEmpty())                    return done(PathVerdict::EmptyRef);
    if (mediaRoot.trimmed().isEmpty())  return done(PathVerdict::BadRoot);

    if (QDir::isAbsolutePath(r)) return done(PathVerdict::NotRelative);

    const QString rootCanon = QFileInfo(mediaRoot).canonicalFilePath();
    if (rootCanon.isEmpty()) return done(PathVerdict::BadRoot);

    const QString  joined = QDir(rootCanon).filePath(r);
    const QFileInfo fi(joined);
    const QString  canon = fi.canonicalFilePath();

    if (canon.isEmpty()) {
        const QString cleaned = QDir::cleanPath(joined);
        return isWithinRoot(cleaned, rootCanon) ? done(PathVerdict::Missing)
                                           : done(PathVerdict::Escapes);
    }

    if (!isWithinRoot(canon, rootCanon)) return done(PathVerdict::Escapes);

    if (!fi.isFile()) return done(PathVerdict::NotAFile);

    return done(PathVerdict::Ok, canon);
}

QString pathVerdictToString(PathVerdict v) {
    switch (v) {
    case PathVerdict::Ok:          return QStringLiteral("ok");
    case PathVerdict::EmptyRef:    return QStringLiteral("empty ref");
    case PathVerdict::NotRelative: return QStringLiteral("ref is absolute");
    case PathVerdict::Missing:     return QStringLiteral("file missing");
    case PathVerdict::NotAFile:    return QStringLiteral("not a file");
    case PathVerdict::Escapes:     return QStringLiteral("refused: outside media root");
    case PathVerdict::BadRoot:     return QStringLiteral("media root unusable");
    }
    return QStringLiteral("unknown");
}
}
