// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// A fake flash for the DFU tests, with a page size of the test: 1 KB as on
// the STM32F1, 256 bytes as the fast page of the CH32V307. An erased byte
// is 0xFF, and a write to a byte that is not erased fails. The fake is also
// the image check of the manifestation and the header check of block 0.

#include <cstdint>
#include <span>
#include <vector>

#include "protocol/Hardware.h"

namespace fakes {

class FakeFlash : public protocol::FlashTarget, public protocol::MemoryReader, public protocol::Application {
   public:
   FakeFlash(uint32_t start, uint32_t size, uint32_t pageSize)
      : start_{ start }
      , pageSize_{ pageSize }
      , bytes(size, 0x00) {
   }

   uint8_t &at(uint32_t address) {
      return bytes.at(address - start_);
   }

   void read(uint32_t address, std::span<uint8_t> data) override {
      for(uint8_t &byte : data) {
         byte = at(address++);
      }
   }

   bool erasePage(uint32_t address) override {
      erasedPages.push_back(address);
      if(address % pageSize_ != 0 || address < start_ || address - start_ + pageSize_ > bytes.size()) {
         ++violations;
         return false;
      }
      if(failErase) {
         return false;
      }
      for(uint32_t i = 0; i < pageSize_; ++i) {
         at(address + i) = 0xFF;
      }
      return true;
   }

   bool write(uint32_t address, std::span<const uint8_t> data) override {
      ++writes;
      if(address % 4 != 0 || data.size() % 4 != 0 || address < start_ || address - start_ + data.size() > bytes.size()) {
         ++violations;
         return false;
      }
      if(failWrite) {
         return false;
      }
      for(uint8_t byte : data) {
         if(at(address) != 0xFF) {
            ++violations;
            return false;
         }
         at(address++) = byte;
      }
      return true;
   }

   bool isValid() const override {
      ++checks;
      return valid;
   }

   bool headerIsValid(std::span<const uint8_t> bytes) const override {
      headerChecks.emplace_back(bytes.begin(), bytes.end());
      return validHeader;
   }

   std::vector<uint32_t> erasedPages;
   int writes = 0;
   int violations = 0;
   mutable int checks = 0;
   // The bytes of each header check
   mutable std::vector<std::vector<uint8_t>> headerChecks;
   bool failErase = false;
   bool failWrite = false;
   bool valid = true;
   bool validHeader = true;

   private:
   uint32_t start_;
   uint32_t pageSize_;

   public:
   std::vector<uint8_t> bytes;
};

} // namespace fakes
