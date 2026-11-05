#!/usr/bin/env bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_DIR="${PROJECT_ROOT}/src-tauri/target/release/bundle"
RELEASE_DIR="${PROJECT_ROOT}/release"

mkdir -p "${RELEASE_DIR}"

# 复制 .app
if [ -d "${TARGET_DIR}/macos" ]; then
    find "${TARGET_DIR}/macos" -maxdepth 1 -name "*.app" -exec cp -R {} "${RELEASE_DIR}/" \;
fi

# 复制 .dmg
if [ -d "${TARGET_DIR}/dmg" ]; then
    find "${TARGET_DIR}/dmg" -maxdepth 1 -name "*.dmg" -exec cp -f {} "${RELEASE_DIR}/" \;
fi

echo "Release artifacts copied to: ${RELEASE_DIR}"
ls -lh "${RELEASE_DIR}"
