// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest/doctest.h>

#include "Layout.h"

using layout::applicationStart;
using layout::flashBase;
using layout::touchesBootloaderArea;

TEST_CASE("the application starts behind the bootloader area") {
   CHECK(applicationStart == flashBase + layout::bootloaderSize);
   CHECK(applicationStart % layout::wrpGroupSize == 0);
}

TEST_CASE("ranges in the application area do not touch the bootloader area") {
   CHECK_FALSE(touchesBootloaderArea(applicationStart, 1));
   CHECK_FALSE(touchesBootloaderArea(applicationStart, 1024));
   CHECK_FALSE(touchesBootloaderArea(0x0801'FFFF, 1));
}

TEST_CASE("ranges with a byte in the bootloader area touch it") {
   CHECK(touchesBootloaderArea(flashBase, 1));
   CHECK(touchesBootloaderArea(applicationStart - 1, 1));
   // Starts in the bootloader area, ends in the application area.
   CHECK(touchesBootloaderArea(applicationStart - 4, 8));
   // Starts below the flash, ends in the bootloader area.
   CHECK(touchesBootloaderArea(flashBase - 4, 8));
   // Covers the full bootloader area.
   CHECK(touchesBootloaderArea(flashBase - 4, layout::bootloaderSize + 8));
}

TEST_CASE("ranges outside the flash do not touch the bootloader area") {
   CHECK_FALSE(touchesBootloaderArea(0x2000'0000, 256));
   CHECK_FALSE(touchesBootloaderArea(flashBase - 4, 4));
   CHECK_FALSE(touchesBootloaderArea(0x1FFF'F000, 0x800));
}

TEST_CASE("an empty range touches nothing") {
   CHECK_FALSE(touchesBootloaderArea(flashBase, 0));
}

TEST_CASE("a range that wraps past the end of the address space touches it") {
   CHECK(touchesBootloaderArea(0xFFFF'FF00, 0x200));
   CHECK(touchesBootloaderArea(0xFFFF'FFFF, 2));
   CHECK_FALSE(touchesBootloaderArea(0xFFFF'FF00, 0x100));
}
