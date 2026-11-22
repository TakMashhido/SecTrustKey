---
title: Custom USB vendor and product ID
description: Build SecTrustKey with your own USB vendor ID, product ID, manufacturer and product strings, register the new ID with pcscd, and check it with lsusb.
---

# USB identity

By default the device identifies as:

| Field | Default | Option |
|---|---|---|
| Vendor ID | `0x2E8A` | `-DUSB_VID` |
| Product ID | `0x10FE` | `-DUSB_PID` |
| Manufacturer | Koustav Kumar Mondal | `-DUSB_MANUFACTURER` |
| Product | SecTrustKey | `-DUSB_PRODUCT` |

!!! warning "Use an ID you are allowed to use"
    `0x2E8A` belongs to another organisation. Anyone distributing devices should use their own ID: for open hardware, request a free product ID under vendor `0x1209` from [pid.codes](https://pid.codes/); otherwise use a vendor ID from the USB-IF.

## Build with your own identity

```bash
scripts/in-container.sh bash scripts/build.sh \
  -DUSB_VID=0x1209 -DUSB_PID=0x0001 \
  "-DUSB_MANUFACTURER=Example Labs" "-DUSB_PRODUCT=Example Key"
```

Quote any option whose value contains spaces, as above. To keep several identities side by side, give each its own build folder:

```bash
scripts/in-container.sh bash -c \
  'BUILD_DIR=/work/build/example bash scripts/build.sh -DUSB_VID=0x1209 -DUSB_PID=0x0001'
```

The values are CMake cache entries: a later build in the same folder keeps them until you change them or delete the folder.

## Check it

Flash the firmware ([update](../getting-started/install.md#update-to-a-newer-release) is enough), then:

```bash
lsusb | grep -i "example"
# Bus 003 Device 012: ID 1209:0001 Example Labs Example Key
```

## Tell pcscd about the new ID

FIDO needs nothing: browsers and `libfido2` find security keys by their HID usage, not by ID. OpenPGP and PIV go through `pcscd`, which needs the new ID in the CCID driver's list:

```bash
sudo nano /usr/lib/pcsc/drivers/ifd-ccid.bundle/Contents/Info.plist
#   ifdVendorID     <string>0x1209</string>
#   ifdProductID    <string>0x0001</string>
#   ifdFriendlyName <string>Example Key</string>
sudo systemctl restart pcscd
pcsc_scan -r
```

Tools that select a reader by name (`ykman --reader …`) then use the new product name.
