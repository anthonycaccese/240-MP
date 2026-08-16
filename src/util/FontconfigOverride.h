#pragma once
#include <QString>

// Writes a fontconfig override file so a subprocess's libass can find the app's
// bundled fonts without them being installed system-wide. Linux only — macOS
// libass uses the coretext provider, where the equivalent is mpv's
// --osd-fonts-dir.
//
// Returns the path to the written config (to be exported as FONTCONFIG_FILE in
// the child's environment), or an empty string if it could not be written.
namespace fcoverride {
QString write(const QString &fontsDir);
}
