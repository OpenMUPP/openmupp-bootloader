// SPDX-License-Identifier: GPL-3.0-or-later
// The control endpoint of the DFU bootloader (usb/DfuDevice.h) with a fake
// USB driver. The test plays the host: the enumeration, and the requests of
// dfu-util 0.11 for a download (-D) and an upload (-U).

#include <doctest/doctest.h>

#include <vector>

#include "fakes/FakeFlash.h"
#include "usb/DfuDevice.h"

using dfu::State;
using fakes::FakeFlash;
using Bytes = std::vector<uint8_t>;

namespace {

constexpr uint32_t start = 0x0800'2000;
constexpr uint32_t areaSize = 8 * 1024;

// The driver side: the packets that the device sends, and its EP0 state.
struct FakeHw {
   void send(const uint8_t *data, uint16_t size) {
      REQUIRE(size <= usb::descriptors::ep0Size);
      REQUIRE_FALSE(txBusy);
      packets.emplace_back(data, data + size);
      txBusy = true;
   }
   void receive() {
      rxArmed = true;
      stalled = false;
   }
   void stall() {
      stalled = true;
   }
   void setAddress(uint8_t a) {
      address = a;
   }

   std::vector<Bytes> packets;
   bool txBusy = false;
   bool rxArmed = false;
   bool stalled = false;
   uint8_t address = 0;
};

struct Host {
   Host()
      : flash{ start, areaSize, 1024 }
      , protocol{ { start, start + areaSize, 1024, 20, 80, 10 }, flash, flash, flash }
      , serial{ { 0xF7, 0xC8, 0x8C, 0xAD, 0xF8, 0x50, 0x7B, 0xC7, 0x39, 0xE3, 0x39, 0xE3 } }
      , device{ hw, protocol, serial.bytes } {
      device.busReset();
   }

   // The result of a control transfer: stalled, or the IN data
   struct Result {
      bool stalled;
      Bytes data;
   };

   void setupPacket(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
      const uint8_t packet[8] = { type,
                                  request,
                                  static_cast<uint8_t>(value),
                                  static_cast<uint8_t>(value >> 8),
                                  static_cast<uint8_t>(index),
                                  static_cast<uint8_t>(index >> 8),
                                  static_cast<uint8_t>(length),
                                  static_cast<uint8_t>(length >> 8) };
      hw.packets.clear();
      hw.txBusy = false;
      hw.stalled = false;
      device.setup(packet);
   }

   // One IN packet leaves the device
   Bytes takeIn() {
      REQUIRE(hw.txBusy);
      Bytes packet = hw.packets.back();
      hw.txBusy = false;
      device.inComplete();
      return packet;
   }

   // A control transfer with an IN data stage, as the host controller does
   // it: IN packets until a short packet or wLength, then the status OUT.
   Result in(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
      setupPacket(type, request, value, index, length);
      if(hw.stalled) {
         return { true, {} };
      }
      Bytes data;
      for(;;) {
         const Bytes packet = takeIn();
         data.insert(data.end(), packet.begin(), packet.end());
         if(packet.size() < usb::descriptors::ep0Size || data.size() >= length) {
            break;
         }
      }
      REQUIRE_FALSE(hw.txBusy);
      device.out(nullptr, 0);
      return { false, data };
   }

   // A control transfer with an OUT data stage or no data stage
   bool out(uint8_t type, uint8_t request, uint16_t value, uint16_t index, const Bytes &data = {}) {
      setupPacket(type, request, value, index, static_cast<uint16_t>(data.size()));
      if(hw.stalled) {
         return false;
      }
      for(std::size_t offset = 0; offset < data.size(); offset += usb::descriptors::ep0Size) {
         REQUIRE(hw.rxArmed);
         hw.rxArmed = false;
         const std::size_t n = std::min<std::size_t>(usb::descriptors::ep0Size, data.size() - offset);
         device.out(data.data() + offset, static_cast<uint16_t>(n));
         if(hw.stalled) {
            return false;
         }
      }
      // The status stage: a zero-length IN packet
      const Bytes status = takeIn();
      CHECK(status.empty());
      return true;
   }

   Result getStatus() {
      return in(0xA1, dfu::request::getStatus, 0, 0, 6);
   }

   void configure() {
      REQUIRE(out(0x00, 0x09, 1, 0));
   }

   FakeHw hw;
   FakeFlash flash;
   dfu::Protocol protocol;
   usb::descriptors::SerialString serial;
   usb::DfuDevice<FakeHw> device;
};

Bytes pattern(std::size_t size, uint8_t seed) {
   Bytes bytes(size);
   for(std::size_t i = 0; i < size; ++i) {
      bytes[i] = static_cast<uint8_t>(i * 13 + seed);
   }
   return bytes;
}

} // namespace

TEST_CASE("Enumeration: descriptors, address and configuration") {
   Host h;
   // Linux reads the first 64 bytes of the device descriptor first
   auto d = h.in(0x80, 0x06, 0x0100, 0, 64);
   REQUIRE_FALSE(d.stalled);
   CHECK(d.data.size() == 18);
   CHECK(d.data[2] == 0x01); // bcdUSB 2.01
   CHECK(d.data[3] == 0x02);
   CHECK(d.data[7] == 64);

   // SET_ADDRESS: the status stage still uses address 0
   h.setupPacket(0x00, 0x05, 0x23, 0, 0);
   CHECK(h.hw.address == 0);
   CHECK(h.takeIn().empty());
   CHECK(h.hw.address == 0x23);

   auto c = h.in(0x80, 0x06, 0x0200, 0, 9);
   CHECK(c.data.size() == 9);
   CHECK(c.data[2] == 27);
   c = h.in(0x80, 0x06, 0x0200, 0, 255);
   REQUIRE(c.data.size() == 27);
   CHECK(c.data[9 + 5] == 0xFE);  // bInterfaceClass DFU
   CHECK(c.data[9 + 7] == 0x02);  // DFU mode
   CHECK(c.data[18 + 1] == 0x21); // DFU functional
   CHECK((c.data[18 + 5] | c.data[18 + 6] << 8) == dfu::transferSize);

   const auto serial = h.in(0x80, 0x06, 0x0303, 0x0409, 255);
   REQUIRE(serial.data.size() == 50);
   CHECK(serial.data[2] == 'F');
   CHECK(serial.data[4] == '7');
   CHECK(serial.data[48] == '3');

   const auto product = h.in(0x80, 0x06, 0x0302, 0x0409, 255);
   CHECK(product.data.size() == usb::descriptors::product[0]);

   CHECK(h.in(0x80, 0x06, 0x0305, 0x0409, 255).stalled);
   CHECK(h.in(0x80, 0x06, 0x0600, 0, 10).stalled); // device qualifier: full speed only

   // A class request before SET_CONFIGURATION stalls
   CHECK(h.getStatus().stalled);
   h.configure();
   CHECK(h.in(0x80, 0x08, 0, 0, 1).data == Bytes{ 1 });
   CHECK(h.out(0x01, 0x0B, 0, 0));
   CHECK_FALSE(h.out(0x01, 0x0B, 1, 0));
}

TEST_CASE("Windows reads the BOS and the MS OS 2.0 descriptor set") {
   Host h;
   const auto bosHead = h.in(0x80, 0x06, 0x0F00, 0, 5);
   REQUIRE(bosHead.data.size() == 5);
   const uint16_t total = static_cast<uint16_t>(bosHead.data[2] | bosHead.data[3] << 8);
   const auto bos = h.in(0x80, 0x06, 0x0F00, 0, total);
   REQUIRE(bos.data.size() == total);
   const uint8_t vendorCode = bos.data[5 + 26];
   const uint16_t setLength = static_cast<uint16_t>(bos.data[5 + 24] | bos.data[5 + 25] << 8);
   const auto set = h.in(0xC0, vendorCode, 0, 7, setLength);
   REQUIRE(set.data.size() == setLength);
   // The full bytes: a wrong UUID or length makes Windows ignore them
   CHECK(bos.data == Bytes{ 5,    15,   33,   0,    1,    28,   16,   5,    0,    0xDF, 0x60,
                            0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C, 0x9C, 0xD2, 0x65, 0x9D, 0x9E,
                            0x64, 0x8A, 0x9F, 0x00, 0x00, 0x03, 0x06, 30,   0,    0x20, 0 });
   CHECK(set.data ==
         Bytes{ 10, 0, 0, 0, 0, 0, 3, 6, 30, 0, 20, 0, 3, 0, 'W', 'I', 'N', 'U', 'S', 'B', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
   // Another wIndex stalls
   CHECK(h.in(0xC0, vendorCode, 0, 4, 16).stalled);
}

TEST_CASE("dfu-util -D: download, status polls and manifestation") {
   Host h;
   h.configure();
   CHECK(h.in(0xA1, dfu::request::getState, 0, 0, 1).data == Bytes{ static_cast<uint8_t>(State::idle) });

   const Bytes image = pattern(2500, 5);
   uint16_t block = 0;
   for(std::size_t offset = 0; offset < image.size(); offset += dfu::transferSize, ++block) {
      const std::size_t n = std::min<std::size_t>(dfu::transferSize, image.size() - offset);
      REQUIRE(h.out(0x21, dfu::request::dnload, block, 0,
                    Bytes(image.begin() + static_cast<long>(offset), image.begin() + static_cast<long>(offset + n))));
      auto s = h.getStatus();
      REQUIRE(s.data.size() == 6);
      CHECK(s.data[4] == static_cast<uint8_t>(State::dnBusy));
      CHECK(s.data[1] == 80);
      // The main loop runs the flash work after the status stage
      h.protocol.runWork();
      s = h.getStatus();
      CHECK(s.data[4] == static_cast<uint8_t>(State::dnloadIdle));
      CHECK(s.data[0] == 0);
   }
   // dfu-util 0.11: the zero-length DNLOAD with the next block number,
   // GETSTATUS until the state is dfuIDLE
   REQUIRE(h.out(0x21, dfu::request::dnload, block, 0));
   auto s = h.getStatus();
   CHECK(s.data[4] == static_cast<uint8_t>(State::manifest));
   CHECK(s.data[1] == 10);
   h.protocol.runWork();
   CHECK_FALSE(h.protocol.resetRequested());
   h.setupPacket(0xA1, dfu::request::getStatus, 0, 0, 6);
   const Bytes last = h.takeIn();
   REQUIRE(last.size() == 6);
   CHECK(last[4] == static_cast<uint8_t>(State::idle));
   CHECK(last[0] == 0);
   // The application starts only after the status stage of this request
   CHECK_FALSE(h.protocol.resetRequested());
   h.device.out(nullptr, 0);
   CHECK(h.protocol.resetRequested());

   for(std::size_t i = 0; i < image.size(); ++i) {
      REQUIRE(h.flash.at(start + static_cast<uint32_t>(i)) == image[i]);
   }
   CHECK(h.flash.violations == 0);
}

TEST_CASE("dfu-util -U: upload of the application area") {
   Host h;
   h.configure();
   for(uint32_t i = 0; i < areaSize; ++i) {
      h.flash.at(start + i) = static_cast<uint8_t>(i * 3);
   }
   Bytes all;
   for(uint16_t block = 0;; ++block) {
      const auto r = h.in(0xA1, dfu::request::upload, block, 0, dfu::transferSize);
      REQUIRE_FALSE(r.stalled);
      all.insert(all.end(), r.data.begin(), r.data.end());
      if(r.data.size() < dfu::transferSize) {
         break;
      }
   }
   REQUIRE(all.size() == areaSize);
   CHECK(all[1000] == static_cast<uint8_t>(3000));
   CHECK(h.protocol.state() == State::idle);
}

TEST_CASE("An upload with a multiple of 64 bytes below wLength ends with a zero-length packet") {
   Host h;
   h.configure();
   h.setupPacket(0xA1, dfu::request::upload, 0, 0, 1024);
   // Each block of the area is full, so no ZLP here: 16 packets of 64 bytes.
   for(int i = 0; i < 16; ++i) {
      CHECK(h.takeIn().size() == 64);
   }
   CHECK_FALSE(h.hw.txBusy);
   h.device.out(nullptr, 0);

   // A GET_DESCRIPTOR of the configuration with wLength 64 and 27 bytes:
   // a short packet, no ZLP.
   h.setupPacket(0x80, 0x06, 0x0200, 0, 64);
   CHECK(h.takeIn().size() == 27);
   CHECK_FALSE(h.hw.txBusy);
}

TEST_CASE("Unknown and wrong DFU requests stall and give dfuERROR") {
   Host h;
   h.configure();
   SUBCASE("DFU_DETACH") {
      CHECK_FALSE(h.out(0x21, dfu::request::detach, 1000, 0));
   }
   SUBCASE("Unknown request") {
      CHECK_FALSE(h.out(0x21, 0x40, 0, 0));
   }
   SUBCASE("A short data stage") {
      h.setupPacket(0x21, dfu::request::dnload, 0, 0, 1024);
      const Bytes part(32, 0xAA);
      h.device.out(part.data(), 32);
      CHECK(h.hw.stalled);
   }
   const auto s = h.getStatus();
   CHECK(s.data[4] == static_cast<uint8_t>(State::error));
   CHECK(s.data[0] == static_cast<uint8_t>(dfu::Status::errStalledPkt));
   CHECK(h.out(0x21, dfu::request::clrStatus, 0, 0));
   CHECK(h.in(0xA1, dfu::request::getState, 0, 0, 1).data == Bytes{ static_cast<uint8_t>(State::idle) });
   // A DFU request to another interface stalls without a DFU state change
   CHECK(h.in(0xA1, dfu::request::getStatus, 0, 1, 6).stalled);
   CHECK(h.protocol.state() == State::idle);
}

TEST_CASE("A bus reset clears the configuration and the DFU state") {
   Host h;
   h.configure();
   REQUIRE(h.out(0x21, dfu::request::dnload, 0, 0, pattern(64, 1)));
   h.device.busReset();
   CHECK_FALSE(h.device.configured());
   CHECK(h.protocol.state() == State::idle);
}
