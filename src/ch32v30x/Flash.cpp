// SPDX-License-Identifier: GPL-3.0-or-later

#include "ch32v30x/Flash.h"

#include <peripherals/Flash.h>

#include "ch32v30x/Memory.h"

namespace ch32 {

namespace {

bool inApplicationArea(uint32_t address, uint32_t size) {
   return size > 0 && address >= layout::applicationStart && address < memory::applicationEnd &&
          size <= memory::applicationEnd - address;
}

} // namespace

bool Flash::erasePage(uint32_t address) {
   if(address % memory::pageSize != 0 || !inApplicationArea(address, memory::pageSize)) {
      return false;
   }
   const ::Flash::writable_section unlocked;
   ::Flash::erase_sector(reinterpret_cast<void *>(address));
   const auto *word = reinterpret_cast<const volatile uint32_t *>(address);
   for(uint32_t i = 0; i < memory::pageSize / 4; ++i) {
      // An erased word of the CH32V307 reads 0xE339E339 through the bus
      // (the flash scrambling of the reads), not 0xFFFFFFFF.
      if(word[i] != 0xE339'E339u && word[i] != 0xFFFF'FFFFu) {
         return false;
      }
   }
   return true;
}

bool Flash::write(uint32_t address, std::span<const uint8_t> data) {
   const auto size = static_cast<uint32_t>(data.size());
   if(address % 16 != 0 || size % 4 != 0 || !inApplicationArea(address, size)) {
      return false;
   }
   {
      const ::Flash::writable_section unlocked;
      // write_buffer() does not change the data; its interface has no const.
      ::Flash::write_buffer(reinterpret_cast<void *>(address), { const_cast<uint8_t *>(data.data()), data.size() });
   }
   const auto *target = reinterpret_cast<const volatile uint8_t *>(address);
   for(uint32_t i = 0; i < size; ++i) {
      if(target[i] != data[i]) {
         return false;
      }
   }
   return true;
}

void Flash::read(uint32_t address, std::span<uint8_t> data) {
   const auto *source = reinterpret_cast<const volatile uint8_t *>(address);
   for(uint8_t &byte : data) {
      byte = *source++;
   }
}

} // namespace ch32
