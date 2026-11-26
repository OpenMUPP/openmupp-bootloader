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

volatile uint32_t &pwrCtlr() {
   return reg32(0x4000'7000);
}
constexpr uint32_t pwrCtlrDbp = 1u << 8;
volatile uint32_t &crcDatar() {
   return reg32(0x4002'3000);
}
volatile uint32_t &crcCtlr() {
   return reg32(0x4002'3008);
}
constexpr uint32_t crcCtlrReset = 1u << 0;

constexpr uint32_t rccPwrEn = 1u << 28; // RCC_APB1PCENR
constexpr uint32_t rccBkpEn = 1u << 27;
constexpr uint32_t rccCrcEn = 1u << 6; // RCC_AHBPCENR
constexpr uint32_t decisionApb1Clocks = rccPwrEn | rccBkpEn;

class CrcUnit {
   public:
   void reset() {
      crcCtlr() = crcCtlrReset;
   }
   void add(uint32_t word) {
      crcDatar() = word;
   }
   uint32_t value() const {
      return crcDatar();
   }
};

[[noreturn]] void startApplication() {
   // Peripherals back to the reset state. The CRC unit has no reset bit: a
   // reset of the calculation gives the reset value of CRC_DATAR.
   crcCtlr() = crcCtlrReset;
   RCC->AHBPCENR &= ~rccCrcEn;
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

void writeBackup(volatile uint32_t &reg, uint16_t value) {
   pwrCtlr() |= pwrCtlrDbp;
   reg = value;
   pwrCtlr() &= ~pwrCtlrDbp;
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
   RCC->AHBPCENR |= rccCrcEn;

   const auto dr1 = static_cast<uint16_t>(bkpDatar1());
   const auto dr2 = static_cast<uint16_t>(bkpDatar2());
   // Each request has an effect for one reset only.
   if(dr1 == boot::bootRequest) {
      writeBackup(bkpDatar1(), 0);
   }
   if(dr2 == boot::goToken) {
      writeBackup(bkpDatar2(), 0);
   }

   const auto decision = boot::decide(dr1, dr2, [] { return false; }, [] { return ApplicationImage{}.isValid(); });
   if(decision == boot::Decision::application) {
      startApplication();
   }

   // Bootloader mode: the default of ch32v00x-system (144 MHz from HSE).
   SystemInit();
}
