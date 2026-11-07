#!/bin/sh
# Build the photo-album QPKG:  ./scripts/build_qpkg.sh
#
# Output: build/photo-album_<ver>_x86_64.qpkg
# Uses QDK's `qbuild` if it is on PATH; otherwise runs the official QDK
# qbuild locally via scripts/qbuild_local.sh (macOS needs:
# brew install gnu-sed gnu-tar grep coreutils).
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
VER=$(sed -n 's/^QPKG_VER="\(.*\)"/\1/p' qpkg/qpkg.cfg)
OUT="build/qpkg/photo-album"

echo "==> front end"
mkdir -p web/public
go run ./scripts/genicons assets/icon.png build/.icons web/public/favicon.png
(cd web && npm ci --no-audit --no-fund && npm run build)
touch web/dist/.gitkeep

echo "==> tests"
go test ./...

echo "==> binary (linux/amd64, static)"
rm -rf "$OUT"
mkdir -p "$OUT/x86_64/bin" "$OUT/shared" "$OUT/icons"
CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -trimpath -ldflags "-s -w" \
  -o "$OUT/x86_64/bin/photo-album-server" ./cmd/photo-album-server

echo "==> QDK tree"
cp qpkg/qpkg.cfg qpkg/package_routines "$OUT/"
cp qpkg/shared/photo-album.sh "$OUT/shared/"
chmod 755 "$OUT/shared/photo-album.sh" "$OUT/x86_64/bin/photo-album-server"
go run ./scripts/genicons assets/icon.png "$OUT/icons"

if command -v qbuild >/dev/null 2>&1; then
  echo "==> qbuild"
  (cd "$OUT" && qbuild)
  cp "$OUT"/build/*.qpkg build/
else
  echo "==> qbuild (local QDK, scripts/qbuild_local.sh)"
  scripts/qbuild_local.sh "$OUT"
fi
echo "Done: build/photo-album_${VER}_x86_64.qpkg"
