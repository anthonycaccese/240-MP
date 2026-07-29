#pragma once
#include <QString>

// Locates the mpv binary the app should spawn.
//
// Resolution order (first hit wins), returning an absolute path or empty:
//   1. <appDir>/mpv  — sibling of the app binary. Inside the Linux AppImage,
//                      usr/bin/mpv sits beside usr/bin/240mp and linuxdeploy's
//                      AppRun does not add usr/bin to PATH, so findExecutable()
//                      alone would miss it.
//   2. mpv on PATH   — system installs (brew on macOS, apt on the Pi). On macOS
//                      the Homebrew locations are prepended first, because a
//                      double-clicked .app bundle inherits launchd's bare PATH.
namespace mpvbin {
QString locate();
}
