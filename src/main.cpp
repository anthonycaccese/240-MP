#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>
#include <QCursor>
#include <QDebug>
#include <QWindow>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <locale.h>
#include <csignal>

#include "AppCore.h"
#include "modules/local_files/LocalFilesBackend.h"
#include "modules/plex/PlexBackend.h"
#include "modules/jellyfin/JellyfinBackend.h"
#include "modules/emby/EmbyBackend.h"
#include "modules/ambient_mode/AmbientModeBackend.h"
#include "modules/nfc_reader/NfcReaderBackend.h"
#include "modules/youtube/YouTubeBackend.h"
#include "modules/weather/WeatherBackend.h"
#include "modules/scripts/ScriptsBackend.h"
#include "modules/airplay/AirPlayBackend.h"
#include "player/MpvController.h"
#include "input/InputManager.h"
#include "input/IdleTracker.h"
#include "update/UpdateManager.h"
#include "util/ExecPath.h"
#include "util/DisplayHandoff.h"
#ifdef Q_OS_MAC
#include "util/MacosUtils.h"
#endif

static QString resolveAppRoot() {
    QString envRoot = qEnvironmentVariable("APP_ROOT");
    if (!envRoot.isEmpty())
        return QDir(envRoot).canonicalPath();

    QString appDir = QCoreApplication::applicationDirPath();

    if (QCoreApplication::applicationFilePath().contains(".app/Contents/MacOS/"))
        return QDir(appDir + "/../Resources").canonicalPath();

    QDir fhsData(appDir + "/../share/240mp");
    if (fhsData.exists())
        return fhsData.canonicalPath();

    return QDir(appDir + "/..").canonicalPath();
}

static QString resolveDataRoot() {
    QString envRoot = qEnvironmentVariable("DATA_ROOT");
    if (!envRoot.isEmpty())
        return QDir(envRoot).canonicalPath();

    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path;
}

// Terminating signals must unwind normally rather than killing the process where
// it stands. Without this, `systemctl stop`, `pkill`, or a shutdown can leave mpv
// or another process running and with the scripts module (takeover specifically), it
// could also leave a headless Pi on a blank VT with DRM master dropped, because
// ~DisplayHandoff never got the chance to put the display back.
//
// Async-signal-safe: only sets a flag. A 100 ms timer in main() polls it and calls
// quit() from the event loop, where destructors run properly.
static volatile std::sig_atomic_t g_termRequested = 0;
extern "C" void mp240HandleTerm(int) { g_termRequested = 1; }

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("240-MP");
    app.setApplicationVersion(QStringLiteral(APP_VERSION));

    // Hide cursor — 240-MP is keyboard/gamepad-only so the cursor serves no
    // purpose. Hidden on all of Linux: headless EGLFS and desktop compositors
    // (Steam Deck / RPi desktop) alike, since the app runs fullscreen kiosk-style.
#ifdef Q_OS_LINUX
    QGuiApplication::setOverrideCursor(Qt::BlankCursor);
#endif
#ifdef Q_OS_MAC
    QGuiApplication::setOverrideCursor(Qt::BlankCursor);
    hideMacOSMenuBar();
#endif

    setlocale(LC_NUMERIC, "C");

    std::signal(SIGTERM, mp240HandleTerm);
    std::signal(SIGINT,  mp240HandleTerm);
    std::signal(SIGHUP,  mp240HandleTerm);
    QTimer termPoll;
    termPoll.setInterval(100);
    QObject::connect(&termPoll, &QTimer::timeout, &app, [&app]() {
        if (g_termRequested) {
            qInfo("[main] Termination signal received — shutting down cleanly");
            app.quit();
        }
    });
    termPoll.start();

    // Once, before anything looks for or spawns mpv / yt-dlp: the locators are
    // pure queries and deliberately do not touch the environment themselves.
    execpath::primeSystemPath();

    const QString appRoot  = resolveAppRoot();
    const QString dataRoot = resolveDataRoot();
    qDebug("[main] appRoot  = %s", qPrintable(appRoot));
    qDebug("[main] dataRoot = %s", qPrintable(dataRoot));

    // Log every attached display so a user can discover which index is their
    // target (e.g. a CRT) and set the app-level "display_index" in config.json
    // accordingly. Name is the localized display name on macOS, the RandR
    // output name on X11, the wl_output name on Wayland, and the single DRM
    // output on EGLFS.
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        const QRect g = screens.at(i)->geometry();
        qInfo("[main] display index %d: \"%s\" %dx%d at (%d,%d)",
              i, qPrintable(screens.at(i)->name()),
              g.width(), g.height(), g.x(), g.y());
    }

    QQmlApplicationEngine engine;

    AppCore             appCore(appRoot, dataRoot);

    // Which physical display the UI launches on. App-level "display_index"
    // (0 = primary screen, the previous hardcoded behaviour). Lets the UI open
    // on a secondary display without making it the OS primary. Applied on
    // macOS and desktop Linux (xcb/wayland); on single-display platforms
    // (Pi EGLFS, Steam Deck gaming mode under gamescope) the clamp below
    // resolves to 0 and everything stays a no-op.
    // On macOS, Qt's screen list and AppKit's NSScreen.screens share ordering,
    // so this index is valid for both the QML geometry below and the native
    // forceWindowFullScreenOnScreen() call after load.
    int displayIndex = appCore.get_setting(QString(), "display_index").toInt();
    if (displayIndex < 0 || displayIndex >= screens.size()) {
        if (displayIndex != 0)
            qWarning("[main] display_index %d is out of range (%lld displays attached) — falling back to display 0",
                     displayIndex, (long long)screens.size());
        displayIndex = 0;
    }
    QScreen *targetScreen = screens.value(displayIndex, QGuiApplication::primaryScreen());
    const QRect screenGeo = targetScreen ? targetScreen->geometry() : QRect(0, 0, 1920, 1080);
    qInfo("[main] UI target display index %d -> %dx%d at (%d,%d)",
          displayIndex, screenGeo.width(), screenGeo.height(), screenGeo.x(), screenGeo.y());

    LocalFilesBackend   localFiles(appRoot, dataRoot);
    PlexBackend         plexBackend(appRoot, dataRoot);
    JellyfinBackend     jellyfinBackend(appRoot, dataRoot);
    EmbyBackend         embyBackend(appRoot, dataRoot);
    AmbientModeBackend  ambientMode(dataRoot);
    NfcReaderBackend    nfcReader(appRoot, dataRoot, &appCore);
    YouTubeBackend      youtubeBackend(appRoot, dataRoot);
    WeatherBackend      weatherBackend(appRoot, dataRoot);
    AirPlayBackend      airplayBackend(appRoot, dataRoot, &appCore);
    DisplayHandoff      displayHandoff;
    ScriptsBackend      scriptsBackend(appRoot, dataRoot, &displayHandoff);
    MpvController       mpvController(appRoot, dataRoot, &appCore, &displayHandoff);
    InputManager        inputManager(dataRoot, &appCore);
    IdleTracker         idleTracker(60);   // disabled until Main.qml applies the saved setting
    UpdateManager       updateManager(appRoot, dataRoot);

    // Playback follows the UI's display: mpv gets a --fs-screen* arg derived
    // from this on macOS / desktop Linux (no-op at index 0 and on headless).
    mpvController.setTargetDisplay(displayIndex, targetScreen ? targetScreen->name() : QString());

    // When the Qt window is inactive (fullscreen mpv has OS focus on macOS),
    // gamepad actions bypass QML and drive mpv directly over IPC.
    QObject::connect(&inputManager, &InputManager::mpvKeyRequested,
                     &mpvController, &MpvController::sendKey);

    // Each module backend is wired in one call: stored for action routing, exposed to QML
    // under its context-property name, and its optional signals/slots connected by
    // introspection. The module ID lives in exactly one place per module.
    QQmlContext *ctx = engine.rootContext();
    appCore.registerModule("com.240mp.local_files",  "localFilesBackend",  &localFiles,  ctx);
    appCore.registerModule("com.240mp.plex",         "plexBackend",        &plexBackend, ctx);
    appCore.registerModule("com.240mp.jellyfin",     "jellyfinBackend",    &jellyfinBackend, ctx);
    appCore.registerModule("com.240mp.emby",         "embyBackend",        &embyBackend, ctx);
    appCore.registerModule("com.240mp.ambient_mode", "ambientModeBackend", &ambientMode, ctx);
    appCore.registerModule("com.240mp.nfc_reader",   "nfcReaderBackend",   &nfcReader,   ctx);
    appCore.registerModule("com.240mp.youtube",      "youtubeBackend",     &youtubeBackend, ctx);
    appCore.registerModule("com.240mp.weather",      "weatherBackend",     &weatherBackend, ctx);
    appCore.registerModule("com.240mp.airplay",      "airplayBackend",     &airplayBackend, ctx);
    appCore.registerModule("com.240mp.scripts",      "scriptsBackend",     &scriptsBackend, ctx);

    ctx->setContextProperty("idleTracker",   &idleTracker);
    ctx->setContextProperty("appCore",       &appCore);
    ctx->setContextProperty("mpvController", &mpvController);
    ctx->setContextProperty("inputManager",  &inputManager);
    ctx->setContextProperty("updateManager", &updateManager);
#ifdef Q_OS_MAC
    // Target display geometry in Qt coordinates (top-left origin), so the QML
    // Window bindings position onto the chosen screen. The native fullscreen
    // call after load then nails the exact frame in AppKit coordinates.
    // QVariant(...) not a literal — a bare 0 is a null pointer constant and
    // resolves to the QObject* overload, handing QML null instead of an int.
    engine.rootContext()->setContextProperty("macScreenX",      QVariant(screenGeo.x()));
    engine.rootContext()->setContextProperty("macScreenY",      QVariant(screenGeo.y()));
    engine.rootContext()->setContextProperty("macScreenWidth",  QVariant(screenGeo.width()));
    engine.rootContext()->setContextProperty("macScreenHeight", QVariant(screenGeo.height()));
#endif

    engine.addImportPath(appRoot + "/views");

    engine.load(QUrl::fromLocalFile(appRoot + "/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        qCritical("[main] QML engine failed to load Main.qml");
        return 1;
    }

    // Gamepad key events are posted straight to the root window so they reach
    // the QML focus item even when another window (mpv) holds OS focus.
    inputManager.setTargetWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));

#ifdef Q_OS_MAC
    if (QWindow *win = qobject_cast<QWindow *>(engine.rootObjects().first())) {
        // Move the window onto the target screen before forcing fullscreen, so
        // both Qt's and AppKit's notion of the window's screen agree.
        if (targetScreen)
            win->setScreen(targetScreen);
        win->setGeometry(screenGeo);
        win->winId(); // ensure native NSWindow is created
        forceWindowFullScreenOnScreen(reinterpret_cast<void *>(win->winId()), displayIndex);
    }
#elif defined(Q_OS_LINUX)
    // Desktop Linux (Steam Deck desktop mode, generic x86_64) only: EGLFS has
    // a single screen so displayIndex is already clamped to 0 there, but gate
    // on the platform name anyway so this can never disturb the Pi path.
    // Wayland ignores setGeometry (clients can't self-position); re-issuing
    // FullScreen after setScreen makes QtWayland send
    // xdg_toplevel.set_fullscreen(output) for the target.  On xcb the geometry
    // move plus the fullscreen re-request lands it the same way.
    if (displayIndex > 0 && targetScreen
        && (QGuiApplication::platformName() == QLatin1String("xcb")
            || QGuiApplication::platformName() == QLatin1String("wayland"))) {
        if (QWindow *win = qobject_cast<QWindow *>(engine.rootObjects().first())) {
            win->setScreen(targetScreen);
            win->setGeometry(screenGeo);
            win->setVisibility(QWindow::FullScreen);
        }
    }
#endif

    return app.exec();
}
