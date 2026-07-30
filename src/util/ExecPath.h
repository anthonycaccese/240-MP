#pragma once

// One-time PATH fixup for finding the external binaries the app spawns (mpv,
// yt-dlp).
//
// A .app bundle launched by double-click inherits launchd's minimal PATH, which
// excludes Homebrew — so QStandardPaths::findExecutable() misses a brew-installed
// mpv even though a Terminal launch finds it. Prepending the Homebrew locations
// fixes that, but it mutates the process-global environment, which is inherited
// by every subprocess and is not safe to do concurrently.
//
// So it happens exactly once, from main(), before anything is spawned or looked
// up. The locator functions (mpvbin::locate, ytdlp::locate) are then pure
// queries with no side effects, callable from anywhere.
namespace execpath {
// Call once from main(), on the main thread, before any locate() call. No-op
// off macOS.
void primeSystemPath();
}
