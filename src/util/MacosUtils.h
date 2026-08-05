#pragma once

// QtGlobal defines Q_OS_MAC. Include it first so this header is self-contained:
// the #ifdef below then works regardless of include order, and callers don't
// need to pull in Qt platform macros (or the types) before including this.
#include <QtGlobal>

#ifdef Q_OS_MAC
#include <QList>
#include <QRect>
#include <QString>

void hideMacOSMenuBar();
int  macMainScreenWidth();
int  macMainScreenHeight();

// Number of attached displays, in the same order Qt's QGuiApplication::screens()
// enumerates them (both derive from NSScreen.screens), so an index is stable
// across the Qt and AppKit sides.
int  macScreenCount();

// One-line human-readable description per display (index, localized name,
// AppKit frame). Logged at startup so a user can discover which index is
// their target display (e.g. a CRT) and set "mac_display_index" accordingly.
QList<QString> macScreenDescriptions();

// Force the Qt window's NSWindow to exactly cover a chosen display.
// screenIndex selects into NSScreen.screens; pass -1 to fall back to the
// screen the window currently sits on (original behaviour).
void forceWindowFullScreenOnScreen(void *nsViewHandle, int screenIndex);

// Backwards-compatible wrapper: cover whichever screen the window is on.
void forceWindowFullScreen(void *nsViewHandle);
#endif
