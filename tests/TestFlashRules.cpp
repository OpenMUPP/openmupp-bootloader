// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest/doctest.h>

#include "Layout.h"
#include "boot/FlashRules.h"

using flashrules::SelfProtection;
using flashrules::selfProtection;
using flashrules::writable;

namespace {

constexpr uint32_t flashEnd = layout::flashBase + 64 * 1024;
constexpr uint32_t app = layout::applicationStart;
constexpr uint32_t all = optionbytes::bootloaderGroups;

constexpr uint32_t groupOf(uint32_t address) {
   return (address - layout::flashBase) / layout::wrpGroupSize;
}

} // namespace

TEST_CASE("the application area up to the end of the flash is writable") {
   CHECK(writable(app, 1024, flashEnd, all));
   CHECK(writable(flashEnd - 4, 4, flashEnd, all));
   CHECK(writable(app, flashEnd - app, flashEnd, all));
}

TEST_CASE("the bootloader area and ranges outside the flash are not writable") {
   CHECK_FALSE(writable(app - 4, 8, flashEnd, 0));
   CHECK_FALSE(writable(layout::flashBase, 4, flashEnd, 0));
   CHECK_FALSE(writable(flashEnd, 4, flashEnd, all));
   CHECK_FALSE(writable(flashEnd - 4, 8, flashEnd, all));
   CHECK_FALSE(writable(app, 0, flashEnd, all));
   // size > flashEnd - address, also when address + size wraps.
   CHECK_FALSE(writable(app, 0xFFFF'FFFF, flashEnd, all));
   CHECK_FALSE(writable(0x2000'0000, 4, flashEnd, all));
}

TEST_CASE("a range with a byte in a protected group is not writable") {
   const uint32_t group = groupOf(app + 0x1000);
   CHECK_FALSE(writable(app + 0x1000, 4, flashEnd, all | 1u << group));
   // Starts in the group before, ends in the protected group.
   CHECK_FALSE(writable(app + 0x0FFC, 8, flashEnd, all | 1u << group));
   CHECK(writable(app + 0x0FFC, 4, flashEnd, all | 1u << group));
   CHECK(writable(app + 0x2000, 4, flashEnd, all | 1u << group));
}

TEST_CASE("a protected bootloader area needs no self-protection") {
   CHECK(selfProtection(false, all, false) == SelfProtection::none);
   CHECK(selfProtection(true, all | 0x10, true) == SelfProtection::none);
}

TEST_CASE("an unprotected bootloader area gets the self-protection") {
   CHECK(selfProtection(false, 0, false) == SelfProtection::protect);
   // One group of the area is not enough.
   CHECK(selfProtection(false, 1, false) == SelfProtection::protect);
}

TEST_CASE("the self-protection runs once: no second reset") {
   CHECK(selfProtection(true, 0, false) == SelfProtection::triedBefore);
}

TEST_CASE("no option byte update with readout protection") {
   CHECK(selfProtection(false, 0, true) == SelfProtection::readoutProtected);
}
