#pragma once
#include <QString>

// Locates the mpv binary the app should spawn. The single source of truth for
// this — MpvController (video), WeatherBackend (music) and AmbientModeBackend
// (custom audio) all ask here, so a new resolution tier is a one-file change.
//
// Resolution order (first hit wins), returning an absolute path or empty:
//   1. <appDir>/mpv  — sibling of the app binary. Inside the Linux AppImage,
//                      usr/bin/mpv sits beside usr/bin/240mp and linuxdeploy's
//                      AppRun does not add usr/bin to PATH, so findExecutable()
//                      alone would miss it.
//   2. mpv on PATH   — system installs (brew on macOS, apt on the Pi).
//
// Pure query, no side effects: the macOS Homebrew PATH fixup that step 2 depends
// on happens once in main() via execpath::primeSystemPath(). Callers therefore
// never mutate the environment, and this is safe to call from any thread.
namespace mpvbin {
QString locate();
}
