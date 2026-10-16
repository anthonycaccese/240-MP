#pragma once
#include <QString>

namespace vchan {

enum class PathVerdict {
    Ok,
    EmptyRef,
    NotRelative,
    Missing,
    NotAFile,
    Escapes,
    BadRoot
};

QString resolveMediaRef(const QString &mediaRoot,
                        const QString &ref,
                        PathVerdict *verdict = nullptr);

bool isWithinRoot(const QString &path, const QString &root);

QString pathVerdictToString(PathVerdict v);
}
