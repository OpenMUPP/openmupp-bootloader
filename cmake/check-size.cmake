# Fail when an image is larger than the bootloader area. The check removes
# the image and the files in REMOVE_ON_FAIL, so nobody can flash them.
# Usage: cmake -DIMAGE=<file> -DMAX_SIZE=<bytes> [-DREMOVE_ON_FAIL=<list>]
#              -P check-size.cmake
file(SIZE "${IMAGE}" _size)
if(_size GREATER MAX_SIZE)
    file(REMOVE "${IMAGE}" ${REMOVE_ON_FAIL})
    message(FATAL_ERROR
        "${IMAGE} is ${_size} bytes. The bootloader area is ${MAX_SIZE} bytes.")
endif()
message(STATUS "${IMAGE}: ${_size} of ${MAX_SIZE} bytes")
