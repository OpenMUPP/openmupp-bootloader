// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The flash driver of the bootloader on the CH32V307: erase of 4 KB sectors
// and fast programming of 256-byte pages (Flash of ch32v00x-system), with
// a verify.
//
// The driver checks each request again, also if the DFU protocol checked
// it: it erases and writes only in the application area. Other requests
// return false. The write protection (WRP) is not in this driver yet.

#include <cstdint>
#include <span>

#include "protocol/Hardware.h"

namespace ch32 {

class Flash : public protocol::FlashTarget, public protocol::MemoryReader {
   public:
   bool erasePage(uint32_t address) override;
   bool write(uint32_t address, std::span<const uint8_t> data) override;
   void read(uint32_t address, std::span<uint8_t> data) override;
};

} // namespace ch32
