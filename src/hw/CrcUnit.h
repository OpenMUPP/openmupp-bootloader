// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The CRC unit of the STM32F1 (RM0008, section 4) for the image check
// (boot/ImageHeader.h). The CRC clock (RCC_AHBENR.CRCEN) must be on.

#include <cmsis_device.h>
#include <cstdint>

namespace hw {

class CrcUnit {
   public:
   void reset() {
      CRC->CR = CRC_CR_RESET;
   }

   void add(uint32_t word) {
      CRC->DR = word;
   }

   uint32_t value() const {
      return CRC->DR;
   }
};

} // namespace hw
