# Versaloon bootloader

USB bootloader for Versaloon boards with an STM32F103 microcontroller.
The bootloader shows a USB CDC (Communication Device Class) serial port
with the ID `0483:5740`. It uses the STM32 serial bootloader protocol
(ST AN3155), so `stm32flash` can program the application.

## Boards

| Board | `BOARD` | Crystal | USB D+ pull-up | LED | Key |
|---|---|---|---|---|---|
| Versaloon Mini (MiniRelease1) | `mini` | 12 MHz | PC13 | PA15 (red), on when low | PB9 |
| OtherUse | `otheruse` | 12 MHz | PA2 | PA15 (red), on when low | PB9 |
| ST-Link/V2 and clones | `stlinkv2` | 8 MHz | Fixed | PA9, on when high | None |
| ST-Link/V2 mini clone (USB stick) | `stlinkv2miniclone` | 8 MHz | Fixed | PA9: high is blue, low is red | PB7 (SWIM) |

The pull-up pin is high when the USB device is connected.
The key is down when PB9 is low.
On the ST-Link/V2 mini clone, the key is the SWIM pin of the header (PB7, low when down).
The pins are the same as in versaloon-next-gen (`src/board/boards/`).

The ST-Link/V2 support is not tested on hardware. Its pins come from
versaloon-next-gen. Do these checks on the real board before you use it:

- The ST-Link/V2 has no switch for the D+ pull-up. To show a disconnect
  to the host, the bootloader sets D+ (PA12) low for 20 ms.
- The ST-Link/V2 has no key. The bootloader starts only with a boot
  request from the application, or when the application is not valid.
  If the application does not send boot requests, use a debug probe to
  erase it.
- The original ST-Link/V2 has readout protection. `stm32f1x unlock 0`
  removes it and erases all of the flash, see
  [Update the bootloader](#update-the-bootloader). Connect the debug
  probe to the SWDIO and SWCLK pads of the board.
- Some clones use a compatible MCU from another manufacturer, not an
  STM32F103. The bootloader is tested on one of these MCUs only (on the
  ST-Link/V2 mini clone, see below).

The ST-Link/V2 mini clone is the USB stick clone with a 2x5 header. Its
pin map is measured (versaloon-next-gen `docs/stlinkv2-mini-clone.md`).
It has a key: the SWIM pin of the header. The board connects SWIM to PB7
to PB11 and has a pull-up on it. A wire from SWIM to GND at power-up
starts the bootloader, also when the application is valid. Thus a broken
application does not need a debug probe.

The bootloader is tested on one mini clone. The boot request, the key,
the D+ disconnect, and a write and GO with `stm32flash` pass. The MCU of
that board has ST markings, but it is an STM32F103 compatible MCU, not a
genuine STM32F103.

## Memory layout

| Area | Address | Size |
|---|---|---|
| Bootloader | `0x08000000` | 8 KB |
| Application | `0x08002000` | Rest of the flash: 56 KB on a 64 KB part |

The layout is the same for all boards.

The bootloader was rewritten in C++ (issue #4). The legacy bootloader used
a 10 KB area, with the application at `0x08002800`. The two layouts are not
compatible. A board with the legacy bootloader keeps the legacy layout
until you write the new bootloader with a debug probe, see
[Update the bootloader](#update-the-bootloader). The legacy bootloader has
the tag `v1-legacy`.

The flash size is different on each part. For example, the STM32F103C8
has 64 KB and the STM32F103CB has 128 KB. The bootloader reads the flash
size from the flash size register (`0x1FFFF7E0`) at run time. It sends a
NACK (negative acknowledge) for a write or an erase outside the flash.

## Build

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

Use the preset `otheruse` for the OtherUse board, `stlinkv2` for the
ST-Link/V2 and `stlinkv2miniclone` for the ST-Link/V2 mini clone.

The build writes these files to the build directory. A preset uses
`build/<board>/`:

- `versaloon-bootloader-<board>.elf`
- `versaloon-bootloader-<board>.hex`
- `versaloon-bootloader-<board>.bin`
- `versaloon-bootloader-<board>.map`

The build fails when the image is larger than the bootloader area
(8 KB). In that case the build removes the image files. The image is
about 7 KB. When the image grows above 7.5 KB, change to the 12 KB area.

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
| `BOARD` | *(empty)* | Board to build for (`mini`, `otheruse`, `stlinkv2`, `stlinkv2miniclone`). Overrides `VERSALOON_BOOTLOADER_BOARDS`. A preset selects one board. |
| `VERSALOON_BOOTLOADER_BOARDS` | `mini;otheruse` | Boards to build when `BOARD` is not set. Use a semicolon list for multiple boards. All boards of one build must have the same crystal: build `stlinkv2` and `stlinkv2miniclone` alone or together. |
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

1. Hold the key (PB9) down and reset the board. An application can also
   send a boot request, see
   [Start the bootloader from the application](#start-the-bootloader-from-the-application).
   The ST-Link/V2 has no key: it starts the bootloader only with a boot
   request, or when the application is not valid.
   On the ST-Link/V2 mini clone, connect SWIM (header pin 3) to GND
   (header pin 5), then connect the board to USB.
2. The bootloader starts and the LED comes on. The LED
   flickers when there is USB traffic.
3. Write a stamped application image (see
   [Application image header](#application-image-header)) with
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

### Start-up

The bootloader decides straight after the reset, before the clock setup.
It uses this order:

1. `0xB007` in `BKP->DR1` (boot request): stay in the bootloader.
2. GO token in `BKP->DR2`: start the application.
3. Key down: stay in the bootloader.
4. Application not valid: stay in the bootloader.
5. Otherwise: start the application.

The bootloader clears the boot request and the GO token. Each has an
effect for one reset only.

The application starts with all peripherals in their reset state. The
bootloader sets `VTOR` and the main stack pointer from the application
vector table. Then it jumps to the reset vector with all interrupts masked
(`PRIMASK` set). The application must clear `PRIMASK`.

`GO` does not jump from the bootloader. It writes the GO token to
`BKP->DR2`, disconnects from USB and resets the MCU. Step 2 then starts
the application. Thus the host sees a USB disconnect, and the application
starts in the reset state.

The bootloader resets the MCU on a fault.

### Commands

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
gets a NACK. A command that stops for more than 1 s between two bytes is
discarded. A USB reset or a disconnect also discards the command.

## Flash protection

The bootloader area is always write-protected. At each start in
bootloader mode, the bootloader reads the write protection (WRP) bits of
its area. If a bit is clear, the bootloader sets it and resets the MCU.
This occurs one time after a debug probe writes the bootloader. If the
option byte update fails, the bootloader continues without the protection
and tries again at the next start.

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

A power loss during the option byte update erases all option bytes. After
the erase, RDP is `0xFF`, which is readout protection. The bootloader
still runs, but it cannot change the option bytes. The write protection is
removed. Only a debug probe can remove the readout protection, for example
with OpenOCD `stm32f1x unlock 0`. This also erases all of the flash: write
the bootloader and the application again.

The bootloader keeps the USER byte. If the USER byte selects the hardware
watchdog (`WDG_SW` = 0), the bootloader reloads the watchdog in its main
loop.

### Update the bootloader

To write a new bootloader, use a debug probe (SWD). Remove the write
protection first. For example, with OpenOCD and an ST-Link:

```sh
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
    -c "init; reset halt; stm32f1x unlock 0; reset halt; exit"
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
    -c "program build/mini/versaloon-bootloader-mini.hex verify reset exit"
```

`stm32f1x unlock 0` erases the option bytes and removes all write
protection. The change takes effect after a reset. If the write fails
after the unlock, disconnect the power for some seconds and try again.
At its first start, the new bootloader protects its area again.

The application stays in the flash. Write it again only when the layout
changes, for example from the legacy layout.

## Application image header

Each application image has a header with a CRC (cyclic redundancy check).
The bootloader starts only an application with a valid header. Thus a
blank or half-written application does not start: the board stays in the
bootloader. An image without the header also stays in the bootloader. The
legacy bootloader ignores the header, so a stamped image also runs behind
it.

The bootloader calculates the CRC with the CRC unit of the STM32F1, at
each start. 56 KB take some ms.

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
- The length is a multiple of 4, between 64 bytes and the size of the
  application area.
- The CRC is correct.
- The initial stack pointer (word `0x00`) is in RAM.
- The reset vector (word `0x04`) is odd and inside the image.

### Stamp tool

`tools/vsl-stamp-image.py` adds the header to an ELF image. It needs
Python 3.7 or later and `arm-none-eabi-objcopy`, and no other packages.

```sh
tools/vsl-stamp-image.py app.elf --bin app.bin --hex app.hex --max-length 57344
```

The application starts at `0x08002000`. The application area of an
STM32F103x8 (64 KB flash) is 57344 bytes (56 KB).

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
tools/vsl-stamp-image.py --check app.bin --base 0x08002000
```

### CMake

A CMake project adds the header with a post-build step:

```cmake
include(<versaloon-bootloader>/cmake/VslStampImage.cmake)
vsl_stamp_image(app.elf BIN app.bin HEX app.hex MAX_LENGTH 57344)
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

The bootloader checks the request straight after the reset, before it
reads the key (see [Start-up](#start-up)). A system reset does not clear
`BKP->DR1`. A power-up clears `BKP->DR1` only when the backup domain
(VBAT) had no power.

The bootloader also uses these backup registers. An application must not
use them:

| Register | Use |
|---|---|
| `BKP->DR2` | GO token: start the application after the reset |
| `BKP->DR3` | The bootloader tried to protect its area before the last reset |

The legacy functions `entryFromProgram()` and the reset with a magic
number in RAM are removed. Use the boot request in `BKP->DR1`.
