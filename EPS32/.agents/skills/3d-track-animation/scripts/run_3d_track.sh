#!/usr/bin/env bash
set -euo pipefail

SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_DIR="$(cd "${SCRIPT_PATH}/.." && pwd)"
SCRIPTS_DIR="${SKILL_DIR}/scripts"
RES_DIR="${SKILL_DIR}/resources"
BIN_DIR="${SCRIPTS_DIR}/bin"

mkdir -p "${BIN_DIR}"

OS_TYPE="$(uname -s)"

sha256_file() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    elif command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        python3 -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$1"
    fi
}

if [[ "${OS_TYPE}" == "Darwin" ]]; then
    BIN_PATH="${BIN_DIR}/render_3d_track"
    SRC_FILE="${SCRIPTS_DIR}/render_3d_track.mm"
    HASH_FILE="${BIN_DIR}/render_3d_track.mm.sha256"
    CUR_HASH="$(sha256_file "${SRC_FILE}")"
    SAVED_HASH=""
    if [[ -f "${HASH_FILE}" ]]; then
        SAVED_HASH="$(cat "${HASH_FILE}")"
    fi

    if [[ ! -x "${BIN_PATH}" || "${SRC_FILE}" -nt "${BIN_PATH}" || "${CUR_HASH}" != "${SAVED_HASH}" ]]; then
        echo "Compiling macOS renderer: render_3d_track.mm -> ${BIN_PATH}..."
        clang++ -O3 -std=c++17 -fobjc-arc \
            -framework Foundation \
            -framework Cocoa \
            -framework CoreGraphics \
            -framework CoreText \
            -framework ImageIO \
            -framework AVFoundation \
            -framework CoreVideo \
            -framework CoreMedia \
            "${SRC_FILE}" \
            -o "${BIN_PATH}"
        echo "${CUR_HASH}" > "${HASH_FILE}"
    fi
else
    BIN_PATH="${BIN_DIR}/render_3d_track_linux"
    SRC_FILE="${SCRIPTS_DIR}/render_3d_track_linux.cpp"
    HASH_FILE="${BIN_DIR}/render_3d_track_linux.cpp.sha256"
    CUR_HASH="$(sha256_file "${SRC_FILE}")"
    SAVED_HASH=""
    if [[ -f "${HASH_FILE}" ]]; then
        SAVED_HASH="$(cat "${HASH_FILE}")"
    fi

    CXX_BIN="g++"
    if ! command -v g++ >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
        CXX_BIN="clang++"
    fi

    if [[ ! -x "${BIN_PATH}" || "${SRC_FILE}" -nt "${BIN_PATH}" || "${CUR_HASH}" != "${SAVED_HASH}" ]]; then
        echo "Compiling Linux renderer: render_3d_track_linux.cpp -> ${BIN_PATH}..."
        "${CXX_BIN}" -O3 -std=c++17 -pthread "${SRC_FILE}" -o "${BIN_PATH}"
        echo "${CUR_HASH}" > "${HASH_FILE}"
    fi
fi

if [[ "${1:-}" == "--compile-only" ]]; then
    echo "Renderer binary ready (${OS_TYPE}): ${BIN_PATH}"
    exit 0
fi

JSON_PATH="${1:?Usage: run_3d_track.sh <track_data.json> [output_prefix]}"
OUT_ARG="${2:-}"

TRACK_NAME="$(python3 -c "import json,sys; d=json.load(open(sys.argv[1],'r',encoding='utf-8')); print(d.get('name','3D运动轨迹').strip() or '3D运动轨迹')" "${JSON_PATH}")"
CLEANUP_GPX_DIR="$(python3 -c "import json,sys; d=json.load(open(sys.argv[1],'r',encoding='utf-8')); print(d.get('cleanup_gpx_dir','').strip())" "${JSON_PATH}")"

if [[ -z "${OUT_ARG}" ]]; then
    OUT_DIR="$(pwd)/output"
    mkdir -p "${OUT_DIR}"
    OUT_PREFIX="${OUT_DIR}/${TRACK_NAME}"
elif [[ -d "${OUT_ARG}" ]]; then
    OUT_DIR="${OUT_ARG}"
    mkdir -p "${OUT_DIR}"
    OUT_PREFIX="${OUT_DIR}/${TRACK_NAME}"
else
    PARENT_DIR="$(dirname "${OUT_ARG}")"
    BASE_NAME="$(basename "${OUT_ARG}")"
    if [[ "$(basename "${PARENT_DIR}")" == "output" ]]; then
        OUT_DIR="${PARENT_DIR}"
    else
        OUT_DIR="${PARENT_DIR}/output"
    fi
    mkdir -p "${OUT_DIR}"
    OUT_PREFIX="${OUT_DIR}/${BASE_NAME}"
fi

OUT_16X9="${OUT_PREFIX}_16x9.mp4"
OUT_9X16="${OUT_PREFIX}_9x16.mp4"

if [[ "${OS_TYPE}" == "Darwin" ]]; then
    echo "Rendering 16:9 (1280x720) -> ${OUT_16X9}"
    "${BIN_PATH}" --json "${JSON_PATH}" --res-dir "${RES_DIR}" --out "${OUT_16X9}" --16x9

    echo "Rendering 9:16 (720x1280) -> ${OUT_9X16}"
    "${BIN_PATH}" --json "${JSON_PATH}" --res-dir "${RES_DIR}" --out "${OUT_9X16}" --9x16
else
    WORK_DIR="$(dirname "${JSON_PATH}")/linux_assets"
    echo "Preparing Linux raw terrain & HUD assets in ${WORK_DIR}..."
    python3 "${SCRIPTS_DIR}/prepare_linux_assets.py" \
        --json "${JSON_PATH}" \
        --res-dir "${RES_DIR}" \
        --work-dir "${WORK_DIR}"

    echo "Rendering 16:9 (1280x720) -> ${OUT_16X9}"
    "${BIN_PATH}" --assets "${WORK_DIR}" --out "${OUT_16X9}" --16x9

    echo "Rendering 9:16 (720x1280) -> ${OUT_9X16}"
    "${BIN_PATH}" --assets "${WORK_DIR}" --out "${OUT_9X16}" --9x16
fi

if [[ -s "${OUT_16X9}" && -s "${OUT_9X16}" ]]; then
    if [[ -n "${CLEANUP_GPX_DIR}" && -d "${CLEANUP_GPX_DIR}" ]]; then
        echo "Video generation complete; clearing GPX directory: ${CLEANUP_GPX_DIR}"
        find "${CLEANUP_GPX_DIR}" -mindepth 1 -delete
    fi
fi

echo "Done! Generated in output directory:"
ls -lh "${OUT_16X9}" "${OUT_9X16}"
