---
title: Getting started
description: What you need to run SecTrustKey on the M5Stack Cardputer ADV, and the three steps from a new device to a secured security key.
---

# Getting started

SecTrustKey runs on one board: the **M5Stack Cardputer ADV** (ESP32-S3, 8 MB flash, 240 × 135 screen and keyboard). Getting from a new Cardputer to a secured security key takes three steps.

<div class="stk-grid" markdown>
<div class="stk-card" markdown>
### :material-numeric-1-circle: Install
Flash the ready-made image from the latest release. [Install from a release →](install.md)
</div>
<div class="stk-card" markdown>
### :material-numeric-2-circle: Build (optional)
Build from source in the pinned ESP-IDF container, for example with your own USB identity. [Build from source →](build.md)
</div>
<div class="stk-card" markdown>
### :material-numeric-3-circle: Secure it
Change the factory PINs, set a FIDO PIN, and decide on a startup passphrase and a backup. [First steps →](first-steps.md)
</div>
</div>

## What you need

| Item | Notes |
|---|---|
| M5Stack Cardputer ADV | The original Cardputer (non-ADV) uses a different keyboard and is not supported. |
| USB-C data cable | Charge-only cables do not work. |
| A computer | Linux, macOS or Windows for FIDO2. OpenPGP and PIV have been tested on Linux. |
| `esptool` or a Chromium-based browser | To flash a release. Docker is needed only to build from source. |
| microSD card (optional) | FAT32, for encrypted backups. SecTrustKey never formats it. |

!!! danger "Flashing replaces what is on the Cardputer"
    A full install erases the whole chip, including M5 Launcher and its apps. [Back up the flash](install.md#back-up-the-cardputer) first if you may want it back.
