#!/bin/sh
# Build the photo-album QPKG.
#
#   ./scripts/build_qpkg.sh
#
# Produces build/qpkg/photo-album/ (QDK source tree). If QDK's `qbuild` is on
# PATH (Linux, or on the NAS with the QDK app installed) it also builds the
# .qpkg; otherwise it creates build/photo-album-qdk.tar.gz to copy to the NAS:
#
#   scp build/photo-album-qdk.tar.gz admin@nas:/share/Public/
#   ssh admin@nas 'cd /share/Public && tar xzf photo-album-qdk.tar.gz && cd photo-album && qbuild'
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
VER=$(sed -n 's/^QPKG_VER="\(.*\)"/\1/p' qpkg/qpkg.cfg)
OUT="build/qpkg/photo-album"

echo "==> front end"
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
go run ./scripts/genicons "$OUT/icons"

if command -v qbuild >/dev/null 2>&1; then
  echo "==> qbuild"
  (cd "$OUT" && qbuild)
  ls -la "$OUT/build/"
else
  tar -C build/qpkg -czf build/photo-album-qdk.tar.gz photo-album
  echo "qbuild not found; QDK source tree packed to build/photo-album-qdk.tar.gz (version $VER)."
  echo "Run qbuild on the NAS (QDK app) — see the header of this script."
fi
