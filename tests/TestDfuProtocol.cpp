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
      , protocol{ { start, start + areaSize, pageSize, 20, 80, 10 }, flash, flash, flash } {
   }

   uint8_t (&status())[6] {
      protocol.getStatus(reply);
      return reply;
   }

   // DNLOAD, GETSTATUS (busy), status stage, work, GETSTATUS (idle)
   // pollMs: 80 for a block that erases, 20 for a block that only writes
   void download(uint16_t block, const std::vector<uint8_t> &data, uint8_t pollMs = 80) {
      REQUIRE(protocol.beginDownload(block, static_cast<uint16_t>(data.size())));
      std::copy(data.begin(), data.end(), protocol.buffer().begin());
      protocol.endDownload();
      CHECK(status()[4] == static_cast<uint8_t>(State::dnBusy));
      CHECK(reply[1] == pollMs);
      protocol.requestDone();
      protocol.runWork();
      CHECK(status()[4] == static_cast<uint8_t>(State::dnloadIdle));
      CHECK(reply[0] == static_cast<uint8_t>(Status::ok));
   }

   // The zero-length DNLOAD at block (the next block number of dfu-util),
   // GETSTATUS (manifest), status stage, work
   void manifest(uint16_t block = 0) {
      REQUIRE(protocol.beginDownload(block, 0));
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
   f.manifest(3);

   // dfu-util reads the end of the manifestation: dfuIDLE, OK. The
   // application starts only after the status stage of that request.
   CHECK(f.protocol.state() == State::idle);
   CHECK_FALSE(f.protocol.resetRequested());
   CHECK(f.status()[4] == static_cast<uint8_t>(State::idle));
   CHECK(f.reply[0] == static_cast<uint8_t>(Status::ok));
   CHECK_FALSE(f.protocol.resetRequested());
   f.protocol.requestDone();
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

TEST_CASE("Only a block that starts in a page that is not erased reports the erase poll time") {
   // Four blocks in a 4 KB sector: the first erases it, the other three
   // only write
   Fixture f{ 4096 };
   f.download(0, pattern(1024), 80);
   f.download(1, pattern(1024), 20);
   f.download(2, pattern(1024), 20);
   f.download(3, pattern(1024), 20);
   f.download(4, pattern(1024), 80);
   f.download(5, pattern(100), 20);
   CHECK(f.flash.erasedPages == std::vector<uint32_t>{ start, start + 4096 });
   CHECK(f.flash.violations == 0);
}

TEST_CASE("A GETSTATUS in dfuDNBUSY repeats the poll time of the block") {
   Fixture f{ 4096 };
   // Block 0 erases
   REQUIRE(f.protocol.beginDownload(0, 1024));
   f.protocol.endDownload();
   CHECK(f.status()[1] == 80);
   CHECK(f.status()[4] == static_cast<uint8_t>(State::dnBusy));
   CHECK(f.reply[1] == 80);
   f.protocol.requestDone();
   f.protocol.runWork();
   CHECK(f.status()[4] == static_cast<uint8_t>(State::dnloadIdle));
   // Block 1 only writes
   REQUIRE(f.protocol.beginDownload(1, 1024));
   f.protocol.endDownload();
   CHECK(f.status()[1] == 20);
   CHECK(f.status()[4] == static_cast<uint8_t>(State::dnBusy));
   CHECK(f.reply[1] == 20);
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
   f.manifest(1);
   CHECK(f.protocol.state() == State::error);
   CHECK(f.status()[0] == static_cast<uint8_t>(Status::errVerify));
   f.protocol.requestDone();
   f.protocol.usbReset();
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
   SUBCASE("A request with another size than wTransferSize stalls") {
      CHECK(f.protocol.upload(0, 512, out) == -1);
      CHECK(f.protocol.state() == State::error);
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

TEST_CASE("Only the last block of a download can be shorter than wTransferSize") {
   Fixture f;
   f.download(0, pattern(1024));
   f.download(1, pattern(512));
   SUBCASE("A block after the short block stalls") {
      CHECK_FALSE(f.protocol.beginDownload(2, 512));
      CHECK(f.protocol.state() == State::error);
      CHECK(f.status()[0] == static_cast<uint8_t>(Status::errStalledPkt));
   }
   SUBCASE("The end of the download is accepted") {
      f.manifest(2);
      CHECK(f.protocol.state() == State::idle);
      CHECK(f.flash.checks == 1);
   }
}

TEST_CASE("A bus reset after a valid manifestation starts the application (dfu-util -R)") {
   Fixture f;
   f.download(0, pattern(64));
   f.manifest(1);
   SUBCASE("After a stalled DFU_DETACH") {
      f.protocol.stall();
   }
   SUBCASE("Without the final GETSTATUS") {
   }
   CHECK_FALSE(f.protocol.resetRequested());
   f.protocol.usbReset();
   CHECK(f.protocol.resetRequested());
}

TEST_CASE("A new download clears the valid manifestation") {
   Fixture f;
   f.download(0, pattern(64));
   f.manifest(1);
   f.download(0, pattern(64, 4));
   f.protocol.abort();
   f.status();
   f.protocol.requestDone();
   f.protocol.usbReset();
   CHECK_FALSE(f.protocol.resetRequested());
}

TEST_CASE("Config::valid() refuses an area that a page can leave") {
   using dfu::Config;
   CHECK(Config{ 0x0800'2000, 0x0804'0000, 256, 20, 40, 20 }.valid());
   CHECK(Config{ 0x0800'2000, 0x0802'0000, 1024, 20, 80, 10 }.valid());
   CHECK(Config{ 0x0800'2000, 0x0802'0000, 2048, 20, 80, 10 }.valid());
   CHECK_FALSE(Config{ 0x0800'2400, 0x0804'0000, 4096, 20, 40, 20 }.valid()); // start inside a page
   CHECK_FALSE(Config{ 0x0800'2000, 0x0803'FF00, 1024, 20, 40, 20 }.valid()); // end inside a page
   CHECK_FALSE(Config{ 0x0800'2000, 0x0804'0000, 768, 20, 40, 20 }.valid());  // 768 and 1024
   CHECK_FALSE(Config{ 0x0800'2000, 0x0800'2000, 256, 20, 40, 20 }.valid());  // empty
}
