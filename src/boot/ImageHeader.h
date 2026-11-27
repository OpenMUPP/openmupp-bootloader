// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The check of the application image header (issue #9, plan section 3.7).
// tools/mupp-stamp-image.py writes the header; check() of that tool uses
// the same rules.
//
// The header is in reserved words of the vector table. On the Cortex-M3,
// they are the reserved exception vectors 7 to 10. On the CH32V307, the
// image starts with the "j _start" of the reset handler, and the vector
// table follows at offset 4. There, 0x1C and 0x28 are reserved exceptions
// of the QingKe V4F, 0x20 is Ecall-U and 0x24 is BreakPoint. The firmware
// runs in machine mode (no Ecall-U) and has no BreakPoint handler: an
// ebreak without a debugger jumps to the CRC value.
//
// | Offset | Content                                                  |
// |--------|----------------------------------------------------------|
// | `0x1C` | Magic `0x314C5356` ("VSL1")                              |
// | `0x20` | Image length in bytes, a multiple of 4                   |
// | `0x24` | CRC32 of the image, the words 0x1C-0x27 count as zero    |
// | `0x28` | Reserved, `0`                                            |
//
// No hardware access: the host tests use this header too. The CRC is a
// template parameter: the bootloader uses the CRC unit of the STM32F1 or of
// the CH32V307 (the same algorithm), the host tests use a software CRC.

#include <cstddef>
#include <cstdint>

namespace image {

constexpr uint32_t magic = 0x314C'5356;

// Word indexes in the vector table.
constexpr std::size_t stackPointerWord = 0;
constexpr std::size_t resetVectorWord = 1;
constexpr std::size_t magicWord = 0x1C / 4;
constexpr std::size_t lengthWord = 0x20 / 4;
constexpr std::size_t crcWord = 0x24 / 4;

constexpr uint32_t minLength = 64;

// The RAM of the STM32F103x8/xB. The initial SP can be the end of the RAM.
constexpr uint32_t ramStart = 0x2000'0000;
constexpr uint32_t ramSize = 20 * 1024;

// The architecture of the image: the rule for the first words.
enum class Arch : uint8_t { cortexM3, riscv };

// The offset of a RISC-V "j" (JAL x0) in word 0 of the image, or -1 when
// word 0 is not a JAL x0. The offset is relative to the start of the image.
constexpr int32_t riscvJumpOffset(uint32_t instruction) {
   // Opcode JAL (0x6F) and rd = x0
   if((instruction & 0xFFFu) != 0x06Fu) {
      return -1;
   }
   // imm[20|10:1|11|19:12] in bits 31:12
   const uint32_t imm = ((instruction >> 31) & 0x1u) << 20 | ((instruction >> 12) & 0xFFu) << 12 |
                        ((instruction >> 20) & 0x1u) << 11 | ((instruction >> 21) & 0x3FFu) << 1;
   if(imm & (1u << 20)) {
      return -1; // a jump backwards leaves the image
   }
   return static_cast<int32_t>(imm);
}

// True when the image at base has a valid header and CRC:
//
// - The magic is correct.
// - The length is a multiple of 4, between minLength and maxLength.
// - Cortex-M3: the initial SP is in the RAM, and the reset vector is odd
//   (Thumb) and inside the image.
// - RISC-V: word 0 is a "j" to an address inside the image, behind the
//   vector table words of the header.
// - The CRC is correct.
//
// words: the image in memory, at the address base. maxLength: the size of
// the application area.
// Crc: reset() starts a new CRC, add(word) adds one word, value() gives
// the CRC.
template <typename Crc>
bool isValid(const uint32_t *words, uint32_t base, uint32_t maxLength, Crc &crc, Arch arch = Arch::cortexM3) {
   if(words[magicWord] != magic) {
      return false;
   }
   const uint32_t length = words[lengthWord];
   if(length % 4 != 0 || length < minLength || length > maxLength) {
      return false;
   }
   if(arch == Arch::riscv) {
      const int32_t target = riscvJumpOffset(words[0]);
      if(target < static_cast<int32_t>((crcWord + 2) * 4) || static_cast<uint32_t>(target) >= length) {
         return false;
      }
   } else {
      const uint32_t sp = words[stackPointerWord];
      if(sp < ramStart || sp - ramStart > ramSize) {
         return false;
      }
      const uint32_t reset = words[resetVectorWord];
      if((reset & 1) == 0 || (reset & ~1u) - base >= length) {
         return false;
      }
   }
   crc.reset();
   for(std::size_t i = 0; i < length / 4; ++i) {
      crc.add(i >= magicWord && i <= crcWord ? 0 : words[i]);
   }
   return crc.value() == words[crcWord];
}

} // namespace image
