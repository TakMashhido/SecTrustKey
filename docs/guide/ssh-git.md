---
title: SSH and Git signing
description: Log in over SSH with the SecTrustKey OpenPGP authentication key through gpg-agent, and sign Git commits with its signing key.
---

# SSH and Git signing

Once you have [OpenPGP keys on the device](openpgp.md#generate-keys-on-the-device), the authentication key works for SSH and the signing key for Git.

## SSH through gpg-agent

```bash
# ~/.gnupg/gpg-agent.conf
enable-ssh-support
```

```bash
# ~/.bashrc or ~/.zshrc
export SSH_AUTH_SOCK="$(gpgconf --list-dirs agent-ssh-socket)"
gpg-connect-agent updatestartuptty /bye >/dev/null
```

```bash
gpgconf --kill gpg-agent
ssh-add -L          # prints the card's key, ending in "cardno:…"
```

Add that line to `~/.ssh/authorized_keys` on the server, or to your GitHub account under **SSH keys**. With `uif 3 on`, every login shows a prompt on the device.

## Signed Git commits

```bash
gpg --card-status | grep -A1 "Signature key"
git config --global user.signingkey <KEY-ID>
git config --global commit.gpgsign true
git config --global tag.gpgsign true
```

Export the public key with `gpg --armor --export <KEY-ID>` and add it to GitHub under **GPG keys**, so your commits show as *Verified*.

!!! tip "Several machines"
    The public key and the card are all a new computer needs: `gpg --import pubkey.asc`, then `gpg --card-status` to link the stubs to the card.
