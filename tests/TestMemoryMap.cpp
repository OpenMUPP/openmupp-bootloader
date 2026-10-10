#include <doctest/doctest.h>

#include "protocol/MemoryMap.h"

using layout::applicationStart;
using protocol::MemoryMap;
namespace memory = protocol::memory;

TEST_CASE("the flash size is rounded down to pages and limited to 128 KB") {
   CHECK(MemoryMap{64 * 1024}.flash().size == 64 * 1024);
   CHECK(MemoryMap{65 * 1024 + 3}.flash().size == 65 * 1024);
   CHECK(MemoryMap{0xFFFF * 1024u}.flash().size == 128 * 1024);
   CHECK(MemoryMap{64 * 1024}.pageCount() == 64);
   CHECK(MemoryMap{64 * 1024}.groupCount() == 16);
}

TEST_CASE("the readable regions are readable up to their last byte") {
   const MemoryMap map{64 * 1024};
   CHECK(map.readable(layout::flashBase, 256));
   CHECK(map.readable(map.flashEnd() - 256, 256));
   CHECK(map.readable(memory::ram.start, 256));
   CHECK(map.readable(memory::ram.start + memory::ram.size - 1, 1));
   CHECK(map.readable(memory::systemMemory.start, 256));
   CHECK(map.readable(memory::optionBytes.start, memory::optionBytes.size));
   CHECK(map.readable(memory::flashSizeRegister.start, 2));
   CHECK(map.readable(memory::uid.start, 12));
}

TEST_CASE("a read can cross from the system memory into the option bytes") {
   const MemoryMap map{64 * 1024};
   CHECK(map.readable(memory::optionBytes.start - 16, 32));
}

TEST_CASE("addresses outside the regions are not readable") {
   const MemoryMap map{64 * 1024};
   CHECK_FALSE(map.readable(map.flashEnd(), 1));
   CHECK_FALSE(map.readable(map.flashEnd() - 128, 256));
   CHECK_FALSE(map.readable(memory::ram.start + memory::ram.size, 1));
   CHECK_FALSE(map.readable(memory::ram.start + memory::ram.size - 128, 256));
   CHECK_FALSE(map.readable(memory::optionBytes.start + memory::optionBytes.size, 1));
   CHECK_FALSE(map.readable(memory::optionBytes.start, 256));
   CHECK_FALSE(map.readable(memory::systemMemory.start - 1, 1));
   CHECK_FALSE(map.readable(0x4000'0000, 1));
   CHECK_FALSE(map.readable(0xE000'E000, 4));
   CHECK_FALSE(map.readable(0x0000'0000, 4));
   CHECK_FALSE(map.readable(0xFFFF'FFFF, 2));
   CHECK_FALSE(map.readable(layout::flashBase, 0));
}

TEST_CASE("the flash size sets the end of the readable flash") {
   CHECK(MemoryMap{128 * 1024}.readable(0x0801'FF00, 256));
   CHECK_FALSE(MemoryMap{64 * 1024}.readable(0x0801'FF00, 256));
}

TEST_CASE("only the application area is writable") {
   const MemoryMap map{64 * 1024};
   CHECK(map.writable(applicationStart, 256));
   CHECK(map.writable(map.flashEnd() - 256, 256));
   CHECK_FALSE(map.writable(applicationStart - 4, 8));
   CHECK_FALSE(map.writable(layout::flashBase, 4));
   CHECK_FALSE(map.writable(map.flashEnd() - 128, 256));
   CHECK_FALSE(map.writable(memory::ram.start, 4));
   CHECK_FALSE(map.writable(memory::optionBytes.start, 4));
   CHECK_FALSE(map.writable(applicationStart, 0));
   CHECK_FALSE(map.writable(0xFFFF'FFF0, 0x20));
}

TEST_CASE("only whole application pages are erasable") {
   const MemoryMap map{64 * 1024};
   CHECK(map.erasable(applicationStart));
   CHECK(map.erasable(map.flashEnd() - memory::pageSize));
   CHECK_FALSE(map.erasable(applicationStart - memory::pageSize));
   CHECK_FALSE(map.erasable(applicationStart + 4));
   CHECK_FALSE(map.erasable(map.flashEnd()));
}
