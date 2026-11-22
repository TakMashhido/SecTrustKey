---
title: Passkeys and FIDO2
description: Use SecTrustKey as a FIDO2 passkey and U2F security key — register on websites, sign in with on-screen confirmation, set a FIDO PIN, manage stored passkeys and reset FIDO.
---

# Passkeys and FIDO2

SecTrustKey is a FIDO2 (CTAP 2.0 to 2.3) and U2F authenticator. Browsers on Linux, macOS and Windows find it as a USB security key; no driver is needed.

| Supported | |
|---|---|
| Algorithms | Ed25519 (EdDSA), ES256 (P-256), ES384, ES512 |
| Credentials | Resident (passkeys) and non-resident; credential management |
| Extensions | `hmac-secret`, `credProtect`, `credBlob`, `largeBlobKey`, `minPinLength` |
| User verification | FIDO PIN, typed on the computer |

## Register a key or create a passkey

1. In the website's security settings, choose **Add a security key** or **Create a passkey**, and pick the USB security key when the browser asks.
2. The device shows **Create passkey?** with the site, for example `github.com`, and the user name.
3. Press ++enter++ to approve.

<div class="stk-screens" markdown>
<figure markdown>
![Create passkey prompt](../assets/screens/prompt-create.png)
<figcaption>Registering</figcaption>
</figure>
<figure markdown>
![Sign in prompt](../assets/screens/prompt-signin.png)
<figcaption>Signing in</figcaption>
</figure>
</div>

Signing in works the same way: the device shows **Sign in?** with the site and account.

## Set a FIDO PIN

A PIN means a stolen device alone is not enough to sign in. Set it from any of:

=== "Chrome / Edge"

    **Settings › Privacy and security › Security › Manage security keys › Create a PIN**.

=== "Windows"

    **Settings › Accounts › Sign-in options › Security key › Manage**.

=== "macOS / Linux (libfido2)"

    ```bash
    fido2-token -L                   # find the device, for example /dev/hidraw4
    fido2-token -S /dev/hidraw4      # set a PIN
    fido2-token -C /dev/hidraw4      # change it later
    ```

After 8 wrong PINs, FIDO is blocked until it is [reset](#reset-fido). After 3 wrong PINs in a row, unplug and replug the device to try again.

## Passkeys on the device

**Saved Keys** lists the passkeys stored on the device. Open one to see its account; press ++enter++ on it to delete it.

<figure class="stk-screen-one" markdown>
![Saved keys list](../assets/screens/saved-keys.png)
</figure>

## Reset FIDO

A FIDO reset erases every passkey and the FIDO PIN. It does **not** touch OpenPGP, PIV or OATH data.

- It is accepted only in the first **10 seconds** after the device is plugged in.
- It needs two separate presses of ++enter++ on the red **FIDO RESET** prompt.

Start it from Chrome's **Manage security keys › Reset security key**, Windows' **Reset**, or `fido2-token -R /dev/hidrawN`.
