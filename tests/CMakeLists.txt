# Host unit tests. They build with the host compiler: use the preset
# host-tests (MUPP_HOST_TESTS=ON), not a board preset.
include(FetchContent)

FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.12
        GIT_SHALLOW TRUE
)
set(DOCTEST_NO_INSTALL ON)
FetchContent_MakeAvailable(doctest)

add_executable(mupp-bootloader-tests
        main.cpp
        TestAn3155Protocol.cpp
        TestBootDecision.cpp
        TestFlashRules.cpp
        TestImageHeader.cpp
        TestLayout.cpp
        TestMemoryMap.cpp
        TestOptionByteValues.cpp
        TestStm32flashSequences.cpp
        "${PROJECT_SOURCE_DIR}/src/protocol/An3155Protocol.cpp")
set_target_properties(mupp-bootloader-tests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON)
target_include_directories(mupp-bootloader-tests PRIVATE "${PROJECT_SOURCE_DIR}/src" .)
# The CRC test vectors, shared with the tests of tools/mupp-stamp-image.py.
target_compile_definitions(mupp-bootloader-tests PRIVATE
        MUPP_CRC_VECTORS="${CMAKE_CURRENT_LIST_DIR}/vectors/crc32-stm32.txt")
target_compile_options(mupp-bootloader-tests PRIVATE -Wall -Wextra -Wconversion
        $<$<BOOL:${MUPP_WERROR}>:-Werror>)
target_link_libraries(mupp-bootloader-tests PRIVATE mupp-layout doctest::doctest)

add_test(NAME mupp-bootloader-tests COMMAND mupp-bootloader-tests)
