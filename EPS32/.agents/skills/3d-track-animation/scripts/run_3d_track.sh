#!/usr/bin/env zsh
set -euo pipefail

SKILL_DIR="${0:A:h:h}"
SCRIPTS_DIR="${SKILL_DIR}/scripts"
RES_DIR="${SKILL_DIR}/resources"
BIN_DIR="${SCRIPTS_DIR}/bin"
BIN_PATH="${BIN_DIR}/render_3d_track"

mkdir -p "${BIN_DIR}"

if [[ ! -x "${BIN_PATH}" || "${SCRIPTS_DIR}/render_3d_track.mm" -nt "${BIN_PATH}" ]]; then
    echo "Compiling render_3d_track.mm -> ${BIN_PATH}..."
    clang++ -O3 -std=c++17 -fobjc-arc \
        -framework Foundation \
        -framework Cocoa \
        -framework CoreGraphics \
        -framework CoreText \
        -framework ImageIO \
        -framework AVFoundation \
        -framework CoreVideo \
        -framework CoreMedia \
        "${SCRIPTS_DIR}/render_3d_track.mm" \
        -o "${BIN_PATH}"
fi

if [[ "${1:-}" == "--compile-only" ]]; then
    echo "Renderer binary ready: ${BIN_PATH}"
    exit 0
fi

JSON_PATH="${1:?Usage: run_3d_track.sh <track_data.json> <output_prefix>}"
OUT_PREFIX="${2:?Usage: run_3d_track.sh <track_data.json> <output_prefix>}"

OUT_16X9="${OUT_PREFIX}_16x9.mp4"
OUT_9X16="${OUT_PREFIX}_9x16.mp4"

echo "Rendering 16:9 (1280x720) -> ${OUT_16X9}"
"${BIN_PATH}" --json "${JSON_PATH}" --res-dir "${RES_DIR}" --out "${OUT_16X9}" --16x9

echo "Rendering 9:16 (720x1280) -> ${OUT_9X16}"
"${BIN_PATH}" --json "${JSON_PATH}" --res-dir "${RES_DIR}" --out "${OUT_9X16}" --9x16

echo "Done! Generated:"
ls -lh "${OUT_16X9}" "${OUT_9X16}"
