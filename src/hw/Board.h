#pragma once

// The pins of the bootloader (plan section 3.5): the key, the LED and the
// USB D+ pull-up. They are the same pins as in versaloon-next-gen
// (src/board/boards/). The bootloader has its own small table, because
// versaloon-next-gen depends on the bootloader and not the other way.
//
// The functions write the GPIO registers directly. The boot decision uses
// them before the start-up code initialises the RAM.

#include <cmsis_device.h>
#include <cstdint>

namespace board {

struct Pin {
   uintptr_t portBase;
   uint8_t number;
   // True when a high level is the active state (LED on, key down, ...).
   bool activeHigh;

   // CNF and MODE fields of GPIOx_CRL/CRH (RM0008, section 9.2.1).
   static constexpr uint32_t outputPushPull2MHz = 0x2;
   static constexpr uint32_t inputPull = 0x8;

   GPIO_TypeDef *port() const {
      return reinterpret_cast<GPIO_TypeDef *>(portBase);
   }

   // The IOPxEN bit of RCC_APB2ENR for the port.
   uint32_t clockBit() const {
      return RCC_APB2ENR_IOPAEN << ((portBase - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE));
   }

   void configure(uint32_t mode) const {
      volatile uint32_t &reg = number < 8 ? port()->CRL : port()->CRH;
      const uint32_t shift = 4u * (number % 8u);
      reg = (reg & ~(0xFu << shift)) | (mode << shift);
   }

   // For an output: sets the active or inactive level. For an input with
   // inputPull: active false selects the pull to the inactive level.
   void set(bool active) const {
      const uint32_t bit = 1u << number;
      port()->BSRR = active == activeHigh ? bit : bit << 16;
   }

   bool active() const {
      return ((port()->IDR >> number) & 1u) == (activeHigh ? 1u : 0u);
   }
};

// Key: PB9, low when pressed. The internal pull-up holds it high.
constexpr Pin key{ GPIOB_BASE, 9, false };

// Red LED: PA15, on when low. PA15 is JTDI after the reset: the bootloader
// disables the JTAG-DP (SWD stays on) to use it.
constexpr Pin led{ GPIOA_BASE, 15, false };

#if defined(BOARD_MINI)
constexpr Pin usbPullUp{ GPIOC_BASE, 13, true };
#elif defined(BOARD_OTHERUSE)
constexpr Pin usbPullUp{ GPIOA_BASE, 2, true };
#else
#error "No board selected: define BOARD_MINI or BOARD_OTHERUSE"
#endif

} // namespace board
