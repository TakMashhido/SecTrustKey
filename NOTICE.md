# Notice

**SecTrustKey** is Copyright (c) 2026 Koustav Kumar Mondal and is licensed under the GNU
Affero General Public License, version 3 (see [`LICENSE`](LICENSE)).

SecTrustKey incorporates and modifies free software written by others. Their copyright and
licence notices are kept intact in every source file that carries one, and their licence
texts are in [`licenses/`](licenses/) and [`platform/LICENSE`](platform/LICENSE). This
file lists that software, states that it was modified, and records what was changed.

## Software incorporated

| Component | Author | Licence | Used for |
|---|---|---|---|
| Cardputer ADV port of Pico FIDO, [`cardputer-fido-adv`](https://github.com/lessucettes/cardputer-fido-adv) (revision `4a46976`) | lessucettes | AGPL-3.0 | the starting point of this repository: board support, display and keyboard UI, key store, presence prompts, encrypted SD backup |
| [Pico FIDO](https://github.com/polhenarejos/pico-fido) (revision `99337e1`) | Pol Henarejos | AGPL-3.0 | FIDO2 / U2F, OATH, Yubico OTP, device management (`apps/fido/`) |
| [Pico Keys SDK](https://github.com/polhenarejos/pico-keys-sdk) (revision `d8fce97`) | Pol Henarejos | AGPL-3.0 | USB and APDU transport, flash filesystem, object stores, vault (`platform/`) |
| [Pico OpenPGP](https://github.com/polhenarejos/pico-openpgp) v5.2 (revision `9b23276`) | Pol Henarejos | AGPL-3.0 | OpenPGP card and PIV (`apps/openpgp/`, `apps/piv/`) |
| Keyboard reader sources | M5Stack Technology CO LTD | MIT | `board/keyboard/` |
| Adafruit TCA8418 driver | Limor Fried, Adafruit Industries | BSD (3-clause), [`licenses/Adafruit-TCA8418-BSD.txt`](licenses/Adafruit-TCA8418-BSD.txt) | `board/Adafruit_TCA8418/` |

Fetched at build time and not part of this repository: Mbed TLS (Apache-2.0 or
GPL-2.0-or-later), TinyCBOR (MIT), ESP-IDF (Apache-2.0), `esp_tinyusb` (Apache-2.0),
TinyUSB (MIT), M5Unified and M5GFX (MIT), and the `zorxx/neopixel` component (MIT). The
EdDSA (Ed25519) build, which is the default, fetches a patched Mbed TLS from
`polhenarejos/mbedtls` (`platform/cmake/deps.cmake`).

## Modifications

The software above was modified by Koustav Kumar Mondal in 2026. In summary:

- **Combined into one firmware.** FIDO2/U2F/OATH/OTP, OpenPGP and PIV run together on one
  USB device and one file store. The two upstream file tables are merged into
  `apps/fido/files.c`, together with the list of OpenPGP/PIV-owned files.
- **Isolation between applications.** OpenPGP key containers moved from FID prefixes
  `0xD0–0xD5` to `0xF0–0xF5`; the SDK's shared key-metadata file `EF_META` moved from
  `0xE010` to `0xF610`; OpenPGP TERMINATE DF and PIV RESET are limited to their own
  application's files (`apps/openpgp/stk_isolation.c`); PIV does not register the Yubico OTP
  AID alias when the OTP application is built.
- **OpenPGP confirmation.** OpenPGP "touch required" now uses the on-screen confirmation,
  with new prompt kinds and an `OPENPGP CONFIRM` header. The upstream request was approved
  immediately with no prompt on this board.
- **Version headers.** The OpenPGP version header was renamed and given its own include
  guard; the PIV `cmd_version` handler was renamed to avoid a symbol clash.
- **Identity.** New AAGUID (first 16 bytes of SHA-256 of `"SecTrustKey FIDO2"`); USB
  manufacturer and product strings; certificate subject names; PIV applet label; on-screen
  title; package name; the WebUSB landing page was removed.
- **Renames and removals.** The tree was restructured; identifiers and files carrying the
  old project prefixes were renamed. Removed because this firmware does not use them:
  RP2040/RP2350 support and its LED drivers, the Windows, macOS and Linux-TPM key backends,
  the network-over-USB interface and its REST server, the CVC library, the macOS packaging
  helpers, and the upstream conformance harness and shell wrappers. The optional
  post-quantum code (ML-KEM, ML-DSA) is still present but disabled.
- **Cardputer ADV features.** On-device user interface, on-screen TOTP codes, optional
  startup passphrase that encrypts the master keys at rest, security health dashboard, SD
  card backup tools, and fixes for concurrency and storage defects found on hardware.
- **Clock and Linux login (v1.2.0).** The clock can be set on the device over Wi-Fi or by
  hand; the SDK's `aes_encrypt`/`aes_decrypt` are linked as `stk_aes_encrypt`/`stk_aes_decrypt`
  so they cannot meet the Wi-Fi supplicant's functions of the same name; core1's command
  stack is reserved during boot and core0's stack is smaller. Registrations for `pam://`
  relying parties return the device attestation certificate instead of self attestation.

Each modified file is a derivative work; the notices it carried before are unchanged.

## Identifiers deliberately kept

Some names that look like another project's are protocol, cryptographic or on-flash
identifiers. Changing them would silently change behaviour, so they were kept:

- the vault's cryptographic domain-separation labels in `platform/src/vault.c`
  (`"PicoKeys Vault …"`): changing them changes the keys derived for vault enrollment;
- the numeric product and MCU identifiers reported by the management and rescue
  applications;
- the on-flash and backup format markers (`CFKEY001` and the backup file markers).

## Before you distribute hardware running this firmware

- **USB vendor and product ID.** The firmware currently identifies as `2E8A:10FE`, a
  vendor ID that belongs to Raspberry Pi Trading and a product ID that was allocated to
  another project. This project has no ID of its own. Obtain one (for example from
  [pid.codes](https://pid.codes) for open hardware, or a USB-IF vendor ID) and build with
  `-DUSB_VID=... -DUSB_PID=...`. Do this together with hardware testing: hosts and `pcscd`
  recognise smart-card readers partly by these IDs.
- **FIDO attestation.** The authenticator uses self attestation and the AAGUID above. It has
  not been submitted for FIDO certification and is not in any metadata service.
