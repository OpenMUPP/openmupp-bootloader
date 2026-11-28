# Use in a parent project

A parent project (for example OpenMUPP) builds this repository
with `ExternalProject`. Pass these arguments in `CMAKE_ARGS`:

- OpenMUPP v1: `CMAKE_TOOLCHAIN_FILE` set to `cmake/gcc_riscv.cmake`,
  and `BOARD=openmuppv1`. `OPENMUPP_BOOTLOADER_BOARDS` does not accept
  `openmuppv1`.
- STM32F1 boards: `CMAKE_TOOLCHAIN_FILE` set to `cmake/gcc_arm.cmake`,
  and `OPENMUPP_BOOTLOADER_BOARDS` or `BOARD`.

 The install
step copies the `.elf`, `.hex` and `.bin` files of each board to
`CMAKE_INSTALL_PREFIX`. The same files are in the build directory of the
external project.

Pass the library directory of the parent project:
`MUPP_CH32V00X_SYSTEM_DIR` with its `ch32v00x-system` directory for
OpenMUPP v1, or `MUPP_SYSTEM_STM32_DIR` with its `system-stm32`
directory for the STM32F1 boards. Then the two projects use the same library commit, and
the parent does not need the nested submodule. Without it, the parent
must check out the submodules recursively.

A parent project can also add this repository with `add_subdirectory()`.
The parent project must set a cross toolchain with C, C++ and ASM before
its first `project()` call. This does not work when the parent project
has a target `system-stm32`, `ch32v00x-system` or `art-build-config`. Then the configure
step stops with an error: use `ExternalProject`.
