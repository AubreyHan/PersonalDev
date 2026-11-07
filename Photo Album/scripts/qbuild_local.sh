#!/bin/bash
# Run QNAP's qbuild on macOS (or a Linux box without QDK installed).
#
#   scripts/qbuild_local.sh <QDK source tree dir>   # e.g. build/qpkg/photo-album
#
# Fetches qnap-dev/QDK into build/.qdk-src, compiles qpkg_encrypt, and runs a
# copy of qbuild whose hard-coded /bin/X and /usr/bin/X tool paths are
# redirected to <shim>/bin/X and <shim>/usr/bin/X (GNU tools on macOS:
# brew install gnu-sed gnu-tar grep coreutils). The installer header that
# qbuild writes into the package is restored to the original /bin, /usr/bin
# paths before its length is computed, so the .qpkg is identical in form to
# one built on Linux. The result is copied to build/.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC_TREE=$(cd "$1" && pwd)
CACHE="$ROOT/build/.qdk-src"
WORK=$(mktemp -d /tmp/paqb.XXXXXX) # qbuild does not like spaces in paths
trap 'rm -rf "$WORK"' EXIT

if [ ! -d "$CACHE/shared" ]; then
  git clone -q --depth 1 https://github.com/qnap-dev/QDK.git "$CACHE"
fi

TOOL="$WORK/qdk-tool"
R="$WORK/r" # shim root: $R/bin, $R/usr/bin
cp -R "$CACHE/shared" "$TOOL"
printf 'QDK_VERSION=2.5.3\nQDK_PATH="%s"\n' "$TOOL" > "$TOOL/qdk.conf"
mkdir -p "$R/bin" "$R/usr/bin" "$WORK/path"
cc -O2 -o "$WORK/path/qpkg_encrypt" "$CACHE/src/qpkg_encrypt.c"

pick() { # first existing executable
  for c in "$@"; do [ -x "$c" ] && { echo "$c"; return; }; done
  echo "missing tool: $1 (macOS: brew install gnu-sed gnu-tar grep coreutils)" >&2
  exit 1
}
link() { ln -sf "$2" "$R/bin/$1"; ln -sf "$2" "$R/usr/bin/$1"; }

if [ "$(uname)" = "Darwin" ]; then
  for b in /opt/homebrew/opt /usr/local/opt; do [ -d "$b/coreutils" ] && BREW=$b; done
  : "${BREW:?coreutils not found (brew install gnu-sed gnu-tar grep coreutils)}"
  for t in cat chmod cp cut dd echo ls mkdir mv rm uname basename expr tail whoami; do
    link "$t" "$(pick "$BREW/coreutils/libexec/gnubin/$t")"
  done
  link sed "$(pick "$BREW/gnu-sed/libexec/gnubin/sed")"
  link tar "$(pick "$BREW/gnu-tar/libexec/gnubin/tar")"
  link grep "$(pick "$BREW/grep/libexec/gnubin/grep")"
fi
for t in bash sh gzip gunzip bunzip2 hexdump rsync python python2 python3 cat chmod cp cut dd echo ls mkdir mv rm uname basename expr tail whoami sed tar grep; do
  [ -e "$R/bin/$t" ] && continue
  p=$(command -v "$t" 2>/dev/null) || continue
  link "$t" "$p"
done

Q="$TOOL/bin/qbuild"
# 1. Redirect /bin/X and /usr/bin/X (not /sbin, /usr/local/bin, ...).
perl -pi -e "s#(?<![A-Za-z0-9_])/((?:usr/)?bin/[A-Za-z0-9_]+)#$R/\$1#g" "$Q"
# 2. Restore the original paths in the generated installer header before
#    qbuild measures its length.
perl -pi -e "print \"\t$R/bin/sed -i 's#$R##g' \\\$QDK_QPKG_FILE\n\" if /local script_len=\\\$\\(\\(\\\$\\(/" "$Q"
grep -q "sed -i 's#$R##g'" "$Q" || { echo "failed to patch qbuild" >&2; exit 1; }

cp -R "$SRC_TREE" "$WORK/pkg"
(cd "$WORK/pkg" && PATH="$WORK/path:$R/bin:$PATH" QDK_PATH="$TOOL" "$R/bin/bash" "$Q")

QPKG=$(ls "$WORK"/pkg/build/*.qpkg)
if LC_ALL=C grep -a -q "$R" "$QPKG"; then
  echo "error: build paths leaked into $QPKG" >&2
  exit 1
fi
cp "$QPKG" "$QPKG.md5" "$ROOT/build/" 2>/dev/null || cp "$QPKG" "$ROOT/build/"
ls -la "$ROOT/build/$(basename "$QPKG")"
