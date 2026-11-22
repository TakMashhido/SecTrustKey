---
title: Build options
description: All SecTrustKey build options — USB_VID, USB_PID, USB_MANUFACTURER, USB_PRODUCT, ENABLE_EDDSA, STK_DEBUG_CRASHDUMP and BUILD_DIR.
---

# Build options

CMake options are appended to `scripts/build.sh`; environment variables go before it (`env NAME=value`).

| Option | Kind | Default | Effect |
|---|---|---|---|
| `-DUSB_VID=` | CMake | `0x2E8A` | USB vendor ID |
| `-DUSB_PID=` | CMake | `0x10FE` | USB product ID |
| `-DUSB_MANUFACTURER=` | CMake | `Koustav Kumar Mondal` | USB manufacturer string |
| `-DUSB_PRODUCT=` | CMake | `SecTrustKey` | USB product string; also the reader name |
| `-DENABLE_EDDSA=` | CMake | `ON` | Ed25519/Ed448 for FIDO2 and OpenPGP |
| `STK_DEBUG_CRASHDUMP=1` | environment | off | Crash-dump [debug image](../customize/debug-build.md) in `build/cardputer-adv-debug` |
| `BUILD_DIR=` | environment | `build/cardputer-adv` | Output folder (a path inside the container, under `/work`) |

Example combining several:

```bash
scripts/in-container.sh env BUILD_DIR=/work/build/mykey bash scripts/build.sh \
  -DUSB_VID=0x1209 -DUSB_PID=0x0001 "-DUSB_PRODUCT=My Key"
```
