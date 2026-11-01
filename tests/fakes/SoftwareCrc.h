// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Software model of the STM32F1 CRC unit for the host tests: CRC-32/MPEG-2
// on 32-bit words. Polynomial 0x04C11DB7, initial value 0xFFFFFFFF, each
// word shifted in with the most significant bit first, no reflection and
// no final XOR. Same interface as the CRC unit driver of the bootloader.

#include <cstdint>

namespace fakes {

class SoftwareCrc {
   public:
   void reset() {
      crc_ = 0xFFFF'FFFF;
   }

   void add(uint32_t word) {
      crc_ ^= word;
      for(int bit = 0; bit < 32; ++bit) {
         crc_ = crc_ & 0x8000'0000 ? (crc_ << 1) ^ 0x04C1'1DB7 : crc_ << 1;
      }
      ++words;
   }

   uint32_t value() const {
      return crc_;
   }

   // The number of words that add() took, for the tests.
   int words = 0;

   private:
   uint32_t crc_ = 0xFFFF'FFFF;
};

} // namespace fakes
