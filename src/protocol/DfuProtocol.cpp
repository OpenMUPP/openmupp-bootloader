// SPDX-License-Identifier: GPL-3.0-or-later

#include "DfuProtocol.h"

namespace dfu {

Protocol::Protocol(const Config &config, protocol::FlashTarget &flash, protocol::MemoryReader &memory, protocol::Application &application)
   : config_{ config }
   , flash_{ flash }
   , memory_{ memory }
   , application_{ application } {
}

void Protocol::fail(Status status) {
   state_ = State::error;
   status_ = status;
   workPending_ = workReady_ = false;
}

void Protocol::stall() {
   fail(Status::errStalledPkt);
}

bool Protocol::beginDownload(uint16_t block, uint16_t length) {
   if(state_ == State::idle) {
      // A new download starts at block 0. A zero-length download in
      // dfuIDLE has nothing to end.
      if(length == 0 || block != 0) {
         stall();
         return false;
      }
      nextBlock_ = 0;
      erasedEnd_ = config_.start;
   } else if(state_ != State::dnloadIdle) {
      stall();
      return false;
   }
   if(length == 0) {
      state_ = State::manifestSync;
      return true;
   }
   const uint32_t address = config_.start + uint32_t{ block } * transferSize;
   if(block != nextBlock_ || length > transferSize || address >= config_.end || length > config_.end - address) {
      fail(block != nextBlock_ || length > transferSize ? Status::errStalledPkt : Status::errAddress);
      return false;
   }
   blockSize_ = length;
   return true;
}

void Protocol::endDownload() {
   state_ = State::dnloadSync;
}

int Protocol::upload(uint16_t block, uint16_t length, std::span<uint8_t> out) {
   if((state_ != State::idle && state_ != State::uploadIdle) || length > out.size() || length > transferSize) {
      stall();
      return -1;
   }
   const uint32_t offset = uint32_t{ block } * transferSize;
   const uint32_t available = config_.end - config_.start;
   const uint32_t size = offset >= available ? 0 : (available - offset < length ? available - offset : length);
   if(size > 0) {
      memory_.read(config_.start + offset, out.first(size));
   }
   state_ = size < length ? State::idle : State::uploadIdle;
   return static_cast<int>(size);
}

void Protocol::getStatus(uint8_t (&reply)[6]) {
   uint32_t pollMs = 0;
   if(state_ == State::dnloadSync) {
      state_ = State::dnBusy;
      pollMs = config_.blockPollMs;
      workPending_ = true;
   } else if(state_ == State::manifestSync) {
      state_ = State::manifest;
      pollMs = config_.manifestPollMs;
      workPending_ = true;
   } else if(state_ == State::dnBusy) {
      pollMs = config_.blockPollMs;
   } else if(state_ == State::manifest) {
      pollMs = config_.manifestPollMs;
   }
   reply[0] = static_cast<uint8_t>(status_);
   reply[1] = static_cast<uint8_t>(pollMs);
   reply[2] = static_cast<uint8_t>(pollMs >> 8);
   reply[3] = static_cast<uint8_t>(pollMs >> 16);
   reply[4] = static_cast<uint8_t>(state_);
   reply[5] = 0;
}

bool Protocol::clearStatus() {
   if(state_ != State::error) {
      stall();
      return false;
   }
   state_ = State::idle;
   status_ = Status::ok;
   return true;
}

bool Protocol::abort() {
   switch(state_) {
   case State::idle:
   case State::dnloadSync:
   case State::dnloadIdle:
   case State::manifestSync:
   case State::uploadIdle:
      state_ = State::idle;
      return true;
   default:
      stall();
      return false;
   }
}

void Protocol::usbReset() {
   if(state_ != State::manifestWaitReset) {
      state_ = State::idle;
      status_ = Status::ok;
   }
   workPending_ = workReady_ = false;
}

void Protocol::runWork() {
   if(!workReady_) {
      return;
   }
   workPending_ = workReady_ = false;
   if(state_ == State::dnBusy) {
      if(write()) {
         ++nextBlock_;
         state_ = State::dnloadIdle;
      }
   } else if(state_ == State::manifest) {
      if(manifest()) {
         state_ = State::manifestWaitReset;
      }
   }
}

bool Protocol::write() {
   const uint32_t address = config_.start + uint32_t{ nextBlock_ } * transferSize;
   // The flash writes words: fill the last word of a short block with 0xFF,
   // the value of erased flash.
   uint32_t size = blockSize_;
   while(size % 4 != 0) {
      buffer_[size++] = 0xFF;
   }
   const uint32_t end = address + size;
   // The blocks come in order, so the pages below erasedEnd_ are erased in
   // this download. A block can end inside a page; the next block then
   // starts in an erased page.
   for(uint32_t page = erasedEnd_; page < end; page += config_.pageSize) {
      if(!flash_.erasePage(page)) {
         fail(Status::errErase);
         return false;
      }
      erasedEnd_ = page + config_.pageSize;
   }
   if(!flash_.write(address, { buffer_, size })) {
      fail(Status::errWrite);
      return false;
   }
   return true;
}

bool Protocol::manifest() {
   if(!application_.isValid()) {
      fail(Status::errVerify);
      return false;
   }
   return true;
}

} // namespace dfu
