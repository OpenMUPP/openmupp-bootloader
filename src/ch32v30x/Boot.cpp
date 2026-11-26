// SPDX-License-Identifier: GPL-3.0-or-later

#include "ch32v30x/Boot.h"

#include <startup-hooks.h>
#include <system_ch32v30x.h>
#include <wch_device.h>

#include "Layout.h"
#include "boot/BootDecision.h"
#include "boot/ImageHeader.h"
#include "ch32v30x/Memory.h"

// The code in this file runs before the start-up code initialises the RAM:
// it must not use variables with static storage.

namespace ch32 {

namespace {

constexpr uint32_t decisionApb1Clocks = RCC_PWREN | RCC_BKPEN;

class CrcUnit {
   public:
   void reset() {
      CRC->CTLR = CRC_CTLR_RESET;
   }
   void add(uint32_t word) {
      CRC->DATAR = word;
   }
   uint32_t value() const {
      return CRC->DATAR;
   }
};

[[noreturn]] void startApplication() {
   // Peripherals back to the reset state. The CRC unit has no reset bit: a
   // reset of the calculation gives the reset value of CRC_DATAR.
   CRC->CTLR = CRC_CTLR_RESET;
   RCC->AHBPCENR &= ~RCC_CRCEN;
   RCC->APB1PCENR &= ~decisionApb1Clocks;
   // cpuInit() of the start-up set mtvec, mstatus and INTSYSCR. The
   // application sets them again; interrupts stay off until then. The
   // application is linked at its address in the flash alias at 0.
   asm volatile("csrw mstatus, zero\n"
                "fence.i\n"
                "jr %0\n"
                :
                : "r"(layout::applicationStart - layout::flashBase));
   __builtin_unreachable();
}

} // namespace

void writeBackup(volatile uint16_t &reg, uint16_t value) {
   PWR->CTLR |= PWR_CTLR_DBP;
   reg = value;
   PWR->CTLR &= ~PWR_CTLR_DBP;
}

bool ApplicationImage::isValid() const {
   CrcUnit crc;
   return image::isValid(reinterpret_cast<const uint32_t *>(layout::applicationStart), layout::applicationStart,
                         memory::applicationEnd - layout::applicationStart, crc, image::Arch::riscv);
}

} // namespace ch32

using namespace ch32;

extern "C" void os_startup_initialize_hardware_early() {
   RCC->APB1PCENR |= decisionApb1Clocks;
   RCC->AHBPCENR |= RCC_CRCEN;

   const auto dr1 = static_cast<uint16_t>(BKP->DATAR1);
   const auto dr2 = static_cast<uint16_t>(BKP->DATAR2);
   // Each request has an effect for one reset only.
   if(dr1 == boot::bootRequest) {
      writeBackup(BKP->DATAR1, 0);
   }
   if(dr2 == boot::goToken) {
      writeBackup(BKP->DATAR2, 0);
   }

   const auto decision = boot::decide(dr1, dr2, [] { return false; }, [] { return ApplicationImage{}.isValid(); });
   if(decision == boot::Decision::application) {
      startApplication();
   }

   // Bootloader mode: the default of ch32v00x-system (144 MHz from HSE).
   SystemInit();
}
