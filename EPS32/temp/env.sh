#!/usr/bin/env zsh
# Source this file to activate the local ESP32-S3 ESP-IDF environment:
#   source ./env.sh
# ESP-IDF and its tools live one level up (repo root), shared with the project in this folder.

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"
export IDF_PATH="${ROOT_DIR}/.esp-idf"
export IDF_TOOLS_PATH="${ROOT_DIR}/.espressif"
if [ -r "/dev/cu.usbmodem101" ]; then
    export ESPPORT="/dev/cu.usbmodem101"
fi

if [ -f "${IDF_PATH}/export.sh" ]; then
    source "${IDF_PATH}/export.sh"
else
    echo "Error: ESP-IDF not found at ${IDF_PATH}"
    return 1 2>/dev/null || exit 1
fi
