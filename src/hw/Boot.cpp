#include "hw/Boot.h"

#include <cmsis_device.h>
#include <startup-hooks.h>

#include "Layout.h"
#include "boot/BootDecision.h"
#include "boot/ImageHeader.h"
#include "hw/Board.h"
#include "hw/CrcUnit.h"
#include "protocol/MemoryMap.h"

// The code in this file runs before the start-up code initialises the RAM:
// it must not use variables with static storage.

extern unsigned int __vectors_start;

namespace hw::startup {

namespace {

constexpr uint32_t decisionApb1Clocks = RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;

// About 100 us at 8 MHz (HSI after the reset), for the key pull-up.
void settle() {
   for(volatile uint32_t i = 0; i < 200; ++i) {
   }
}

bool keyDown() {
   RCC->APB2ENR |= board::key.clockBit();
   board::key.set(false);
   board::key.configure(board::Pin::inputPull);
   settle();
   return board::key.active();
}

[[noreturn]] void startApplication() {
   // Peripherals back to the reset state. The CRC unit has no reset bit: a
   // reset of the calculation gives the reset value of CRC_DR.
   CRC->CR = CRC_CR_RESET;
   RCC->AHBENR &= ~RCC_AHBENR_CRCEN;
   RCC->APB2RSTR = board::key.clockBit();
   RCC->APB2RSTR = 0;
   RCC->APB2ENR &= ~board::key.clockBit();
   RCC->APB1ENR &= ~decisionApb1Clocks;

   const uint32_t *vectors = reinterpret_cast<const uint32_t *>(layout::applicationStart);
   __disable_irq();
   SCB->VTOR = layout::applicationStart;
   __DSB();
   __ISB();
   asm volatile("msr msp, %0\n"
                "bx %1\n"
                :
                : "r"(vectors[image::stackPointerWord]), "r"(vectors[image::resetVectorWord]));
   __builtin_unreachable();
}

} // namespace

void writeBackup(volatile uint32_t &reg, uint16_t value) {
   PWR->CR |= PWR_CR_DBP;
   reg = value;
   PWR->CR &= ~PWR_CR_DBP;
}

uint32_t flashSize() {
   // FLASH_SIZE register: the size in KB.
   return uint32_t{ *reinterpret_cast<const volatile uint16_t *>(FLASHSIZE_BASE) } * 1024;
}

bool ApplicationImage::isValid() const {
   const protocol::MemoryMap map{ flashSize() };
   CrcUnit crc;
   return image::isValid(reinterpret_cast<const uint32_t *>(layout::applicationStart), layout::applicationStart,
                         map.flashEnd() - layout::applicationStart, crc);
}

} // namespace hw::startup

using namespace hw::startup;

extern "C" void os_startup_initialize_hardware_early() {
   RCC->APB1ENR |= decisionApb1Clocks;
   RCC->AHBENR |= RCC_AHBENR_CRCEN;

   const auto dr1 = static_cast<uint16_t>(BKP->DR1);
   const auto dr2 = static_cast<uint16_t>(BKP->DR2);
   // Each request has an effect for one reset only.
   if(dr1 == boot::bootRequest) {
      writeBackup(BKP->DR1, 0);
   }
   if(dr2 == boot::goToken) {
      writeBackup(BKP->DR2, 0);
   }

   const auto decision = boot::decide(dr1, dr2, keyDown, [] { return ApplicationImage{}.isValid(); });
   if(decision == boot::Decision::application) {
      startApplication();
   }

   // Bootloader mode: the default of system-stm32.
   SystemInit();
   SCB->VTOR = reinterpret_cast<uint32_t>(&__vectors_start);
   __DSB();
}
