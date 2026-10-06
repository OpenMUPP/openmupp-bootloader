# Versaloon bootloader

USB bootloader for Versaloon boards with an STM32F103 microcontroller.
The bootloader shows a USB CDC (Communication Device Class) serial port
with the ID `0483:5740`. It uses the STM32 serial bootloader protocol
(ST AN3155), so `stm32flash` can program the application.

## Boards

| Board | `BOARD` | USB D+ pull-up | LEDs | Key |
|---|---|---|---|---|
| Versaloon Mini (MiniRelease1) | `mini` | PC13 | PA15, PB5 | PB9 |
| OtherUse | `otheruse` | PA2 | PA15, PB5 | PB9 |

Both boards have a 12 MHz crystal.
The pull-up pin is high when the USB device is connected.

## Memory layout

| Area | Start | Size |
|---|---|---|
| Bootloader | `0x08000000` | 10 KB |
| Application | `0x08002800` | rest of the flash |

The layout is the same for all boards.

## Build

You need the Arm GNU toolchain (`arm-none-eabi-gcc`) and GNU make.

To build the bootloader for all boards:

```sh
make
```

To build the bootloader for one board:

```sh
make BOARD=mini
```

The build writes `build/<board>/versaloon-bootloader-<board>.{elf,hex,bin}`.
To use a different output directory, set `BUILD_ROOT`.
To use a different toolchain prefix, set `CROSS_COMPILE`.

The Eclipse project (`.cproject`) builds the OtherUse board only.

## Use

1. Hold the key (PB9) down and reset the board.
2. The bootloader starts and the red LED (PA15) comes on.
3. Write the application with `stm32flash`:

   ```sh
   stm32flash -w app.bin -v -S 0x08002800 /dev/ttyACM0
   ```

When the key is not down at reset, the bootloader starts the application.
