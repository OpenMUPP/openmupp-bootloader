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

The flash size is different on each part. For example, the STM32F103C8
has 64 KB and the STM32F103CB has 128 KB. The bootloader reads the flash
size from the flash size register (`0x1FFFF7E0`) at run time. It sends a
NACK (negative acknowledge) for a write or an erase outside the flash.

## Build

You need CMake 3.21 or later and the Arm GNU toolchain
(`arm-none-eabi-gcc`).

Each board has a CMake preset. To build the bootloader for one board:

```sh
cmake --preset mini
cmake --build --preset mini
```

Use the preset `otheruse` for the OtherUse board.

The build writes these files to the build directory. A preset uses
`build/<board>/`:

- `versaloon-bootloader-<board>.elf`
- `versaloon-bootloader-<board>.hex`
- `versaloon-bootloader-<board>.bin`
- `versaloon-bootloader-<board>.map`

The build fails when the image is larger than the bootloader area
(10 KB). In that case the build removes the image files.

The toolchain file `cmake/arm-none-eabi.cmake` finds the compiler in the
`PATH`. To use another toolchain, set `ARM_GNU_TOOLCHAIN` to the root
directory of the toolchain. You can set it as a CMake variable
(`-DARM_GNU_TOOLCHAIN=<dir>`) or as an environment variable.

Without a preset, the option `VERSALOON_BOOTLOADER_BOARDS` selects the
boards. The default builds all boards.

```sh
cmake -S . -B build/all -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build/all
```

### Use in a parent project

A parent project can add this repository with `add_subdirectory()`. The
parent project sets the toolchain. The targets are
`versaloon-bootloader-<board>`. Set `VERSALOON_BOOTLOADER_BOARDS` to
select the boards.

A parent project can also build the repository with `ExternalProject`.
Pass `CMAKE_TOOLCHAIN_FILE` and `VERSALOON_BOOTLOADER_BOARDS` in
`CMAKE_ARGS`. The install step copies the `.elf`, `.hex` and `.bin`
files of each board to `CMAKE_INSTALL_PREFIX`. The same files are in the
build directory of the external project.

The parent project must set a cross toolchain before its first
`project()` call. Without one, the configure step stops with an error.

### Makefile

The `Makefile` is an alternative build. It makes the same `.hex` and
`.bin` files as the CMake build. CI builds both and compares the files.

```sh
make                 # all boards
make BOARD=mini      # one board
```

The `Makefile` writes `build/<board>/versaloon-bootloader-<board>.*`.
To use another toolchain prefix, set `CROSS_COMPILE`.

## Use

1. Hold the key (PB9) down and reset the board.
2. The bootloader starts and the red LED (PA15) comes on.
3. Write the application with `stm32flash`:

   ```sh
   stm32flash -w app.bin -v -S 0x08002800 /dev/ttyACM0
   ```

When the key is not down at reset, the bootloader starts the application.
