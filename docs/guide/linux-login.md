---
title: Linux login (pam_u2f)
description: Use SecTrustKey to log in to Linux and approve sudo with pam_u2f and pamu2fcfg, including the older pamu2fcfg in Ubuntu 24.04 and Debian 12.
---

# Linux login (pam_u2f)

`pam_u2f` lets a security key approve your Linux login, `sudo` or the screen lock. Each request appears on the Cardputer as a registration or sign-in for `pam://` and your computer's name.

!!! note "Needs v1.2.0 or later"
    Before v1.2.0, `pamu2fcfg` 1.1 (Ubuntu 24.04, Debian 12) stopped with `fido_cred_verify (-7) FIDO_ERR_INVALID_ARGUMENT`, because it only accepts registrations that carry an attestation certificate. SecTrustKey now adds the device certificate for `pam://` registrations. Websites still get self attestation, which cannot link your accounts across sites.

## Register the key

```bash
sudo apt install libpam-u2f
sudo mkdir -p /etc/security/u2f
pamu2fcfg -u "$USER" | sudo tee /etc/security/u2f/u2f_keys
```

Press ++enter++ on the Cardputer when it shows the `pam://` request. If you set a FIDO PIN and want it asked at every login, add `-N`:

```bash
pamu2fcfg -N -u "$USER" | sudo tee /etc/security/u2f/u2f_keys
```

The file holds one line per user: `user:<credential>,<public key>,es256,+presence`. To add a second key for the same user, append it with `pamu2fcfg -n` to the end of that line, not as a new line.

## Use it for sudo

!!! warning "Keep a way back in"
    Open a second terminal and run `sudo -i` there **before** you edit PAM files, and keep it open until a test works. A mistake in PAM can lock you out of `sudo`.

Add this line near the top of `/etc/pam.d/sudo`, above `@include common-auth`:

```
auth sufficient pam_u2f.so authfile=/etc/security/u2f/u2f_keys cue
```

`sufficient` means the key alone is enough and your password still works without it. Use `required` instead to need both. Then test in a new terminal:

```bash
sudo -k && sudo true
# Please touch the device.
```

The same line in `/etc/pam.d/gdm-password` (GNOME) or `/etc/pam.d/login` covers the desktop and console logins.

## Troubleshooting

- **`No U2F device available`**: the key is not plugged in or is in a maintenance screen. Return to the home screen.
- **`fido_cred_verify (-7) FIDO_ERR_INVALID_ARGUMENT`**: the firmware is older than v1.2.0. [Update it](../getting-started/install.md).
- **Login asks for the key on another computer**: registrations are tied to `pam://<hostname>`. Register again on each computer, or set the same `origin=` and `appid=` options on all of them.
