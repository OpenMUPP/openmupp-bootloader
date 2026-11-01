// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests of the AN3155 protocol: each command as AN3155 defines it,
// the error replies, the timeouts and the abort in each state.

#include <doctest/doctest.h>

#include <vector>

#include "fakes/Session.h"

using namespace fakes;
using layout::applicationStart;
using protocol::An3155Protocol;
namespace command = protocol::command;
namespace memory = protocol::memory;

namespace {

constexpr uint32_t pageSize = memory::pageSize;
constexpr uint16_t firstAppPage = layout::bootloaderSize / pageSize;
constexpr uint16_t lastPage = FakeTarget::defaultFlashSize / pageSize - 1;
constexpr uint8_t firstAppGroup = layout::bootloaderSize / layout::wrpGroupSize;

uint32_t pageAddress(uint32_t page) {
   return layout::flashBase + page * pageSize;
}

Bytes counting(std::size_t size, uint8_t start = 0) {
   Bytes bytes(size);
   for(std::size_t i = 0; i < size; ++i) {
      bytes[i] = static_cast<uint8_t>(start + i);
   }
   return bytes;
}

// Fills the full flash with 0x00, so a test can see which pages an erase
// changed.
void fillFlash(Session &s) {
   for(uint8_t &byte : s.target.flash) {
      byte = 0x00;
   }
}

bool pageErased(Session &s, uint32_t page) {
   for(uint32_t i = 0; i < pageSize; ++i) {
      if(s.target.flashByte(pageAddress(page) + i) != 0xFF) {
         return false;
      }
   }
   return true;
}

// A Get command works: the protocol is in a good state.
void checkResponsive(Session &s) {
   CHECK(s.bl.waitingForCommand());
   CHECK(s.send({ protocol::synchronise }) == Bytes{ ack });
   CHECK(s.send(cmd(command::getVersion)) == Bytes{ ack, 0x31, 0x00, 0x00, ack });
}

} // namespace

TEST_CASE("0x7F gets an ACK") {
   Session s;
   CHECK(s.send({ 0x7F }) == Bytes{ ack });
   CHECK(s.send({ 0x7F, 0x7F }) == Bytes{ ack, ack });
}

TEST_CASE("Get gives version 3.1 and the command list") {
   Session s;
   CHECK(s.send(cmd(command::get)) == Bytes{ ack, 11, 0x31, 0x00, 0x01, 0x02, 0x11, 0x21, 0x31, 0x44, 0x63, 0x73, 0x82, 0x92, ack });
}

TEST_CASE("Get Version gives the version and two option bytes") {
   Session s;
   CHECK(s.send(cmd(command::getVersion)) == Bytes{ ack, 0x31, 0x00, 0x00, ack });
}

TEST_CASE("Get ID gives the PID of the STM32F10xx medium density") {
   Session s;
   CHECK(s.send(cmd(command::getId)) == Bytes{ ack, 0x01, 0x04, 0x10, ack });
}

TEST_CASE("a wrong complement gets a NACK") {
   Session s;
   CHECK(s.send({ command::get, 0x00 }) == Bytes{ nack });
   CHECK(s.send({ command::getId, 0xFF }) == Bytes{ nack });
   checkResponsive(s);
}

TEST_CASE("an unknown command gets a NACK") {
   Session s;
   CHECK(s.send(cmd(0x55)) == Bytes{ nack });
   // Erase (0x43) is not in the command list of version 3.1.
   CHECK(s.send(cmd(command::erase)) == Bytes{ nack });
   checkResponsive(s);
}

TEST_CASE("Readout Protect and Readout Unprotect always get a NACK") {
   Session s;
   CHECK(s.send(cmd(command::readoutProtect)) == Bytes{ nack });
   CHECK(s.send(cmd(command::readoutUnprotect)) == Bytes{ nack });
   CHECK(s.target.optionByteWrites.empty());
   CHECK(s.target.erasedPages.empty());
   CHECK(s.bl.action() == An3155Protocol::Action::none);
   checkResponsive(s);
}

TEST_CASE("Read Memory reads the flash") {
   Session s;
   const Bytes data = counting(256, 7);
   std::copy(data.begin(), data.end(), s.target.flash.begin() + 0x2000);

   SUBCASE("256 bytes") {
      Bytes expected{ ack };
      expected.insert(expected.end(), data.begin(), data.end());
      CHECK(s.send(cmd(command::readMemory)) == Bytes{ ack });
      CHECK(s.send(address(0x0800'2000)) == Bytes{ ack });
      CHECK(s.send({ 0xFF, 0x00 }) == expected);
   }

   SUBCASE("1 byte") {
      CHECK(s.send(concat({ cmd(command::readMemory), address(0x0800'2003), { 0x00, 0xFF } })) == Bytes{ ack, ack, ack, data[3] });
   }
   checkResponsive(s);
}

TEST_CASE("Read Memory reads the system memory, the option bytes and the UID") {
   Session s;
   for(uint32_t start :
       { memory::systemMemory.start, memory::optionBytes.start, memory::flashSizeRegister.start, memory::uid.start }) {
      CAPTURE(start);
      const Bytes reply = s.send(concat({ cmd(command::readMemory), address(start), { 0x01, 0xFE } }));
      CHECK(reply == Bytes{ ack, ack, ack, FakeTarget::pattern(start), FakeTarget::pattern(start + 1) });
   }
}

TEST_CASE("Read Memory outside the readable regions gets a NACK") {
   Session s;

   SUBCASE("unmapped address") {
      CHECK(s.send(cmd(command::readMemory)) == Bytes{ ack });
      CHECK(s.send(address(0x4000'0000)) == Bytes{ nack });
   }

   SUBCASE("range past the end of the flash") {
      CHECK(s.send(concat({ cmd(command::readMemory), address(0x0800'FF80) })) == Bytes{ ack, ack });
      CHECK(s.send({ 0xFF, 0x00 }) == Bytes{ nack });
   }

   SUBCASE("range past the end of the option bytes") {
      CHECK(s.send(concat({ cmd(command::readMemory), address(memory::optionBytes.start) })) == Bytes{ ack, ack });
      CHECK(s.send({ 0x10, 0xEF }) == Bytes{ nack });
   }
   CHECK(s.target.reads == 0);
   checkResponsive(s);
}

TEST_CASE("Read Memory with a wrong checksum gets a NACK") {
   Session s;

   SUBCASE("address checksum") {
      Bytes addr = address(0x0800'2000);
      addr.back() ^= 0x01;
      CHECK(s.send(concat({ cmd(command::readMemory), addr })) == Bytes{ ack, nack });
   }

   SUBCASE("length complement") {
      CHECK(s.send(concat({ cmd(command::readMemory), address(0x0800'2000), { 0x0F, 0x0F } })) == Bytes{ ack, ack, nack });
   }
   CHECK(s.target.reads == 0);
   checkResponsive(s);
}

TEST_CASE("Write Memory programs the application area") {
   Session s;
   const Bytes data = counting(256);
   CHECK(s.send(cmd(command::writeMemory)) == Bytes{ ack });
   CHECK(s.send(address(applicationStart)) == Bytes{ ack });
   CHECK(s.send(writeData(data)) == Bytes{ ack });
   CHECK(Bytes(s.target.flash.begin() + 0x2000, s.target.flash.begin() + 0x2100) == data);
   CHECK(s.target.violations == 0);
   checkResponsive(s);
}

TEST_CASE("Write Memory pads the data to a multiple of 4 with 0xFF") {
   Session s;
   CHECK(s.send(concat({ cmd(command::writeMemory), address(0x0800'4000), writeData({ 1, 2, 3, 4, 5 }) })) == Bytes{ ack, ack, ack });
   CHECK(Bytes(s.target.flash.begin() + 0x4000, s.target.flash.begin() + 0x4008) == Bytes{ 1, 2, 3, 4, 5, 0xFF, 0xFF, 0xFF });
   CHECK(s.target.violations == 0);
}

TEST_CASE("Write Memory in the bootloader area gets a NACK") {
   Session s;

   SUBCASE("start of the flash") {
      CHECK(s.send(concat({ cmd(command::writeMemory), address(layout::flashBase) })) == Bytes{ ack, nack });
   }

   SUBCASE("last word of the bootloader area") {
      CHECK(s.send(concat({ cmd(command::writeMemory), address(applicationStart - 4) })) == Bytes{ ack, nack });
   }
   CHECK(s.target.writes == 0);
   checkResponsive(s);
}

TEST_CASE("Write Memory outside the application area gets a NACK") {
   Session s;

   SUBCASE("RAM") {
      CHECK(s.send(concat({ cmd(command::writeMemory), address(memory::ram.start) })) == Bytes{ ack, nack });
   }

   SUBCASE("option bytes") {
      CHECK(s.send(concat({ cmd(command::writeMemory), address(memory::optionBytes.start) })) == Bytes{ ack, nack });
   }

   SUBCASE("range past the end of the flash") {
      CHECK(s.send(concat({ cmd(command::writeMemory), address(0x0800'FF80), writeData(counting(256)) })) == Bytes{ ack, ack, nack });
   }

   SUBCASE("address not a multiple of 4") {
      CHECK(s.send(concat({ cmd(command::writeMemory), address(applicationStart + 2) })) == Bytes{ ack, nack });
   }
   CHECK(s.target.writes == 0);
   checkResponsive(s);
}

TEST_CASE("Write Memory with a wrong checksum gets a NACK and writes nothing") {
   Session s;
   Bytes frame = writeData(counting(16));
   frame.back() ^= 0x80;
   CHECK(s.send(concat({ cmd(command::writeMemory), address(applicationStart), frame })) == Bytes{ ack, ack, nack });
   CHECK(s.target.writes == 0);
   checkResponsive(s);
}

TEST_CASE("Write Memory in a write-protected group gets a NACK") {
   Session s;
   s.target.protectedGroups |= 1u << 3;
   CHECK(s.send(concat({ cmd(command::writeMemory), address(0x0800'3FFC), writeData(counting(8)) })) == Bytes{ ack, ack, nack });
   CHECK(s.target.writes == 0);
   checkResponsive(s);
}

TEST_CASE("a flash error in Write Memory gets a NACK") {
   Session s;

   SUBCASE("write error") {
      s.target.failWrite = true;
   }

   SUBCASE("range not erased") {
      s.target.flashByte(applicationStart + 4) = 0x00;
   }
   CHECK(s.send(concat({ cmd(command::writeMemory), address(applicationStart), writeData(counting(8)) })) == Bytes{ ack, ack, nack });
   checkResponsive(s);
}

TEST_CASE("Extended Erase erases the listed pages") {
   Session s;
   fillFlash(s);
   CHECK(s.send(cmd(command::extendedErase)) == Bytes{ ack });
   CHECK(s.send(erasePages({ firstAppPage, 20, lastPage })) == Bytes{ ack });
   CHECK(pageErased(s, firstAppPage));
   CHECK(pageErased(s, 20));
   CHECK(pageErased(s, lastPage));
   CHECK_FALSE(pageErased(s, firstAppPage + 1));
   CHECK(s.target.erasedPages.size() == 3);
   CHECK(s.target.violations == 0);
   checkResponsive(s);
}

TEST_CASE("Extended Erase erases one page for each poll") {
   Session s;
   s.send(cmd(command::extendedErase));
   const Bytes frame = erasePages({ 10, 11, 12 });
   CHECK(s.bl.receive(frame) == frame.size());
   CHECK(s.bl.busy());
   CHECK(s.bl.receive(Bytes{ 0x7F }) == 0);
   s.bl.poll();
   CHECK(s.target.erasedPages.size() == 1);
   s.bl.poll();
   CHECK(s.target.erasedPages.size() == 2);
   s.bl.poll();
   CHECK(s.target.erasedPages.size() == 3);
   CHECK(s.bl.output().empty());
   s.bl.poll();
   CHECK(s.bl.output().size() == 1);
   CHECK(s.bl.output()[0] == ack);
   CHECK_FALSE(s.bl.busy());
}

TEST_CASE("Extended Erase with a page in the bootloader area erases nothing") {
   Session s;
   CHECK(s.send(concat({ cmd(command::extendedErase), erasePages({ 20, 0, 21 }) })) == Bytes{ ack, nack });
   CHECK(s.send(concat({ cmd(command::extendedErase), erasePages({ firstAppPage - 1 }) })) == Bytes{ ack, nack });
   CHECK(s.target.erasedPages.empty());
   checkResponsive(s);
}

TEST_CASE("Extended Erase with a page outside the flash gets a NACK") {
   Session s;
   CHECK(s.send(concat({ cmd(command::extendedErase), erasePages({ 20, uint16_t(lastPage + 1) }) })) == Bytes{ ack, nack });
   CHECK(s.send(concat({ cmd(command::extendedErase), erasePages({ 0x1234 }) })) == Bytes{ ack, nack });
   CHECK(s.target.erasedPages.empty());
   checkResponsive(s);
}

TEST_CASE("Extended Erase with a write-protected page erases nothing") {
   Session s;
   s.target.protectedGroups |= 1u << 5;
   CHECK(s.send(concat({ cmd(command::extendedErase), erasePages({ 12, 20 }) })) == Bytes{ ack, nack });
   CHECK(s.target.erasedPages.empty());
   checkResponsive(s);
}

TEST_CASE("Extended Erase with a wrong checksum erases nothing") {
   Session s;
   Bytes frame = erasePages({ 12, 13 });
   frame.back() ^= 0x01;
   CHECK(s.send(concat({ cmd(command::extendedErase), frame })) == Bytes{ ack, nack });
   CHECK(s.target.erasedPages.empty());
   checkResponsive(s);
}

TEST_CASE("mass erase erases only the application pages") {
   Session s;
   fillFlash(s);
   CHECK(s.send(concat({ cmd(command::extendedErase), { 0xFF, 0xFF, 0x00 } })) == Bytes{ ack, ack });
   for(uint32_t page = 0; page <= lastPage; ++page) {
      CAPTURE(page);
      CHECK(pageErased(s, page) == (page >= firstAppPage));
   }
   CHECK(s.target.erasedPages.size() == lastPage + 1u - firstAppPage);
   CHECK(s.target.violations == 0);
   checkResponsive(s);
}

TEST_CASE("mass erase skips write-protected pages and gets a NACK") {
   Session s;
   fillFlash(s);
   s.target.protectedGroups |= 1u << 4;
   CHECK(s.send(concat({ cmd(command::extendedErase), { 0xFF, 0xFF, 0x00 } })) == Bytes{ ack, nack });
   for(uint32_t page = firstAppPage; page <= lastPage; ++page) {
      CAPTURE(page);
      CHECK(pageErased(s, page) == (page / 4 != 4));
   }
   CHECK(s.target.violations == 0);
   checkResponsive(s);
}

TEST_CASE("mass erase with a wrong checksum erases nothing") {
   Session s;
   CHECK(s.send(concat({ cmd(command::extendedErase), { 0xFF, 0xFF, 0xFF } })) == Bytes{ ack, nack });
   CHECK(s.target.erasedPages.empty());
   checkResponsive(s);
}

TEST_CASE("bank erase and the reserved erase codes get a NACK") {
   Session s;
   for(uint16_t code : { uint16_t{ 0xFFFE }, uint16_t{ 0xFFFD }, uint16_t{ 0xFFF0 }, uint16_t{ 0xFFFC } }) {
      CAPTURE(code);
      const auto msb = static_cast<uint8_t>(code >> 8);
      const auto lsb = static_cast<uint8_t>(code);
      CHECK(s.send(concat({ cmd(command::extendedErase), { msb, lsb, static_cast<uint8_t>(msb ^ lsb) } })) == Bytes{ ack, nack });
   }
   CHECK(s.target.erasedPages.empty());
   checkResponsive(s);
}

TEST_CASE("an erase error stops the erase with a NACK") {
   Session s;
   s.target.failErase = true;
   CHECK(s.send(concat({ cmd(command::extendedErase), erasePages({ 12, 13, 14 }) })) == Bytes{ ack, nack });
   CHECK(s.target.erasedPages.size() == 1);
   checkResponsive(s);
}

TEST_CASE("Go starts a valid application at the application start") {
   Session s;
   CHECK(s.send(cmd(command::go)) == Bytes{ ack });
   CHECK(s.bl.receive(address(applicationStart)) == 5);
   // The ACK goes out before the start.
   CHECK(s.bl.output().size() == 1);
   CHECK(s.bl.action() == An3155Protocol::Action::none);
   s.bl.consumeOutput(1);
   CHECK(s.bl.action() == An3155Protocol::Action::startApplication);
   // After the action, the protocol takes no more bytes.
   CHECK(s.bl.receive(Bytes{ 0x7F }) == 0);
}

TEST_CASE("Go gets a NACK for an invalid application or another address") {
   Session s;

   SUBCASE("invalid application") {
      s.target.applicationValid = false;
      CHECK(s.send(concat({ cmd(command::go), address(applicationStart) })) == Bytes{ ack, nack });
   }

   SUBCASE("other flash address") {
      CHECK(s.send(concat({ cmd(command::go), address(applicationStart + 4) })) == Bytes{ ack, nack });
   }

   SUBCASE("RAM") {
      CHECK(s.send(concat({ cmd(command::go), address(memory::ram.start) })) == Bytes{ ack, nack });
   }

   SUBCASE("bootloader") {
      CHECK(s.send(concat({ cmd(command::go), address(layout::flashBase) })) == Bytes{ ack, nack });
   }
   CHECK(s.bl.action() == An3155Protocol::Action::none);
   checkResponsive(s);
}

TEST_CASE("Write Protect protects application groups, then resets") {
   Session s;
   s.target.protectedGroups |= 1u << 10;
   const Bytes groups{ firstAppGroup, 7 };
   Bytes frame{ static_cast<uint8_t>(groups.size() - 1) };
   frame.insert(frame.end(), groups.begin(), groups.end());
   frame.push_back(checksum(frame));

   CHECK(s.send(cmd(command::writeProtect)) == Bytes{ ack });
   CHECK(s.send(frame) == Bytes{ ack });
   CHECK(s.target.optionByteWrites ==
         std::vector<uint32_t>{ FakeTarget::bootloaderGroupMask | 1u << firstAppGroup | 1u << 7 | 1u << 10 });
   CHECK(s.bl.action() == An3155Protocol::Action::reset);
}

TEST_CASE("Write Protect of a bootloader or missing group gets a NACK") {
   Session s;
   for(uint8_t group : { uint8_t{ 0 }, uint8_t{ firstAppGroup - 1 }, uint8_t{ 16 }, uint8_t{ 31 }, uint8_t{ 200 } }) {
      CAPTURE(group);
      CHECK(s.send(concat({ cmd(command::writeProtect), { 0x00, group, static_cast<uint8_t>(0x00 ^ group) } })) ==
            Bytes{ ack, nack });
   }
   CHECK(s.target.optionByteWrites.empty());
   CHECK(s.bl.action() == An3155Protocol::Action::none);
   checkResponsive(s);
}

TEST_CASE("Write Protect with a wrong checksum gets a NACK") {
   Session s;
   CHECK(s.send(concat({ cmd(command::writeProtect), { 0x00, 4, 5 } })) == Bytes{ ack, nack });
   CHECK(s.target.optionByteWrites.empty());
   checkResponsive(s);
}

TEST_CASE("Write Unprotect unprotects the application area, then resets") {
   Session s;
   s.target.protectedGroups = 0xFFFF;
   CHECK(s.bl.receive(cmd(command::writeUnprotect)) == 2);
   // The first ACK goes out before the option byte update.
   s.bl.poll();
   CHECK(s.target.optionByteWrites.empty());
   CHECK(s.bl.output().size() == 1);
   s.bl.consumeOutput(1);
   s.bl.poll();
   CHECK(s.target.optionByteWrites == std::vector<uint32_t>{ FakeTarget::bootloaderGroupMask });
   CHECK(s.bl.output().size() == 1);
   CHECK(s.bl.output()[0] == ack);
   CHECK(s.bl.action() == An3155Protocol::Action::none);
   s.bl.consumeOutput(1);
   CHECK(s.bl.action() == An3155Protocol::Action::reset);
}

TEST_CASE("an option byte error gets a NACK, then a reset") {
   Session s;
   s.target.failOptionBytes = true;
   CHECK(s.send(cmd(command::writeUnprotect)) == Bytes{ ack, nack });
   CHECK(s.bl.action() == An3155Protocol::Action::reset);
}

TEST_CASE("receive takes no bytes while a reply waits") {
   Session s;
   const Bytes syncs{ 0x7F, 0x7F, 0x7F };
   CHECK(s.bl.receive(syncs) == 1);
   CHECK(s.bl.receive(syncs) == 0);
   s.bl.consumeOutput(1);
   CHECK(s.bl.receive(syncs) == 1);
   CHECK(s.bl.output().size() == 1);
}

TEST_CASE("consumeOutput removes a part of the reply") {
   Session s;
   s.bl.receive(cmd(command::getId));
   CHECK(s.bl.output().size() == 5);
   s.bl.consumeOutput(2);
   CHECK(Bytes(s.bl.output().begin(), s.bl.output().end()) == Bytes{ 0x04, 0x10, ack });
}

namespace {

// The full byte sequence of each command with more than one byte.
std::vector<Bytes> multiByteCommands() {
   return {
      cmd(command::get),
      concat({ cmd(command::readMemory), address(applicationStart), { 0x0F, 0xF0 } }),
      concat({ cmd(command::go), address(applicationStart) }),
      concat({ cmd(command::writeMemory), address(applicationStart), writeData(counting(8)) }),
      concat({ cmd(command::extendedErase), erasePages({ 12, 13 }) }),
      concat({ cmd(command::extendedErase), { 0xFF, 0xFF, 0x00 } }),
      concat({ cmd(command::writeProtect), { 0x01, 4, 5, 0x01 ^ 4 ^ 5 } }),
      cmd(command::writeUnprotect),
   };
}

} // namespace

TEST_CASE("a timeout in each state discards the command") {
   for(const Bytes &full : multiByteCommands()) {
      for(std::size_t length = 1; length < full.size(); ++length) {
         CAPTURE(full);
         CAPTURE(length);
         Session s;
         s.target.now = 0xFFFF'FF00; // The clock wraps during the test.
         s.send(Bytes(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(length)));
         REQUIRE_FALSE(s.bl.waitingForCommand());
         s.advance(An3155Protocol::timeoutMs - 1);
         CHECK_FALSE(s.bl.waitingForCommand());
         s.advance(1);
         CHECK(s.bl.waitingForCommand());
         CHECK(s.bl.output().empty());
         CHECK(s.target.writes == 0);
         CHECK(s.target.erasedPages.empty());
         CHECK(s.target.optionByteWrites.empty());
         checkResponsive(s);
      }
   }
}

TEST_CASE("each byte restarts the timeout") {
   Session s;
   s.send(cmd(command::writeMemory));
   s.send(address(applicationStart));
   const Bytes frame = writeData(counting(16));
   for(std::size_t i = 0; i + 1 < frame.size(); ++i) {
      s.advance(An3155Protocol::timeoutMs - 1);
      s.send({ frame[i] });
      REQUIRE_FALSE(s.bl.waitingForCommand());
   }
   s.advance(An3155Protocol::timeoutMs - 1);
   CHECK(s.send({ frame.back() }) == Bytes{ ack });
}

TEST_CASE("the timeout starts when the reply is out") {
   Session s;
   s.bl.receive(cmd(command::readMemory));
   s.target.now += 5000;
   s.bl.poll();
   // The ACK is not sent yet: the host cannot answer.
   s.bl.consumeOutput(1);
   s.advance(An3155Protocol::timeoutMs - 1);
   CHECK(s.send(concat({ address(applicationStart), { 0x00, 0xFF } })) == Bytes{ ack, ack, 0xFF });
}

TEST_CASE("an abort in each state discards the command") {
   for(const Bytes &full : multiByteCommands()) {
      for(std::size_t length = 1; length < full.size(); ++length) {
         CAPTURE(full);
         CAPTURE(length);
         Session s;
         s.send(Bytes(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(length)));
         s.bl.reset();
         CHECK(s.bl.output().empty());
         CHECK(s.bl.action() == An3155Protocol::Action::none);
         CHECK(s.target.writes == 0);
         CHECK(s.target.optionByteWrites.empty());
         checkResponsive(s);
      }
   }
}

TEST_CASE("reset discards a reply and an action") {
   Session s;
   s.bl.receive(cmd(command::get));
   CHECK_FALSE(s.bl.output().empty());
   s.bl.reset();
   CHECK(s.bl.output().empty());

   s.send(concat({ cmd(command::go), address(applicationStart) }));
   CHECK(s.bl.action() == An3155Protocol::Action::startApplication);
   s.bl.reset();
   CHECK(s.bl.action() == An3155Protocol::Action::none);
   checkResponsive(s);
}
