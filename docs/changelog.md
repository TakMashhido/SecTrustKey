---
title: Release notes
description: SecTrustKey release notes and the feature checklist of each version.
---

# Release notes

Downloads are on the [GitHub releases page](https://github.com/TakMashhido/SecTrustKey/releases).

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
