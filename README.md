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

When the key is not down at reset and no boot request is set, the
bootloader starts the application.

## Start the bootloader from the application

An application starts the bootloader with a boot request in the backup
register `BKP->DR1`. Applications depend on this contract:

| Item | Value |
|---|---|
| Register | `BKP->DR1` |
| Boot request value | `0xB007` |

The application does these steps:

1. Enable the `PWR` and `BKP` clocks.
2. Set `PWR_CR.DBP` to enable write access to the backup domain.
3. Write `0xB007` to `BKP->DR1`.
4. Call `NVIC_SystemReset()`.

```c
RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
PWR->CR |= PWR_CR_DBP;
BKP->DR1 = 0xB007;
NVIC_SystemReset();
```

After the reset, the bootloader reads `BKP->DR1`. If the value is
`0xB007`, the bootloader clears the register and stays in bootloader mode.
This is the same behavior as with the key down. The next reset without a
request starts the application again.

The bootloader checks the request after the clock and USB setup, before
it reads the key. A system reset does not clear `BKP->DR1`. A power-up
clears `BKP->DR1` only when the backup domain (VBAT) had no power.

### Deprecated: `entryFromProgram()`

An application can also call `entryFromProgram()` in the bootloader. This
function is deprecated. Its address changes with the linker script and the
device density. It also starts with the hardware state of the application,
so the host does not always see a USB disconnect. Use the boot request in
`BKP->DR1` instead.
