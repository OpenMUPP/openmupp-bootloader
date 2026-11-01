#pragma once

// Flash layout of the rewritten bootloader (issue #4). The bootloader area
// is at the start of the flash; the application starts behind it.
//
// The area size comes from CMake (OPENMUPP_BOOTLOADER_AREA_SIZE), so the
// code, the linker script and the size check use the same value.
// No hardware access: the host tests use this header too.

#include <cstdint>

#if !defined(MUPP_BOOTLOADER_AREA_SIZE)
#error "MUPP_BOOTLOADER_AREA_SIZE is not set. The CMake target mupp-layout sets it."
#endif

namespace layout {

constexpr uint32_t flashBase = 0x0800'0000;

// One write protection (WRP) bit protects 4 pages of 1 KB on the
// STM32F103x8/xB. The bootloader protects its own area, so the area is a
// multiple of this size.
constexpr uint32_t wrpGroupSize = 4096;

constexpr uint32_t bootloaderSize = MUPP_BOOTLOADER_AREA_SIZE;
constexpr uint32_t applicationStart = flashBase + bootloaderSize;

static_assert(bootloaderSize > 0 && bootloaderSize % wrpGroupSize == 0, "the bootloader area must be a multiple of 4 KB");

// True when [address, address + size) has a byte in the bootloader area.
// An erase or a write there gets a NACK. A range that wraps past the end
// of the address space counts as touching it.
constexpr bool touchesBootloaderArea(uint32_t address, uint32_t size) {
   if(size == 0) {
      return false;
   }
   const uint32_t last = address + (size - 1);
   if(last < address) {
      return true;
   }
   return address < applicationStart && last >= flashBase;
}

} // namespace layout
