cmake_minimum_required(VERSION 3.21)

# VSL_HOST_TESTS=ON builds the host unit tests with the host compiler,
# instead of the bootloader images. The preset host-tests sets it.
option(VSL_HOST_TESTS "Build the host unit tests instead of the bootloader images" OFF)

# Use the toolchain file when the user does not set a toolchain. A parent
# project that adds this directory with add_subdirectory() sets its own.
if(NOT VSL_HOST_TESTS AND NOT CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_CROSSCOMPILING)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake")
endif()

if(VSL_HOST_TESTS)
    project(versaloon-bootloader LANGUAGES CXX)
else()
    project(versaloon-bootloader LANGUAGES C CXX ASM)
endif()

option(VSL_WERROR "Treat compiler warnings in the bootloader sources as errors" OFF)

# The bootloader uses the first 8 KB of the flash. The application starts
# at 0x08002000. The linker script (ld/mem.ld.in), the size check and the
# code (src/Layout.h) use this value. The write protection works on 4 KB
# groups, so the size is a multiple of 4 KB.
set(VERSALOON_BOOTLOADER_AREA_SIZE 8192 CACHE STRING
    "Size of the bootloader area in bytes, a multiple of 4096")
if(NOT VERSALOON_BOOTLOADER_AREA_SIZE MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "VERSALOON_BOOTLOADER_AREA_SIZE must be a size in bytes, got '${VERSALOON_BOOTLOADER_AREA_SIZE}'")
endif()
math(EXPR _area_rest "${VERSALOON_BOOTLOADER_AREA_SIZE} % 4096")
if(NOT _area_rest EQUAL 0)
    message(FATAL_ERROR
        "VERSALOON_BOOTLOADER_AREA_SIZE (${VERSALOON_BOOTLOADER_AREA_SIZE}) must be a multiple of 4096")
endif()

add_library(vsl-layout INTERFACE)
target_compile_definitions(vsl-layout INTERFACE
    VSL_BOOTLOADER_AREA_SIZE=${VERSALOON_BOOTLOADER_AREA_SIZE})

if(VSL_HOST_TESTS)
    enable_testing()
    add_subdirectory(tests)
    return()
endif()

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
        "The bootloader needs a cross toolchain for the Cortex-M3. Set "
        "CMAKE_TOOLCHAIN_FILE before the first project() call in the "
        "parent project, for example cmake/arm-none-eabi.cmake. For the "
        "host unit tests, set VSL_HOST_TESTS=ON.")
endif()

# system-stm32 gets its compile flags from the target art-build-config
# (the name comes from libArT). A parent project with its own copy of
# system-stm32 or art-build-config would give a second target of the same
# name.
if(TARGET system-stm32 OR TARGET art-build-config)
    message(FATAL_ERROR
        "The parent project already has the target system-stm32 or "
        "art-build-config. Build the bootloader with ExternalProject and "
        "pass VSL_SYSTEM_STM32_DIR.")
endif()

# Get the size tool from the compiler name: arm-none-eabi-gcc gives
# arm-none-eabi-size. CMake sets CMAKE_OBJCOPY for the GNU toolchain.
string(REGEX REPLACE "gcc$" "size" _size_tool "${CMAKE_C_COMPILER}")

# Boards to build. One image is built for each board.
set(VERSALOON_BOOTLOADER_BOARDS "mini;otheruse" CACHE STRING
    "Boards to build: mini (Versaloon Mini), otheruse (OtherUse)")
set(_known_boards mini otheruse)

# The source of system-stm32. The default is the submodule. A parent
# project (versaloon-next-gen) passes its own copy, so both projects use
# the same library commit.
set(VSL_SYSTEM_STM32_DIR "${CMAKE_CURRENT_LIST_DIR}/dependencies/system-stm32" CACHE PATH
    "Source directory of system-stm32")
if(NOT EXISTS "${VSL_SYSTEM_STM32_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "system-stm32 not found in ${VSL_SYSTEM_STM32_DIR}. Run: "
        "git submodule update --init, or set VSL_SYSTEM_STM32_DIR")
endif()

option(VSL_LTO "Link-time optimisation" ON)

set(_dir "${CMAKE_CURRENT_LIST_DIR}")

# Flags for the bootloader and for system-stm32. The size spike (#6) gives
# the reasons: no exceptions, no RTTI, no heap and LTO.
# -fno-use-cxa-atexit: a static object with a destructor then needs atexit
# and malloc (about 0.8 KB), so the bootloader must not have one.
set(_common -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -g
    -ffunction-sections -fdata-sections -fno-common -fsigned-char
    --specs=nano.specs --specs=nosys.specs)
if(VSL_LTO)
    list(APPEND _common -flto)
endif()

add_library(art-build-config INTERFACE)
target_compile_options(art-build-config INTERFACE ${_common}
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-use-cxa-atexit;-fno-threadsafe-statics;-Wno-volatile>")

# Settings of system-stm32. Both boards have a 12 MHz crystal. FORCE: the
# bootloader supports only this part.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(STM32_FAMILY "STM32F1" CACHE STRING "" FORCE)
set(STM32_DEVICE "STM32F103xB" CACHE STRING "" FORCE)
set(SYSTEM_STM32_HSE_VALUE "12000000" CACHE STRING "" FORCE)
# EXCLUDE_FROM_ALL: the bootloader targets build the library objects they
# need. The install rules of system-stm32 (headers, CMake package) are not
# part of the install step of the bootloader.
add_subdirectory("${VSL_SYSTEM_STM32_DIR}" system-stm32 EXCLUDE_FROM_ALL)

# Linker script for the memory map, with the size of the bootloader area.
set(_ld_dir "${CMAKE_CURRENT_BINARY_DIR}/ld")
configure_file("${_dir}/ld/mem.ld.in" "${_ld_dir}/mem.ld" @ONLY)
configure_file("${_dir}/ld/sections.ld" "${_ld_dir}/sections.ld" COPYONLY)

set(_sources
    "${_dir}/src/main.cpp"
    # The USB driver for #10. The empty image does not use it yet; the
    # build compiles it, so CI checks it.
    "${_dir}/src/usb/UsbCdc.cpp")

foreach(_board IN LISTS VERSALOON_BOOTLOADER_BOARDS)
    if(NOT _board IN_LIST _known_boards)
        message(FATAL_ERROR
            "Unknown board '${_board}'. Use one of: ${_known_boards}")
    endif()

    string(TOUPPER "${_board}" _board_upper)
    set(_target versaloon-bootloader-${_board})

    add_executable(${_target} ${_sources})
    set_target_properties(${_target} PROPERTIES
        SUFFIX .elf
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

    target_include_directories(${_target} PRIVATE "${_dir}/src")
    target_compile_definitions(${_target} PRIVATE BOARD_${_board_upper})
    target_compile_options(${_target} PRIVATE
        -Wall -Wextra -Wconversion
        $<$<BOOL:${VSL_WERROR}>:-Werror>
        # Slim LTO objects skip the optimiser at compile time, so warnings
        # such as -Warray-bounds and -Wmaybe-uninitialized come only at the
        # link, without these flags. Fat objects give them at compile time.
        $<$<BOOL:${VSL_LTO}>:-ffat-lto-objects>
        "-ffile-prefix-map=${_dir}=.")
    target_link_libraries(${_target} PRIVATE art-build-config vsl-layout system-stm32)

    set(_map "${CMAKE_CURRENT_BINARY_DIR}/${_target}.map")
    target_link_options(${_target} PRIVATE
        ${_common} -nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments
        "-L${_ld_dir}" -Tmem.ld -Tsections.ld
        "-Wl,-Map,${_map}")
    set_property(TARGET ${_target} APPEND PROPERTY LINK_DEPENDS
        "${_ld_dir}/mem.ld" "${_ld_dir}/sections.ld")
    set_property(TARGET ${_target} APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${_map}")

    set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_target}")
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND "${_size_tool}" "$<TARGET_FILE:${_target}>"
        COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${_target}>" "${_out}.hex"
        COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${_target}>" "${_out}.bin"
        COMMAND "${CMAKE_COMMAND}" -DIMAGE=${_out}.bin
                "-DREMOVE_ON_FAIL=${_out}.hex$<SEMICOLON>$<TARGET_FILE:${_target}>"
                -DMAX_SIZE=${VERSALOON_BOOTLOADER_AREA_SIZE}
                -P "${_dir}/cmake/check-size.cmake"
        BYPRODUCTS "${_out}.hex" "${_out}.bin"
        VERBATIM)

    install(FILES "${_out}.elf" "${_out}.hex" "${_out}.bin" DESTINATION .)
endforeach()
