#import <AppKit/AppKit.h>

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

// Forces the Qt window's NSWindow to exactly cover its screen.
// Called after the QML engine loads so the native NSWindow exists.
// Using win.screen.frame ensures we use the screen the window is on,
// bypassing any Qt geometry constraints or dock/menubar reservations.
void forceWindowFullScreen(void *handle) {
    NSView   *view   = (__bridge NSView *)(void *)handle;
    NSWindow *win    = [view window];
    if (!win) { NSLog(@"[240-MP] forceWindowFullScreen: no NSWindow"); return; }

    NSScreen *screen = win.screen ?: [NSScreen mainScreen];
    if (!screen) { NSLog(@"[240-MP] forceWindowFullScreen: no NSScreen"); return; }

    NSLog(@"[240-MP] forceWindowFullScreen: screen.frame = {{%.0f,%.0f},{%.0f,%.0f}}",
          screen.frame.origin.x, screen.frame.origin.y,
          screen.frame.size.width, screen.frame.size.height);

    win.styleMask = NSWindowStyleMaskBorderless;
    win.hasShadow = NO;
    [win setFrame:screen.frame display:YES animate:NO];
}
