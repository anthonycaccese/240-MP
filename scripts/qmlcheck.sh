#!/usr/bin/env bash
# Real QML errors only.
#
# qmllint prefixes advisories with "Warning:" or "Info:"; a syntax error is
# printed bare. Grepping for the word "Error" misses them entirely — which is
# how a stray brace reached the Pi and left the sources screen unable to load.
# Run from the repo root whatever the caller's working directory is, and find
# qmllint on PATH -- it is qmllint on the Pi, qmllint-qt6 on some distributions,
# and inside the Qt prefix on macOS. QMLLINT overrides both.
cd "$(dirname "$0")/.." || exit 1
LINT=${QMLLINT:-$(command -v qmllint || command -v qmllint-qt6 || echo /usr/lib/qt6/bin/qmllint)}
if [ ! -x "$LINT" ]; then
  echo "qmllint not found; set QMLLINT to its path" >&2
  exit 2
fi

bad=0
for f in "$@"; do
  out=$("$LINT" -I modules --bare "$f" 2>&1 \
        | grep -E ':[0-9]+:[0-9]+:' | grep -vE '^(Warning|Info):')
  if [ -n "$out" ]; then bad=1; echo "=== $f ==="; echo "$out"; fi
done
[ "$bad" = 0 ] && echo "QML OK (${#} file(s))"
exit $bad
