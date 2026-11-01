// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// A host for the protocol in the tests: it sends bytes as the USB glue of
// #10 does (keep the bytes that receive() did not take, send the reply,
// call poll()) and collects the reply bytes.

#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include "fakes/FakeTarget.h"
#include "protocol/An3155Protocol.h"
#include "protocol/MemoryMap.h"

namespace fakes {

using Bytes = std::vector<uint8_t>;

// A command code and its complement.
inline Bytes cmd(uint8_t code) {
   return { code, static_cast<uint8_t>(code ^ 0xFF) };
}

// An address, MSB first, and its XOR checksum.
inline Bytes address(uint32_t value) {
   Bytes bytes{ static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8),
                static_cast<uint8_t>(value) };
   bytes.push_back(static_cast<uint8_t>(bytes[0] ^ bytes[1] ^ bytes[2] ^ bytes[3]));
   return bytes;
}

// The XOR of all bytes.
inline uint8_t checksum(const Bytes &bytes) {
   uint8_t sum = 0;
   for(uint8_t byte : bytes) {
      sum ^= byte;
   }
   return sum;
}

inline Bytes concat(std::initializer_list<Bytes> parts) {
   Bytes all;
   for(const Bytes &part : parts) {
      all.insert(all.end(), part.begin(), part.end());
   }
   return all;
}

// Write Memory data: N, the bytes, the checksum.
inline Bytes writeData(const Bytes &data) {
   Bytes frame{ static_cast<uint8_t>(data.size() - 1) };
   frame.insert(frame.end(), data.begin(), data.end());
   frame.push_back(checksum(frame));
   return frame;
}

// Extended Erase page list: N (2 bytes), the pages (2 bytes each), the
// checksum.
inline Bytes erasePages(const std::vector<uint16_t> &pages) {
   const auto n = static_cast<uint16_t>(pages.size() - 1);
   Bytes frame{ static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(n) };
   for(uint16_t page : pages) {
      frame.push_back(static_cast<uint8_t>(page >> 8));
      frame.push_back(static_cast<uint8_t>(page));
   }
   frame.push_back(checksum(frame));
   return frame;
}

constexpr uint8_t ack = protocol::ack;
constexpr uint8_t nack = protocol::nack;

struct Session {
   explicit Session(uint32_t flashSize = FakeTarget::defaultFlashSize)
      : target{ flashSize }
      , map{ flashSize }
      , bl{ map, target.hardware() } {
   }

   // Sends the bytes and returns the reply. Runs until the protocol took
   // all bytes and has no reply or operation in progress.
   Bytes send(const Bytes &bytes) {
      Bytes reply;
      std::size_t sent = 0;
      for(int guard = 0; guard < 100000; ++guard) {
         sent += bl.receive(std::span{ bytes }.subspan(sent));
         const auto output = bl.output();
         reply.insert(reply.end(), output.begin(), output.end());
         bl.consumeOutput(output.size());
         bl.poll();
         if(sent == bytes.size() && bl.output().empty() && !bl.busy()) {
            break;
         }
         if(bl.action() != protocol::An3155Protocol::Action::none) {
            break;
         }
      }
      return reply;
   }

   void advance(uint32_t ms) {
      target.now += ms;
      bl.poll();
   }

   FakeTarget target;
   protocol::MemoryMap map;
   protocol::An3155Protocol bl;
};

} // namespace fakes
