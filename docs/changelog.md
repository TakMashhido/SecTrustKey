---
title: Release notes
description: SecTrustKey release notes and the feature checklist of each version.
---

# Release notes

Downloads are on the [GitHub releases page](https://github.com/TakMashhido/SecTrustKey/releases).

## v1.2.1: new contact details

- [x] The About page shows **koustavmondal.com** and **koustav@koustavmondal.com**; the QR code opens the new website
- [x] Tested on a Cardputer ADV: full erase, install, FIDO2

After a fresh install that erases the device (M5Burner, or esptool with `--erase-all`), unplug and replug the Cardputer once before using GnuPG or PIV; until then the smart-card interface may not respond.

**Updating:** install `sectrustkey-v1.2.1-cardputer-adv.bin` from M5 Launcher or M5Burner, or write `sectrustkey-v1.2.1-app.bin` at `0x20000` with esptool. Your keys are kept.

## v1.2.0: clock on the device, Linux login

- [x] **Settings → Clock**: set the time over Wi-Fi or type it; on-screen codes no longer need a script on the computer
- [x] Choose a network from a scan, type a hidden one, or forget the saved one; optional **Sync at start**
- [x] **Time zone** for the status-bar clock and typed times
- [x] The time survives the restarts into backup, passphrase and clock screens
- [x] [Linux login](guide/linux-login.md): `pamu2fcfg` 1.1 (Ubuntu 24.04, Debian 12) accepts SecTrustKey; `pam://` registrations carry the device certificate
- [x] Health → Memory also shows the screen task's free stack
- [x] The website moved to **koustavmondal.com**: About page, QR code and links; these docs are now at https://koustavmondal.com/SecTrustKey/
- [x] Tested on a Cardputer ADV: FIDO2, OpenPGP, FIDO/smart-card switching under `pcscd`, `pamu2fcfg`, Wi-Fi sync, time zone

Wi-Fi runs only in the Clock restart before USB starts, and the device restarts afterwards. WPA2 and mixed WPA2/WPA3 networks work; WPA3-only networks do not. Linking the Wi-Fi code left the heap without a 32 KB block for the command task, so that block is now reserved at start-up and the screen task's stack is smaller (16 KB; about 7 KB used).

**Updating:** install `sectrustkey-v1.2.0-cardputer-adv.bin` from M5 Launcher or M5Burner, or write `sectrustkey-v1.2.0-app.bin` at `0x20000` with esptool. Your keys are kept.

## v1.1.1: startup passphrase fix

- [x] Setting, changing or removing the startup passphrase works again. The device used to restart as soon as you approved the warning, so no passphrase was saved.
- [x] Unlocking at power-up runs with enough memory for the key decryption.
- [x] Tested on a Cardputer ADV under M5 Launcher 3.0: set a passphrase, restart, unlock, Health shows the passphrase as on.

The cause: the background task that shows the progress bar had an 8 KB stack, while these key-store steps need up to about 20 KB. It now has 32 KB. Your keys were not at risk: the key store only ever replaces a record after writing and checking the new one, so an interruption leaves the old state in place.

**Updating:** install `sectrustkey-v1.1.1-cardputer-adv.bin` from M5 Launcher or M5Burner, or write `sectrustkey-v1.1.1-app.bin` at `0x20000` with esptool. Your keys are kept.

## v1.1.0: M5 Launcher and M5Burner

- [x] Install and update SecTrustKey from [M5 Launcher](getting-started/launcher.md) next to other apps
- [x] Keys survive Launcher updates, app switches and Launcher's flash compaction (tested with Launcher 3.0)
- [x] One image for esptool, the browser flasher, M5Burner and Launcher (`scripts/make-image.sh`)
- [x] New key storage partition `stk_store` with a 4 KiB installer pad; v1.0 devices keep `fido_store`
- [x] **Set up storage?** on the first start when the storage area holds another app's old data, instead of a dead end
- [x] Erasing a store that once held keys asks twice

**Upgrading from v1.0**: flash `sectrustkey-v1.1.0-app.bin` at `0x20000` as before; your keys and layout stay.

## v1.0.0 — first public release

**FIDO2 and passkeys**

- [x] CTAP 2.0 to 2.3 and U2F over USB HID
- [x] Ed25519, ES256, ES384 and ES512
- [x] Resident passkeys, credential management, FIDO PIN
- [x] `hmac-secret`, `credProtect`, `credBlob`, `largeBlobKey`, `minPinLength`
- [x] Site and account shown on screen for every registration and sign-in

**OpenPGP, PIV and OATH**

- [x] OpenPGP card with on-device key generation: Curve25519, RSA 2048 to 4096, NIST, secp256k1, Brainpool
- [x] Per-key touch policy with an on-screen OpenPGP confirm prompt
- [x] PIV with PIN, PUK and management key
- [x] OATH TOTP/HOTP and Yubico OTP
- [x] Resetting one application never touches the others

**On the device**

- [x] Five-tile home screen with Saved Keys, Codes, Health, Settings and Power Off
- [x] Live TOTP codes with a countdown; touch accounts revealed on Enter
- [x] Health check that flags factory PINs without using a try
- [x] Progress bars for every long operation, boot screen and About page

**Protecting your keys**

- [x] Optional startup passphrase sealing the master keys (PBKDF2-SHA256, AES-256-GCM)
- [x] Growing delays after wrong passphrases, never a wipe
- [x] Encrypted SD backup, verify and restore (PBKDF2 600,000 rounds, AES-256-GCM)
- [x] Power-cut safe key store and backups

**Building**

- [x] Reproducible build in the pinned ESP-IDF v5.5.3 container
- [x] Custom USB vendor ID, product ID and names at build time
- [x] Crash-dump debug build

**Known limitations**

- Secure Boot and flash encryption are not enabled; see the [security model](concepts/security.md).
- The clock must be set after each power-up for on-screen codes.
- OpenPGP and PIV have been tested on Linux only.
