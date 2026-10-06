cmake_minimum_required(VERSION 3.21)

# Use the toolchain file when the user does not set a toolchain. A parent
# project that adds this directory with add_subdirectory() sets its own.
if(NOT CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_CROSSCOMPILING)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake")
endif()

project(versaloon-bootloader LANGUAGES C)

# Boards to build. One image is built for each board.
set(VERSALOON_BOOTLOADER_BOARDS "mini;otheruse" CACHE STRING
    "Boards to build: mini (Versaloon Mini), otheruse (OtherUse)")
set(_known_boards mini otheruse)

# The bootloader uses the first 10 KB of the flash. The application starts
# at 0x08002800. The linker script etc/mem.ld has the same size. The build
# fails when an image is larger than this size.
set(VERSALOON_BOOTLOADER_AREA_SIZE 10240 CACHE STRING
    "Size of the bootloader area in bytes")

set(_dir "${CMAKE_CURRENT_LIST_DIR}")

# The link order is the directory order below. It decides where the linker
# places the data in RAM, so keep it the same as in the Makefile.
set(_source_dirs
    src
    usbserial
    Libraries/system
    Libraries/CMSIS/src
    Libraries/StdPeriph/src
    Libraries/STM32_USB-FS-Device_Driver/src)
set(_sources)
foreach(_source_dir IN LISTS _source_dirs)
    file(GLOB _dir_sources CONFIGURE_DEPENDS "${_dir}/${_source_dir}/*.c")
    list(APPEND _sources ${_dir_sources})
endforeach()

foreach(_board IN LISTS VERSALOON_BOOTLOADER_BOARDS)
    if(NOT _board IN_LIST _known_boards)
        message(FATAL_ERROR
            "Unknown board '${_board}'. Use one of: ${_known_boards}")
    endif()

    string(TOUPPER "${_board}" _board_upper)
    set(_target versaloon-bootloader-${_board})
    set(_arch -mcpu=cortex-m3 -mthumb)

    add_executable(${_target} ${_sources})
    set_target_properties(${_target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS ON
        SUFFIX .elf
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

    target_include_directories(${_target} PRIVATE
        "${_dir}/include"
        "${_dir}/src"
        "${_dir}/usbserial"
        "${_dir}/Libraries/CMSIS/include"
        "${_dir}/Libraries/StdPeriph/include"
        "${_dir}/Libraries/STM32_USB-FS-Device_Driver/inc")

    target_compile_definitions(${_target} PRIVATE
        STM32F10X_MD
        USE_STDPERIPH_DRIVER
        HSE_VALUE=12000000
        BOARD_${_board_upper})

    target_compile_options(${_target} PRIVATE
        ${_arch} -Os -g3
        -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections)

    set(_map "${CMAKE_CURRENT_BINARY_DIR}/${_target}.map")
    target_link_options(${_target} PRIVATE
        ${_arch} -nostartfiles -Wl,--gc-sections
        "-L${_dir}/etc" -Tmem.ld -Tlibs.ld -Tsections.ld
        "-Wl,-Map,${_map}")
    set_property(TARGET ${_target} APPEND PROPERTY LINK_DEPENDS
        "${_dir}/etc/mem.ld" "${_dir}/etc/libs.ld" "${_dir}/etc/sections.ld")
    set_property(TARGET ${_target} APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${_map}")

    set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_target}")
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${_target}>"
        COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${_target}>" "${_out}.hex"
        COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${_target}>" "${_out}.bin"
        COMMAND "${CMAKE_COMMAND}" -DIMAGE=${_out}.bin
                -DMAX_SIZE=${VERSALOON_BOOTLOADER_AREA_SIZE}
                -P "${_dir}/cmake/check-size.cmake"
        BYPRODUCTS "${_out}.hex" "${_out}.bin"
        VERBATIM)
endforeach()
