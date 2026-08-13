#import <AppKit/AppKit.h>
#include "MacosUtils.h"

void hideMacOSMenuBar() {
    [NSApp setPresentationOptions:
        NSApplicationPresentationHideMenuBar |
        NSApplicationPresentationHideDock];
}

// Forces the Qt window's NSWindow to exactly cover a chosen screen.
// Called after the QML engine loads so the native NSWindow exists.
// When screenIndex is valid it targets that specific NSScreen (the app-level
// "display_index" setting); otherwise it uses the screen the window is on,
// which keeps the mpv-over-window layering intact and bypasses Qt
// geometry/dock constraints.
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
