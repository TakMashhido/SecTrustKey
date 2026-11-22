---
title: Build from source
description: Build SecTrustKey firmware from source with ESP-IDF v5.5.3 in Docker, with optional custom USB identity and a crash-dump debug image.
---

# Build from source

The build runs inside the pinned `espressif/idf:v5.5.3` container, so the only thing your computer needs is **Docker**.

```bash
git clone https://github.com/TakMashhido/SecTrustKey.git
cd SecTrustKey
scripts/in-container.sh bash scripts/build.sh
```

The first build downloads ESP-IDF components and the pinned Mbed TLS and TinyCBOR sources, so it needs network access. A clean build takes a few minutes.

## Output

Everything is written to `build/cardputer-adv/`:

| File | Address | Contents |
|---|---|---|
| `bootloader/bootloader.bin` | `0x0` | second-stage bootloader |
| `partition_table/partition-table.bin` | `0x8000` | partition table |
| `ota_data_initial.bin` | `0xF000` | boot slot selection |
| `sectrustkey.bin` | `0x20000` | the firmware |

Flash settings are DIO, 80 MHz, 8 MB. Write all four for a first install:

```bash
docker run --rm --device=/dev/ttyACM0 -v "$PWD/build/cardputer-adv":/fw espressif/idf:v5.5.3 \
  bash -c 'cd /fw && esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
    --after watchdog_reset write_flash --erase-all \
    --flash_mode dio --flash_freq 80m --flash_size 8MB \
    0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin \
    0xF000 ota_data_initial.bin 0x20000 sectrustkey.bin'
```

For an update, write only `0x20000 sectrustkey.bin` and leave out `--erase-all`.

## One image for `0x0`

To produce the single image that releases ship, for esptool, M5Burner and M5 Launcher:

```bash
scripts/in-container.sh bash scripts/make-image.sh
# -> build/cardputer-adv/sectrustkey-cardputer-adv.bin
```

It merges the bootloader, partition table, boot data and firmware, and ends with the 4 KB installer pad of the key storage partition `stk_store`. Installers such as M5 Launcher create the partition from the table and copy only that pad, so the keys after it are never part of an image. See [M5 Launcher and M5Burner](launcher.md#how-the-storage-survives).

## Build options

Extra CMake options are passed straight through `build.sh`:

```bash
scripts/in-container.sh bash scripts/build.sh \
  -DUSB_VID=0x1209 -DUSB_PID=0x0001 "-DUSB_MANUFACTURER=Example Labs" "-DUSB_PRODUCT=Example Key"
```

All options are listed in [Build options](../reference/build-options.md); the USB identity is explained in [USB identity](../customize/usb-identity.md), and the crash-dump image in [Debug build](../customize/debug-build.md).

## Build number

The firmware shows a build number (for example `#2`) under **Health** and **About**. It is the number of commits in the git history, and `0` when the source is not a git checkout (a downloaded archive, for instance).
