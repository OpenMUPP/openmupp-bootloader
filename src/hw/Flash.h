#pragma once

// The flash driver of the bootloader (issue #10, plan section 3.6): page
// erase, half-word programming with a verify, and the option byte update
// for the write protection (WRP).
//
// The driver checks each request again, also if the protocol checked it:
// it erases and writes only in the application area, and only in groups
// that FLASH_WRPR does not protect. Other requests return false.
//
// An erase of one page stalls the CPU for 20-40 ms. The protocol erases
// one page for each poll() call, so the USB device gets a poll between
// two pages (usb/UsbCdc.h).

#include <cstdint>
#include <span>

#include "protocol/Hardware.h"

namespace hw {

class Flash : public protocol::FlashTarget, public protocol::OptionBytes {
   public:
   // flashEnd: the end address of the flash, from the flash size register.
   explicit Flash(uint32_t flashEnd)
      : flashEnd_{ flashEnd } {
   }

   bool erasePage(uint32_t address) override;
   bool write(uint32_t address, std::span<const uint8_t> data) override;

   uint32_t writeProtectedGroups() const override;

   // Erases the option bytes and programs RDP (0xA5, no readout
   // protection), the old USER and data bytes, and the WRP bytes. Then
   // reads all option bytes back. Returns false on an error or a
   // mismatch, and with no change when readout protection is active.
   bool setWriteProtectedGroups(uint32_t groups) override;

   // True when readout protection (RDP) is active (FLASH_OBR.RDPRT).
   bool readoutProtected() const;

   private:
   // True for [address, address + size) in the application area, with no
   // write-protected group.
   bool allowed(uint32_t address, uint32_t size) const;

   uint32_t flashEnd_;
};

} // namespace hw
