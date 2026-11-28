# STM32F1 boards

This document describes the AN3155 bootloader of the boards with an STM32F103: the Versaloon Mini, the ST-Link/V2-1 and the ST-Link/V2 mini clone.
The [README](../README.md#build-the-bootloader) tells how to build it.

## Board notes

The pull-up pin is high when the USB device is connected.
On the Versaloon Mini, the key is down when PB9 is low.
On the ST-Link/V2 mini clone, the key is the SWIM pin of the header (PB7,
low when down).
On the ST-Link/V2-1, the key is pin 6 (SWO) of the SWD connector (PA10,
low when down). Only the bootloader reads this key, so the application
can use PA10 for SWO.

The pins of the ST-Link/V2-1 come from the Nucleo-64 schematics MB1136
(sheet 4) and MB1360 (sheet 7). The two boards use the same MCU pins.
Before you use the bootloader on the board, check these items:

- An NPN transistor connects the 1.5 kOhm pull-up to D+. `USB_RENUMn`
  (PA15) drives its base: high connects the pull-up, low disconnects it.
  PA15 is an open-drain output. When PA15 is released, the divider on
  the base sets a higher D+ level than a 3.3 V output does.
  PA15 is JTDI after the reset, so the bootloader disables the JTAG-DP.
  SWD stays on.
- The key of the ST-Link/V2-1 is SWO (PA10) on pin 6 of the SWD
  connector. Connect pin 6 to GND (pin 3), then connect the board to
  USB. Do not connect pin 6 to a target that holds SWO low at the reset.
  If you do, the bootloader starts.
  On a MB1136 Nucleo that is not broken off, SWO also connects to PB3
  of the target MCU through SB15. If the target drives PB3 low, open
  SB15. Other Nucleo boards use other bridges. Read the user manual of
  your board.
- The original firmware has readout protection. `stm32f1x unlock 0`
  removes it and erases all of the flash, see
  [Update the bootloader](#update-the-bootloader).
- The MCU (STM32F103CBT6) has no SWD header. Connect the debug probe to
  the solder bridge pads of the "reserved" group, on the side away from
  the jumper header. On MB1360, this is the pad next to the silkscreen
  label of the solder bridge. The pads are different on the two boards:

  | Signal | MCU pin | MB1136 | MB1360 |
  |---|---|---|---|
  | SWCLK (`STM_JTCK`) | PA14 (pin 37) | SB6 | SB8 |
  | SWDIO (`STM_JTMS`) | PA13 (pin 34) | SB10 | SB12 |
  | NRST (`STM_RST`) | NRST (pin 7) | SB11, the other pad is GND | JP1, the other pin is GND |
  | 3.3 V | | SB4 | SB6 |
  | GND | | SB8 | SB10 |

  The pads of the "reserved" group are open. Do not close them.
  Measure each pad to its MCU pin before you connect the probe.

The ST-Link/V2 mini clone is the USB stick clone with a 2x5 header. Its
pin map is measured (OpenMUPP `docs/stlinkv2-mini-clone.md`).
It has a key: the SWIM pin of the header. The board connects SWIM to PB7
and PB11 and has a pull-up on it. A wire from SWIM to GND at power-up
starts the bootloader, also when the application is valid. Thus a broken
application does not need a debug probe.

The bootloader passed a test on one mini clone: the boot request, the
key, the D+ disconnect, and a write and GO with `stm32flash`. The MCU of
that board has ST markings, but it is an STM32F103 compatible MCU, not a
genuine STM32F103.

## Memory layout

| Area | Address | Size |
|---|---|---|
| Bootloader | `0x08000000` | 8 KB |
| Application | `0x08002000` | Rest of the flash: 56 KB on a 64 KB part |

The flash size is different on each part. For example, the STM32F103C8
has 64 KB and the STM32F103CB has 128 KB. The bootloader reads the flash
size from the flash size register (`0x1FFFF7E0`) at run time. It sends a
NACK (negative acknowledge) for a write or an erase outside the flash.

The legacy bootloader (tag `v1-legacy`) used a 10 KB area, with the
application at `0x08002800`. The two layouts are not compatible. A board
with the legacy bootloader keeps the legacy layout until you write this
bootloader with a debug probe, see
[Update the bootloader](#update-the-bootloader).

## Write the application

1. Start the bootloader. Hold the key down and reset the board. An
   application can also send a boot request, see
   [Start the bootloader from the application](boot-request.md).
   On the ST-Link/V2-1, connect SWO (SWD connector pin 6) to GND
   (pin 3), then connect the board to USB.
   On the ST-Link/V2 mini clone, connect SWIM (header pin 3) to GND
   (header pin 5), then connect the board to USB.
2. The bootloader starts and the LED comes on. The LED
   flickers when there is USB traffic.
3. Write a stamped application image (see
   [Application image header](image-header.md)) with
   `stm32flash`:

   ```sh
   stm32flash -w app.bin -v -S 0x08002000 /dev/ttyACM0
   ```

4. Start the application:

   ```sh
   stm32flash -g 0x08002000 /dev/ttyACM0
   ```

The bootloader sends a NACK to `GO` when the application is not valid.
`GO` accepts only the application start address.

## Start-up

The bootloader decides immediately after the reset, before the clock
setup. It uses this order:

1. `0xB007` in `BKP->DR1` (boot request): stay in the bootloader.
2. GO token in `BKP->DR2`: start the application.
3. Key down: stay in the bootloader.
4. Application not valid: stay in the bootloader.
5. Otherwise: start the application.

The bootloader clears the boot request and the GO token. Each has an
effect for one reset only.

The application starts with all peripherals in their reset state. The
bootloader sets `VTOR` and the main stack pointer from the application
vector table. Then it jumps to the reset vector with all interrupts
masked (`PRIMASK` set). The application must clear `PRIMASK`.

`GO` does not jump from the bootloader. It writes the GO token to
`BKP->DR2`, disconnects from USB and resets the MCU. Step 2 then starts
the application. Thus the host sees a USB disconnect, and the
application starts in the reset state.

The bootloader resets the MCU on a fault.

## Commands

The bootloader reports protocol version 3.1 and supports these commands:

| Code | Command | Behavior |
|---|---|---|
| `0x00` | Get | Version and command list |
| `0x01` | Get Version | Version 3.1 |
| `0x02` | Get ID | `0x0410` (STM32F10xx medium density) |
| `0x11` | Read Memory | Flash, RAM, system memory, option bytes, flash size and UID registers. Other addresses get a NACK. |
| `0x21` | Go | Application start address and valid application only |
| `0x31` | Write Memory | Application area only |
| `0x44` | Extended Erase | Application pages. `0xFFFF` (mass erase) erases the unprotected application pages. |
| `0x63` | Write Protect | 4 KB groups of the application area, then a reset |
| `0x73` | Write Unprotect | Application area, then a reset |
| `0x82` | Readout Protect | Always NACK |
| `0x92` | Readout Unprotect | Always NACK |

An erase or a write in the bootloader area or in a write-protected group
gets a NACK. The bootloader discards a command that stops for more than
1 s between two bytes. A USB reset or a disconnect also discards the
command.

## Flash protection

The bootloader area is always write-protected. At each start in
bootloader mode, the bootloader reads the write protection (WRP) bits of
its area. If a bit is clear, the bootloader sets it and resets the MCU.
This occurs one time after a debug probe writes the bootloader. If the
option byte update fails, the bootloader continues without the
protection and tries again at the next start.

One WRP bit protects a group of 4 KB (4 pages). Write Protect (`0x63`)
and Write Unprotect (`0x73`) change only the groups of the application
area. They erase the option bytes and write them again:

- RDP (readout protection) is always `0xA5`: no readout protection.
- The USER byte and the two data bytes keep their values.
- The WRP bits of the bootloader area are always set.

The new values take effect after a reset. The bootloader resets the MCU
after the command and stays in bootloader mode. The host then connects
again.

The bootloader does not change the option bytes when readout protection
(RDP) is active. On the STM32F1, the write of RDP `0xA5` then starts a
mass erase of the full flash, also of the bootloader. Write Protect and
Write Unprotect then get a NACK, and the bootloader runs without the
self-protection.

A power loss during the option byte update erases all option bytes.
After the erase, RDP is `0xFF`, which is readout protection. The
bootloader still runs, but it cannot change the option bytes. The write
protection is removed. Only a debug probe can remove the readout
protection, for example with OpenOCD `stm32f1x unlock 0`. This also
erases all of the flash: write the bootloader and the application again.

The bootloader keeps the USER byte. If the USER byte selects the
hardware watchdog (`WDG_SW` = 0), the bootloader reloads the watchdog in
its main loop.

## Update the bootloader

To write a new bootloader, use a debug probe (SWD). Remove the write
protection first. For example, with OpenOCD and an ST-Link:

```sh
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
    -c "init; reset halt; stm32f1x unlock 0; reset halt; exit"
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
    -c "program build/mini/openmupp-bootloader-mini.hex verify reset exit"
```

`stm32f1x unlock 0` erases the option bytes and removes all write
protection. The change takes effect after a reset. If the write fails
after the unlock, disconnect the power for some seconds and try again.
At its first start, the new bootloader protects its area again.

The application stays in the flash. Write it again only when the layout
changes, for example from the legacy layout.
