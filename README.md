# SecTrustKey: FIDO2 passkey, OpenPGP and PIV security key for the M5Stack Cardputer ADV

**SecTrustKey** is open-source firmware that turns an **M5Stack Cardputer ADV** (ESP32-S3)
into a **hardware security key with its own screen and keyboard**. It works as a **FIDO2 /
WebAuthn passkey** and U2F key, an **OpenPGP smart card** for GnuPG, SSH and Git signing, a
**PIV** card, and an authenticator that shows **TOTP one-time codes** on the display. Every
request is shown on the screen and approved with `Enter` or refused with `Del`.

**Documentation:** [koustav.isroot.in/SecTrustKey](https://koustav.isroot.in/SecTrustKey/)

## Features

- **Passkeys and security-key login**: FIDO2 (CTAP 2.0 to 2.3) and U2F with EdDSA (Ed25519),
  ES256, ES384 and ES512; resident credentials, credential management, `hmac-secret`,
  `credProtect`, large blobs.
- **OpenPGP card**: on-device key generation for RSA, Ed25519/X25519, NIST P-256/384/521,
  secp256k1 and Brainpool; works with `gpg`, `ssh` and Git commit signing.
- **PIV card**, **OATH** (TOTP/HOTP) and Yubico-compatible OTP.
- **Codes on screen**: live TOTP codes with a countdown; touch-protected accounts stay hidden
  until you press `Enter`.
- **Startup passphrase**: encrypts the master keys at rest (PBKDF2-HMAC-SHA256, AES-256-GCM);
  wrong tries only add delays and never erase anything.
- **Encrypted microSD backup**, verify and restore, kept in a `SecTrustKey` folder.
- **Security health check** that flags factory PINs, a missing FIDO PIN or passphrase, and no
  backup.
- **Configurable USB identity**: vendor ID, product ID and names at build time.

## Build

Everything runs in the pinned ESP-IDF container; only Docker is needed.

```sh
git clone https://github.com/TakMashhido/SecTrustKey.git
cd SecTrustKey
scripts/in-container.sh bash scripts/build.sh
```

The firmware is written to `build/cardputer-adv/`. Flashing, the full-flash layout, custom
USB IDs and every feature are explained in the
[documentation](https://koustav.isroot.in/SecTrustKey/).

## Security

The ESP32-S3 is a microcontroller, not a secure element, and Secure Boot and flash
encryption are deliberately not enabled. Without a startup passphrase, anyone who can read the
chip's flash can copy the keys. Read the
[security model](https://koustav.isroot.in/SecTrustKey/security.html) before relying on it.

## License

Copyright (c) 2026 Koustav Kumar Mondal. Licensed under the
[GNU Affero General Public License v3.0](LICENSE). See [NOTICE.md](NOTICE.md) for the
software this firmware incorporates and what was changed.

## Credits

SecTrustKey builds on [Pico FIDO](https://github.com/polhenarejos/pico-fido),
[Pico OpenPGP](https://github.com/polhenarejos/pico-openpgp) and the
[Pico Keys SDK](https://github.com/polhenarejos/pico-keys-sdk) by Pol Henarejos, and on the
[Cardputer ADV port](https://github.com/lessucettes/cardputer-fido-adv) by lessucettes.
