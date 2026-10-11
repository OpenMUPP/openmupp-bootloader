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

The bootloader is in a rewrite (issue #4). The rewritten bootloader uses
a smaller area. The old and the new layout are not compatible.

| Area | Legacy bootloader | New bootloader (CMake) |
|---|---|---|
| Bootloader | `0x08000000`, 10 KB | `0x08000000`, 8 KB |
| Application | `0x08002800`, rest of the flash | `0x08002000`, rest of the flash |

The layout is the same for all boards.

The CMake build of the new bootloader is in progress (issue #7). It makes an
empty image. The rest of this README describes the legacy bootloader, except
where it names the new one. Check out commit `41de34d` to build the legacy
bootloader with the `Makefile`.

The flash size is different on each part. For example, the STM32F103C8
has 64 KB and the STM32F103CB has 128 KB. The bootloader reads the flash
size from the flash size register (`0x1FFFF7E0`) at run time. It sends a
NACK (negative acknowledge) for a write or an erase outside the flash.

## Build

### New bootloader (CMake)

You need CMake 3.21 or later and the Arm GNU toolchain
(`arm-none-eabi-gcc`, `arm-none-eabi-g++`). The build gets the library
`system-stm32` from the submodule `dependencies/system-stm32`. Get it
before the first build:

```sh
git submodule update --init
```

`system-stm32` downloads the CMSIS headers from GitHub at configure
time.

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
(8 KB). In that case the build removes the image files. The size spike
(issue #6) gives a margin of about 1 KB. When the image grows above
7.5 KB, change to the 12 KB area.

The toolchain file `cmake/gcc_arm.cmake` finds the compiler in the
`PATH`. To use another toolchain, set `GCC_TOOLCHAIN_PREFIX` to the root
directory of the toolchain. You can set it as a CMake variable
(`-DGCC_TOOLCHAIN_PREFIX=<dir>`) or as an environment variable. The
toolchain file also sets `arm-none-eabi-gcc-ar` and
`arm-none-eabi-gcc-ranlib`, because the build uses link-time
optimisation (LTO).

These CMake options change the build:

| Option | Default | Content |
|---|---|---|
| `BOARD` | *(empty)* | Board to build for (`mini`, `otheruse`). Overrides `VERSALOON_BOOTLOADER_BOARDS`. A preset selects one board. |
| `VERSALOON_BOOTLOADER_BOARDS` | `mini;otheruse` | Boards to build when `BOARD` is not set. Use a semicolon list for multiple boards. |
| `VERSALOON_BOOTLOADER_AREA_SIZE` | `8192` | Size of the bootloader area in bytes. It must be a multiple of 4096, because one write protection bit protects 4 KB. |
| `VSL_SYSTEM_STM32_DIR` | `dependencies/system-stm32` | Source directory of `system-stm32` |
| `VSL_LTO` | `ON` | Link-time optimisation |
| `VSL_WERROR` | `OFF` | Warnings in the bootloader sources are errors. CI sets it. |
| `VSL_HOST_TESTS` | `OFF` | Build the host unit tests instead of the images, see [Host tests](#host-tests) |
| `GCC_TOOLCHAIN_PREFIX` | `/usr` | Install directory of the Arm GNU Toolchain. |

Without a preset, configure and build for one board:

```sh
cmake -S . -B build/mini -DCMAKE_TOOLCHAIN_FILE=cmake/gcc_arm.cmake -DBOARD=mini
cmake --build build/mini
```

### Use in a parent project

A parent project (for example versaloon-next-gen) builds this repository
with `ExternalProject`. Pass `CMAKE_TOOLCHAIN_FILE` and
`VERSALOON_BOOTLOADER_BOARDS` in `CMAKE_ARGS`. The install step copies
the `.elf`, `.hex` and `.bin` files of each board to
`CMAKE_INSTALL_PREFIX`. The same files are in the build directory of the
external project.

Pass `VSL_SYSTEM_STM32_DIR` with the `system-stm32` directory of the
parent project. Then the two projects use the same library commit, and
the parent does not need the nested submodule. Without it, the parent
must check out the submodules recursively.

A parent project can also add this repository with `add_subdirectory()`.
The parent project must set a cross toolchain with C, C++ and ASM before
its first `project()` call. This does not work when the parent project
has a target `system-stm32` or `art-build-config`. Then the configure
step stops with an error: use `ExternalProject`.

### Host tests

The host unit tests use doctest and the host C++ compiler. The build
downloads doctest from GitHub. The tests are in `tests/`.

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
```

## Use

1. Hold the key (PB9) down and reset the board.
2. The bootloader starts and the red LED (PA15) comes on.
3. Write the application with `stm32flash`:

   ```sh
   stm32flash -w app.bin -v -S 0x08002800 /dev/ttyACM0
   ```

When the key is not down at reset and no boot request is set, the
bootloader starts the application.

## Application image header

Each application image has a header with a CRC (cyclic redundancy check).
The header is for the rewrite of the bootloader (issue #4). The rewritten
bootloader will start only an application with a valid header (issue
#10). Thus a half-written application will not start. The current
bootloader ignores the header, so a stamped image also runs behind it.

The header uses the reserved words of the Cortex-M3 vector table, at the
start of the application:

| Offset | Content |
|---|---|
| `0x1C` | Magic `0x314C5356` (`"VSL1"` in memory) |
| `0x20` | Image length in bytes, a multiple of 4, from the application start |
| `0x24` | CRC32 of the image |
| `0x28` | Reserved, `0` |

The CRC uses the algorithm of the STM32F1 CRC unit (CRC-32/MPEG-2):

- Polynomial `0x04C11DB7`, initial value `0xFFFFFFFF`.
- The input is 32-bit little-endian words. The unit reads each word from
  the most significant bit.
- No reflection and no final XOR.

The CRC covers the full image. The words at `0x1C` to `0x27` count as
zero. A gap between two sections of the image counts as `0xFF` (erased
flash), because a debug probe does not write the gaps. The test vectors
are in `tests/vectors/crc32-stm32.txt`.

An application is valid when all of these rules are true:

- The magic is correct.
- The length is between 64 bytes and the size of the application area.
- The CRC is correct.
- The initial stack pointer (word `0x00`) is in RAM.
- The reset vector (word `0x04`) is odd and inside the image.

### Stamp tool

`tools/vsl-stamp-image.py` adds the header to an ELF image. It needs
Python 3.7 or later and `arm-none-eabi-objcopy`, and no other packages.

```sh
tools/vsl-stamp-image.py app.elf --bin app.bin --hex app.hex --max-length 55296
```

The examples use the current layout: the application starts at
`0x08002800`. The application area of an STM32F103x8 (64 KB flash) is
55296 bytes (54 KB).

- The tool writes the header into the `.isr_vector` section of the ELF
  file. Thus an image that a debug probe writes also has the header.
- `--bin` and `--hex` write the stamped image as a binary file and an
  Intel HEX file.
- `--max-length <bytes>` is the size of the application area. The tool
  fails when the image is larger.
- The tool refuses an image where the words `0x1C` to `0x2B` are not zero.
  An image that has a header already gets a new header.
- When the image length is not a multiple of 4, the tool adds `0xFF`
  bytes (erased flash). The `.bin` and `.hex` files include these bytes.
- The `.bin` and `.hex` files contain `0xFF` in the gaps between
  sections, as the CRC does.
- Without `--max-length`, the tool refuses an image larger than 128 KB.
  For example, a section in RAM without a load address in flash gives a
  very large image.

To check a binary image or a flash dump, use `--check`. Set `--base` to
the application start address:

```sh
tools/vsl-stamp-image.py --check app.bin --base 0x08002800
```

### CMake

A CMake project adds the header with a post-build step:

```cmake
include(<versaloon-bootloader>/cmake/VslStampImage.cmake)
vsl_stamp_image(app.elf BIN app.bin HEX app.hex MAX_LENGTH 55296)
```

`BIN`, `HEX` and `MAX_LENGTH` are optional.

Use the `BIN` and `HEX` files of `vsl_stamp_image()` to program the
application. Do not make the `.bin` or `.hex` file with a separate `objcopy`
step. `objcopy -O binary` fills the gaps between sections with `0x00`, so
the CRC is not correct. If a separate step is necessary, add
`--gap-fill 0xff` and run it after `vsl_stamp_image()`. Post-build steps
run in the order of the calls.

### Tool tests

The tool tests use the Python `unittest` module:

```sh
python3 -m unittest discover -s tools/tests
```

The tests that build an ELF image need `arm-none-eabi-gcc` and `cmake`.
Without them, these tests are skipped.

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
