---
title: Install from a release
description: Flash SecTrustKey onto the M5Stack Cardputer ADV from a GitHub release with esptool or Espressif's browser flasher, back up the existing firmware, update without losing keys, and roll back.
---

# Install from a release

Every [release](https://github.com/TakMashhido/SecTrustKey/releases) carries a ready-made image, so you do not need to build anything.

!!! tip "Using M5 Launcher?"
    Install SecTrustKey from Launcher's SD menu instead and keep your other apps: see [M5 Launcher and M5Burner](launcher.md). This page covers flashing the whole chip.

| File | Use it for |
|---|---|
| `sectrustkey-vX.Y.Z-cardputer-adv.bin` | **First install**, with esptool, the browser flasher, M5Burner or M5 Launcher. One image for address `0x0`: bootloader, partition table, boot data, firmware and the 4 KB installer pad of the key storage. |
| `sectrustkey-vX.Y.Z-app.bin` | **Updates** of a device flashed with esptool. Only the firmware, for address `0x20000`; your keys are kept. |
| `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin` | The separate parts, if you prefer to flash them one by one. |
| `SHA256SUMS.txt` | Checksums of all of the above. |

Check the download before flashing:

```bash
sha256sum -c SHA256SUMS.txt --ignore-missing
```

## Enter download mode

1. Slide the power switch **off**.
2. Hold the **G0** button on the side and keep holding it.
3. Plug in the USB-C cable, then release **G0**. The screen stays dark.

The Cardputer now shows up as an Espressif USB serial device (`303a:1001`, usually `/dev/ttyACM0` on Linux, a `COM` port on Windows, `/dev/cu.usbmodem…` on macOS).

## Back up the Cardputer

The full install erases everything. To keep a way back to M5 Launcher or whatever runs now, save the whole 8 MB flash first:

```bash
pip install esptool
esptool.py --chip esp32s3 --baud 921600 read_flash 0x0 0x800000 cardputer-backup-8MB.bin
sha256sum cardputer-backup-8MB.bin > cardputer-backup-8MB.sha256
```

Keep both files somewhere safe. [Roll back](#roll-back) explains how to write it back.

## Flash

=== "esptool"

    ```bash
    esptool.py --chip esp32s3 --baud 921600 --after watchdog_reset \
      write_flash --erase-all 0x0 sectrustkey-v1.2.1-cardputer-adv.bin
    ```

    `--erase-all` clears the whole chip, so the key storage starts empty. Without it, the first start shows **Set up storage?** and waits for ++enter++ before preparing the area. `--after watchdog_reset` starts the firmware when flashing is done; on this board a reset through the serial line would leave it in download mode.

=== "Browser (no install)"

    1. Open Espressif's [esptool-js flasher](https://espressif.github.io/esptool-js/) in Chrome or Edge.
    2. Choose **Connect** and pick the Espressif serial device.
    3. Choose **Erase Flash** and wait for it to finish.
    4. Add the file `sectrustkey-v1.2.1-cardputer-adv.bin` at address **`0x0`** and choose **Program**.
    5. Unplug and plug the Cardputer back in.

=== "Docker"

    ```bash
    docker run --rm --device=/dev/ttyACM0 -v "$PWD":/fw espressif/idf:v5.5.3 \
      esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 --after watchdog_reset \
      write_flash --erase-all 0x0 /fw/sectrustkey-v1.2.1-cardputer-adv.bin
    ```

When it restarts you see the boot screen, then the home screen. The first boot creates the device identity: two random master keys stored in the [key store](../concepts/storage.md).

<figure class="stk-screen-one" markdown>
![Boot screen](../assets/screens/splash.png)
<figcaption>The boot screen. USB starts only after the key store has been checked.</figcaption>
</figure>

## Update to a newer release

Updates write only the firmware at `0x20000`. The key storage is a separate partition at `0x320000` that an update never touches, so your passkeys, OpenPGP keys and settings stay. Devices installed from M5 Launcher update from Launcher instead: see [Update from M5 Launcher](launcher.md#update-from-m5-launcher).

```bash
esptool.py --chip esp32s3 --baud 921600 --after watchdog_reset \
  write_flash 0x20000 sectrustkey-v1.2.1-app.bin
```

!!! tip "Back up before every update"
    An update does not touch your keys, but an [SD backup](../guide/backup.md) costs a minute and covers the unexpected.

## Roll back

To put the Cardputer back exactly as you saved it, write the backup to address `0x0` from download mode:

```bash
esptool.py --chip esp32s3 --baud 921600 --after watchdog_reset \
  write_flash 0x0 cardputer-backup-8MB.bin
```

## Next

Your Cardputer is now a security key with public factory PINs. Continue with [First steps](first-steps.md).
