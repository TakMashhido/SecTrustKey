#!/usr/bin/env bash
# Reproducible Cardputer ADV build entrypoint. Never flashes or erases.
# Extra CMake options are passed through, e.g. -DUSB_VID=0x1209 -DUSB_PID=0x0001.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${IDF_PATH:?IDF_PATH must point at the pinned ESP-IDF installation}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build/cardputer-adv}"
DEFAULTS="${ROOT}/config/sdkconfig.defaults"
# STK_DEBUG_CRASHDUMP=1 builds a debug image that keeps a crash dump (see config/sdkconfig.debug).
if [ "${STK_DEBUG_CRASHDUMP:-0}" = "1" ]; then
    BUILD_DIR="${ROOT}/build/cardputer-adv-debug"
    DEFAULTS="${DEFAULTS};${ROOT}/config/sdkconfig.debug"
fi

mkdir -p "${BUILD_DIR}"

exec python3 "${IDF_PATH}/tools/idf.py" \
    -B "${BUILD_DIR}" \
    -DIDF_TARGET=esp32s3 \
    -DCARDPUTER_ADV=ON \
    -DENABLE_EDDSA=ON \
    -DSDKCONFIG="${BUILD_DIR}/sdkconfig" \
    -DSDKCONFIG_DEFAULTS="${DEFAULTS}" \
    "$@" \
    build
