#pragma once

// Rules of the flash driver and of the self-protection (plan section 3.6).
// No hardware access: the host tests use this header too.

#include <cstdint>

#include "Layout.h"
#include "boot/OptionByteValues.h"

namespace flashrules {

// True for [address, address + size) in the application area, below
// flashEnd, with no group in protectedGroups (bit n = 4 KB group n). The
// flash driver erases and writes only such ranges.
constexpr bool writable(uint32_t address, uint32_t size, uint32_t flashEnd, uint32_t protectedGroups) {
   if(size == 0 || address < layout::applicationStart || address >= flashEnd || size > flashEnd - address) {
      return false;
   }
   const uint32_t first = (address - layout::flashBase) / layout::wrpGroupSize;
   const uint32_t last = (address + size - 1 - layout::flashBase) / layout::wrpGroupSize;
   for(uint32_t group = first; group <= last; ++group) {
      if(protectedGroups & (1u << group)) {
         return false;
      }
   }
   return true;
}

enum class SelfProtection : uint8_t {
   // The bootloader area is protected.
   none,
   // The last start tried it already: no second reset, the bootloader
   // runs without the protection.
   triedBefore,
   // Readout protection (RDP) is active. An option byte update writes RDP
   // 0xA5, and that starts a mass erase of the flash, also of the
   // bootloader. Thus no update.
   readoutProtected,
   // Set the WRP bits of the bootloader area, then reset.
   protect,
};

// The decision of the self-protection at the start in bootloader mode.
// triedBefore: the marker in BKP->DR3 is set. groups: the protected groups
// now (bit n = 4 KB group n).
constexpr SelfProtection selfProtection(bool triedBefore, uint32_t groups, bool readoutProtected) {
   if((groups & optionbytes::bootloaderGroups) == optionbytes::bootloaderGroups) {
      return SelfProtection::none;
   }
   if(triedBefore) {
      return SelfProtection::triedBefore;
   }
   return readoutProtected ? SelfProtection::readoutProtected : SelfProtection::protect;
}

} // namespace flashrules
