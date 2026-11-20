#include "../DurationProbe.h"
#include "TestHarness.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace vchan;
using vtest::check;
using vtest::checkEq;
using vtest::section;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(bytes);
    return true;
}
}

int runDurationProbeTests() {
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("  FAIL  could not create temp dir\n");
        return 1;
    }
    const QString data = tmp.path();

    section("probe: refuses to guess");
    {
        DurationProbe p(data);

        checkEq(p.durationMs(data + "/nope.mkv"), 0, "missing file is 0");
        checkEq(p.durationMs(data), 0, "a directory is 0");
        checkEq(p.durationMs(QString()), 0, "empty path is 0");

        const QString junk = data + "/notmedia.mkv";
        check(writeFile(junk, QByteArray(2048, 'x')), "fixture written");
        checkEq(p.durationMs(junk), 0, "unprobeable file is 0, not a guess");
    }

    section("probe: reports what it can do");
    {
        DurationProbe p(data);
        const QString name = p.proberName();
        check(name == QLatin1String("ffprobe") || name == QLatin1String("mpv")
                  || name == QLatin1String("none"),
              "proberName is one of the known values");
        check(p.isUsable() == (name != QLatin1String("none")),
              "isUsable agrees with proberName");
    }

    section("cache: failures are not cached");
    {
        DurationProbe p(data);
        const QString junk = data + "/notmedia2.mkv";
        check(writeFile(junk, QByteArray(1024, 'y')), "fixture written");
        checkEq(p.durationMs(junk), 0, "probe fails");
        p.save();

        DurationProbe q(data);
        checkEq(q.cachedCount(), 0, "nothing persisted for a failed probe");
    }

    section("cache: survives a round trip");
    {
        const QString cache = data + "/channels/duration-cache.json";
        QDir().mkpath(data + "/channels");
        const QString f = data + "/pretend.mkv";
        check(writeFile(f, QByteArray(4096, 'z')), "fixture written");

        const QFileInfo fi(f);
        const QByteArray json = QStringLiteral(
            R"({"%1":{"dur":123456,"size":%2,"mtime":%3}})")
            .arg(f).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).toUtf8();
        check(writeFile(cache, json), "cache written");

        DurationProbe p(data);
        checkEq(p.cachedCount(), 1, "cache loaded");
        checkEq(p.durationMs(f), 123456, "cached duration served without probing");

        QFile touch(f);
        check(touch.open(QIODevice::WriteOnly | QIODevice::Append), "reopen fixture");
        touch.write("more");
        touch.close();
        checkEq(p.durationMs(f), 0, "changed size invalidates the cached entry");
    }

    section("cache: corrupt file is survivable");
    {
        QDir().mkpath(data + "/channels");
        check(writeFile(data + "/channels/duration-cache.json", "{not json"),
              "corrupt cache written");
        DurationProbe p(data);
        checkEq(p.cachedCount(), 0, "corrupt cache is discarded, not fatal");
        checkEq(p.durationMs(data + "/nope.mkv"), 0, "still usable afterwards");
    }

    return 0;
}
