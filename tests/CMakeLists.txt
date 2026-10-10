# Host unit tests. They build with the host compiler: use the preset
# host-tests (VSL_HOST_TESTS=ON), not a board preset.
include(FetchContent)

FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.12
        GIT_SHALLOW TRUE
)
set(DOCTEST_NO_INSTALL ON)
FetchContent_MakeAvailable(doctest)

add_executable(vsl-bootloader-tests
        main.cpp
        TestLayout.cpp)
set_target_properties(vsl-bootloader-tests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON)
target_include_directories(vsl-bootloader-tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_options(vsl-bootloader-tests PRIVATE -Wall -Wextra -Wconversion
        $<$<BOOL:${VSL_WERROR}>:-Werror>)
target_link_libraries(vsl-bootloader-tests PRIVATE vsl-layout doctest::doctest)

add_test(NAME vsl-bootloader-tests COMMAND vsl-bootloader-tests)
