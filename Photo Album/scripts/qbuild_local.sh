#!/bin/bash
# Run QNAP's qbuild on macOS (or a Linux box without QDK installed).
#
#   scripts/qbuild_local.sh <QDK source tree dir>   # e.g. build/qpkg/photo-album
#
# Fetches qnap-dev/QDK into build/.qdk-src, compiles qpkg_encrypt, and runs a
# copy of qbuild whose hard-coded /bin and /usr/bin tool paths are redirected
# to GNU tools (macOS: brew install gnu-sed gnu-tar grep coreutils).
# The resulting .qpkg is copied to build/.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC_TREE=$(cd "$1" && pwd)
CACHE="$ROOT/build/.qdk-src"
WORK=$(mktemp -d /tmp/pa-qbuild.XXXXXX) # qbuild does not like spaces in paths
trap 'rm -rf "$WORK"' EXIT

if [ ! -d "$CACHE/shared" ]; then
  git clone -q --depth 1 https://github.com/qnap-dev/QDK.git "$CACHE"
fi

TOOL="$WORK/qdk-tool"
SHIM="$WORK/shim"
cp -R "$CACHE/shared" "$TOOL"
printf 'QDK_VERSION=2.5.3\nQDK_PATH="%s"\n' "$TOOL" > "$TOOL/qdk.conf"
mkdir -p "$SHIM"
cc -O2 -o "$SHIM/qpkg_encrypt" "$CACHE/src/qpkg_encrypt.c"

gnu() { # first existing candidate
  for c in "$@"; do [ -x "$c" ] && { echo "$c"; return; }; done
  echo "missing tool: $1" >&2; exit 1
}
B=/opt/homebrew/opt
if [ "$(uname)" = "Darwin" ]; then
  for t in cat chmod cp cut dd echo ls mkdir mv rm uname basename expr tail whoami; do
    ln -s "$(gnu "$B/coreutils/libexec/gnubin/$t" "/usr/local/opt/coreutils/libexec/gnubin/$t")" "$SHIM/$t"
  done
  ln -s "$(gnu $B/gnu-sed/libexec/gnubin/sed /usr/local/opt/gnu-sed/libexec/gnubin/sed)" "$SHIM/sed"
  ln -s "$(gnu $B/gnu-tar/libexec/gnubin/tar /usr/local/opt/gnu-tar/libexec/gnubin/tar)" "$SHIM/tar"
  ln -s "$(gnu $B/grep/libexec/gnubin/grep /usr/local/opt/grep/libexec/gnubin/grep)" "$SHIM/grep"
fi
for t in bash sh gzip gunzip bunzip2 hexdump rsync python3 cat chmod cp cut dd echo ls mkdir mv rm uname basename expr tail whoami sed tar grep; do
  [ -e "$SHIM/$t" ] || ln -s "$(command -v "$t")" "$SHIM/$t" 2>/dev/null || true
done

# Redirect /bin/X and /usr/bin/X (but not /sbin, /usr/local/bin) to the shim.
perl -pi -e "s#(?<![A-Za-z])/(?:usr/)?bin/([A-Za-z0-9_]+)#$SHIM/\$1#g" "$TOOL/bin/qbuild"

cp -R "$SRC_TREE" "$WORK/pkg"
(cd "$WORK/pkg" && PATH="$SHIM:$PATH" QDK_PATH="$TOOL" "$SHIM/bash" "$TOOL/bin/qbuild")
cp "$WORK"/pkg/build/*.qpkg "$WORK"/pkg/build/*.md5 "$ROOT/build/"
ls -la "$ROOT"/build/*.qpkg
