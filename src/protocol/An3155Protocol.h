#pragma once

// The ST AN3155 bootloader protocol (USART protocol of the STM32 system
// bootloader), here over the USB CDC port (issue #8).
//
// The protocol is a state machine with no hardware access. It never
// blocks:
//
// - receive() takes the bytes that the host sent. It takes no bytes while
//   a reply waits in output() or a flash operation runs, so the caller
//   keeps them (the USB OUT endpoint stays NAK). Thus the reply buffer
//   never overflows.
// - poll() handles the timeouts, and runs the flash operations when the
//   reply buffer is empty. An erase does one page for each poll() call, so
//   the USB device gets a poll between two pages (usb/UsbCdc.h).
// - output() gives the reply bytes. The caller sends them and calls
//   consumeOutput().
// - action() tells the caller to reset the MCU or to start the
//   application, after it sent all reply bytes.
//
// A command that stops halfway, for more than timeoutMs between two
// bytes, is discarded with no reply. The protocol then waits for a new
// command. reset() does the same immediately, for a USB reset or a
// disconnect.
//
// Commands (version 3.1, as the system bootloader of the STM32F1):
//
// | Code   | Command            | Behaviour                                      |
// |--------|--------------------|------------------------------------------------|
// | `0x7F` | Synchronise        | ACK, when the protocol waits for a command     |
// | `0x00` | Get                | Version and command list                       |
// | `0x01` | Get Version        | Version, two option bytes `0x00`               |
// | `0x02` | Get ID             | PID `0x0410` (STM32F10xx medium density)       |
// | `0x11` | Read Memory        | Readable regions only (MemoryMap)              |
// | `0x21` | Go                 | Application start, valid application only      |
// | `0x31` | Write Memory       | Application area only, address and size        |
// |        |                    | multiples of 4 after the 0xFF padding          |
// | `0x44` | Extended Erase     | Application pages, mass erase `0xFFFF`         |
// | `0x63` | Write Protect      | 4 KB groups of the application area, then reset|
// | `0x73` | Write Unprotect    | Application area, then reset                   |
// | `0x82` | Readout Protect    | Always NACK                                    |
// | `0x92` | Readout Unprotect  | Always NACK                                    |
//
// An erase or a write in the bootloader area or in a write-protected group
// gets a NACK. Mass erase erases the unprotected application pages, and
// gets a NACK if a page is write-protected.

#include <cstddef>
#include <cstdint>
#include <span>

#include "protocol/Hardware.h"
#include "protocol/MemoryMap.h"

namespace protocol {

constexpr uint8_t ack = 0x79;
constexpr uint8_t nack = 0x1F;
constexpr uint8_t synchronise = 0x7F;

constexpr uint8_t bootloaderVersion = 0x31;
constexpr uint16_t productId = 0x0410;

namespace command {
constexpr uint8_t get = 0x00;
constexpr uint8_t getVersion = 0x01;
constexpr uint8_t getId = 0x02;
constexpr uint8_t readMemory = 0x11;
constexpr uint8_t go = 0x21;
constexpr uint8_t writeMemory = 0x31;
constexpr uint8_t erase = 0x43;
constexpr uint8_t extendedErase = 0x44;
constexpr uint8_t writeProtect = 0x63;
constexpr uint8_t writeUnprotect = 0x73;
constexpr uint8_t readoutProtect = 0x82;
constexpr uint8_t readoutUnprotect = 0x92;
} // namespace command

// The commands in the reply to Get, in this order.
constexpr uint8_t supportedCommands[] = { command::get,
                                          command::getVersion,
                                          command::getId,
                                          command::readMemory,
                                          command::go,
                                          command::writeMemory,
                                          command::extendedErase,
                                          command::writeProtect,
                                          command::writeUnprotect,
                                          command::readoutProtect,
                                          command::readoutUnprotect };

struct Hardware {
   FlashTarget &flash;
   MemoryReader &memory;
   OptionBytes &optionBytes;
   Application &application;
   const Clock &clock;
};

class An3155Protocol {
   public:
   static constexpr uint32_t timeoutMs = 1000;
   static constexpr std::size_t maxReadSize = 256;
   static constexpr std::size_t maxWriteSize = 256;
   // The longest reply: an ACK and 256 bytes of Read Memory.
   static constexpr std::size_t outputCapacity = 1 + maxReadSize;

   enum class Action : uint8_t {
      none,
      // Reset the MCU: the new option bytes take effect.
      reset,
      // Start the application (GO): set the token in BKP->DR2 and reset.
      startApplication,
   };

   An3155Protocol(const MemoryMap &map, const Hardware &hardware);

   // Takes received bytes. Returns the number of bytes taken.
   std::size_t receive(std::span<const uint8_t> data);

   // Handles the timeouts and runs the flash operations. Call it often.
   void poll();

   // The reply bytes that wait for sending.
   std::span<const uint8_t> output() const {
      return { output_, outputSize_ };
   }

   // Removes the first count bytes of output(), after they were sent.
   void consumeOutput(std::size_t count);

   // The action to do now. It is none while reply bytes wait for sending.
   // After an action other than none, the protocol takes no more bytes.
   Action action() const {
      return outputSize_ == 0 ? action_ : Action::none;
   }

   // Discards the command in progress, the reply and the action.
   void reset();

   // True while a flash or option byte operation runs in poll().
   bool busy() const {
      return state_ == State::busy;
   }

   // True when the protocol waits for a new command.
   bool waitingForCommand() const {
      return state_ == State::command;
   }

   private:
   enum class State : uint8_t {
      command,
      complement,
      address,
      readLength,
      writeLength,
      writeData,
      eraseCount,
      erasePages,
      eraseSpecial,
      protectCount,
      protectGroups,
      busy,
      finished,
   };

   enum class Job : uint8_t { write, erase, protect, unprotect };

   void take(uint8_t byte);
   void takeCommand(uint8_t byte);
   void startCommand();
   void takeAddress();
   void takeReadLength(uint8_t byte);
   void takeEraseCount();
   void takeErasePage(uint8_t byte);
   void takeEraseChecksum(uint8_t byte);
   void takeProtectGroup(uint8_t byte);
   void runJob();
   void runErase();
   bool groupsProtected(uint32_t address, uint32_t size) const;

   void enter(State state);
   void finish(bool success);
   void put(uint8_t byte);
   void put(std::span<const uint8_t> bytes);

   const MemoryMap &map_;
   Hardware hardware_;

   State state_ = State::command;
   Job job_ = Job::write;
   Action action_ = Action::none;
   uint8_t command_ = 0;
   uint8_t checksum_ = 0;
   bool failed_ = false;
   uint32_t count_ = 0;
   uint16_t expected_ = 0;
   uint32_t lastByteMs_ = 0;
   uint32_t address_ = 0;
   uint32_t groups_ = 0;
   uint8_t field_[4] = {};
   // Pages to erase: bit n is page n of the flash.
   uint32_t pages_[memory::maxFlashSize / memory::pageSize / 32] = {};
   uint8_t data_[maxWriteSize] = {};
   uint8_t output_[outputCapacity] = {};
   std::size_t outputSize_ = 0;
};

} // namespace protocol
