// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The check of the application image header (issue #9, plan section 3.7).
// tools/mupp-stamp-image.py writes the header; check() of that tool uses
// the same rules.
//
// The header is in the reserved words of the Cortex-M3 vector table:
//
// | Offset | Content                                                  |
// |--------|----------------------------------------------------------|
// | `0x1C` | Magic `0x314C5356` ("VSL1")                              |
// | `0x20` | Image length in bytes, a multiple of 4                   |
// | `0x24` | CRC32 of the image, the words 0x1C-0x27 count as zero    |
// | `0x28` | Reserved, `0`                                            |
//
// No hardware access: the host tests use this header too. The CRC is a
// template parameter: the bootloader uses the CRC unit of the STM32F1, the
// host tests use a software CRC.

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

// True when the image at base has a valid header and CRC:
//
// - The magic is correct.
// - The length is a multiple of 4, between minLength and maxLength.
// - The initial SP is in the RAM.
// - The reset vector is odd (Thumb) and inside the image.
// - The CRC is correct.
//
// words: the image in memory, at the address base. maxLength: the size of
// the application area.
// Crc: reset() starts a new CRC, add(word) adds one word, value() gives
// the CRC.
template <typename Crc>
bool isValid(const uint32_t *words, uint32_t base, uint32_t maxLength, Crc &crc) {
   if(words[magicWord] != magic) {
      return false;
   }
   const uint32_t length = words[lengthWord];
   if(length % 4 != 0 || length < minLength || length > maxLength) {
      return false;
   }
   const uint32_t sp = words[stackPointerWord];
   if(sp < ramStart || sp - ramStart > ramSize) {
      return false;
   }
   const uint32_t reset = words[resetVectorWord];
   if((reset & 1) == 0 || (reset & ~1u) - base >= length) {
      return false;
   }
   crc.reset();
   for(std::size_t i = 0; i < length / 4; ++i) {
      crc.add(i >= magicWord && i <= crcWord ? 0 : words[i]);
   }
   return crc.value() == words[crcWord];
}

} // namespace image
