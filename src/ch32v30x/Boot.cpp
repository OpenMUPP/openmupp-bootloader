// SPDX-License-Identifier: GPL-3.0-or-later

#include "ch32v30x/Boot.h"

#include <Bkp.h>
#include <Crc_V30x.h>
#include <peripherals/Rcc.h>
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

// The CRC interface of image::isValid()
class CrcUnit {
   public:
   void reset() {
      hw::Crc::reset();
   }
   void add(uint32_t word) {
      hw::Crc::add(word);
   }
   uint32_t value() const {
      return hw::Crc::value();
   }
};

[[noreturn]] void startApplication() {
   // Peripherals back to the reset state. The CRC unit has no reset bit: a
   // reset of the calculation gives the reset value of CRC_DATAR.
   hw::Crc::reset();
   Rcc::disableCRCClock();
   hw::Bkp::disable();
   // cpuInit() of the start-up set mtvec, mstatus and INTSYSCR. The
   // application sets them again; interrupts stay off until then.
   asm volatile("csrw mstatus, zero\n"
                "fence.i\n"
                "jr %0\n"
                :
                : "r"(memory::applicationAlias));
   __builtin_unreachable();
}

} // namespace

bool ApplicationImage::isValid() const {
   CrcUnit crc;
   return image::isValid(reinterpret_cast<const uint32_t *>(layout::applicationStart), memory::applicationAlias,
                         memory::applicationEnd - layout::applicationStart, crc, image::Arch::riscv);
}

} // namespace ch32

using namespace ch32;

extern "C" void os_startup_initialize_hardware_early() {
   hw::Bkp::enable();
   Rcc::enableCRCClock();

   const uint16_t dr1 = hw::Bkp::readDR1();
   const uint16_t dr2 = hw::Bkp::readDR2();
   // Each request has an effect for one reset only.
   if(dr1 == boot::bootRequest) {
      hw::Bkp::writeDR1(0);
   }
   if(dr2 == boot::goToken) {
      hw::Bkp::writeDR2(0);
   }

   const auto decision = boot::decide(dr1, dr2, [] { return false; }, [] { return ApplicationImage{}.isValid(); });
   if(decision == boot::Decision::application) {
      startApplication();
   }

   // Bootloader mode: the default of ch32v00x-system (144 MHz from HSE).
   SystemInit();
}
