// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Layout.h"
#include "boot/ImageHeader.h"
#include "fakes/SoftwareCrc.h"

namespace {

constexpr uint32_t base = layout::applicationStart;
constexpr uint32_t areaSize = 56 * 1024;

uint32_t crcOf(const std::vector<uint32_t> &words) {
   fakes::SoftwareCrc crc;
   crc.reset();
   for(uint32_t word : words) {
      crc.add(word);
   }
   return crc.value();
}

// A stamped image of length bytes, as mupp-stamp-image.py makes it.
std::vector<uint32_t> stampedImage(uint32_t length = 256) {
   std::vector<uint32_t> words(length / 4);
   for(std::size_t i = 0; i < words.size(); ++i) {
      words[i] = static_cast<uint32_t>(i * 0x0101'0101u + 0x1234);
   }
   words[image::stackPointerWord] = image::ramStart + image::ramSize;
   words[image::resetVectorWord] = base + 0x31;
   words[image::magicWord] = 0;
   words[image::lengthWord] = 0;
   words[image::crcWord] = 0;
   words[image::crcWord + 1] = 0;
   const uint32_t crc = crcOf(words);
   words[image::magicWord] = image::magic;
   words[image::lengthWord] = length;
   words[image::crcWord] = crc;
   return words;
}

bool valid(const std::vector<uint32_t> &words, uint32_t maxLength = areaSize) {
   fakes::SoftwareCrc crc;
   return image::isValid(words.data(), base, maxLength, crc);
}

// Restamps the CRC after a change, so only the changed rule fails.
void restamp(std::vector<uint32_t> &words) {
   std::vector<uint32_t> copy(words.begin(), words.begin() + words[image::lengthWord] / 4);
   copy[image::magicWord] = copy[image::lengthWord] = copy[image::crcWord] = 0;
   words[image::crcWord] = crcOf(copy);
}

std::vector<uint32_t> wordsOf(const std::string &hex) {
   std::vector<uint32_t> words;
   for(std::size_t i = 0; i + 8 <= hex.size(); i += 8) {
      // Memory order: little-endian bytes.
      uint32_t word = 0;
      for(int byte = 0; byte < 4; ++byte) {
         word |= static_cast<uint32_t>(std::stoul(hex.substr(i + 2 * static_cast<std::size_t>(byte), 2), nullptr, 16))
                 << (8 * byte);
      }
      words.push_back(word);
   }
   return words;
}

} // namespace

TEST_CASE("the software CRC gives the CRC test vectors of the stamp tool") {
   std::ifstream file(MUPP_CRC_VECTORS);
   REQUIRE(file.good());
   int vectors = 0;
   std::string line;
   while(std::getline(file, line)) {
      if(line.empty() || line[0] == '#') {
         continue;
      }
      std::istringstream fields(line);
      std::string crc;
      std::string data;
      fields >> crc >> data;
      CAPTURE(line);
      CHECK(crcOf(data == "-" ? std::vector<uint32_t>{} : wordsOf(data)) == std::stoul(crc, nullptr, 16));
      ++vectors;
   }
   CHECK(vectors >= 8);
}

TEST_CASE("a stamped image is valid") {
   CHECK(valid(stampedImage()));
   CHECK(valid(stampedImage(image::minLength)));
}

TEST_CASE("the CRC covers the image length only") {
   auto words = stampedImage(256);
   words.resize(512 / 4, 0xFFFF'FFFF);
   fakes::SoftwareCrc crc;
   CHECK(image::isValid(words.data(), base, areaSize, crc));
   CHECK(crc.words == 256 / 4);
}

TEST_CASE("an erased or blank application is not valid") {
   CHECK_FALSE(valid(std::vector<uint32_t>(64, 0xFFFF'FFFF)));
   CHECK_FALSE(valid(std::vector<uint32_t>(64, 0)));
}

TEST_CASE("a wrong magic is not valid") {
   auto words = stampedImage();
   words[image::magicWord] ^= 1;
   CHECK_FALSE(valid(words));
}

TEST_CASE("a changed word gives a CRC error") {
   for(std::size_t index : { std::size_t{ 2 }, image::crcWord + 1, std::size_t{ 63 } }) {
      auto words = stampedImage();
      words[index] ^= 0x100;
      CAPTURE(index);
      CHECK_FALSE(valid(words));
   }
}

TEST_CASE("a half-written image is not valid") {
   // The vector table is written, the rest is erased.
   auto words = stampedImage(1024);
   for(std::size_t i = 64; i < words.size(); ++i) {
      words[i] = 0xFFFF'FFFF;
   }
   CHECK_FALSE(valid(words));
}

TEST_CASE("the length must be a multiple of 4 between 64 bytes and the area size") {
   auto words = stampedImage(1024);
   CHECK(valid(words, 1024));
   CHECK_FALSE(valid(words, 1020));

   for(uint32_t length : { 60u, 0u, 258u }) {
      auto changed = stampedImage(1024);
      changed[image::lengthWord] = length;
      CAPTURE(length);
      CHECK_FALSE(valid(changed));
   }
}

TEST_CASE("the initial SP must be in the RAM") {
   for(uint32_t sp : { image::ramStart, image::ramStart + 0x100, image::ramStart + image::ramSize }) {
      auto words = stampedImage();
      words[image::stackPointerWord] = sp;
      restamp(words);
      CAPTURE(sp);
      CHECK(valid(words));
   }
   for(uint32_t sp : { 0u, image::ramStart - 4, image::ramStart + image::ramSize + 4, 0xFFFF'FFFFu }) {
      auto words = stampedImage();
      words[image::stackPointerWord] = sp;
      restamp(words);
      CAPTURE(sp);
      CHECK_FALSE(valid(words));
   }
}

TEST_CASE("the reset vector must be odd and inside the image") {
   for(uint32_t reset : { base + 1, base + 255 }) {
      auto words = stampedImage(256);
      words[image::resetVectorWord] = reset;
      restamp(words);
      CAPTURE(reset);
      CHECK(valid(words));
   }
   for(uint32_t reset : { base + 0x40, base - 3, base + 257, 0xFFFF'FFFFu, 1u }) {
      auto words = stampedImage(256);
      words[image::resetVectorWord] = reset;
      restamp(words);
      CAPTURE(reset);
      CHECK_FALSE(valid(words));
   }
}

namespace {

// JAL x0 with the offset imm (even, 0 to 1 MiB - 2)
constexpr uint32_t jal(uint32_t imm) {
   return ((imm >> 20) & 0x1u) << 31 | ((imm >> 1) & 0x3FFu) << 21 | ((imm >> 11) & 0x1u) << 20 |
          ((imm >> 12) & 0xFFu) << 12 | 0x06Fu;
}

// The application start of the CH32V307 in the flash alias at 0
constexpr uint32_t riscvBase = 0x0000'2000;

// A stamped CH32V307 image: "j _start" in word 0, the vector table behind it
std::vector<uint32_t> riscvImage(uint32_t jumpTo, uint32_t length = 256, uint32_t handler = riscvBase + 0xA0) {
   std::vector<uint32_t> words = stampedImage(length);
   words[0] = jal(jumpTo);
   words[1] = 0;
   words[image::riscvHandlerWord] = handler; // NMI handler, no Thumb bit
   restamp(words);
   return words;
}

bool validRiscv(const std::vector<uint32_t> &words) {
   fakes::SoftwareCrc crc;
   return image::isValid(words.data(), riscvBase, areaSize, crc, image::Arch::riscv);
}

} // namespace

TEST_CASE("riscvJumpOffset() decodes the j of the reset handler") {
   // "j _start" of a CH32V307 image of the firmware: 0x6480006F, offset 0x648
   CHECK(image::riscvJumpOffset(0x6480'006Fu) == 0x648);
   CHECK(image::riscvJumpOffset(jal(0x1234)) == 0x1234);
   CHECK(image::riscvJumpOffset(jal(0xF'FFFE)) == 0xF'FFFE);
   CHECK(image::riscvJumpOffset(0x0000'00EFu) == -1); // JAL ra (a call)
   CHECK(image::riscvJumpOffset(0x0000'0013u) == -1); // NOP
   CHECK(image::riscvJumpOffset(0xFFDF'F06Fu) == -1); // j backwards
}

TEST_CASE("A RISC-V image needs a jump into the image behind the header") {
   CHECK(validRiscv(riscvImage(0x40)));
   CHECK(validRiscv(riscvImage(0x2C)));
   CHECK_FALSE(validRiscv(riscvImage(0x28)));  // into the header words
   CHECK_FALSE(validRiscv(riscvImage(0x100))); // behind the image (256 bytes)
   auto noJump = riscvImage(0x40);
   noJump[0] = 0x0000'0013u;
   restamp(noJump);
   CHECK_FALSE(validRiscv(noJump));
   // The Cortex-M3 rules do not apply: word 0 is no stack pointer
   CHECK_FALSE(valid(riscvImage(0x40)));
}

TEST_CASE("A RISC-V image needs a handler address inside the image") {
   CHECK(validRiscv(riscvImage(0x40, 256, riscvBase)));
   CHECK(validRiscv(riscvImage(0x40, 256, riscvBase + 0xFC)));
   // Issue #6: a standalone image, linked at 0. The jump and the CRC are
   // correct, but the handler address is below the application start.
   CHECK_FALSE(validRiscv(riscvImage(0x40, 256, 0x1A0)));
   CHECK_FALSE(validRiscv(riscvImage(0x40, 256, riscvBase - 4)));
   CHECK_FALSE(validRiscv(riscvImage(0x40, 256, riscvBase + 0x100)));       // behind the image
   CHECK_FALSE(validRiscv(riscvImage(0x40, 256, riscvBase + 0x0800'0000))); // linked at 0x08002000
}

TEST_CASE("headerIsValid() checks the header rules without the CRC") {
   auto words = riscvImage(0x40);
   CHECK(image::headerIsValid(words.data(), riscvBase, areaSize, image::Arch::riscv));
   words[image::crcWord] ^= 1;
   CHECK(image::headerIsValid(words.data(), riscvBase, areaSize, image::Arch::riscv));
   CHECK_FALSE(validRiscv(words));
   CHECK_FALSE(image::headerIsValid(riscvImage(0x40, 256, 0x1A0).data(), riscvBase, areaSize, image::Arch::riscv));
   CHECK(image::headerIsValid(stampedImage().data(), base, areaSize));
   static_assert(!image::headerIsValid(std::array<uint32_t, 16>{}.data(), base, areaSize));
}

namespace {

// The image as bytes in memory order, at offset in the vector
std::vector<uint8_t> bytesOf(const std::vector<uint32_t> &words, std::size_t offset = 0) {
   std::vector<uint8_t> bytes(offset);
   for(uint32_t word : words) {
      for(int byte = 0; byte < 4; ++byte) {
         bytes.push_back(static_cast<uint8_t>(word >> (8 * byte)));
      }
   }
   return bytes;
}

} // namespace

TEST_CASE("headerIsValid() checks the first bytes of an image, for example block 0 of a download") {
   const auto riscv = bytesOf(riscvImage(0x40, 1024));
   const std::span<const uint8_t> block{ riscv.data(), 1024 };
   CHECK(image::headerIsValid(block, riscvBase, areaSize, image::Arch::riscv));
   // The header ends at 0x2C: less is not enough
   CHECK(image::headerIsValid(block.first(0x2C), riscvBase, areaSize, image::Arch::riscv));
   CHECK_FALSE(image::headerIsValid(block.first(0x2B), riscvBase, areaSize, image::Arch::riscv));
   CHECK_FALSE(image::headerIsValid(block.first(0), riscvBase, areaSize, image::Arch::riscv));
   // The bytes need not be word-aligned
   const auto shifted = bytesOf(riscvImage(0x40, 1024), 1);
   CHECK(image::headerIsValid(std::span{ shifted }.subspan(1), riscvBase, areaSize, image::Arch::riscv));

   // The other architecture, a wrong link address, a length above the
   // area, and an image without a header are refused before the erase.
   CHECK_FALSE(image::headerIsValid(block, riscvBase, areaSize));
   const auto cortex = bytesOf(stampedImage(1024));
   CHECK(image::headerIsValid(cortex, base, areaSize));
   CHECK_FALSE(image::headerIsValid(cortex, base, areaSize, image::Arch::riscv));
   CHECK_FALSE(image::headerIsValid(bytesOf(riscvImage(0x40, 1024, 0x1A0)), riscvBase, areaSize, image::Arch::riscv));
   CHECK_FALSE(image::headerIsValid(block, riscvBase, 1020, image::Arch::riscv));
   auto standalone = riscv;
   std::fill(standalone.begin() + 0x1C, standalone.begin() + 0x2C, uint8_t{ 0 });
   CHECK_FALSE(image::headerIsValid(standalone, riscvBase, areaSize, image::Arch::riscv));
   // No CRC check: block 0 does not hold the full image
   auto badCrc = riscv;
   badCrc[0x24] ^= 1;
   CHECK(image::headerIsValid(badCrc, riscvBase, areaSize, image::Arch::riscv));
}
