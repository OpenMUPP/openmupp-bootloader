#!/usr/bin/env python3
"""Add the versaloon application image header to an ELF image.

The header uses the reserved words of the Cortex-M3 vector table:

    0x1C  magic 0x314C5356 ("VSL1")
    0x20  image length in bytes, a multiple of 4, from the application start
    0x24  CRC32 of the image (STM32F1 CRC unit, header words count as zero)
    0x28  reserved, 0

The tool writes the header into the .isr_vector section of the ELF file
(objcopy --update-section). It can also write the stamped image as .bin and
.hex. The bootloader starts only an application with a valid header.

Usage:
    mupp-stamp-image.py <image.elf> [--bin <out.bin>] [--hex <out.hex>]
    mupp-stamp-image.py --check <image.bin> --base <address>

Python 3.7 or later, standard library only. The ELF mode needs
arm-none-eabi-objcopy.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

MAGIC = 0x314C5356
MAGIC_OFFSET = 0x1C
LENGTH_OFFSET = 0x20
CRC_OFFSET = 0x24
RESERVED_OFFSET = 0x28
HEADER_START = MAGIC_OFFSET
HEADER_END = RESERVED_OFFSET + 4
# Words that the CRC counts as zero: magic, length and CRC.
CRC_ZERO_START = MAGIC_OFFSET
CRC_ZERO_END = CRC_OFFSET + 4

MIN_LENGTH = 64
# Largest flash of the STM32F103x8/xB. Without --max-length, the tool
# refuses a larger image, for example a section in RAM without a load address.
MAX_FLASH = 128 * 1024
PT_LOAD = 1
SHT_NOBITS = 8
SHF_ALLOC = 2
RAM_START = 0x20000000
RAM_SIZE = 20 * 1024
VECTOR_SECTION = ".isr_vector"

CRC_POLY = 0x04C11DB7


class StampError(Exception):
    pass


def _make_crc_table():
    table = []
    for i in range(256):
        crc = i << 24
        for _ in range(8):
            crc = ((crc << 1) ^ CRC_POLY) if crc & 0x80000000 else (crc << 1)
        table.append(crc & 0xFFFFFFFF)
    return table


_CRC_TABLE = _make_crc_table()


def crc32_stm32(data, crc=0xFFFFFFFF):
    """CRC of the STM32F1 CRC unit (CRC-32/MPEG-2 on 32-bit words).

    The data is a sequence of 32-bit little-endian words. The unit shifts
    each word in with the most significant bit first. No reflection and no
    final XOR.
    """
    if len(data) % 4:
        raise ValueError("the length must be a multiple of 4")
    table = _CRC_TABLE
    for (word,) in struct.iter_unpack("<I", data):
        for shift in (24, 16, 8, 0):
            crc = ((crc << 8) & 0xFFFFFFFF) ^ table[((crc >> 24) ^ (word >> shift)) & 0xFF]
    return crc


def _word(image, offset):
    return struct.unpack_from("<I", image, offset)[0]


def image_crc(image):
    """CRC of an image with the magic, length and CRC words as zero."""
    data = bytearray(image)
    data[CRC_ZERO_START:CRC_ZERO_END] = bytes(CRC_ZERO_END - CRC_ZERO_START)
    return crc32_stm32(bytes(data))


def stamp(image):
    """Return the image with the header. The image starts with the vector table.

    The image is padded with 0xFF (erased flash) to a multiple of 4 bytes.
    The reserved words must be zero. An image that already has a header is
    stamped again.
    """
    image = bytearray(image)
    if len(image) < MIN_LENGTH:
        raise StampError(f"the image is {len(image)} bytes, the minimum is {MIN_LENGTH} bytes")
    if len(image) % 4:
        image += b"\xff" * (4 - len(image) % 4)
    has_header = _word(image, MAGIC_OFFSET) == MAGIC
    if not has_header and any(image[HEADER_START:HEADER_END]):
        raise StampError(
            f"the vector table words 0x{HEADER_START:02X}-0x{HEADER_END - 1:02X} are not zero; "
            "the image uses them for another purpose")
    if _word(image, RESERVED_OFFSET) != 0:
        raise StampError(f"the reserved header word 0x{RESERVED_OFFSET:02X} is not zero")
    struct.pack_into("<II", image, MAGIC_OFFSET, MAGIC, len(image))
    struct.pack_into("<I", image, CRC_OFFSET, image_crc(image))
    return bytes(image)


def check(image, base, max_length=None):
    """Return a list of the rules that the image breaks. Empty means valid.

    The image is the flash content from the application start (base). It can
    be longer than the length in the header, for example a flash dump.
    """
    if len(image) < MIN_LENGTH:
        return [f"the image is shorter than {MIN_LENGTH} bytes"]
    errors = []
    magic = _word(image, MAGIC_OFFSET)
    if magic != MAGIC:
        return [f"the magic is 0x{magic:08X}, not 0x{MAGIC:08X}"]
    length = _word(image, LENGTH_OFFSET)
    limit = len(image) if max_length is None else min(len(image), max_length)
    if length % 4:
        return [f"the length {length} is not a multiple of 4"]
    if not MIN_LENGTH <= length <= limit:
        return [f"the length {length} is not between {MIN_LENGTH} and {limit} bytes"]
    crc = _word(image, CRC_OFFSET)
    expected = image_crc(image[:length])
    if crc != expected:
        errors.append(f"the CRC is 0x{crc:08X}, expected 0x{expected:08X}")
    sp = _word(image, 0)
    if not RAM_START <= sp <= RAM_START + RAM_SIZE:
        errors.append(f"the initial SP 0x{sp:08X} is not in RAM")
    reset = _word(image, 4)
    if not reset & 1 or not base <= (reset & ~1) < base + length:
        errors.append(f"the reset vector 0x{reset:08X} is not odd or not inside the image")
    return errors


def _loaded_sections(elf_data):
    """Return {name: (load address, size)} of the sections with content in flash.

    These are the sections that objcopy -O binary writes. The load address
    (LMA) comes from the program header of the segment that holds the
    section.
    """
    if elf_data[:4] != b"\x7fELF" or elf_data[4] != 1 or elf_data[5] != 1:
        raise StampError("not an ELF32 little-endian file")
    phoff, shoff = struct.unpack_from("<II", elf_data, 0x1C)
    phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from("<HHHHH", elf_data, 0x2A)

    segments = []
    for index in range(phnum):
        p_type, _, p_vaddr, p_paddr, p_filesz, _ = struct.unpack_from(
            "<IIIIII", elf_data, phoff + index * phentsize)
        if p_type == PT_LOAD:
            segments.append((p_vaddr, p_paddr, p_filesz))

    def section(index):
        return struct.unpack_from("<IIIIII", elf_data, shoff + index * shentsize)

    strtab_offset = section(shstrndx)[4]
    sections = {}
    for index in range(shnum):
        name_offset, sh_type, flags, vma, _, size = section(index)
        if not flags & SHF_ALLOC or sh_type == SHT_NOBITS or size == 0:
            continue
        start = strtab_offset + name_offset
        name = elf_data[start:elf_data.index(b"\0", start)].decode()
        for p_vaddr, p_paddr, p_filesz in segments:
            if p_vaddr <= vma and vma + size <= p_vaddr + p_filesz:
                sections[name] = (p_paddr + (vma - p_vaddr), size)
                break
        else:
            raise StampError(f"no loadable segment holds the {name} section")
    return sections


def _run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise StampError(f"{' '.join(command)} failed:\n{result.stderr.strip()}")


def stamp_elf(elf, objcopy, bin_out=None, hex_out=None, max_length=None):
    """Write the header into the .isr_vector section of the ELF file."""
    with open(elf, "rb") as f:
        elf_data = f.read()
    sections = _loaded_sections(elf_data)
    if VECTOR_SECTION not in sections:
        raise StampError(f"the ELF file has no {VECTOR_SECTION} section")
    address, size = sections[VECTOR_SECTION]
    if size < HEADER_END:
        raise StampError(f"the {VECTOR_SECTION} section is only {size} bytes")
    # Check the size before objcopy makes the binary image: a section in RAM
    # without a load address in flash gives an image of hundreds of MB.
    limit = MAX_FLASH if max_length is None else max_length
    span = (max(lma + size for lma, size in sections.values())
            - min(lma for lma, _ in sections.values()))
    if span > limit:
        raise StampError(f"the image is {span} bytes, the limit is {limit} bytes")

    with tempfile.TemporaryDirectory() as tmp:
        image_file = os.path.join(tmp, "image.bin")
        vectors_file = os.path.join(tmp, "vectors.bin")
        # Fill the gaps between the sections with 0xFF, as in erased flash. A
        # debug probe writes only the sections, so the gaps stay erased.
        _run([objcopy, "-O", "binary", "--gap-fill", "0xff", elf, image_file])
        _run([objcopy, "-O", "binary", "-j", VECTOR_SECTION, elf, vectors_file])
        with open(image_file, "rb") as f:
            image = f.read()
        with open(vectors_file, "rb") as f:
            vectors = f.read()
        if not image.startswith(vectors):
            raise StampError(f"the {VECTOR_SECTION} section is not at the start of the image")

        stamped = stamp(image)
        errors = check(stamped, address, max_length)
        if errors:
            raise StampError("the stamped image is not valid: " + "; ".join(errors))

        with open(vectors_file, "wb") as f:
            f.write(stamped[:len(vectors)])
        # Write a copy next to the ELF file, then replace the ELF file in one
        # step. A failure does not leave a damaged ELF file.
        stamped_elf = elf + ".stamp.tmp"
        try:
            _run([objcopy, "--update-section", f"{VECTOR_SECTION}={vectors_file}", elf, stamped_elf])
            shutil.copymode(elf, stamped_elf)
            os.replace(stamped_elf, elf)
        finally:
            if os.path.exists(stamped_elf):
                os.remove(stamped_elf)

        # The .bin and .hex files are the full image with the gaps and the
        # padding as 0xFF, the same bytes as the CRC.
        if bin_out:
            with open(bin_out, "wb") as f:
                f.write(stamped)
        if hex_out:
            _run([objcopy, "-I", "binary", "-O", "ihex", "--change-addresses", hex(address),
                  bin_out or _write(os.path.join(tmp, "stamped.bin"), stamped), hex_out])
    return address, stamped


def _write(path, data):
    with open(path, "wb") as f:
        f.write(data)
    return path


def _int(text):
    return int(text, 0)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("image", help="ELF file to stamp, or the .bin file with --check")
    parser.add_argument("--bin", help="write the stamped image as a binary file")
    parser.add_argument("--hex", help="write the stamped image as an Intel HEX file")
    parser.add_argument("--objcopy", default="arm-none-eabi-objcopy",
                        help="objcopy tool (default: %(default)s)")
    parser.add_argument("--max-length", type=_int,
                        help="size of the application area in bytes")
    parser.add_argument("--check", action="store_true",
                        help="check a stamped .bin file and do not change it")
    parser.add_argument("--base", type=_int,
                        help="application start address, for --check")
    args = parser.parse_args(argv)

    try:
        if args.check:
            if args.base is None:
                parser.error("--check needs --base")
            with open(args.image, "rb") as f:
                errors = check(f.read(), args.base, args.max_length)
            for error in errors:
                print(f"{args.image}: {error}", file=sys.stderr)
            if not errors:
                print(f"{args.image}: valid application image")
            return 1 if errors else 0

        address, stamped = stamp_elf(args.image, args.objcopy, args.bin, args.hex, args.max_length)
        print(f"{args.image}: stamped, {len(stamped)} bytes at 0x{address:08X}, "
              f"CRC 0x{_word(stamped, CRC_OFFSET):08X}")
        return 0
    except (OSError, StampError) as error:
        print(f"{args.image}: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
