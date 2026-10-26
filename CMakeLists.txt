cmake_minimum_required(VERSION 3.21)

project(versaloon-bootloader LANGUAGES C CXX ASM)

# Add our CMAKE modules folder
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# Compiler settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

# When not cross-compiling, or when explicitly requested via VSL_HOST_TESTS,
# build the host unit tests instead of the bootloader images.
if(VSL_HOST_TESTS OR NOT CMAKE_CROSSCOMPILING)
    if(VSL_HOST_TESTS)
        message(STATUS "VSL_HOST_TESTS enabled; building host unit tests (tests/)")
    else()
        message(STATUS "No cross toolchain configured; building host unit tests (tests/)")
    endif()
    enable_testing()
    add_subdirectory(tests)
else()
    # Boards to build, with the crystal frequency of each board.
    set(VSL_BOARDS mini otheruse stlinkv2 stlinkv2miniclone)
    set(_hse_mini 12000000)
    set(_hse_otheruse 12000000)
    set(_hse_stlinkv2 8000000)
    set(_hse_stlinkv2miniclone 8000000)
    set(BOARD "" CACHE STRING "Board to build for: ${VSL_BOARDS} (overrides VERSALOON_BOOTLOADER_BOARDS)")
    set_property(CACHE BOARD PROPERTY STRINGS "" ${VSL_BOARDS})

    set(VERSALOON_BOOTLOADER_BOARDS "mini;otheruse" CACHE STRING "Boards to build: ${VSL_BOARDS}")

    if(BOARD)
        set(_boards_to_build ${BOARD})
    else()
        set(_boards_to_build ${VERSALOON_BOOTLOADER_BOARDS})
    endif()

    message(STATUS "Cross-compiling versaloon-bootloader for Cortex-M3 (boards: ${_boards_to_build})")

    foreach(_board IN LISTS _boards_to_build)
        if(NOT _board IN_LIST VSL_BOARDS)
            message(FATAL_ERROR
                "Unknown board '${_board}'. Use one of: ${VSL_BOARDS}")
        endif()
    endforeach()

    # system-stm32 is built one time, with one HSE_VALUE: all boards of a
    # build must have the same crystal.
    list(GET _boards_to_build 0 _first_board)
    set(_hse ${_hse_${_first_board}})
    foreach(_board IN LISTS _boards_to_build)
        if(NOT _hse_${_board} EQUAL _hse)
            message(FATAL_ERROR
                "The boards ${_first_board} and ${_board} have different crystals. "
                "Build them in separate build directories (BOARD=<board>).")
        endif()
    endforeach()

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

    # Flags for the bootloader and for system-stm32. The size spike (#6) gives
    # the reasons: no exceptions, no RTTI, no heap and LTO.
    # -fno-use-cxa-atexit: a static object with a destructor then needs atexit
    # and malloc (about 0.8 KB), so the bootloader must not have one.
    set(COMMON_OPTIONS -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -g
        -ffunction-sections -fdata-sections -fno-common -fsigned-char
        --specs=nano.specs --specs=nosys.specs)
    if(VSL_LTO)
        list(APPEND COMMON_OPTIONS -flto)
    endif()
    set(CXX_OPTIONS -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics -Wno-volatile)

    include(BuildConfig)
    art_setup_build_config(art-build-config)

    # Settings of system-stm32. FORCE: the bootloader supports only this part,
    # and the crystal comes from the board.
    set(STM32_FAMILY "STM32F1" CACHE STRING "" FORCE)
    set(STM32_DEVICE "STM32F103xB" CACHE STRING "" FORCE)
    set(SYSTEM_STM32_HSE_VALUE "${_hse}" CACHE STRING "" FORCE)
    # EXCLUDE_FROM_ALL: the bootloader targets build the library objects they
    # need. The install rules of system-stm32 (headers, CMake package) are not
    # part of the install step of the bootloader.
    add_subdirectory("${VSL_SYSTEM_STM32_DIR}" system-stm32 EXCLUDE_FROM_ALL)

    include(output_files)

    # Linker script for the memory map, with the size of the bootloader area.
    set(_ld_dir "${CMAKE_CURRENT_BINARY_DIR}/ld")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/ld/mem.ld.in" "${_ld_dir}/mem.ld" @ONLY)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/ld/sections.ld" "${_ld_dir}/sections.ld" COPYONLY)

    set(_sources
        "${CMAKE_CURRENT_LIST_DIR}/src/main.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/hw/Boot.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/hw/Flash.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/protocol/An3155Protocol.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/usb/UsbCdc.cpp")

    foreach(_board IN LISTS _boards_to_build)
        string(TOUPPER "${_board}" _board_upper)
        set(_target versaloon-bootloader-${_board})

        add_executable(${_target} ${_sources})
        set_target_properties(${_target} PROPERTIES
            SUFFIX .elf
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

        target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
        target_compile_definitions(${_target} PRIVATE BOARD_${_board_upper})
        target_compile_options(${_target} PRIVATE
            -Wall -Wextra -Wconversion
            $<$<BOOL:${VSL_WERROR}>:-Werror>
            # Slim LTO objects skip the optimiser at compile time, so warnings
            # such as -Warray-bounds and -Wmaybe-uninitialized do not come at
            # all: the link does not give them for these flags either. Fat
            # objects give them at compile time.
            $<$<BOOL:${VSL_LTO}>:-ffat-lto-objects>
            "-ffile-prefix-map=${CMAKE_CURRENT_LIST_DIR}=.")
        target_link_libraries(${_target} PRIVATE art-build-config vsl-layout system-stm32)

        set(_map "${CMAKE_CURRENT_BINARY_DIR}/${_target}.map")
        target_link_options(${_target} PRIVATE
            ${COMMON_OPTIONS} -nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments
            "-L${_ld_dir}" -Tmem.ld -Tsections.ld
            "-Wl,-Map,${_map}")
        set_property(TARGET ${_target} APPEND PROPERTY LINK_DEPENDS
            "${_ld_dir}/mem.ld" "${_ld_dir}/sections.ld")
        set_property(TARGET ${_target} APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${_map}")

        ADD_DUMP_TARGET(${_target})
        PRINT_SIZE_OF_TARGETS(${_target})

        set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_target}")
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${_target}>" "${_out}.hex"
            COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${_target}>" "${_out}.bin"
            COMMAND "${CMAKE_COMMAND}" -DIMAGE=${_out}.bin
                    "-DREMOVE_ON_FAIL=${_out}.hex$<SEMICOLON>$<TARGET_FILE:${_target}>"
                    -DMAX_SIZE=${VERSALOON_BOOTLOADER_AREA_SIZE}
                    -P "${CMAKE_CURRENT_LIST_DIR}/cmake/check-size.cmake"
            BYPRODUCTS "${_out}.hex" "${_out}.bin"
            VERBATIM)

        install(FILES "${_out}.elf" "${_out}.hex" "${_out}.bin" DESTINATION .)
    endforeach()
endif()

### Clang-format ###
include(ClangFormat OPTIONAL)
if(COMMAND clangformat_setup)
    file(GLOB_RECURSE FORMAT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Layout.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/hw/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/usb/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/usb/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    )
    clangformat_setup(${FORMAT_SOURCES})
endif()
