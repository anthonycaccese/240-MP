#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>
#include <QCursor>
#include <QDebug>
#include <QWindow>
#include <locale.h>

#include "AppCore.h"
#include "modules/local_files/LocalFilesBackend.h"
#include "modules/plex/PlexBackend.h"
#include "modules/ambient_mode/AmbientModeBackend.h"
#include "player/MpvController.h"
#ifdef Q_OS_MAC
#include "macos_utils.h"
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
    app.setApplicationVersion("2026.06.06");

    // Hide cursor — 240-MP is keyboard-only so the cursor serves no purpose.
    // On Linux, only hide on headless EGLFS (not desktop X11/Wayland sessions).
#ifdef Q_OS_LINUX
    if (qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty())
        QGuiApplication::setOverrideCursor(Qt::BlankCursor);
#endif
#ifdef Q_OS_MAC
    QGuiApplication::setOverrideCursor(Qt::BlankCursor);
    hideMacOSMenuBar();
    int macW = macMainScreenWidth();
    int macH = macMainScreenHeight();
    qDebug("[main] macOS NSScreen main frame: %dx%d", macW, macH);
#endif

    setlocale(LC_NUMERIC, "C");

    const QString appRoot  = resolveAppRoot();
    const QString dataRoot = resolveDataRoot();
    qDebug("[main] appRoot  = %s", qPrintable(appRoot));
    qDebug("[main] dataRoot = %s", qPrintable(dataRoot));

    QQmlApplicationEngine engine;

    AppCore             appCore(appRoot, dataRoot);
    LocalFilesBackend   localFiles(appRoot, dataRoot);
    PlexBackend         plexBackend(appRoot, dataRoot);
    AmbientModeBackend  ambientMode(dataRoot);
    MpvController       mpvController(appRoot);

    // Each module backend is wired in one call: stored for action routing, exposed to QML
    // under its context-property name, and its optional signals/slots connected by
    // introspection. The module ID lives in exactly one place per module.
    QQmlContext *ctx = engine.rootContext();
    appCore.registerModule("com.240mp.local_files",  "localFilesBackend",  &localFiles,  ctx);
    appCore.registerModule("com.240mp.plex",         "plexBackend",        &plexBackend, ctx);
    appCore.registerModule("com.240mp.ambient_mode", "ambientModeBackend", &ambientMode, ctx);

    ctx->setContextProperty("appCore",       &appCore);
    ctx->setContextProperty("mpvController", &mpvController);
#ifdef Q_OS_MAC
    engine.rootContext()->setContextProperty("macScreenX",      0);
    engine.rootContext()->setContextProperty("macScreenY",      0);
    engine.rootContext()->setContextProperty("macScreenWidth",  macW);
    engine.rootContext()->setContextProperty("macScreenHeight", macH);
#endif

    engine.addImportPath(appRoot + "/views");

    engine.load(QUrl::fromLocalFile(appRoot + "/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        qCritical("[main] QML engine failed to load Main.qml");
        return 1;
    }

#ifdef Q_OS_MAC
    if (QWindow *win = qobject_cast<QWindow *>(engine.rootObjects().first())) {
        win->winId(); // ensure native NSWindow is created
        forceWindowFullScreen(reinterpret_cast<void *>(win->winId()));
    }
#endif

    return app.exec();
}
