// SPDX-License-Identifier: GPL-3.0-or-later
// The DFU state machine (protocol/DfuProtocol.h) without USB.

#include <doctest/doctest.h>

#include <vector>

#include "fakes/FakeFlash.h"
#include "protocol/DfuProtocol.h"

using dfu::State;
using dfu::Status;
using fakes::FakeFlash;

namespace {

constexpr uint32_t start = 0x0800'2000;
constexpr uint32_t areaSize = 8 * 1024;

std::vector<uint8_t> pattern(std::size_t size, uint8_t seed = 1) {
   std::vector<uint8_t> bytes(size);
   for(std::size_t i = 0; i < size; ++i) {
      bytes[i] = static_cast<uint8_t>(i * 7 + seed);
   }
   return bytes;
}

struct Fixture {
   explicit Fixture(uint32_t pageSize = 1024)
      : flash{ start, areaSize, pageSize }
      , protocol{ { start, start + areaSize, pageSize, 80, 10 }, flash, flash, flash } {
   }

   uint8_t (&status())[6] {
      protocol.getStatus(reply);
      return reply;
   }

   // DNLOAD, GETSTATUS (busy), status stage, work, GETSTATUS (idle)
   void download(uint16_t block, const std::vector<uint8_t> &data) {
      REQUIRE(protocol.beginDownload(block, static_cast<uint16_t>(data.size())));
      std::copy(data.begin(), data.end(), protocol.buffer().begin());
      protocol.endDownload();
      CHECK(status()[4] == static_cast<uint8_t>(State::dnBusy));
      CHECK(reply[1] == 80);
      protocol.requestDone();
      protocol.runWork();
      CHECK(status()[4] == static_cast<uint8_t>(State::dnloadIdle));
      CHECK(reply[0] == static_cast<uint8_t>(Status::ok));
   }

   void manifest() {
      REQUIRE(protocol.beginDownload(0, 0));
      CHECK(status()[4] == static_cast<uint8_t>(State::manifest));
      CHECK(reply[1] == 10);
      protocol.requestDone();
      protocol.runWork();
   }

   FakeFlash flash;
   dfu::Protocol protocol;
   uint8_t reply[6] = {};
};

} // namespace

TEST_CASE("A download writes the blocks in order and the manifestation starts the application") {
   Fixture f;
   const auto first = pattern(1024, 1);
   const auto second = pattern(1024, 2);
   const auto last = pattern(301, 3);
   f.download(0, first);
   f.download(1, second);
   f.download(2, last);
   f.manifest();

   CHECK(f.protocol.state() == State::manifestWaitReset);
   CHECK(f.protocol.resetRequested());
   CHECK(f.flash.checks == 1);
   CHECK(f.flash.violations == 0);
   CHECK(f.flash.erasedPages == std::vector<uint32_t>{ start, start + 1024, start + 2048 });
   for(std::size_t i = 0; i < 1024; ++i) {
      REQUIRE(f.flash.at(start + static_cast<uint32_t>(i)) == first[i]);
      REQUIRE(f.flash.at(start + 1024 + static_cast<uint32_t>(i)) == second[i]);
   }
   for(std::size_t i = 0; i < last.size(); ++i) {
      REQUIRE(f.flash.at(start + 2048 + static_cast<uint32_t>(i)) == last[i]);
   }
   // The fill of the last word, and the rest of the erased page
   CHECK(f.flash.at(start + 2048 + 301) == 0xFF);
   CHECK(f.flash.at(start + 2048 + 303) == 0xFF);
   // The page behind the image keeps its content
   CHECK(f.flash.at(start + 3072) == 0x00);
}

TEST_CASE("With 256-byte pages, a block erases the four pages under it") {
   Fixture f{ 256 };
   f.download(0, pattern(1024));
   f.download(1, pattern(100));
   CHECK(f.flash.erasedPages == std::vector<uint32_t>{ start, start + 256, start + 512, start + 768, start + 1024 });
   CHECK(f.flash.violations == 0);
}

TEST_CASE("The flash work runs only after the status stage of GETSTATUS") {
   Fixture f;
   REQUIRE(f.protocol.beginDownload(0, 4));
   f.protocol.endDownload();
   f.protocol.runWork();
   CHECK(f.flash.writes == 0);
   CHECK(f.status()[4] == static_cast<uint8_t>(State::dnBusy));
   f.protocol.runWork();
   CHECK(f.flash.writes == 0);
   // A second GETSTATUS while busy reports busy again
   CHECK(f.status()[4] == static_cast<uint8_t>(State::dnBusy));
   f.protocol.requestDone();
   f.protocol.runWork();
   CHECK(f.flash.writes == 1);
   CHECK(f.protocol.state() == State::dnloadIdle);
}

TEST_CASE("A block out of order stalls and gives dfuERROR until CLRSTATUS") {
   Fixture f;
   SUBCASE("The first block is not 0") {
      CHECK_FALSE(f.protocol.beginDownload(1, 1024));
   }
   SUBCASE("A block is skipped") {
      f.download(0, pattern(1024));
      CHECK_FALSE(f.protocol.beginDownload(2, 1024));
   }
   SUBCASE("A block is larger than wTransferSize") {
      CHECK_FALSE(f.protocol.beginDownload(0, 1025));
   }
   CHECK(f.protocol.state() == State::error);
   CHECK(f.status()[0] == static_cast<uint8_t>(Status::errStalledPkt));
   CHECK_FALSE(f.protocol.beginDownload(0, 1024));
   CHECK(f.protocol.clearStatus());
   CHECK(f.protocol.state() == State::idle);
   CHECK(f.status()[0] == static_cast<uint8_t>(Status::ok));
}

TEST_CASE("A block behind the application area gives errADDRESS") {
   Fixture f;
   for(uint16_t block = 0; block < areaSize / 1024; ++block) {
      f.download(block, pattern(1024));
   }
   CHECK_FALSE(f.protocol.beginDownload(areaSize / 1024, 4));
   CHECK(f.status()[0] == static_cast<uint8_t>(Status::errAddress));
   CHECK(f.flash.violations == 0);
}

TEST_CASE("A zero-length download in dfuIDLE stalls") {
   Fixture f;
   CHECK_FALSE(f.protocol.beginDownload(0, 0));
   CHECK(f.protocol.state() == State::error);
}

TEST_CASE("Erase and write errors give errERASE and errWRITE") {
   Fixture f;
   REQUIRE(f.protocol.beginDownload(0, 8));
   f.protocol.endDownload();
   SUBCASE("Erase") {
      f.flash.failErase = true;
      f.status();
      f.protocol.requestDone();
      f.protocol.runWork();
      CHECK(f.status()[0] == static_cast<uint8_t>(Status::errErase));
   }
   SUBCASE("Write") {
      f.flash.failWrite = true;
      f.status();
      f.protocol.requestDone();
      f.protocol.runWork();
      CHECK(f.status()[0] == static_cast<uint8_t>(Status::errWrite));
   }
   CHECK(f.protocol.state() == State::error);
   CHECK_FALSE(f.protocol.resetRequested());
}

TEST_CASE("An image that is not valid gives errVERIFY and no reset") {
   Fixture f;
   f.flash.valid = false;
   f.download(0, pattern(64));
   f.manifest();
   CHECK(f.protocol.state() == State::error);
   CHECK(f.status()[0] == static_cast<uint8_t>(Status::errVerify));
   CHECK_FALSE(f.protocol.resetRequested());
}

TEST_CASE("An upload reads the application area and ends with a short block") {
   Fixture f;
   for(uint32_t i = 0; i < areaSize; ++i) {
      f.flash.at(start + i) = static_cast<uint8_t>(i);
   }
   uint8_t out[1024];
   for(uint16_t block = 0; block < areaSize / 1024; ++block) {
      REQUIRE(f.protocol.upload(block, 1024, out) == 1024);
      CHECK(out[0] == static_cast<uint8_t>(block * 1024));
      CHECK(f.protocol.state() == State::uploadIdle);
   }
   CHECK(f.protocol.upload(areaSize / 1024, 1024, out) == 0);
   CHECK(f.protocol.state() == State::idle);
   SUBCASE("A short request also ends the upload") {
      CHECK(f.protocol.upload(0, 1024, out) == 1024);
      CHECK(f.protocol.upload(7, 1024, out) == 1024);
      CHECK(f.protocol.state() == State::uploadIdle);
   }
   SUBCASE("No upload while a download runs") {
      f.download(0, pattern(4));
      CHECK(f.protocol.upload(0, 1024, out) == -1);
      CHECK(f.protocol.state() == State::error);
   }
}

TEST_CASE("ABORT and a bus reset end a download, and the next one starts at block 0") {
   Fixture f;
   f.download(0, pattern(1024));
   SUBCASE("ABORT") {
      CHECK(f.protocol.abort());
   }
   SUBCASE("Bus reset") {
      f.protocol.usbReset();
   }
   CHECK(f.protocol.state() == State::idle);
   CHECK_FALSE(f.protocol.beginDownload(1, 1024));
   REQUIRE(f.protocol.clearStatus());
   f.download(0, pattern(1024, 9));
   // The new download erases the page again
   CHECK(f.flash.erasedPages == std::vector<uint32_t>{ start, start });
   CHECK(f.flash.at(start + 1) == pattern(1024, 9)[1]);
}

TEST_CASE("ABORT in dfuERROR stalls, CLRSTATUS outside dfuERROR stalls") {
   Fixture f;
   CHECK_FALSE(f.protocol.clearStatus());
   CHECK(f.protocol.state() == State::error);
   CHECK_FALSE(f.protocol.abort());
   CHECK(f.protocol.clearStatus());
}
