// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest/doctest.h>

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
