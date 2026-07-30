#pragma once
#include <QString>

// Locates the yt-dlp binary the app and mpv should both use.
//
// yt-dlp is deliberately NOT bundled with the app (YouTube breaks it every few
// weeks, so it can't be frozen per release). Instead the user manages a single
// copy that both the app's browse-time subprocesses and mpv's ytdl_hook share.
//
// Resolution order (first hit wins), returning an absolute path or empty:
//   1. <dataRoot>/bin/yt-dlp   — user-updatable drop-in (writable even on an
//                                immutable SteamOS rootfs; `yt-dlp -U` in place)
//   2. <appDir>/yt-dlp         — sibling of the app binary (future bundling/dev)
//   3. yt-dlp on PATH          — brew / apt / ~/.local/bin (the macOS Homebrew
//                                PATH fixup that double-clicked .app bundles need
//                                is done once in main(), see util/ExecPath.h)
namespace ytdlp {
QString locate(const QString &dataRoot);
}
