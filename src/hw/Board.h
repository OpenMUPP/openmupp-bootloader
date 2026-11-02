// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The pins of the bootloader (plan section 3.5): the key, the LED and the
// USB D+ pull-up. A board without a key or a pull-up switch has
// std::nullopt. They are the same pins as in OpenMUPP
// (src/board/boards/). The bootloader has its own small table, because
// OpenMUPP depends on the bootloader and not the other way.
//
// The functions write the GPIO registers directly. The boot decision uses
// them before the start-up code initialises the RAM.

#include <cmsis_device.h>
#include <cstdint>
#include <optional>

namespace board {

struct Pin {
   uintptr_t portBase;
   uint8_t number;
   // True when a high level is the active state (LED on, key down, ...).
   bool activeHigh;
   // Output: open-drain instead of push-pull. The high level then comes
   // from the board.
   bool openDrain = false;

   // CNF and MODE fields of GPIOx_CRL/CRH (RM0008, section 9.2.1).
   static constexpr uint32_t outputPushPull2MHz = 0x2;
   static constexpr uint32_t outputOpenDrain2MHz = 0x6;
   static constexpr uint32_t inputFloating = 0x4;
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

#if defined(BOARD_MINI) || defined(BOARD_OTHERUSE)
// Key: PB9, low when pressed. The internal pull-up holds it high.
constexpr std::optional<Pin> key = Pin{ GPIOB_BASE, 9, false };

// Red LED: PA15, on when low. PA15 is JTDI after the reset: the bootloader
// disables the JTAG-DP (SWD stays on) to use it.
constexpr Pin led{ GPIOA_BASE, 15, false };
#endif

#if defined(BOARD_MINI)
constexpr std::optional<Pin> usbPullUp = Pin{ GPIOC_BASE, 13, true };
#elif defined(BOARD_OTHERUSE)
constexpr std::optional<Pin> usbPullUp = Pin{ GPIOA_BASE, 2, true };
#elif defined(BOARD_STLINKV21)
// ST-Link/V2-1 of the Nucleo-64 boards (schematics MB1136 and MB1360, same
// MCU pins). The key is SWO (PA10, SWD connector pin 6, through 22 Ohm):
// a wire from pin 6 to GND (pin 3) at power-up starts the bootloader. Only
// the bootloader reads it, so the application can use PA10 for SWO.
// LED_STLINK PA9 drives a bicolor LED: high is red, low
// is green. USB_RENUMn PA15 drives an NPN emitter follower to the 1.5 kOhm
// D+ pull-up: high connects, low disconnects. Open-drain: when released,
// the base divider (10k to 5 V, 36k to GND) lifts D+ higher than a 3.3 V
// push-pull output. PA15 is 5 V tolerant. PA15 is JTDI after the reset:
// the bootloader disables the JTAG-DP (SWD stays on) to use it.
constexpr std::optional<Pin> key = Pin{ GPIOA_BASE, 10, false };
constexpr Pin led{ GPIOA_BASE, 9, true };
constexpr std::optional<Pin> usbPullUp = Pin{ GPIOA_BASE, 15, true, true };
#elif defined(BOARD_STLINKV2MINICLONE)
// ST-Link/V2 mini clone (USB stick, pin map measured, see OpenMUPP
// docs/stlinkv2-mini-clone.md). The key is the SWIM pin of the header:
// PB7-PB11 are tied to it and have a pull-up on the board. A wire from
// SWIM (pin 3) to GND (pin 5) at power-up starts the bootloader.
constexpr std::optional<Pin> key = Pin{ GPIOB_BASE, 7, false };
// Two LEDs on PA9: high lights the blue LED, low the red LED.
constexpr Pin led{ GPIOA_BASE, 9, true };
constexpr std::optional<Pin> usbPullUp = std::nullopt;
#else
#error "No board selected: define BOARD_MINI, BOARD_OTHERUSE, BOARD_STLINKV21 or BOARD_STLINKV2MINICLONE"
#endif

// USB D+. With a fixed pull-up, D+ low shows a disconnect to the host.
constexpr Pin usbDp{ GPIOA_BASE, 12, true };

} // namespace board
