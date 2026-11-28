# OpenMUPP v1 (CH32V307)

This document describes the DFU bootloader of the OpenMUPP v1 probe.
The [README](../README.md#build-the-bootloader) tells how to build it.

## Write the application

The bootloader is a USB DFU 1.1 device. It is not an AN3155 device. Use
`dfu-util`:

```sh
dfu-util -d 1209:0001 -D openmupp-app-8k.bin
```

- USB ID: `1209:0001`, product "OpenMUPP Bootloader". The serial number
  is the chip UID, as in the firmware.
- One alternate setting, "Application". Block n is at
  `0x08002000 + n x 1024`. The host cannot select an address, so it
  cannot write the bootloader area or the settings page of the firmware.
- After a download, the bootloader checks the image header and the CRC.
  With a valid image, it starts the application. With an image that is
  not valid, it reports `errVERIFY` and stays in DFU mode.
- `dfu-util -U` reads the application area.
- USBHS runs at full speed. The bootloader polls EP0. It has no other
  endpoint and no interrupt.
- Windows binds WinUSB through MS OS 2.0 descriptors.
- The bootloader erases 4 KB sectors. Each sector erase takes about
  9 ms (8.64 ms, measured on OpenMUPP v1).

On Linux, the user needs access to the device, for example with this
udev rule:

```text
SUBSYSTEM=="usb", ATTRS{idVendor}=="1209", ATTRS{idProduct}=="0001", ATTRS{product}=="OpenMUPP Bootloader", MODE="0660", TAG+="uaccess"
```

## Start the bootloader

The application starts the bootloader with the boot request: `0xB007` in
`BKP_DATAR1`, then a system reset (see
[Start the bootloader from the application](boot-request.md)).

The key of OpenMUPP v1 is on BOOT0. A key that is down at a reset starts
the ROM ISP bootloader of WCH (`4348:55e0`, `wchisp`), not this
bootloader. Thus the ROM bootloader is the recovery path when this
bootloader is damaged.

## Memory layout and application link

| Area | Address | Size |
|---|---|---|
| Bootloader | `0x08000000` | 8 KB (the image is 5.3 KB) |
| Application | `0x08002000` | To the end of the zero-wait flash: 248 KB with the default user option `CODE-256KB + RAM-64KB` |

The bootloader does not set a write protection.

Link the application at `0x00002000`, the flash alias of `0x08002000`,
with the reset jump (`j _start`) at offset 0. The bootloader jumps to the
alias. The startup code loads `gp` and `sp` relative to the PC, so an
image that is linked at `0x08002000` gets wrong pointers. The stamp tool
refuses such an image (see [Stamp tool](image-header.md#stamp-tool)).
