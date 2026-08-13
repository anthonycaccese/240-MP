#pragma once

// QtGlobal defines Q_OS_MAC. Include it first so this header is self-contained:
// the #ifdef below then works regardless of include order, and callers don't
// need to pull in Qt platform macros (or the types) before including this.
#include <QtGlobal>

#ifdef Q_OS_MAC
void hideMacOSMenuBar();

// Force the Qt window's NSWindow to exactly cover a chosen display.
// screenIndex selects into NSScreen.screens — the same ordering Qt's
// QGuiApplication::screens() enumerates (both derive from NSScreen.screens),
// so the app-level "display_index" setting is valid on both sides. Pass -1 to
// fall back to the screen the window currently sits on.
void forceWindowFullScreenOnScreen(void *nsViewHandle, int screenIndex);
#endif
