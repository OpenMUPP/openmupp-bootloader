# OpenMUPP bootloader

USB bootloader for the OpenMUPP firmware (Open Multi-architecture
Universal Programmer & Probe). The bootloader lets the host write a new
firmware over USB, without a debug probe. The main target is the
OpenMUPP v1 probe with a CH32V307 microcontroller. The bootloader also
builds for the older probe boards with an STM32F103 microcontroller.

The bootloader uses a different USB protocol on each MCU family:

| MCU family | Boards | USB device | Host tool |
|---|---|---|---|
| CH32V307 | OpenMUPP v1 | USB DFU 1.1 (Device Firmware Upgrade), ID `1209:0001`, product "OpenMUPP Bootloader" | `dfu-util` |
| STM32F103 | Versaloon Mini, ST-Link/V2-1, ST-Link/V2 mini clone | USB CDC (Communication Device Class) serial port, ID `0483:5740`, STM32 serial bootloader protocol (ST AN3155) | `stm32flash` |

On all boards, the bootloader uses the first 8 KB of the flash, and the
application starts at `0x08002000`. The bootloader starts only an
application with a valid image header (see
[Application image header](docs/image-header.md)). The application
starts the bootloader with a boot request (see
[Start the bootloader from the application](docs/boot-request.md)).

## Boards

| Board | `BOARD` | Crystal | USB D+ pull-up | LED | Key |
|---|---|---|---|---|---|
| OpenMUPP v1 (CH32V307) | `openmuppv1` | 8 MHz | Internal (USBHS) | PA10 (red), PA9 (green), on when high | None, BOOT0 starts the ROM bootloader of WCH |
| Versaloon Mini (MiniRelease1) | `mini` | 12 MHz | PC13 | PA15 (red), on when low | PB9 |
| ST-Link/V2-1 of a Nucleo-64 board | `stlinkv21` | 8 MHz | PA15 | PA9: high is red, low is green | PA10 (SWO) |
| ST-Link/V2 mini clone (USB stick) | `stlinkv2miniclone` | 8 MHz | Fixed | PA9: high is blue, low is red | PB7 (SWIM) |

The pins are the same as in OpenMUPP (`src/board/boards/`).

## Get the source code

The libraries are Git submodules in `dependencies/`.
Clone with the submodules:

```sh
git clone --recurse-submodules https://github.com/OpenMUPP/openmupp-bootloader.git
```

In an existing clone, run `git submodule update --init --recursive`.

The submodules are:

- `ch32v00x-system`: start-up code, device headers and peripheral drivers of the WCH CH32V30x. Only the OpenMUPP v1 board (`openmuppv1`) uses it. The primary repository is https://github.com/armint/ch32v00x-system.
- `system-stm32`: start-up code, CMSIS and peripheral drivers of the STM32. Only the STM32F1 boards use it. The primary repository is https://github.com/armint/system-stm32.

## Requirements

- CMake 3.21 or later.
- The xPack RISC-V GCC (`riscv-none-elf`), for example as in the build image of CI.
- For the Versaloon Mini and the ST-Link boards only: the Arm GNU Toolchain (`arm-none-eabi`), for example 15.3.rel2 as in CI.
- A host C++ compiler with C++23 support, for the unit tests.
- `clang-format`, for the `clangformat` target.
- Internet access at configure time: CMake FetchContent downloads doctest, and `system-stm32` downloads the CMSIS headers.

## Build the bootloader

Select a preset. Give the install directory of the toolchain with `-DGCC_TOOLCHAIN_PREFIX`. Then configure and build:

```sh
cmake --preset openmuppv1 \
      -DGCC_TOOLCHAIN_PREFIX=/opt/xpack-riscv-none-elf-gcc-15.2.0-1
cmake --build --preset openmuppv1 -j
```

The build directory is `build/<preset>`, here `build/openmuppv1`.
If the compiler of the toolchain is in `/usr/bin`, you can omit `GCC_TOOLCHAIN_PREFIX`.
The RISC-V toolchain file also reads the environment variable `RISCV_GCC_TOOLCHAIN_PREFIX` when `GCC_TOOLCHAIN_PREFIX` is not set.

Each board has a preset with the name of the board.
The STM32F1 boards (`mini`, `stlinkv21`, `stlinkv2miniclone`) need the Arm GNU Toolchain:

```sh
cmake --preset mini \
      -DGCC_TOOLCHAIN_PREFIX=/opt/arm-gnu-toolchain-15.3.rel2-x86_64-arm-none-eabi
cmake --build --preset mini -j
```

`cmake --list-presets` shows all presets.

The build writes these files into the build directory:

- `openmupp-bootloader-<board>.elf`
- `openmupp-bootloader-<board>.hex`
- `openmupp-bootloader-<board>.bin`
- `openmupp-bootloader-<board>.map`

The build fails when the image is larger than the bootloader area
(8 KB). In that case the build removes the image files. The OpenMUPP v1
image is about 5.3 KB, and the STM32F1 image is about 7 KB. When the
STM32F1 image grows above 7.5 KB, change to the 12 KB area.

These CMake options change the build:

| Option | Default | Content |
|---|---|---|
| `BOARD` | *(empty)* | Board to build for (`openmuppv1`, `mini`, `stlinkv21`, `stlinkv2miniclone`). Overrides `OPENMUPP_BOOTLOADER_BOARDS`. A preset selects one board. |
| `OPENMUPP_BOOTLOADER_BOARDS` | `mini;otheruse` | STM32F1 boards to build when `BOARD` is not set. Use a semicolon list for more than one board. All boards of one build must have the same crystal: build `stlinkv21` and `stlinkv2miniclone` alone or together. |
| `OPENMUPP_BOOTLOADER_AREA_SIZE` | `8192` | Size of the bootloader area in bytes. It must be a multiple of 4096, because one write protection bit of the STM32F1 protects 4 KB. |
| `MUPP_SYSTEM_STM32_DIR` | `dependencies/system-stm32` | Source directory of `system-stm32` |
| `MUPP_LTO` | `ON` | Link-time optimisation (LTO) |
| `MUPP_WERROR` | `OFF` | Warnings in the bootloader sources are errors. CI sets it. |
| `MUPP_HOST_TESTS` | `OFF` | Build the host unit tests instead of the images |
| `GCC_TOOLCHAIN_PREFIX` | `/usr` | Install directory of the GNU toolchain |

The toolchain file `cmake/gcc_arm.cmake` also sets `arm-none-eabi-gcc-ar`
and `arm-none-eabi-gcc-ranlib`, because the build uses LTO.

Without a preset, configure and build for one board:

```sh
cmake -S . -B build/mini -DCMAKE_TOOLCHAIN_FILE=cmake/gcc_arm.cmake \
      -DBOARD=mini -DGCC_TOOLCHAIN_PREFIX=<Arm toolchain root>
cmake --build build/mini
```

The host unit tests use doctest and the host C++ compiler. The build
downloads doctest from GitHub. The tests are in `tests/`.

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
```

## Write the firmware

On OpenMUPP v1, write the application with `dfu-util`:

```sh
dfu-util -d 1209:0001 -D openmupp-app-8k.bin
```

On an STM32F1 board, write a stamped application with `stm32flash`, then
start it:

```sh
stm32flash -w app.bin -v -S 0x08002000 /dev/ttyACM0
stm32flash -g 0x08002000 /dev/ttyACM0
```

## Documentation

| Document | Content |
|---|---|
| [docs/openmupp-v1.md](docs/openmupp-v1.md) | The DFU bootloader of OpenMUPP v1: the DFU device, the udev rule, the start of the bootloader, the memory layout and the link address of the application |
| [docs/stm32f1.md](docs/stm32f1.md) | The AN3155 bootloader of the STM32F1 boards: board notes, memory layout, write steps, start-up, commands, flash protection and the update of the bootloader |
| [docs/image-header.md](docs/image-header.md) | The application image header, the CRC, the stamp tool and its CMake function |
| [docs/boot-request.md](docs/boot-request.md) | The boot request that an application sends to start the bootloader, and the backup registers of the bootloader |
| [docs/parent-project.md](docs/parent-project.md) | How a parent project, for example OpenMUPP, builds this repository |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Copyright (C) 2026 Armin van der Togt <armin@vandertogt.org>

This project is free software. You can redistribute it and modify it under the terms of the GNU General Public License, version 3 or any later version. See [LICENSE](LICENSE).
The program has no warranty.

`cmake/ClangFormat.cmake` uses the Boost Software License 1.0. See [LICENSES/BSL-1.0.txt](LICENSES/BSL-1.0.txt).
The submodules `system-stm32` and `ch32v00x-system` use the MIT license. The WCH device files of `ch32v00x-system` keep the WCH terms (see its README). The build also downloads the CMSIS headers of Arm and ST (Apache-2.0).
