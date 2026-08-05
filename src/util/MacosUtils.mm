#import <AppKit/AppKit.h>
#include "MacosUtils.h"

void hideMacOSMenuBar() {
    [NSApp setPresentationOptions:
        NSApplicationPresentationHideMenuBar |
        NSApplicationPresentationHideDock];
}

int macMainScreenWidth() {
    NSScreen *s = [NSScreen mainScreen];
    return s ? (int)s.frame.size.width : 1920;
}

int macMainScreenHeight() {
    NSScreen *s = [NSScreen mainScreen];
    return s ? (int)s.frame.size.height : 1080;
}

int macScreenCount() {
    return (int)[[NSScreen screens] count];
}

QList<QString> macScreenDescriptions() {
    QList<QString> out;
    NSArray<NSScreen *> *screens = [NSScreen screens];
    for (int i = 0; i < (int)screens.count; ++i) {
        NSScreen *s = screens[i];
        NSRect f = s.frame;
        NSString *name = @"";
        if (@available(macOS 10.15, *))
            name = s.localizedName ?: @"";
        out.append(QString("index %1: \"%2\" %3x%4 at (%5,%6)")
                       .arg(i)
                       .arg(QString::fromNSString(name))
                       .arg((int)f.size.width).arg((int)f.size.height)
                       .arg((int)f.origin.x).arg((int)f.origin.y));
    }
    return out;
}

// Forces the Qt window's NSWindow to exactly cover a chosen screen.
// Called after the QML engine loads so the native NSWindow exists.
// When screenIndex is valid it targets that specific NSScreen; otherwise it
// uses the screen the window is on (the original behaviour), which keeps the
// mpv-over-window layering intact and bypasses Qt geometry/dock constraints.
void forceWindowFullScreenOnScreen(void *handle, int screenIndex) {
    NSView   *view   = (__bridge NSView *)(void *)handle;
    NSWindow *win    = [view window];
    if (!win) { NSLog(@"[240-MP] forceWindowFullScreen: no NSWindow"); return; }

    NSArray<NSScreen *> *screens = [NSScreen screens];
    NSScreen *screen = nil;
    if (screenIndex >= 0 && screenIndex < (int)screens.count)
        screen = screens[screenIndex];
    if (!screen)
        screen = win.screen ?: [NSScreen mainScreen];
    if (!screen) { NSLog(@"[240-MP] forceWindowFullScreen: no NSScreen"); return; }

    NSLog(@"[240-MP] forceWindowFullScreen: index=%d screen.frame = {{%.0f,%.0f},{%.0f,%.0f}}",
          screenIndex,
          screen.frame.origin.x, screen.frame.origin.y,
          screen.frame.size.width, screen.frame.size.height);

    win.styleMask = NSWindowStyleMaskBorderless;
    win.hasShadow = NO;
    [win setFrame:screen.frame display:YES animate:NO];
}

void forceWindowFullScreen(void *handle) {
    forceWindowFullScreenOnScreen(handle, -1);
}
