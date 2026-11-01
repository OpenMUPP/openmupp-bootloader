// SPDX-License-Identifier: GPL-3.0-or-later

#include "protocol/An3155Protocol.h"

namespace protocol {

namespace {

constexpr uint32_t bootloaderGroups = layout::bootloaderSize / layout::wrpGroupSize;
constexpr uint32_t bootloaderGroupMask = (1u << bootloaderGroups) - 1;

// The first extended erase code with a special meaning. 0xFFFF is the
// mass erase; 0xFFFE and 0xFFFD (bank erase) and 0xFFF0-0xFFFC (reserved)
// get a NACK.
constexpr uint16_t firstSpecialErase = 0xFFF0;
constexpr uint16_t massErase = 0xFFFF;

} // namespace

An3155Protocol::An3155Protocol(const MemoryMap &map, const Hardware &hardware)
   : map_{ map }
   , hardware_{ hardware } {
}

std::size_t An3155Protocol::receive(std::span<const uint8_t> data) {
   std::size_t taken = 0;
   while(taken < data.size() && outputSize_ == 0 && state_ != State::busy && state_ != State::finished) {
      take(data[taken]);
      ++taken;
   }
   return taken;
}

void An3155Protocol::poll() {
   switch(state_) {
   case State::command:
   case State::finished:
      break;
   case State::busy:
      // The reply to the command goes out first.
      if(outputSize_ == 0) {
         runJob();
      }
      break;
   default:
      // No timeout while the reply waits: the host cannot answer yet.
      if(outputSize_ == 0 && hardware_.clock.milliseconds() - lastByteMs_ >= timeoutMs) {
         enter(State::command);
      }
      break;
   }
}

void An3155Protocol::consumeOutput(std::size_t count) {
   if(count >= outputSize_) {
      outputSize_ = 0;
   } else {
      for(std::size_t i = count; i < outputSize_; ++i) {
         output_[i - count] = output_[i];
      }
      outputSize_ -= count;
   }
   // The host answers a reply, so the timeout starts when the reply is out.
   lastByteMs_ = hardware_.clock.milliseconds();
}

void An3155Protocol::reset() {
   state_ = State::command;
   action_ = Action::none;
   outputSize_ = 0;
}

void An3155Protocol::take(uint8_t byte) {
   lastByteMs_ = hardware_.clock.milliseconds();

   switch(state_) {
   case State::command:
      takeCommand(byte);
      break;

   case State::complement:
      if(byte == (command_ ^ 0xFF)) {
         startCommand();
      } else {
         finish(false);
      }
      break;

   case State::address:
      if(count_ < sizeof field_) {
         field_[count_++] = byte;
         checksum_ ^= byte;
      } else if(byte == checksum_) {
         takeAddress();
      } else {
         finish(false);
      }
      break;

   case State::readLength:
      if(count_ == 0) {
         field_[0] = byte;
         count_ = 1;
      } else {
         takeReadLength(byte);
      }
      break;

   case State::writeLength:
      enter(State::writeData);
      expected_ = static_cast<uint16_t>(byte + 1);
      checksum_ = byte;
      break;

   case State::writeData:
      if(count_ < expected_) {
         data_[count_++] = byte;
         checksum_ ^= byte;
      } else if(byte != checksum_) {
         finish(false);
      } else {
         // Pad to a multiple of 4 with the erased value, as stm32flash.
         while(count_ % 4 != 0) {
            data_[count_++] = 0xFF;
         }
         expected_ = static_cast<uint16_t>(count_);
         if(map_.writable(address_, expected_) && !groupsProtected(address_, expected_)) {
            job_ = Job::write;
            enter(State::busy);
         } else {
            finish(false);
         }
      }
      break;

   case State::eraseCount:
      field_[count_++] = byte;
      checksum_ ^= byte;
      if(count_ == 2) {
         takeEraseCount();
      }
      break;

   case State::erasePages:
      takeErasePage(byte);
      break;

   case State::eraseSpecial:
      takeEraseChecksum(byte);
      break;

   case State::protectCount:
      enter(State::protectGroups);
      expected_ = static_cast<uint16_t>(byte + 1);
      checksum_ = byte;
      groups_ = 0;
      failed_ = false;
      break;

   case State::protectGroups:
      takeProtectGroup(byte);
      break;

   case State::busy:
   case State::finished:
      break;
   }
}

void An3155Protocol::takeCommand(uint8_t byte) {
   if(byte == synchronise) {
      put(ack);
      return;
   }
   command_ = byte;
   enter(State::complement);
}

void An3155Protocol::startCommand() {
   switch(command_) {
   case command::get:
      put(ack);
      put(static_cast<uint8_t>(sizeof supportedCommands));
      put(bootloaderVersion);
      put(supportedCommands);
      finish(true);
      break;

   case command::getVersion:
      put(ack);
      put(bootloaderVersion);
      // Two option bytes, 0x00 as in the STM32F1 system bootloader.
      put(0x00);
      put(0x00);
      finish(true);
      break;

   case command::getId:
      put(ack);
      put(0x01);
      put(static_cast<uint8_t>(productId >> 8));
      put(static_cast<uint8_t>(productId & 0xFF));
      finish(true);
      break;

   case command::readMemory:
   case command::go:
   case command::writeMemory:
      put(ack);
      enter(State::address);
      break;

   case command::extendedErase:
      put(ack);
      enter(State::eraseCount);
      break;

   case command::writeProtect:
      put(ack);
      enter(State::protectCount);
      break;

   case command::writeUnprotect:
      put(ack);
      job_ = Job::unprotect;
      enter(State::busy);
      break;

   default:
      // Readout Protect and Readout Unprotect: on the STM32F1, the
      // unprotect step erases the full flash and thus the bootloader.
      // Erase (0x43): version 3.1 has Extended Erase instead.
      finish(false);
      break;
   }
}

void An3155Protocol::takeAddress() {
   address_ = static_cast<uint32_t>(field_[0]) << 24 | static_cast<uint32_t>(field_[1]) << 16 |
              static_cast<uint32_t>(field_[2]) << 8 | field_[3];

   switch(command_) {
   case command::readMemory:
      if(!map_.readable(address_, 1)) {
         finish(false);
         return;
      }
      put(ack);
      enter(State::readLength);
      break;

   case command::go:
      if(address_ != layout::applicationStart || !hardware_.application.isValid()) {
         finish(false);
         return;
      }
      put(ack);
      action_ = Action::startApplication;
      enter(State::finished);
      break;

   default:
      // Write Memory. The flash takes whole words.
      if(address_ % 4 != 0 || !map_.writable(address_, 1)) {
         finish(false);
         return;
      }
      put(ack);
      enter(State::writeLength);
      break;
   }
}

void An3155Protocol::takeReadLength(uint8_t byte) {
   const uint32_t size = field_[0] + 1u;
   if(byte != (field_[0] ^ 0xFF) || !map_.readable(address_, size)) {
      finish(false);
      return;
   }
   put(ack);
   hardware_.memory.read(address_, std::span{ output_ + outputSize_, size });
   outputSize_ += size;
   enter(State::command);
}

void An3155Protocol::takeEraseCount() {
   // The checksum covers the two count bytes too.
   const uint8_t checksum = checksum_;
   const auto code = static_cast<uint16_t>(field_[0] << 8 | field_[1]);
   if(code >= firstSpecialErase) {
      enter(State::eraseSpecial);
      checksum_ = checksum;
      return;
   }
   enter(State::erasePages);
   checksum_ = checksum;
   expected_ = static_cast<uint16_t>(code + 1);
   failed_ = false;
   for(uint32_t &word : pages_) {
      word = 0;
   }
}

void An3155Protocol::takeErasePage(uint8_t byte) {
   if(count_ == 2u * expected_) {
      // The checksum. Check all pages before the first erase: a bad page
      // number erases nothing.
      if(byte == checksum_ && !failed_) {
         job_ = Job::erase;
         enter(State::busy);
      } else {
         finish(false);
      }
      return;
   }

   checksum_ ^= byte;
   if(count_ % 2 == 0) {
      field_[0] = byte;
   } else {
      const uint32_t page = static_cast<uint32_t>(field_[0]) << 8 | byte;
      const uint32_t address = layout::flashBase + page * memory::pageSize;
      if(page < map_.pageCount() && map_.erasable(address) && !groupsProtected(address, memory::pageSize)) {
         pages_[page / 32] |= 1u << (page % 32);
      } else {
         failed_ = true;
      }
   }
   ++count_;
}

void An3155Protocol::takeEraseChecksum(uint8_t byte) {
   const auto code = static_cast<uint16_t>(field_[0] << 8 | field_[1]);
   if(byte != checksum_ || code != massErase) {
      finish(false);
      return;
   }

   // Mass erase: the application pages that are not write-protected. A
   // protected page gives a NACK at the end.
   failed_ = false;
   for(uint32_t &word : pages_) {
      word = 0;
   }
   for(uint32_t page = layout::bootloaderSize / memory::pageSize; page < map_.pageCount(); ++page) {
      if(groupsProtected(layout::flashBase + page * memory::pageSize, memory::pageSize)) {
         failed_ = true;
      } else {
         pages_[page / 32] |= 1u << (page % 32);
      }
   }
   job_ = Job::erase;
   enter(State::busy);
}

void An3155Protocol::takeProtectGroup(uint8_t byte) {
   if(count_ < expected_) {
      checksum_ ^= byte;
      if(byte >= bootloaderGroups && byte < map_.groupCount()) {
         groups_ |= 1u << byte;
      } else {
         failed_ = true;
      }
      ++count_;
      return;
   }

   if(byte == checksum_ && !failed_) {
      job_ = Job::protect;
      enter(State::busy);
   } else {
      finish(false);
   }
}

void An3155Protocol::runJob() {
   switch(job_) {
   case Job::write:
      finish(hardware_.flash.write(address_, std::span<const uint8_t>{ data_, expected_ }));
      break;

   case Job::erase:
      runErase();
      break;

   case Job::protect:
   case Job::unprotect: {
      const uint32_t groups = job_ == Job::protect ? hardware_.optionBytes.writeProtectedGroups() | groups_ : 0;
      const bool ok = hardware_.optionBytes.setWriteProtectedGroups(groups | bootloaderGroupMask);
      // Reset also after an error: the option bytes can be half-written,
      // and the bootloader protects its own area again at the start.
      put(ok ? ack : nack);
      action_ = Action::reset;
      enter(State::finished);
      break;
   }
   }
}

void An3155Protocol::runErase() {
   // One page for each call, so the USB device gets a poll between pages.
   for(uint32_t i = 0; i < sizeof pages_ / sizeof pages_[0]; ++i) {
      if(pages_[i] == 0) {
         continue;
      }
      const auto bit = static_cast<uint32_t>(__builtin_ctz(pages_[i]));
      pages_[i] &= ~(1u << bit);
      const uint32_t page = i * 32 + bit;
      if(!hardware_.flash.erasePage(layout::flashBase + page * memory::pageSize)) {
         finish(false);
      }
      return;
   }
   finish(!failed_);
}

bool An3155Protocol::groupsProtected(uint32_t address, uint32_t size) const {
   const uint32_t first = (address - layout::flashBase) / layout::wrpGroupSize;
   const uint32_t last = (address + size - 1 - layout::flashBase) / layout::wrpGroupSize;
   const uint32_t protectedGroups = hardware_.optionBytes.writeProtectedGroups();
   for(uint32_t group = first; group <= last; ++group) {
      if(protectedGroups & (1u << group)) {
         return true;
      }
   }
   return false;
}

void An3155Protocol::enter(State state) {
   state_ = state;
   count_ = 0;
   checksum_ = 0;
}

void An3155Protocol::finish(bool success) {
   put(success ? ack : nack);
   enter(State::command);
}

void An3155Protocol::put(uint8_t byte) {
   if(outputSize_ < outputCapacity) {
      output_[outputSize_++] = byte;
   }
}

void An3155Protocol::put(std::span<const uint8_t> bytes) {
   for(uint8_t byte : bytes) {
      put(byte);
   }
}

} // namespace protocol
