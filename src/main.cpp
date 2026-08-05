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
#include <locale.h>

#include "AppCore.h"
#include "modules/local_files/LocalFilesBackend.h"
#include "modules/plex/PlexBackend.h"
#include "modules/jellyfin/JellyfinBackend.h"
#include "modules/emby/EmbyBackend.h"
#include "modules/ambient_mode/AmbientModeBackend.h"
#include "modules/nfc_reader/NfcReaderBackend.h"
#include "modules/youtube/YouTubeBackend.h"
#include "modules/weather/WeatherBackend.h"
#include "player/MpvController.h"
#include "input/InputManager.h"
#include "input/IdleTracker.h"
#include "update/UpdateManager.h"
#include "util/ExecPath.h"
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
    // Log every attached display so a user can discover which index is their
    // target (e.g. a CRT) and set the app-level "mac_display_index" in
    // config.json accordingly. The actual target is resolved below, once
    // AppCore exists to read the setting.
    for (const QString &desc : macScreenDescriptions())
        qDebug("[main] %s", qPrintable(desc));
#endif

    setlocale(LC_NUMERIC, "C");

    // Once, before anything looks for or spawns mpv / yt-dlp: the locators are
    // pure queries and deliberately do not touch the environment themselves.
    execpath::primeSystemPath();

    const QString appRoot  = resolveAppRoot();
    const QString dataRoot = resolveDataRoot();
    qDebug("[main] appRoot  = %s", qPrintable(appRoot));
    qDebug("[main] dataRoot = %s", qPrintable(dataRoot));

    QQmlApplicationEngine engine;

    AppCore             appCore(appRoot, dataRoot);

#ifdef Q_OS_MAC
    // Which physical display the UI launches on. App-level "mac_display_index"
    // (0 = primary/menu-bar screen, the previous hardcoded behaviour). Lets the
    // UI open on a secondary display without making it the macOS primary.
    // Qt's screen list and AppKit's NSScreen.screens share ordering, so this
    // index is valid for both the QML geometry below and the native
    // forceWindowFullScreenOnScreen() call after load.
    int macDisplayIndex = appCore.get_setting(QString(), "mac_display_index").toInt();
    const QList<QScreen *> macScreens = QGuiApplication::screens();
    if (macDisplayIndex < 0 || macDisplayIndex >= macScreens.size())
        macDisplayIndex = 0;
    QScreen *macTargetScreen = macScreens.value(macDisplayIndex, QGuiApplication::primaryScreen());
    const QRect macGeo = macTargetScreen ? macTargetScreen->geometry()
                                         : QRect(0, 0, macMainScreenWidth(), macMainScreenHeight());
    qDebug("[main] macOS UI target display index %d -> %dx%d at (%d,%d)",
           macDisplayIndex, macGeo.width(), macGeo.height(), macGeo.x(), macGeo.y());
#endif

    LocalFilesBackend   localFiles(appRoot, dataRoot);
    PlexBackend         plexBackend(appRoot, dataRoot);
    JellyfinBackend     jellyfinBackend(appRoot, dataRoot);
    EmbyBackend         embyBackend(appRoot, dataRoot);
    AmbientModeBackend  ambientMode(dataRoot);
    NfcReaderBackend    nfcReader(appRoot, dataRoot);
    YouTubeBackend      youtubeBackend(appRoot, dataRoot);
    WeatherBackend      weatherBackend(appRoot, dataRoot);
    MpvController       mpvController(appRoot, dataRoot, &appCore);
    InputManager        inputManager(dataRoot, &appCore);
    IdleTracker         idleTracker(60);   // disabled until Main.qml applies the saved setting
    UpdateManager       updateManager(appRoot, dataRoot);

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
    engine.rootContext()->setContextProperty("macScreenX",      QVariant(macGeo.x()));
    engine.rootContext()->setContextProperty("macScreenY",      QVariant(macGeo.y()));
    engine.rootContext()->setContextProperty("macScreenWidth",  QVariant(macGeo.width()));
    engine.rootContext()->setContextProperty("macScreenHeight", QVariant(macGeo.height()));
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
        if (macTargetScreen)
            win->setScreen(macTargetScreen);
        win->setGeometry(macGeo);
        win->winId(); // ensure native NSWindow is created
        forceWindowFullScreenOnScreen(reinterpret_cast<void *>(win->winId()), macDisplayIndex);
    }
#endif

    return app.exec();
}
