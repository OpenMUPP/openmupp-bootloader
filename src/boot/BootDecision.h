#pragma once

// The boot decision (issue #10, plan section 3.1). It runs straight after
// the reset, before the clock setup. The order:
//
// 1. Boot request 0xB007 in BKP->DR1: stay in the bootloader.
// 2. GO token in BKP->DR2: start the application.
// 3. Key down: stay in the bootloader.
// 4. Application not valid: stay in the bootloader.
// 5. Otherwise: start the application.
//
// The caller clears DR1 and DR2 before it starts the application or the
// bootloader, so each request has an effect for one reset only.
// No hardware access: the host tests use this header too.

#include <cstdint>

namespace boot {

// Written by the application (OpenMUPP VERSALOON_FW_UPDATE) to
// start the bootloader. It is part of the bootloader contract.
constexpr uint16_t bootRequest = 0xB007;

// Written by the bootloader before the reset of GO (plan section 3.4).
constexpr uint16_t goToken = 0x60A7;

enum class Decision : uint8_t { bootloader, application };

// keyDown and applicationValid are callables. decide() calls them only
// when the result depends on them: the CRC check of the application takes
// some ms.
template <typename KeyDown, typename ApplicationValid>
constexpr Decision decide(uint16_t dr1, uint16_t dr2, KeyDown keyDown, ApplicationValid applicationValid) {
   if(dr1 == bootRequest) {
      return Decision::bootloader;
   }
   if(dr2 == goToken) {
      return Decision::application;
   }
   if(keyDown()) {
      return Decision::bootloader;
   }
   return applicationValid() ? Decision::application : Decision::bootloader;
}

} // namespace boot
