# Toolchain file for the Arm GNU toolchain, Cortex-M3 (STM32F103).
#
# The compiler comes from, in this order:
#   1. the ARM_GNU_TOOLCHAIN variable or environment variable
#      (the root directory of the toolchain, with a bin/ subdirectory)
#   2. the PATH
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT ARM_GNU_TOOLCHAIN AND DEFINED ENV{ARM_GNU_TOOLCHAIN})
    set(ARM_GNU_TOOLCHAIN "$ENV{ARM_GNU_TOOLCHAIN}")
endif()

if(ARM_GNU_TOOLCHAIN)
    set(_tool_prefix "${ARM_GNU_TOOLCHAIN}/bin/arm-none-eabi-")
else()
    set(_tool_prefix "arm-none-eabi-")
endif()

set(CMAKE_C_COMPILER   "${_tool_prefix}gcc")
set(CMAKE_CXX_COMPILER "${_tool_prefix}g++")
set(CMAKE_ASM_COMPILER "${_tool_prefix}gcc")

# Static libraries with LTO objects need the archiver with the LTO plugin.
# The plain ar and ranlib make an archive without a symbol index for these
# objects, and the link does not find the symbols.
set(CMAKE_AR     "${_tool_prefix}gcc-ar"     CACHE FILEPATH "Archiver with the LTO plugin")
set(CMAKE_RANLIB "${_tool_prefix}gcc-ranlib" CACHE FILEPATH "ranlib with the LTO plugin")

# Do not link a test program: the linker scripts need the project memory map.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
