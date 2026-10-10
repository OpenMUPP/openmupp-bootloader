# Test project for cmake/VslStampImage.cmake. tools/tests runs it.
cmake_minimum_required(VERSION 3.21)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/arm-none-eabi.cmake")
project(stamp-project LANGUAGES C)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/VslStampImage.cmake")

add_executable(app app.c)
set_target_properties(app PROPERTIES SUFFIX .elf)
target_compile_options(app PRIVATE -mcpu=cortex-m3 -mthumb -Os)
target_link_options(app PRIVATE -mcpu=cortex-m3 -mthumb -nostdlib
    "-T${CMAKE_CURRENT_LIST_DIR}/app.ld")
vsl_stamp_image(app
    BIN "${CMAKE_CURRENT_BINARY_DIR}/app.bin"
    HEX "${CMAKE_CURRENT_BINARY_DIR}/app.hex"
    MAX_LENGTH 55296)
