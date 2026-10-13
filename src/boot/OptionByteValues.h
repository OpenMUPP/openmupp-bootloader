#pragma once

// The values of an option byte update (plan section 3.6). The flash driver
// (hw/Flash.cpp) erases the option bytes and programs these values.
//
// The option bytes of the STM32F103x8/xB are 8 half-words at 0x1FFFF800
// (PM0075, section 2.5): RDP, USER, Data0, Data1, WRP0-WRP3. The low byte
// is the value, the high byte is its complement. The flash interface
// writes the complement itself, so the driver programs only the low byte.
//
// No hardware access: the host tests use this header too.

#include <array>
#include <cstddef>
#include <cstdint>

#include "Layout.h"

namespace optionbytes {

// The RDP value for "no readout protection". The bootloader always writes
// this constant, never a value from a variable (plan section 6).
constexpr uint8_t rdpNone = 0xA5;

constexpr std::size_t count = 8;
constexpr std::size_t rdpIndex = 0;
constexpr std::size_t userIndex = 1;
constexpr std::size_t data0Index = 2;
constexpr std::size_t data1Index = 3;
constexpr std::size_t wrp0Index = 4;

// The WRP groups of the bootloader area: bit n is the 4 KB group n.
constexpr uint32_t bootloaderGroups = (1u << (layout::bootloaderSize / layout::wrpGroupSize)) - 1;

// The values to program, in the order of the option bytes. The RDP value
// comes first: after the erase, RDP is 0xFF, which is "readout protected".
// groups: bit n set = 4 KB group n write-protected. The bootloader groups
// are always protected. In the WRP bytes, a clear bit means protected.
constexpr std::array<uint8_t, count> program(uint8_t user, uint8_t data0, uint8_t data1, uint32_t groups) {
   const uint32_t wrp = ~(groups | bootloaderGroups);
   return { rdpNone,
            user,
            data0,
            data1,
            static_cast<uint8_t>(wrp),
            static_cast<uint8_t>(wrp >> 8),
            static_cast<uint8_t>(wrp >> 16),
            static_cast<uint8_t>(wrp >> 24) };
}

// The half-word that the option byte holds after a correct program: the
// value and its complement.
constexpr uint16_t stored(uint8_t value) {
   return static_cast<uint16_t>(value | (static_cast<uint8_t>(~value) << 8));
}

// The erased value of an option byte.
constexpr uint16_t erased = 0xFFFF;

// True when the driver does not program the option byte: a WRP byte of
// 0xFF (no group protected) stays erased, as in the ST StdPeriph and HAL
// drivers. The option byte loader accepts an erased WRP byte.
constexpr bool skip(std::size_t index, uint8_t value) {
   return index >= wrp0Index && value == 0xFF;
}

// The half-word that the option byte holds after the update.
constexpr uint16_t expected(std::size_t index, uint8_t value) {
   return skip(index, value) ? erased : stored(value);
}

} // namespace optionbytes
