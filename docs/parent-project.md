# Use in a parent project

A parent project (for example OpenMUPP) builds this repository
with `ExternalProject`. Pass `CMAKE_TOOLCHAIN_FILE` and
`OPENMUPP_BOOTLOADER_BOARDS` (or `BOARD`) in `CMAKE_ARGS`. The install
step copies the `.elf`, `.hex` and `.bin` files of each board to
`CMAKE_INSTALL_PREFIX`. The same files are in the build directory of the
external project.

Pass `MUPP_SYSTEM_STM32_DIR` with the `system-stm32` directory of the
parent project. Then the two projects use the same library commit, and
the parent does not need the nested submodule. Without it, the parent
must check out the submodules recursively.

A parent project can also add this repository with `add_subdirectory()`.
The parent project must set a cross toolchain with C, C++ and ASM before
its first `project()` call. This does not work when the parent project
has a target `system-stm32` or `art-build-config`. Then the configure
step stops with an error: use `ExternalProject`.
