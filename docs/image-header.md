# Application image header

Each application image has a header with a CRC (cyclic redundancy
check). The bootloader starts only an application with a valid header.
Thus a blank or half-written application does not start: the board stays
in the bootloader. An image without the header also stays in the
bootloader.

The bootloader calculates the CRC with the CRC unit of the MCU, at each
start. The CH32V307 CRC unit gives the same CRC as the STM32F1 unit.
On the STM32F1, 56 KB take some ms.

The header uses the reserved words of the Cortex-M3 vector table, at the
start of the application. A CH32V307 image starts with the `j _start` of
the reset handler, and its vector table follows at offset 4. The header
has the same offsets there. On the QingKe V4F, `0x1C` and `0x28` are
reserved exceptions. `0x20` is the Ecall-U entry and `0x24` is the
BreakPoint entry. The firmware runs in machine mode, so it does not use
Ecall-U. The firmware must not use a BreakPoint handler: without a
debugger, an `ebreak` jumps to the CRC value.

| Offset | Content |
|---|---|
| `0x1C` | Magic `0x314C5356` (`"VSL1"` in memory) |
| `0x20` | Image length in bytes, a multiple of 4, from the application start |
| `0x24` | CRC32 of the image |
| `0x28` | Reserved, `0` |

The CRC uses the algorithm of the STM32F1 CRC unit (CRC-32/MPEG-2):

- Polynomial `0x04C11DB7`, initial value `0xFFFFFFFF`.
- The input is 32-bit little-endian words. The unit reads each word from
  the most significant bit.
- No reflection and no final XOR.

The CRC covers the full image. The words at `0x1C` to `0x27` count as
zero. A gap between two sections of the image counts as `0xFF` (erased
flash), because a debug probe does not write the gaps. The test vectors
are in `tests/vectors/crc32-stm32.txt`.

An application is valid when all of these rules are true:

- The magic is correct.
- The length is a multiple of 4, between 64 bytes and the size of the
  application area.
- The CRC is correct.
- Cortex-M3: the initial stack pointer (word `0x00`) is in RAM, and the
  reset vector (word `0x04`) is odd and inside the image.
- RISC-V (CH32V307): word `0x00` is a `j` (JAL x0) to an address inside
  the image, behind the header. The handler address at `0x08` (the NMI
  entry) is inside the image. The image starts at `0x00002000`, the
  application start in the flash alias. The `j` is PC-relative, so only
  this handler address shows an image with a different link address.

## Stamp tool

`tools/mupp-stamp-image.py` adds the header to an ELF image. It needs
Python 3.7 or later and the objcopy of the toolchain
(`arm-none-eabi-objcopy` or `riscv-none-elf-objcopy`), and no other
packages. It takes the architecture from the ELF header, and selects the
objcopy for that architecture. Use `--objcopy` for an objcopy that is
not in `PATH`. For `--check` of a CH32V307 image, add `--arch riscv`.

```sh
tools/mupp-stamp-image.py app.elf --bin app.bin --hex app.hex --max-length 57344
```

In this example, the application starts at `0x08002000`. The application
area of an STM32F103x8 (64 KB flash) is 57344 bytes (56 KB). On OpenMUPP
v1, the application area is 253952 bytes (248 KB).

- The tool writes the header into the `.isr_vector` section of the ELF
  file. Thus an image that a debug probe writes also has the header.
- `--bin` and `--hex` write the stamped image as a binary file and an
  Intel HEX file.
- `--max-length <bytes>` is the size of the application area. The tool
  fails when the image is larger.
- The tool refuses a RISC-V image that does not start at `0x00002000`,
  the application start in the flash alias. For example, it refuses a
  `standalone` image (linked at 0) and an image linked at `0x08002000`.
- The tool refuses an image where the words `0x1C` to `0x2B` are not
  zero. An image that has a header already gets a new header.
- When the image length is not a multiple of 4, the tool adds `0xFF`
  bytes (erased flash). The `.bin` and `.hex` files include these bytes.
- The `.bin` and `.hex` files contain `0xFF` in the gaps between
  sections, as the CRC does.
- Without `--max-length`, the tool refuses an image larger than 128 KB
  (Cortex-M3) or 256 KB (RISC-V).
  For example, a section in RAM without a load address in flash gives a
  very large image.

To check a binary image or a flash dump, use `--check`. Set `--base` to
the application start address:

```sh
tools/mupp-stamp-image.py --check app.bin --base 0x08002000
```

For a CH32V307 image, set `--base 0x2000` (the address in the flash
alias) and `--arch riscv`.

## CMake

A CMake project adds the header with a post-build step:

```cmake
include(<openmupp-bootloader>/cmake/MuppStampImage.cmake)
mupp_stamp_image(app.elf BIN app.bin HEX app.hex MAX_LENGTH 57344)
```

`BIN`, `HEX` and `MAX_LENGTH` are optional.

Use the `BIN` and `HEX` files of `mupp_stamp_image()` to program the
application. Do not make the `.bin` or `.hex` file with a separate
`objcopy` step. `objcopy -O binary` fills the gaps between sections with
`0x00`, so the CRC is not correct. If a separate step is necessary, add
`--gap-fill 0xff` and run it after `mupp_stamp_image()`. Post-build
steps run in the order of the calls.

## Tool tests

The tool tests use the Python `unittest` module:

```sh
python3 -m unittest discover -s tools/tests
```

The tests that build an ELF image need `cmake`, and the GCC and the
objcopy of the Arm or the RISC-V toolchain. Without a toolchain, the test
run skips its tests.
