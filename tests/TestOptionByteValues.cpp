// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest/doctest.h>

#include "boot/OptionByteValues.h"

using optionbytes::program;

TEST_CASE("RDP is always the value for no readout protection") {
   CHECK(program(0xFF, 0xFF, 0xFF, 0)[optionbytes::rdpIndex] == 0xA5);
   CHECK(program(0, 0, 0, 0xFFFF'FFFF)[optionbytes::rdpIndex] == 0xA5);
}

TEST_CASE("the USER byte and the data bytes keep their values") {
   const auto values = program(0xF8, 0x12, 0x34, 0);
   CHECK(values[optionbytes::userIndex] == 0xF8);
   CHECK(values[optionbytes::data0Index] == 0x12);
   CHECK(values[optionbytes::data1Index] == 0x34);
}

TEST_CASE("the bootloader groups are always protected") {
   const auto values = program(0xFF, 0xFF, 0xFF, 0);
   // A clear WRP bit is a protected group. 8 KB area: groups 0 and 1.
   CHECK(optionbytes::bootloaderGroups == ((1u << (layout::bootloaderSize / layout::wrpGroupSize)) - 1));
   CHECK(values[optionbytes::wrp0Index] == static_cast<uint8_t>(~optionbytes::bootloaderGroups));
   CHECK(values[optionbytes::wrp0Index + 1] == 0xFF);
   CHECK(values[optionbytes::wrp0Index + 2] == 0xFF);
   CHECK(values[optionbytes::wrp0Index + 3] == 0xFF);
}

TEST_CASE("each group maps to its WRP bit") {
   for(uint32_t group = 0; group < 32; ++group) {
      const auto values = program(0xFF, 0xFF, 0xFF, 1u << group);
      uint32_t wrp = 0;
      for(std::size_t i = 0; i < 4; ++i) {
         wrp |= static_cast<uint32_t>(values[optionbytes::wrp0Index + i]) << (8 * i);
      }
      CAPTURE(group);
      CHECK(~wrp == ((1u << group) | optionbytes::bootloaderGroups));
   }
}

TEST_CASE("a stored option byte has the complement in the high byte") {
   CHECK(optionbytes::stored(0xA5) == 0x5AA5);
   CHECK(optionbytes::stored(0xFF) == 0x00FF);
   CHECK(optionbytes::stored(0x00) == 0xFF00);
}

TEST_CASE("WRP bytes of 0xFF stay erased, other bytes are programmed") {
   const auto values = program(0xFF, 0xFF, 0xFF, 0);
   CHECK_FALSE(optionbytes::skip(optionbytes::rdpIndex, values[optionbytes::rdpIndex]));
   // USER and data bytes are always programmed, also 0xFF.
   CHECK_FALSE(optionbytes::skip(optionbytes::userIndex, 0xFF));
   CHECK_FALSE(optionbytes::skip(optionbytes::data0Index, 0xFF));
   CHECK_FALSE(optionbytes::skip(optionbytes::data1Index, 0xFF));
   // WRP0 has the bootloader groups: never 0xFF.
   CHECK_FALSE(optionbytes::skip(optionbytes::wrp0Index, values[optionbytes::wrp0Index]));
   CHECK(optionbytes::skip(optionbytes::wrp0Index + 1, values[optionbytes::wrp0Index + 1]));
   CHECK_FALSE(optionbytes::skip(optionbytes::wrp0Index + 1, 0xFE));
}

TEST_CASE("the expected read-back of a skipped byte is the erased value") {
   CHECK(optionbytes::expected(optionbytes::wrp0Index + 3, 0xFF) == optionbytes::erased);
   CHECK(optionbytes::expected(optionbytes::wrp0Index + 3, 0x7F) == 0x807F);
   CHECK(optionbytes::expected(optionbytes::userIndex, 0xFF) == 0x00FF);
}
