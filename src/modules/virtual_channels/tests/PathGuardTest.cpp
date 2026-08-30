#include "../PathGuard.h"
#include "TestHarness.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace vchan;
using vtest::check;
using vtest::section;

namespace {

bool touch(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write("x");
    return true;
}

PathVerdict verdictOf(const QString &root, const QString &ref) {
    PathVerdict v = PathVerdict::Ok;
    resolveMediaRef(root, ref, &v);
    return v;
}
}

int runPathGuardTests() {
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("  FAIL  could not create temp dir\n");
        return 1;
    }

    const QString base    = tmp.path();
    const QString root    = base + "/media";
    const QString sibling = base + "/media2";
    const QString outside = base + "/outside";

    QDir().mkpath(root + "/sub");
    QDir().mkpath(root + "/adir");
    QDir().mkpath(sibling);
    QDir().mkpath(outside);

    check(touch(root + "/ok.mkv"),          "fixture: ok.mkv");
    check(touch(root + "/sub/deep.mkv"),    "fixture: sub/deep.mkv");
    check(touch(sibling + "/other.mkv"),    "fixture: media2/other.mkv");
    check(touch(outside + "/secret.mkv"),   "fixture: outside/secret.mkv");
    check(QFile::link(root + "/ok.mkv",     root + "/link_in"),  "fixture: link_in");
    check(QFile::link(outside + "/secret.mkv", root + "/link_out"), "fixture: link_out");

    section("resolves legitimate refs");
    {
        PathVerdict v;
        const QString p = resolveMediaRef(root, "ok.mkv", &v);
        check(v == PathVerdict::Ok, "plain file is Ok");
        check(!p.isEmpty(), "returns a path");
        check(QFileInfo(p).isAbsolute(), "path is absolute");
        check(p.endsWith(QLatin1String("/ok.mkv")), "path points at the file");

        check(verdictOf(root, "sub/deep.mkv")   == PathVerdict::Ok, "nested file");
        check(verdictOf(root, "./ok.mkv")       == PathVerdict::Ok, "leading ./");
        check(verdictOf(root, "  ok.mkv  ")     == PathVerdict::Ok, "surrounding space trimmed");
        check(verdictOf(root, "sub/../ok.mkv")  == PathVerdict::Ok,
              "traversal that stays inside is fine");
        check(verdictOf(root, "link_in")        == PathVerdict::Ok,
              "symlink inside the root is fine");
    }

    section("refuses escapes");
    {
        check(verdictOf(root, "../outside/secret.mkv") == PathVerdict::Escapes,
              "parent traversal refused");
        check(verdictOf(root, "sub/../../outside/secret.mkv") == PathVerdict::Escapes,
              "traversal via a subdirectory refused");
        check(verdictOf(root, "../../../../etc/passwd") == PathVerdict::Escapes,
              "deep traversal refused");
        check(verdictOf(root, "link_out") == PathVerdict::Escapes,
              "symlink pointing outside the root refused");

        check(verdictOf(root, "../media2/other.mkv") == PathVerdict::Escapes,
              "sibling directory sharing a name prefix refused");

        check(verdictOf(root, "../outside/nothere.mkv") == PathVerdict::Escapes,
              "escape reported even when the target is missing");
    }

    section("rejects malformed refs");
    {
        check(verdictOf(root, "")      == PathVerdict::EmptyRef,    "empty ref");
        check(verdictOf(root, "   ")   == PathVerdict::EmptyRef,    "whitespace ref");
        check(verdictOf(root, "/etc/passwd")  == PathVerdict::NotRelative,
              "absolute ref refused by shape, not by containment");
        check(verdictOf(root, "/" + root + "/ok.mkv") == PathVerdict::NotRelative,
              "absolute ref refused even when it points inside the root");
        check(verdictOf(root, "nope.mkv") == PathVerdict::Missing,  "missing file");
        check(verdictOf(root, "adir")     == PathVerdict::NotAFile, "directory is not media");
        check(verdictOf(root, ".")        == PathVerdict::NotAFile, "the root itself");
    }

    section("rejects a bad root");
    {
        check(verdictOf("", "ok.mkv") == PathVerdict::BadRoot, "empty root");
        check(verdictOf("   ", "ok.mkv") == PathVerdict::BadRoot, "whitespace root");
        check(verdictOf(base + "/does-not-exist", "ok.mkv") == PathVerdict::BadRoot,
              "non-existent root");
    }

    section("containment is one comparison, shared");
    {
        check(isWithinRoot("/media/shows", "/media"),        "a child is inside");
        check(isWithinRoot("/media", "/media"),              "the root is inside itself");
        check(isWithinRoot("/media/a/b/c.mkv", "/media"),    "so is a deep child");
        check(!isWithinRoot("/media2", "/media"),            "a sibling sharing a prefix is not");
        check(!isWithinRoot("/mediaX/shows", "/media"),       "nor one a directory deeper");
        check(!isWithinRoot("/etc/shadow", "/media"),        "nor somewhere else entirely");
        check(isWithinRoot("/etc", "/"),                      "everything is inside a root of /");
        check(isWithinRoot("/", "/"),                         "including / itself");
    }

    section("verdict strings");
    {
        check(!pathVerdictToString(PathVerdict::Ok).isEmpty(),      "Ok has a string");
        check(!pathVerdictToString(PathVerdict::Escapes).isEmpty(), "Escapes has a string");
        check(pathVerdictToString(PathVerdict::Escapes)
                  != pathVerdictToString(PathVerdict::Missing),
              "escape and missing read differently in logs");
    }

    return 0;
}
