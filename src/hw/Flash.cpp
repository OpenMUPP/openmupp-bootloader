#include "hw/Flash.h"

#include <cmsis_device.h>

#include "Layout.h"
#include "boot/OptionByteValues.h"
#include "protocol/MemoryMap.h"

namespace hw {

namespace {

constexpr uint32_t key1 = 0x4567'0123;
constexpr uint32_t key2 = 0xCDEF'89AB;
constexpr uint32_t errorFlags = FLASH_SR_PGERR | FLASH_SR_WRPRTERR;

volatile uint16_t *halfWords(uint32_t address) {
   return reinterpret_cast<volatile uint16_t *>(address);
}

// Unlocks FLASH_CR and clears old status flags, so finished() sees only
// the flags of the next operation.
void unlock() {
   if(FLASH->CR & FLASH_CR_LOCK) {
      FLASH->KEYR = key1;
      FLASH->KEYR = key2;
   }
   FLASH->SR = errorFlags | FLASH_SR_EOP;
}

// Waits for the end of the operation and clears the status flags. Returns
// false when the operation set an error flag.
bool finished() {
   while(FLASH->SR & FLASH_SR_BSY) {
   }
   const uint32_t status = FLASH->SR;
   FLASH->SR = status & (errorFlags | FLASH_SR_EOP);
   return (status & errorFlags) == 0;
}

} // namespace

bool Flash::allowed(uint32_t address, uint32_t size) const {
   if(size == 0 || address < layout::applicationStart || address >= flashEnd_ || size > flashEnd_ - address) {
      return false;
   }
   const uint32_t first = (address - layout::flashBase) / layout::wrpGroupSize;
   const uint32_t last = (address + size - 1 - layout::flashBase) / layout::wrpGroupSize;
   const uint32_t protectedGroups = writeProtectedGroups();
   for(uint32_t group = first; group <= last; ++group) {
      if(protectedGroups & (1u << group)) {
         return false;
      }
   }
   return true;
}

bool Flash::erasePage(uint32_t address) {
   if(address % protocol::memory::pageSize != 0 || !allowed(address, protocol::memory::pageSize)) {
      return false;
   }
   unlock();
   FLASH->CR = FLASH_CR_PER;
   FLASH->AR = address;
   FLASH->CR = FLASH_CR_PER | FLASH_CR_STRT;
   const bool ok = finished();
   FLASH->CR = FLASH_CR_LOCK;
   if(!ok) {
      return false;
   }
   const volatile uint32_t *word = reinterpret_cast<const volatile uint32_t *>(address);
   for(uint32_t i = 0; i < protocol::memory::pageSize / 4; ++i) {
      if(word[i] != 0xFFFF'FFFF) {
         return false;
      }
   }
   return true;
}

bool Flash::write(uint32_t address, std::span<const uint8_t> data) {
   const auto size = static_cast<uint32_t>(data.size());
   if(address % 4 != 0 || size % 4 != 0 || !allowed(address, size)) {
      return false;
   }
   unlock();
   FLASH->CR = FLASH_CR_PG;
   bool ok = true;
   volatile uint16_t *target = halfWords(address);
   for(uint32_t i = 0; ok && i < size; i += 2) {
      const auto value = static_cast<uint16_t>(data[i] | data[i + 1] << 8);
      // An erased half-word is 0xFFFF already: no program cycle. The
      // verify below also checks it.
      if(value != 0xFFFF) {
         *target = value;
         ok = finished();
      }
      ok = ok && *target == value;
      ++target;
   }
   FLASH->CR = FLASH_CR_LOCK;
   return ok;
}

uint32_t Flash::writeProtectedGroups() const {
   // A clear FLASH_WRPR bit is a protected group.
   return ~FLASH->WRPR;
}

bool Flash::setWriteProtectedGroups(uint32_t groups) {
   volatile uint16_t *optionBytes = halfWords(OB_BASE);
   const auto values = optionbytes::program(static_cast<uint8_t>(optionBytes[optionbytes::userIndex]),
                                            static_cast<uint8_t>(optionBytes[optionbytes::data0Index]),
                                            static_cast<uint8_t>(optionBytes[optionbytes::data1Index]), groups);
   unlock();
   FLASH->OPTKEYR = key1;
   FLASH->OPTKEYR = key2;
   if((FLASH->CR & FLASH_CR_OPTWRE) == 0) {
      FLASH->CR = FLASH_CR_LOCK;
      return false;
   }
   // OPTWRE stays set in each write: a 0 clears it.
   FLASH->CR = FLASH_CR_OPTWRE | FLASH_CR_OPTER;
   FLASH->CR = FLASH_CR_OPTWRE | FLASH_CR_OPTER | FLASH_CR_STRT;
   bool ok = finished();
   // Program also after an error: RDP comes first, and an erased RDP is
   // readout protection.
   FLASH->CR = FLASH_CR_OPTWRE | FLASH_CR_OPTPG;
   for(std::size_t i = 0; i < optionbytes::count; ++i) {
      optionBytes[i] = values[i];
      ok = finished() && ok;
   }
   FLASH->CR = FLASH_CR_LOCK;
   for(std::size_t i = 0; i < optionbytes::count; ++i) {
      ok = ok && optionBytes[i] == optionbytes::stored(values[i]);
   }
   return ok;
}

} // namespace hw
