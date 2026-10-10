#pragma once

// The memory regions that the AN3155 commands can access (issue #8).
//
// READ_MEMORY reads only from the readable regions: the flash, the RAM,
// the system memory, the option bytes, the flash size register and the
// unique ID (UID). Other addresses get a NACK, so a read never causes a
// bus fault.
//
// WRITE_MEMORY and the erase commands work only in the application area
// of the flash. The bootloader area, the RAM and the option bytes are not
// writable: the bootloader uses the RAM, GO starts only the application,
// and only Write Protect / Write Unprotect change the option bytes.
//
// No hardware access: the host tests use this header too.

#include <cstdint>

#include "Layout.h"

namespace protocol {

struct Region {
   uint32_t start;
   uint32_t size;

   constexpr bool contains(uint32_t address) const {
      return address - start < size;
   }
};

// Memory of the STM32F103x8/xB (RM0008, section 3.3).
namespace memory {
constexpr Region ram{0x2000'0000, 20 * 1024};
constexpr Region systemMemory{0x1FFF'F000, 0x800};
constexpr Region optionBytes{0x1FFF'F800, 16};
constexpr Region flashSizeRegister{0x1FFF'F7E0, 2};
constexpr Region uid{0x1FFF'F7E8, 12};

constexpr uint32_t pageSize = 1024;
// The WRP bits cover at most 32 groups of 4 KB.
constexpr uint32_t maxFlashSize = 32 * layout::wrpGroupSize;
} // namespace memory

class MemoryMap {
public:
   // flashSize: the flash size in bytes, from the flash size register.
   // The value is rounded down to whole pages and limited to 128 KB.
   explicit constexpr MemoryMap(uint32_t flashSize)
      : flash_{layout::flashBase, limit(flashSize)} {}

   constexpr const Region &flash() const { return flash_; }

   constexpr uint32_t flashEnd() const { return flash_.start + flash_.size; }

   // The number of 1 KB pages and of 4 KB WRP groups in the flash.
   constexpr uint32_t pageCount() const { return flash_.size / memory::pageSize; }
   constexpr uint32_t groupCount() const { return flash_.size / layout::wrpGroupSize; }

   // True when each byte of [address, address + size) is in a readable
   // region. The range can cross from one region into an adjacent one.
   constexpr bool readable(uint32_t address, uint32_t size) const {
      if(size == 0 || address + (size - 1) < address) {
         return false;
      }
      const Region regions[] = {flash_, memory::ram, memory::systemMemory, memory::optionBytes,
                                memory::flashSizeRegister, memory::uid};
      uint32_t remaining = size;
      while(remaining > 0) {
         const Region *found = nullptr;
         for(const Region &region : regions) {
            if(region.contains(address)) {
               found = &region;
               break;
            }
         }
         if(found == nullptr) {
            return false;
         }
         const uint32_t available = found->start + found->size - address;
         if(available >= remaining) {
            return true;
         }
         address += available;
         remaining -= available;
      }
      return true;
   }

   // True when [address, address + size) is in the application area.
   constexpr bool writable(uint32_t address, uint32_t size) const {
      if(size == 0 || address + (size - 1) < address) {
         return false;
      }
      return address >= layout::applicationStart && address + (size - 1) < flashEnd() &&
             !layout::touchesBootloaderArea(address, size);
   }

   // True when the page that starts at address is in the application area.
   constexpr bool erasable(uint32_t address) const {
      return address % memory::pageSize == 0 && writable(address, memory::pageSize);
   }

private:
   static constexpr uint32_t limit(uint32_t flashSize) {
      const uint32_t size = flashSize < memory::maxFlashSize ? flashSize : memory::maxFlashSize;
      return size - size % memory::pageSize;
   }

   Region flash_;
};

} // namespace protocol
