#pragma once

// Fakes of the hardware interfaces (protocol/Hardware.h) for the host
// tests. The fake flash behaves as the STM32F1 flash: an erased byte is
// 0xFF, and a write to a byte that is not erased fails. The fake counts
// each erase or write that the protocol must never do (bootloader area,
// protected group), so a test can check that there is none.

#include <cstdint>
#include <span>
#include <vector>

#include "Layout.h"
#include "protocol/An3155Protocol.h"

namespace fakes {

class FakeTarget : public protocol::FlashTarget,
                   public protocol::MemoryReader,
                   public protocol::OptionBytes,
                   public protocol::Application,
                   public protocol::Clock {
public:
   static constexpr uint32_t defaultFlashSize = 64 * 1024;
   static constexpr uint32_t bootloaderGroupMask =
      (1u << (layout::bootloaderSize / layout::wrpGroupSize)) - 1;

   explicit FakeTarget(uint32_t flashSize = defaultFlashSize) : flash(flashSize, 0xFF) {}

   protocol::Hardware hardware() { return {*this, *this, *this, *this, *this}; }

   uint8_t &flashByte(uint32_t address) { return flash.at(address - layout::flashBase); }

   // The value that the fake returns for a readable address outside the
   // flash.
   static uint8_t pattern(uint32_t address) {
      return static_cast<uint8_t>((address ^ (address >> 8) ^ 0x5A) & 0xFF);
   }

   void read(uint32_t address, std::span<uint8_t> data) override {
      for(uint8_t &byte : data) {
         const uint32_t offset = address - layout::flashBase;
         byte = offset < flash.size() ? flash[offset] : pattern(address);
         ++address;
      }
      ++reads;
   }

   bool erasePage(uint32_t address) override {
      erasedPages.push_back(address);
      if(!allowed(address, protocol::memory::pageSize) || address % protocol::memory::pageSize != 0) {
         ++violations;
         return false;
      }
      if(failErase) {
         return false;
      }
      for(uint32_t i = 0; i < protocol::memory::pageSize; ++i) {
         flashByte(address + i) = 0xFF;
      }
      return true;
   }

   bool write(uint32_t address, std::span<const uint8_t> data) override {
      ++writes;
      if(!allowed(address, static_cast<uint32_t>(data.size())) || address % 4 != 0 ||
         data.size() % 4 != 0) {
         ++violations;
         return false;
      }
      if(failWrite) {
         return false;
      }
      for(uint8_t byte : data) {
         uint8_t &cell = flashByte(address++);
         if(cell != 0xFF) {
            return false;
         }
         cell = byte;
      }
      return true;
   }

   uint32_t writeProtectedGroups() const override { return protectedGroups; }

   bool setWriteProtectedGroups(uint32_t groups) override {
      optionByteWrites.push_back(groups);
      if(failOptionBytes) {
         return false;
      }
      // As on the MCU: the new value takes effect after a reset.
      pendingGroups = groups | bootloaderGroupMask;
      return true;
   }

   bool isValid() const override { return applicationValid; }

   uint32_t milliseconds() const override { return now; }

   std::vector<uint8_t> flash;
   uint32_t now = 0;
   uint32_t protectedGroups = bootloaderGroupMask;
   uint32_t pendingGroups = 0;
   bool applicationValid = true;
   bool failErase = false;
   bool failWrite = false;
   bool failOptionBytes = false;
   int reads = 0;
   int writes = 0;
   int violations = 0;
   std::vector<uint32_t> erasedPages;
   std::vector<uint32_t> optionByteWrites;

private:
   // True for a range in the application area with no protected group.
   bool allowed(uint32_t address, uint32_t size) const {
      if(size == 0 || address < layout::applicationStart ||
         address - layout::flashBase + size > flash.size()) {
         return false;
      }
      for(uint32_t a = address; a < address + size; a += 1) {
         if(protectedGroups & (1u << ((a - layout::flashBase) / layout::wrpGroupSize))) {
            return false;
         }
      }
      return true;
   }
};

} // namespace fakes
