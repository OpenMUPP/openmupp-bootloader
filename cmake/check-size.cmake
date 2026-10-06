# Fail when an image is larger than the bootloader area.
# Usage: cmake -DIMAGE=<file> -DMAX_SIZE=<bytes> -P check-size.cmake
file(SIZE "${IMAGE}" _size)
if(_size GREATER MAX_SIZE)
    message(FATAL_ERROR
        "${IMAGE} is ${_size} bytes. The bootloader area is ${MAX_SIZE} bytes.")
endif()
message(STATUS "${IMAGE}: ${_size} of ${MAX_SIZE} bytes")
