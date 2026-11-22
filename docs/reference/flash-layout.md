---
title: Flash layout
description: SecTrustKey partition table for the 8 MB ESP32-S3 flash on the Cardputer ADV — offsets, sizes and what a full install and an update write.
---

# Flash layout

![ESP32-S3 flash layout](../assets/diagrams/flash-layout.png){ .stk-diagram }

| Region | Offset | Size | Contents |
|---|---|---|---|
| Bootloader | `0x0` | 32 KiB | Second-stage bootloader |
| Partition table | `0x8000` | 4 KiB | This table |
| `nvs` | `0x9000` | 24 KiB | Settings (volume, brightness, timeout) |
| `otadata` | `0xF000` | 8 KiB | Boot slot selection |
| `app0` (OTA slot 0) | `0x20000` | 3 MiB | `sectrustkey.bin` |
| `stk_store` (data, SPIFFS type) | `0x320000` | 1 MiB + 12 KiB | 4 KiB installer pad, two 4 KiB key-store pages, then the 1 MiB file store |
| `coredump` (debug only) | `0x430000` | 64 KiB | Crash dump |

The contents of `stk_store` are not a SPIFFS volume; the type only lets installers such as M5 Launcher create and keep the partition. Under Launcher, the offsets of `stk_store` and the app follow Launcher's own layout, and SecTrustKey finds its storage by name.

| Operation | Writes |
|---|---|
| Full install | Everything up to the installer pad; `--erase-all` clears the rest |
| esptool update | Only `0x20000`; the key store is kept |
| M5 Launcher install or update | The app, plus only the 4 KiB installer pad of `stk_store` |
| SD restore | Only the key pages and file store |

## Devices installed with v1.0

v1.0 used a custom-type partition, `fido_store` (type `0x40`, subtype `0x01`, `0x102000` bytes at `0x320000`), with the key pages at its very start. Newer firmware still recognises it and prefers it when present, so a v1.0 device updated with esptool keeps its keys and layout.
