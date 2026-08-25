// Standalone smoke test for AirPlayBackend — no QML, no real shairport-sync
// build, no phone. Drives the real backend class against
// scripts/dev/fake-airplay/shairport-sync (put first on PATH before running)
// and checks that the process lifecycle and metadata-pipe parsing actually
// work end to end. See scripts/dev/README.md for how to build/run this.
//
// Exit code 0 = pass, 1 = fail/timeout. Prints what it's waiting for as it
// goes, so a hang points at exactly which stage broke.
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>
#include <functional>

#include "AppCore.h"
#include "modules/airplay/AirPlayBackend.h"

namespace {

// Set by the message handler below whenever AirPlayBackend logs a process
// error (e.g. shairport-sync dying unexpectedly) — checked at the end so a
// crash-during-shutdown regression fails the run instead of getting lost
// next to a PASS. See stopReceiver()'s shutdown-ordering comment for the bug
// this originally caught.
bool g_sawProcessError = false;

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type >= QtWarningMsg && msg.contains(QLatin1String("shairport-sync process error"))) {
        g_sawProcessError = true;
    }
    fprintf(stderr, "%s\n", qPrintable(msg));
}

// Runs the Qt event loop until `predicate()` is true or `timeoutMs` elapses.
// Returns true if the predicate was satisfied.
bool waitUntil(std::function<bool()> predicate, int timeoutMs)
{
    if (predicate()) return true;
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer pollTimer;
    QObject::connect(&pollTimer, &QTimer::timeout, [&]() {
        if (predicate()) loop.quit();
    });
    pollTimer.start(50);
    timeoutTimer.start(timeoutMs);
    loop.exec();
    return predicate();
}

bool fail(const QString &msg)
{
    qCritical().noquote() << "FAIL:" << msg;
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(messageHandler);
    QCoreApplication app(argc, argv);

    // Fail loudly and immediately if the fake binary isn't actually first on
    // PATH — a silent fallback to a real (or missing) `shairport-sync` would
    // make failures here very confusing.
    const QString which = QStandardPaths::findExecutable("shairport-sync");
    if (!which.contains("fake-airplay")) {
        qCritical().noquote() << "FAIL: `shairport-sync` on PATH resolves to" << which
                               << "— expected scripts/dev/fake-airplay/shairport-sync."
                               << "Run via scripts/dev/run-airplay-smoke-test.sh, or prepend"
                               << "scripts/dev/fake-airplay to PATH yourself.";
        return 1;
    }

    const QString scratchRoot = QDir::tempPath() + "/240mp-airplay-smoke-" + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(scratchRoot);
    qInfo().noquote() << "Scratch data root:" << scratchRoot;

    AppCore appCore(scratchRoot /* appRoot: no modules/ needed for this test */, scratchRoot);
    AirPlayBackend backend(scratchRoot, scratchRoot, &appCore);

    bool ok = true;

    qInfo() << "Starting fake shairport-sync...";
    backend.startReceiver();

    ok = ok && waitUntil([&]() { return backend.isConnected(); }, 5000);
    if (!ok) { fail("isConnected() never became true (ssnc/pbeg not observed)"); }

    if (ok) {
        qInfo() << "Connected. Waiting for first track's metadata...";
        ok = waitUntil([&]() { return backend.trackTitle() == "Bohemian Rhapsody"; }, 5000);
        if (!ok) fail(QStringLiteral("trackTitle never became 'Bohemian Rhapsody', got '%1'").arg(backend.trackTitle()));
    }

    if (ok) {
        ok = waitUntil([&]() { return backend.artist() == "Queen" && backend.album() == "A Night at the Opera"; }, 2000);
        if (!ok) fail(QStringLiteral("artist/album mismatch: got '%1' / '%2'").arg(backend.artist(), backend.album()));
    }

    if (ok) {
        // ssnc/snam — confirmed against shairport-sync's real source (see
        // docs/airplay-module-plan.md §6), not the placeholder this test
        // originally shipped without.
        ok = waitUntil([&]() { return backend.senderName() == "Test iPhone"; }, 2000);
        if (!ok) fail(QStringLiteral("senderName never became 'Test iPhone' (ssnc/snam), got '%1'").arg(backend.senderName()));
    }

    if (ok) {
        qInfo() << "Waiting for album artwork to be written to disk...";
        ok = waitUntil([&]() { return !backend.artworkPath().isEmpty() && QFile::exists(backend.artworkPath()); }, 2000);
        if (!ok) fail(QStringLiteral("artworkPath never populated with an existing file (got '%1')").arg(backend.artworkPath()));
    }

    if (ok) {
        qInfo() << "Waiting for the second track (metadata updates on a live connection)...";
        ok = waitUntil([&]() { return backend.trackTitle() == "Suzanne"; }, 12000);
        if (!ok) fail(QStringLiteral("trackTitle never advanced to 'Suzanne', stuck at '%1'").arg(backend.trackTitle()));
    }

    qInfo() << "Stopping receiver...";
    backend.stopReceiver();
    ok = ok && waitUntil([&]() { return !backend.isConnected(); }, 2000);
    if (!ok) fail("isConnected() did not clear after stopReceiver()");

    // Give handleProcessErrorOccurred a beat to fire before checking, since
    // process exit is reported asynchronously.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    if (ok && g_sawProcessError) {
        ok = fail("shairport-sync reported a process error during shutdown (see log above) — "
                   "expected a clean exit from terminate()");
    }

    if (ok) {
        qInfo().noquote() << "PASS";
    } else {
        qCritical().noquote() << "FAIL — see above";
    }
    return ok ? 0 : 1;
}
