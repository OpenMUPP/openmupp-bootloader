// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The DFU class of the bootloader: USB Device Firmware Upgrade 1.1, DFU
// mode only, one alternate setting (the application area).
//
// The address is fixed: block n of a download or an upload is at
// start + n * transferSize. The host cannot select an address, so it cannot
// write the bootloader area or the settings page.
//
// A download writes the blocks in order, from block 0. Before a block is
// written, the pages under it are erased. The pages behind the last block
// keep their content. The zero-length download ends the download: the
// manifestation checks the image header and the CRC. A valid image puts
// the device back into dfuIDLE (bitManifestationTolerant): dfu-util 0.11
// ends a download with "Done!" and exit code 0 only when the GETSTATUS
// after the manifestation reports dfuIDLE. After the status stage of that
// GETSTATUS, or after a bus reset (dfu-util -R), resetRequested() is true,
// and the bootloader resets into the application. An image that is not
// valid gives errVERIFY, and the device stays in DFU mode.
//
// Each block of a download and of an upload has wTransferSize bytes. Only
// the last block of a download can be shorter.
//
// The flash work does not run in the USB request. GETSTATUS reports
// dfuDNBUSY or dfuMANIFEST with bwPollTimeout. After the status stage of
// that request, the main loop calls runWork(). The host waits
// bwPollTimeout and then sends the next GETSTATUS.
//
// No hardware access: the host tests use this header too.

#include <cstddef>
#include <cstdint>
#include <span>

#include "protocol/Hardware.h"

namespace dfu {

// DFU 1.1, section 6.1.2 and appendix A
enum class State : uint8_t {
   appIdle = 0,
   appDetach = 1,
   idle = 2,
   dnloadSync = 3,
   dnBusy = 4,
   dnloadIdle = 5,
   manifestSync = 6,
   manifest = 7,
   manifestWaitReset = 8,
   uploadIdle = 9,
   error = 10,
};

enum class Status : uint8_t {
   ok = 0x00,
   errTarget = 0x01,
   errFile = 0x02,
   errWrite = 0x03,
   errErase = 0x04,
   errCheckErased = 0x05,
   errProg = 0x06,
   errVerify = 0x07,
   errAddress = 0x08,
   errNotDone = 0x09,
   errFirmware = 0x0A,
   errVendor = 0x0B,
   errUsbr = 0x0C,
   errPor = 0x0D,
   errUnknown = 0x0E,
   errStalledPkt = 0x0F,
};

// The class requests (bRequest), DFU 1.1 section 3
namespace request {
constexpr uint8_t detach = 0;
constexpr uint8_t dnload = 1;
constexpr uint8_t upload = 2;
constexpr uint8_t getStatus = 3;
constexpr uint8_t clrStatus = 4;
constexpr uint8_t getState = 5;
constexpr uint8_t abort = 6;
} // namespace request

// The block size (wTransferSize of the DFU functional descriptor). The
// buffer of the protocol has this size.
constexpr uint16_t transferSize = 1024;

struct Config {
   // True when the area and the page size fit together: a page never
   // reaches outside the area.
   constexpr bool valid() const {
      return pageSize > 0 && pageSize % 4 == 0 && start < end && start % pageSize == 0 && end % pageSize == 0 &&
             (transferSize % pageSize == 0 || pageSize % transferSize == 0);
   }

   // The application area: [start, end)
   uint32_t start;
   uint32_t end;
   // The erase unit of the flash. A multiple of 4, and transferSize is a
   // multiple of it or it is a multiple of transferSize.
   uint32_t pageSize;
   // bwPollTimeout for the write of one block, for the erase and the write
   // of a block that starts in a page that is not erased yet, and for the
   // check of the image.
   uint32_t blockPollMs;
   uint32_t erasePollMs;
   uint32_t manifestPollMs;
};

class Protocol {
   public:
   Protocol(const Config &config, protocol::FlashTarget &flash, protocol::MemoryReader &memory, protocol::Application &application);

   // Each request returns false when the device stalls it. A stalled
   // request also puts the device into dfuERROR (DFU 1.1 appendix A).

   // DFU_DNLOAD, SETUP stage. length > 0: the control pipe then puts the
   // data into buffer() and calls endDownload(). length 0: the end of the
   // download.
   bool beginDownload(uint16_t block, uint16_t length);
   std::span<uint8_t> buffer() {
      return { buffer_, transferSize };
   }
   // DFU_DNLOAD, all data received
   void endDownload();

   // DFU_UPLOAD: copies up to length bytes of block into out. Returns the
   // size of the data, or -1 for a stall. A short block ends the upload.
   int upload(uint16_t block, uint16_t length, std::span<uint8_t> out);

   // DFU_GETSTATUS: the 6 bytes of the reply
   void getStatus(uint8_t (&reply)[6]);
   State state() const {
      return state_;
   }
   bool clearStatus();
   bool abort();
   // A request that the DFU class does not know
   void stall();

   // The status stage of the last request is complete
   void requestDone() {
      workReady_ = workPending_;
      if(startPending_) {
         resetRequested_ = true;
      }
   }
   // The flash work of dfuDNBUSY or dfuMANIFEST. The main loop calls it.
   void runWork();
   // True after a valid manifestation and the GETSTATUS that reported it:
   // start the application
   bool resetRequested() const {
      return resetRequested_;
   }
   // A USB bus reset: the device goes back to dfuIDLE
   void usbReset();

   private:
   void fail(Status status);
   bool write();
   // The end of the next block, its size rounded up to a word
   uint32_t blockEnd() const;
   uint32_t blockPollMs() const;
   bool manifest();

   Config config_;
   protocol::FlashTarget &flash_;
   protocol::MemoryReader &memory_;
   protocol::Application &application_;
   State state_ = State::idle;
   Status status_ = Status::ok;
   bool workPending_ = false;
   bool workReady_ = false;
   // A valid image is in the flash, from the manifestation until the next
   // download. startPending_: the next status stage starts the application.
   bool manifested_ = false;
   bool startPending_ = false;
   bool resetRequested_ = false;
   // The last block was shorter than transferSize: only the end follows
   bool shortBlock_ = false;
   // The next block of the download, its size, and the end of the erased
   // area of this download
   uint16_t nextBlock_ = 0;
   uint16_t blockSize_ = 0;
   uint32_t erasedEnd_ = 0;
   alignas(4) uint8_t buffer_[transferSize];
};

} // namespace dfu
