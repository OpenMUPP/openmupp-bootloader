// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Interfaces between the AN3155 protocol and the hardware (issue #8). The
// protocol code has no hardware access: #10 implements these interfaces on
// the STM32F1, and the host tests use the fakes in tests/fakes.
//
// The interfaces have no virtual destructor: nothing deletes an object
// through them. The protected destructor prevents that at compile time.

#include <cstdint>
#include <span>

namespace protocol {

// A millisecond counter. It wraps after 2^32 ms; the protocol uses only
// differences.
class Clock {
   public:
   virtual uint32_t milliseconds() const = 0;

   protected:
   ~Clock() = default;
};

// Reads memory. The protocol calls it only for ranges that the MemoryMap
// marks as readable.
class MemoryReader {
   public:
   virtual void read(uint32_t address, std::span<uint8_t> data) = 0;

   protected:
   ~MemoryReader() = default;
};

// Erases and programs the flash. The protocol calls it only for the
// application area, and only for pages that are not write-protected.
class FlashTarget {
   public:
   // Erases the page that starts at address. Returns false on an error.
   virtual bool erasePage(uint32_t address) = 0;

   // Programs data at address, then verifies it. The address and the size
   // are multiples of 4, and the range is erased. Returns false on an
   // error or a verify mismatch.
   virtual bool write(uint32_t address, std::span<const uint8_t> data) = 0;

   protected:
   ~FlashTarget() = default;
};

// The write protection (WRP) bits of the option bytes. Bit n stands for
// the 4 KB group n of the flash (pages 4n to 4n+3); a set bit means that
// the group is write-protected. This is the inverse of FLASH_WRPR.
class OptionBytes {
   public:
   // The protection that is active now.
   virtual uint32_t writeProtectedGroups() const = 0;

   // Erases the option bytes and programs them with this protection. RDP,
   // the USER byte and the data bytes keep their values. The bootloader
   // groups are always protected, also when groups does not have them.
   // The new value takes effect after a reset. Returns false on an error.
   virtual bool setWriteProtectedGroups(uint32_t groups) = 0;

   protected:
   ~OptionBytes() = default;
};

// The application image in the flash.
class Application {
   public:
   // True when the image header and the CRC are correct (#9).
   virtual bool isValid() const = 0;

   protected:
   ~Application() = default;
};

} // namespace protocol
