# Adds the versaloon application image header to an ELF target.
#
#   include(<openmupp-bootloader>/cmake/VslStampImage.cmake)
#   vsl_stamp_image(<target> [BIN <file>] [HEX <file>] [MAX_LENGTH <bytes>])
#
# A post-build step runs tools/vsl-stamp-image.py. It writes the header into
# the .isr_vector section of the ELF file. BIN and HEX also write the stamped
# image as a binary file and an Intel HEX file. MAX_LENGTH is the size of the
# application area: the build fails when the image is larger.
#
# Program the BIN or HEX files of this step. Their gaps between the
# sections are 0xFF, as the CRC expects. A separate "objcopy -O binary" step
# fills the gaps with 0x00, which gives a wrong CRC. A separate step needs
# --gap-fill 0xff and must come after vsl_stamp_image(): post-build steps
# run in the order of the calls.
#
# The step needs Python 3.7 and CMAKE_OBJCOPY.

set(_VSL_STAMP_IMAGE_TOOL "${CMAKE_CURRENT_LIST_DIR}/../tools/vsl-stamp-image.py")

function(vsl_stamp_image target)
    cmake_parse_arguments(PARSE_ARGV 1 _arg "" "BIN;HEX;MAX_LENGTH" "")
    if(_arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "vsl_stamp_image: unknown arguments: ${_arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT CMAKE_OBJCOPY)
        message(FATAL_ERROR "vsl_stamp_image: CMAKE_OBJCOPY is not set")
    endif()
    find_package(Python3 3.7 REQUIRED COMPONENTS Interpreter)

    get_filename_component(_tool "${_VSL_STAMP_IMAGE_TOOL}" ABSOLUTE)
    set(_options --objcopy "${CMAKE_OBJCOPY}")
    set(_byproducts)
    foreach(_kind BIN HEX)
        if(_arg_${_kind})
            string(TOLOWER "${_kind}" _flag)
            list(APPEND _options --${_flag} "${_arg_${_kind}}")
            list(APPEND _byproducts "${_arg_${_kind}}")
        endif()
    endforeach()
    if(_arg_MAX_LENGTH)
        list(APPEND _options --max-length ${_arg_MAX_LENGTH})
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${_tool}" "$<TARGET_FILE:${target}>" ${_options}
        BYPRODUCTS ${_byproducts}
        COMMENT "Adding the application image header to ${target}"
        VERBATIM)
endfunction()
