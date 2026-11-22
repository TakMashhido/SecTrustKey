#!/usr/bin/env bash
# Builds the single image that releases ship, after scripts/build.sh. Never flashes or erases.
#
# The image is written at 0x0 by esptool or M5Burner, or installed from M5 Launcher, which
# reads its partition table. It ends with the 4 KiB installer pad of stk_store: an installer
# creates the key store at full size from the table and copies only that sector. The key
# pages and filesystem that follow are never part of the image, so no install or update
# can overwrite them.
#
#   scripts/in-container.sh bash scripts/make-image.sh            # -> build/cardputer-adv/
#   scripts/in-container.sh bash scripts/make-image.sh out.bin
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build/cardputer-adv}"
OUT="${1:-${BUILD_DIR}/sectrustkey-cardputer-adv.bin}"
PAD="${BUILD_DIR}/stk-installer-pad.bin"

# The pad goes where the partition table puts stk_store.
STORE_OFFSET="$(awk -F, '$1 ~ /^stk_store *$/ {gsub(/ /, "", $4); print $4}' "${ROOT}/config/partitions.csv")"
if [ -z "${STORE_OFFSET}" ]; then
    echo "config/partitions.csv has no stk_store partition" >&2
    exit 1
fi

for part in bootloader/bootloader.bin partition_table/partition-table.bin ota_data_initial.bin sectrustkey.bin; do
    if [ ! -f "${BUILD_DIR}/${part}" ]; then
        echo "missing ${BUILD_DIR}/${part}: run scripts/build.sh first" >&2
        exit 1
    fi
done

# Readable text first: an installer treats a partition whose first bytes are all 0xFF or
# all 0x00 as empty and would then create it at a default, too small size.
python3 - "${PAD}" <<'EOF'
import sys
text = (b"SecTrustKey installer pad. Installers rewrite this sector on every install and\n"
        b"update. The keys start 4 KiB further on and are never part of a firmware image.\n")
with open(sys.argv[1], "wb") as pad:
    pad.write(text + b"\xff" * (4096 - len(text)))
EOF

esptool.py --chip esp32s3 merge_bin -o "${OUT}" \
    --flash_mode dio --flash_freq 80m --flash_size 8MB \
    0x0 "${BUILD_DIR}/bootloader/bootloader.bin" \
    0x8000 "${BUILD_DIR}/partition_table/partition-table.bin" \
    0xF000 "${BUILD_DIR}/ota_data_initial.bin" \
    0x20000 "${BUILD_DIR}/sectrustkey.bin" \
    "${STORE_OFFSET}" "${PAD}"
echo "image: ${OUT}"
