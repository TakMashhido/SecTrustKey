#!/usr/bin/env bash
# Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
# Run a command inside the pinned ESP-IDF toolchain container, so nothing needs installing on
# the host. The repository is mounted at /work and the command runs there as your own user.
#
#   scripts/in-container.sh bash scripts/build.sh
#   scripts/in-container.sh env STK_DEBUG_CRASHDUMP=1 bash scripts/build.sh   # debug image
#
# This never touches a USB device: no --device is passed to the container.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${STK_IDF_IMAGE:-espressif/idf:v5.5.3}"

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <command> [args...]" >&2
    exit 2
fi

exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "${ROOT}":/work -w /work "${IMAGE}" \
    bash -c '. /opt/esp/idf/export.sh >/dev/null 2>&1
             export IDF_PATH=/opt/esp/idf
             exec "$@"' _ "$@"
