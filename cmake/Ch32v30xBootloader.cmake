# SPDX-License-Identifier: GPL-3.0-or-later
# The DFU bootloader for the CH32V307 of OpenMUPP v1 (BOARD=openmuppv1).
# CMakeLists.txt includes this file for a cross build with
# cmake/gcc_riscv.cmake.

if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "RISCV")
    message(FATAL_ERROR "BOARD=openmuppv1 needs the RISC-V toolchain: use the preset openmuppv1 "
        "(toolchain file cmake/gcc_riscv.cmake)")
endif()

# The source of ch32v00x-system. The default is the submodule. A parent
# project (OpenMUPP) passes its own copy, so both projects use the same
# library commit.
set(MUPP_CH32V00X_SYSTEM_DIR "${PROJECT_SOURCE_DIR}/dependencies/ch32v00x-system" CACHE PATH
    "Source directory of ch32v00x-system")
if(NOT EXISTS "${MUPP_CH32V00X_SYSTEM_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "ch32v00x-system not found in ${MUPP_CH32V00X_SYSTEM_DIR}. Run: "
        "git submodule update --init, or set MUPP_CH32V00X_SYSTEM_DIR")
endif()
if(TARGET ch32v00x-system OR TARGET art-build-config)
    message(FATAL_ERROR "The parent project already has the target ch32v00x-system or art-build-config. "
        "Build the bootloader with ExternalProject and pass MUPP_CH32V00X_SYSTEM_DIR.")
endif()

option(MUPP_LTO "Link-time optimisation" ON)

message(STATUS "Cross-compiling openmupp-bootloader for the CH32V307 (board: openmuppv1)")

# The flags of the size spike of the STM32F1 bootloader: no exceptions, no
# RTTI, no heap, LTO.
set(COMMON_OPTIONS -march=rv32imafc_zicsr_zifencei -mabi=ilp32f -Os -g
    -ffunction-sections -fdata-sections -fno-common -fsigned-char
    --specs=nano.specs --specs=nosys.specs)
if(MUPP_LTO)
    list(APPEND COMMON_OPTIONS -flto)
endif()
set(CXX_OPTIONS -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics -Wno-volatile)

include(BuildConfig)
art_setup_build_config(art-build-config)

set(CH32_FAMILY "CH32V30X" CACHE STRING "" FORCE)
set(CH32_DEVICE "CH32V30x_D8C" CACHE STRING "" FORCE)
set(CH32V00X_SYSTEM_HSE_VALUE "8000000" CACHE STRING "" FORCE)
add_subdirectory("${MUPP_CH32V00X_SYSTEM_DIR}" ch32v00x-system EXCLUDE_FROM_ALL)

include(output_files)

set(_target openmupp-bootloader-openmuppv1)
add_executable(${_target}
    "${PROJECT_SOURCE_DIR}/src/ch32v30x/main.cpp"
    "${PROJECT_SOURCE_DIR}/src/ch32v30x/Boot.cpp"
    "${PROJECT_SOURCE_DIR}/src/ch32v30x/Flash.cpp"
    "${PROJECT_SOURCE_DIR}/src/ch32v30x/UsbHs.cpp"
    "${PROJECT_SOURCE_DIR}/src/protocol/DfuProtocol.cpp")
set_target_properties(${_target} PROPERTIES
    SUFFIX .elf
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(${_target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(${_target} PRIVATE BOARD_OPENMUPPV1)
target_compile_options(${_target} PRIVATE
    -Wall -Wextra -Wconversion
    $<$<BOOL:${MUPP_WERROR}>:-Werror>
    $<$<BOOL:${MUPP_LTO}>:-ffat-lto-objects>
    "-ffile-prefix-map=${PROJECT_SOURCE_DIR}=.")
target_link_libraries(${_target} PRIVATE art-build-config mupp-layout ch32v00x-system)

set(_ld_dir "${MUPP_CH32V00X_SYSTEM_DIR}/ldscripts")
set(_map "${CMAKE_CURRENT_BINARY_DIR}/${_target}.map")
target_link_options(${_target} PRIVATE
    ${COMMON_OPTIONS} -nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments
    -Wl,--defsym=__flash_length=${OPENMUPP_BOOTLOADER_AREA_SIZE}
    "-L${PROJECT_SOURCE_DIR}/ldscripts" "-L${_ld_dir}"
    -Tmem-ch32v307-bootloader.ld -Tsections.ld -Tnoheap.ld
    "-Wl,-Map,${_map}")
set_property(TARGET ${_target} APPEND PROPERTY LINK_DEPENDS
    "${PROJECT_SOURCE_DIR}/ldscripts/mem-ch32v307-bootloader.ld" "${_ld_dir}/sections.ld" "${_ld_dir}/noheap.ld")
set_property(TARGET ${_target} APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${_map}")

ADD_DUMP_TARGET(${_target})
PRINT_SIZE_OF_TARGETS(${_target})

set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_target}")
add_custom_command(TARGET ${_target} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${_target}>" "${_out}.hex"
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${_target}>" "${_out}.bin"
    COMMAND "${CMAKE_COMMAND}" -DIMAGE=${_out}.bin
            "-DREMOVE_ON_FAIL=${_out}.hex$<SEMICOLON>$<TARGET_FILE:${_target}>"
            -DMAX_SIZE=${OPENMUPP_BOOTLOADER_AREA_SIZE}
            -P "${PROJECT_SOURCE_DIR}/cmake/check-size.cmake"
    BYPRODUCTS "${_out}.hex" "${_out}.bin"
    VERBATIM)

install(FILES "${_out}.elf" "${_out}.hex" "${_out}.bin" DESTINATION .)
