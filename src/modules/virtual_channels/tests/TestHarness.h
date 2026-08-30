#pragma once
#include <QString>
#include <cstdio>

namespace vtest {

inline int g_checks = 0;
inline int g_failed = 0;

inline void check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) {
        ++g_failed;
        std::printf("  FAIL  %s\n", what);
    }
}

inline void checkEq(long long got, long long want, const char *what) {
    ++g_checks;
    if (got != want) {
        ++g_failed;
        std::printf("  FAIL  %s (got %lld, want %lld)\n", what, got, want);
    }
}

inline void checkStr(const QString &got, const QString &want, const char *what) {
    ++g_checks;
    if (got != want) {
        ++g_failed;
        std::printf("  FAIL  %s (got \"%s\", want \"%s\")\n",
                    what, qUtf8Printable(got), qUtf8Printable(want));
    }
}

inline void section(const char *name) { std::printf("%s\n", name); }
}
