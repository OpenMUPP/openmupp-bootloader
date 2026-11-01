// SPDX-License-Identifier: GPL-3.0-or-later
// The command sequences of stm32flash (stm32.c of stm32flash 0.7): the
// connect, the device query, the erase, the write in 256-byte blocks, the
// verify and the start of the application. The test plays the host side
// as stm32flash does and checks each reply.

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "fakes/Session.h"

using namespace fakes;
using layout::applicationStart;
using protocol::An3155Protocol;
namespace command = protocol::command;

namespace {

Bytes image(std::size_t size) {
   Bytes bytes(size);
   for(std::size_t i = 0; i < size; ++i) {
      bytes[i] = static_cast<uint8_t>(i * 7 + 3);
   }
   return bytes;
}

// stm32_init(): the synchronisation byte, then Get, Get Version and Get ID.
void connect(Session &s) {
   REQUIRE(s.send({ 0x7F }) == Bytes{ ack });

   // Get: stm32flash reads the length byte, then length + 1 bytes, then
   // the ACK.
   const Bytes get = s.send(cmd(command::get));
   REQUIRE(get.size() >= 3);
   CHECK(get.front() == ack);
   const std::size_t length = get[1];
   REQUIRE(get.size() == length + 4);
   CHECK(get[2] == 0x31);
   const Bytes commands(get.begin() + 3, get.end() - 1);
   CHECK(std::find(commands.begin(), commands.end(), command::extendedErase) != commands.end());
   CHECK(std::find(commands.begin(), commands.end(), command::erase) == commands.end());
   CHECK(get.back() == ack);

   CHECK(s.send(cmd(command::getVersion)) == Bytes{ ack, 0x31, 0x00, 0x00, ack });
   CHECK(s.send(cmd(command::getId)) == Bytes{ ack, 0x01, 0x04, 0x10, ack });
}

// stm32_write_memory(): the address must be a multiple of 4; the data is
// padded to a multiple of 4 with 0xFF.
void writeBlock(Session &s, uint32_t addr, const Bytes &data) {
   REQUIRE(s.send(cmd(command::writeMemory)) == Bytes{ ack });
   REQUIRE(s.send(address(addr)) == Bytes{ ack });
   Bytes padded = data;
   while(padded.size() % 4 != 0) {
      padded.push_back(0xFF);
   }
   REQUIRE(s.send(writeData(padded)) == Bytes{ ack });
}

// stm32_read_memory().
Bytes readBlock(Session &s, uint32_t addr, std::size_t size) {
   REQUIRE(s.send(cmd(command::readMemory)) == Bytes{ ack });
   REQUIRE(s.send(address(addr)) == Bytes{ ack });
   const auto n = static_cast<uint8_t>(size - 1);
   const Bytes reply = s.send({ n, static_cast<uint8_t>(n ^ 0xFF) });
   REQUIRE(reply.size() == size + 1);
   CHECK(reply.front() == ack);
   return Bytes(reply.begin() + 1, reply.end());
}

void writeAndVerify(Session &s, const Bytes &data) {
   for(std::size_t offset = 0; offset < data.size(); offset += 256) {
      const std::size_t size = std::min<std::size_t>(256, data.size() - offset);
      const auto first = data.begin() + static_cast<std::ptrdiff_t>(offset);
      writeBlock(s, applicationStart + static_cast<uint32_t>(offset), Bytes(first, first + static_cast<std::ptrdiff_t>(size)));
   }
   for(std::size_t offset = 0; offset < data.size(); offset += 256) {
      const std::size_t size = std::min<std::size_t>(256, data.size() - offset);
      const auto first = data.begin() + static_cast<std::ptrdiff_t>(offset);
      CHECK(readBlock(s, applicationStart + static_cast<uint32_t>(offset), size) ==
            Bytes(first, first + static_cast<std::ptrdiff_t>(size)));
   }
}

// stm32_go().
void go(Session &s) {
   REQUIRE(s.send(cmd(command::go)) == Bytes{ ack });
   CHECK(s.send(address(applicationStart)) == Bytes{ ack });
   CHECK(s.bl.action() == An3155Protocol::Action::startApplication);
}

} // namespace

TEST_CASE("stm32flash: write with a page erase, verify and go") {
   // stm32flash -w image.bin -v -g 0x08002000 -S 0x08002000
   Session s;
   for(uint8_t &byte : s.target.flash) {
      byte = 0x00;
   }
   const Bytes data = image(3000);
   connect(s);

   // stm32_erase_memory() with the extended erase: the pages that the
   // image covers, in one command.
   const uint16_t first = layout::bootloaderSize / protocol::memory::pageSize;
   REQUIRE(s.send(cmd(command::extendedErase)) == Bytes{ ack });
   CHECK(s.send(erasePages({ first, uint16_t(first + 1), uint16_t(first + 2) })) == Bytes{ ack });

   writeAndVerify(s, data);
   go(s);
   CHECK(s.target.violations == 0);
}

TEST_CASE("stm32flash: write with a mass erase") {
   // stm32flash -w image.bin -v (the full flash, from the start address).
   Session s;
   const Bytes data = image(1024 + 5);
   connect(s);
   REQUIRE(s.send(cmd(command::extendedErase)) == Bytes{ ack });
   CHECK(s.send({ 0xFF, 0xFF, 0x00 }) == Bytes{ ack });
   writeAndVerify(s, data);
   CHECK(s.target.violations == 0);
}

TEST_CASE("stm32flash: write to the bootloader area fails") {
   Session s;
   connect(s);
   REQUIRE(s.send(cmd(command::writeMemory)) == Bytes{ ack });
   CHECK(s.send(address(layout::flashBase)) == Bytes{ nack });
   CHECK(s.target.writes == 0);
}

TEST_CASE("stm32flash: read the full flash") {
   // stm32flash -r dump.bin reads in blocks of 256 bytes up to the end of
   // the flash.
   Session s;
   connect(s);
   for(uint32_t addr = layout::flashBase; addr < s.map.flashEnd(); addr += 256) {
      CAPTURE(addr);
      CHECK(readBlock(s, addr, 256).size() == 256);
   }
}

TEST_CASE("stm32flash: write unprotect, then the reconnect") {
   // stm32flash -u: the command, two ACKs, then the device resets.
   Session s;
   s.target.protectedGroups = 0xFFFF;
   connect(s);
   CHECK(s.send(cmd(command::writeUnprotect)) == Bytes{ ack, ack });
   CHECK(s.bl.action() == An3155Protocol::Action::reset);
}

TEST_CASE("stm32flash: readout unprotect gets a NACK") {
   // stm32flash -k
   Session s;
   connect(s);
   CHECK(s.send(cmd(command::readoutUnprotect)) == Bytes{ nack });
   CHECK(s.target.optionByteWrites.empty());
   CHECK(s.target.erasedPages.empty());
}

TEST_CASE("a host that stops in a write and connects again") {
   Session s;
   connect(s);
   REQUIRE(s.send(cmd(command::writeMemory)) == Bytes{ ack });
   REQUIRE(s.send(address(applicationStart)) == Bytes{ ack });
   const Bytes frame = writeData(image(256));
   s.send(Bytes(frame.begin(), frame.begin() + 100));
   s.advance(An3155Protocol::timeoutMs);
   connect(s);
   CHECK(s.target.writes == 0);
}

TEST_CASE("a USB reset during an erase stops the erase") {
   Session s;
   s.send(cmd(command::extendedErase));
   const Bytes frame = erasePages({ 10, 11, 12 });
   s.bl.receive(frame);
   s.bl.poll();
   REQUIRE(s.bl.busy());
   s.bl.reset();
   CHECK_FALSE(s.bl.busy());
   s.bl.poll();
   CHECK(s.target.erasedPages.size() == 1);
   connect(s);
}
