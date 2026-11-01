"""Tests for tools/mupp-stamp-image.py.

Run from the repository root:
    python3 -m unittest discover -s tools/tests

The ELF tests need arm-none-eabi-gcc and arm-none-eabi-objcopy in the PATH,
the CMake test also needs cmake. Without them, these tests are skipped.
"""

import importlib.util
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VECTORS = os.path.join(ROOT, "tests", "vectors", "crc32-stm32.txt")

_spec = importlib.util.spec_from_file_location(
    "mupp_stamp_image", os.path.join(ROOT, "tools", "mupp-stamp-image.py"))
tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tool)

BASE = 0x08002800
GCC = shutil.which("arm-none-eabi-gcc")
OBJCOPY = shutil.which("arm-none-eabi-objcopy")


def load_vectors():
    vectors = []
    with open(VECTORS) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            crc, data = line.split()
            vectors.append((int(crc, 16), b"" if data == "-" else bytes.fromhex(data)))
    return vectors


def reference_crc(data):
    """Bit-by-bit model of the STM32F1 CRC unit, independent of the tool."""
    crc = 0xFFFFFFFF
    for (word,) in struct.iter_unpack("<I", data):
        crc ^= word
        for _ in range(32):
            crc = ((crc << 1) ^ 0x04C11DB7) if crc & 0x80000000 else (crc << 1)
            crc &= 0xFFFFFFFF
    return crc


def reference_crc_bytes(data):
    """CRC-32/MPEG-2 on single bytes, to check the model with the catalogue value."""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) if crc & 0x80000000 else (crc << 1)
            crc &= 0xFFFFFFFF
    return crc


def make_image(length=256, sp=0x20005000, reset=BASE + 0x41):
    """An unstamped image: a vector table with zero reserved words, then a pattern."""
    image = bytearray((i * 7 + 3) & 0xFF for i in range(length))
    struct.pack_into("<II", image, 0, sp, reset)
    image[0x1C:0x2C] = bytes(16)
    return bytes(image)


class CrcTest(unittest.TestCase):
    def test_reference_model_matches_catalogue(self):
        # CRC-32/MPEG-2 check value from the CRC catalogue.
        self.assertEqual(reference_crc_bytes(b"123456789"), 0x0376E6E7)

    def test_reference_word_model_matches_byte_model(self):
        # The unit takes each word with the most significant byte first.
        data = bytes(range(64))
        swapped = b"".join(data[i:i + 4][::-1] for i in range(0, len(data), 4))
        self.assertEqual(reference_crc(data), reference_crc_bytes(swapped))

    def test_known_stm32_value(self):
        # The value that the STM32 CRC unit gives for the word 0x12345678.
        self.assertEqual(tool.crc32_stm32(struct.pack("<I", 0x12345678)), 0xDF8A8A2B)

    def test_shared_vectors(self):
        vectors = load_vectors()
        self.assertGreaterEqual(len(vectors), 5)
        for crc, data in vectors:
            with self.subTest(data=data.hex()):
                self.assertEqual(reference_crc(data), crc)
                self.assertEqual(tool.crc32_stm32(data), crc)

    def test_rejects_partial_word(self):
        with self.assertRaises(ValueError):
            tool.crc32_stm32(b"123")


class StampTest(unittest.TestCase):
    def test_header_fields(self):
        image = make_image()
        stamped = tool.stamp(image)
        magic, length, crc, reserved = struct.unpack_from("<IIII", stamped, 0x1C)
        self.assertEqual(magic, 0x314C5356)
        self.assertEqual(length, len(image))
        self.assertEqual(crc, reference_crc(image))  # header words are zero in the input
        self.assertEqual(reserved, 0)
        # Only the header words change.
        self.assertEqual(stamped[:0x1C], image[:0x1C])
        self.assertEqual(stamped[0x2C:], image[0x2C:])

    def test_magic_in_memory_is_vsl1(self):
        self.assertEqual(tool.stamp(make_image())[0x1C:0x20], b"VSL1")

    def test_stamped_image_is_valid(self):
        self.assertEqual(tool.check(tool.stamp(make_image()), BASE, 54 * 1024), [])

    def test_stamp_again_gives_the_same_image(self):
        stamped = tool.stamp(make_image())
        self.assertEqual(tool.stamp(stamped), stamped)

    def test_pads_to_word(self):
        stamped = tool.stamp(make_image(length=258))
        self.assertEqual(len(stamped), 260)
        self.assertEqual(stamped[-2:], b"\xff\xff")
        self.assertEqual(struct.unpack_from("<I", stamped, 0x20)[0], 260)
        self.assertEqual(tool.check(stamped, BASE), [])

    def test_refuses_used_reserved_words(self):
        for offset in range(0x1C, 0x2C, 4):
            image = bytearray(make_image())
            struct.pack_into("<I", image, offset, 0x08002101)
            with self.subTest(offset=hex(offset)), self.assertRaises(tool.StampError):
                tool.stamp(bytes(image))

    def test_refuses_short_image(self):
        with self.assertRaises(tool.StampError):
            tool.stamp(make_image()[:60])


class CheckTest(unittest.TestCase):
    def setUp(self):
        self.image = bytearray(tool.stamp(make_image()))

    def errors(self, image=None, base=BASE, max_length=None):
        return tool.check(bytes(self.image if image is None else image), base, max_length)

    def test_unstamped_image(self):
        self.assertTrue(self.errors(make_image()))

    def test_blank_flash(self):
        self.assertTrue(self.errors(b"\xff" * 1024))

    def test_wrong_crc(self):
        struct.pack_into("<I", self.image, 0x24, struct.unpack_from("<I", self.image, 0x24)[0] ^ 1)
        self.assertTrue(self.errors())

    def test_changed_byte(self):
        self.image[200] ^= 0x01
        self.assertTrue(self.errors())

    def test_half_written_image(self):
        # The vector table is written, the rest is erased.
        image = self.image[:64] + b"\xff" * (len(self.image) - 64)
        self.assertTrue(self.errors(image))

    def test_length_rules(self):
        for length in (0, 60, 62, 257, len(self.image) + 4):
            image = bytearray(self.image)
            struct.pack_into("<I", image, 0x20, length)
            with self.subTest(length=length):
                self.assertTrue(self.errors(image))

    def test_length_above_area(self):
        self.assertTrue(self.errors(max_length=len(self.image) - 4))
        self.assertEqual(self.errors(max_length=len(self.image)), [])

    def test_flash_dump_longer_than_image(self):
        self.assertEqual(self.errors(bytes(self.image) + b"\xff" * 1024), [])

    def test_stack_pointer_outside_ram(self):
        for sp in (0x1FFFFFFC, 0x20005004, 0x08002000):
            with self.subTest(sp=hex(sp)):
                self.assertTrue(self.errors(tool.stamp(make_image(sp=sp))))

    def test_stack_pointer_at_ram_end(self):
        self.assertEqual(self.errors(tool.stamp(make_image(sp=0x20005000))), [])

    def test_reset_vector(self):
        for reset in (BASE + 0x40, BASE - 0x3FF, BASE + 256 + 1, 0x08000101):
            with self.subTest(reset=hex(reset)):
                self.assertTrue(self.errors(tool.stamp(make_image(reset=reset))))


PROJECT = os.path.join(ROOT, "tools", "tests", "stamp-project")
CMAKE = shutil.which("cmake")


@unittest.skipUnless(GCC and OBJCOPY, "arm-none-eabi-gcc and arm-none-eabi-objcopy not found")
class ElfTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = self.tmp.name
        self.elf = self.path("app.elf")
        self.link(os.path.join(PROJECT, "app.ld"))

    def path(self, name):
        return os.path.join(self.dir, name)

    def link(self, linker_script):
        subprocess.run([GCC, "-mcpu=cortex-m3", "-mthumb", "-Os", "-nostdlib",
                        "-T", linker_script, os.path.join(PROJECT, "app.c"), "-o", self.elf],
                       check=True)

    def link_with(self, replace, by):
        with open(os.path.join(PROJECT, "app.ld")) as f:
            script = f.read()
        self.assertIn(replace, script)
        with open(self.path("changed.ld"), "w") as f:
            f.write(script.replace(replace, by))
        self.link(self.path("changed.ld"))

    def probe_view(self, elf):
        """The flash content after a probe writes the ELF: only the sections, the rest erased."""
        hex_file = self.path("probe.hex")
        out = self.path("probe.bin")
        subprocess.run([OBJCOPY, "-O", "ihex", elf, hex_file], check=True)
        subprocess.run([OBJCOPY, "-I", "ihex", "-O", "binary", "--gap-fill", "0xff", hex_file, out],
                       check=True)
        with open(out, "rb") as f:
            return f.read()

    def objcopy_bin(self, elf):
        out = self.path("objcopy.bin")
        subprocess.run([OBJCOPY, "-O", "binary", "--gap-fill", "0xff", elf, out], check=True)
        with open(out, "rb") as f:
            return f.read()

    def run_tool(self, *args):
        return tool.main([self.elf, "--objcopy", OBJCOPY, *args])

    def test_stamps_elf_bin_and_hex(self):
        unstamped = self.objcopy_bin(self.elf)
        self.assertEqual(self.run_tool("--bin", self.path("app.bin"),
                                       "--hex", self.path("app.hex")), 0)

        with open(self.path("app.bin"), "rb") as f:
            stamped = f.read()
        self.assertEqual(tool.check(stamped, BASE, 54 * 1024), [])
        self.assertEqual(stamped, tool.stamp(unstamped))

        # The ELF has the same content, so a probe also writes the header.
        self.assertEqual(self.probe_view(self.elf), stamped)

        # The HEX file has the same content at the application start.
        hex_bin = self.path("hex.bin")
        subprocess.run([OBJCOPY, "-I", "ihex", "-O", "binary", self.path("app.hex"), hex_bin],
                       check=True)
        with open(hex_bin, "rb") as f:
            self.assertEqual(f.read(), stamped)
        with open(self.path("app.hex")) as f:
            hex_text = f.read()
        # Extended linear address record for 0x0800, first data record at 0x2800.
        self.assertIn(":020000040800F2", hex_text)
        self.assertIn(":102800", hex_text)

    def test_gap_between_sections_counts_as_erased(self):
        out = self.path("zero-gap.bin")
        subprocess.run([OBJCOPY, "-O", "binary", self.elf, out], check=True)
        with open(out, "rb") as f:
            zero_gap = f.read()
        # The test image has a gap; objcopy fills it with 0x00 by default.
        self.assertNotEqual(zero_gap, self.objcopy_bin(self.elf))
        self.assertEqual(self.run_tool(), 0)
        self.assertEqual(tool.check(self.probe_view(self.elf), BASE, 54 * 1024), [])

    def test_vector_table_with_load_address(self):
        # .isr_vector linked into RAM and loaded from flash: the header and the
        # HEX file use the load address.
        self.link_with(".isr_vector : { KEEP(*(.isr_vector)) } > FLASH",
                       ".isr_vector : { KEEP(*(.isr_vector)) } > RAM AT > FLASH")
        self.assertEqual(self.run_tool("--hex", self.path("app.hex")), 0)
        self.assertEqual(tool.check(self.probe_view(self.elf), BASE, 54 * 1024), [])
        with open(self.path("app.hex")) as f:
            self.assertIn(":102800", f.read())

    def test_section_without_load_address(self):
        # .data in RAM without AT > FLASH: the image would span 0x08002800 to
        # 0x20000000. The tool refuses it without a large temporary file.
        self.link_with(".data : { *(.data*) } > RAM AT > FLASH", ".data : { *(.data*) } > RAM")
        before = self.probe_view(self.elf)
        self.assertEqual(self.run_tool(), 1)
        self.assertEqual(self.probe_view(self.elf), before)

    def test_no_temporary_file_left(self):
        self.assertEqual(self.run_tool(), 0)
        self.assertEqual(sorted(os.listdir(self.dir)), ["app.elf"])

    def test_stamp_twice(self):
        self.assertEqual(self.run_tool(), 0)
        first = self.objcopy_bin(self.elf)
        self.assertEqual(self.run_tool(), 0)
        self.assertEqual(self.objcopy_bin(self.elf), first)

    def test_check_mode(self):
        self.assertEqual(self.run_tool("--bin", self.path("app.bin")), 0)
        self.assertEqual(tool.main([self.path("app.bin"), "--check", "--base", hex(BASE)]), 0)
        with open(self.path("bad.bin"), "wb") as f:
            f.write(self.objcopy_bin(self.elf)[:64] + b"\xff" * 64)
        self.assertEqual(tool.main([self.path("bad.bin"), "--check", "--base", hex(BASE)]), 1)

    def test_area_too_small(self):
        before = self.objcopy_bin(self.elf)
        self.assertEqual(self.run_tool("--max-length", "64"), 1)
        self.assertEqual(self.objcopy_bin(self.elf), before)


@unittest.skipUnless(GCC and OBJCOPY and CMAKE, "cmake or the Arm GNU toolchain not found")
class CmakeTest(unittest.TestCase):
    def test_mupp_stamp_image(self):
        with tempfile.TemporaryDirectory() as build:
            subprocess.run([CMAKE, "-S", PROJECT, "-B", build], check=True,
                           stdout=subprocess.DEVNULL)
            subprocess.run([CMAKE, "--build", build], check=True, stdout=subprocess.DEVNULL)
            with open(os.path.join(build, "app.bin"), "rb") as f:
                stamped = f.read()
            self.assertEqual(tool.check(stamped, BASE, 54 * 1024), [])
            self.assertTrue(os.path.exists(os.path.join(build, "app.hex")))
            elf_bin = os.path.join(build, "elf.bin")
            subprocess.run([OBJCOPY, "-O", "binary", "--gap-fill", "0xff",
                            os.path.join(build, "app.elf"), elf_bin],
                           check=True)
            with open(elf_bin, "rb") as f:
                self.assertEqual(f.read(), stamped)


if __name__ == "__main__":
    unittest.main()
