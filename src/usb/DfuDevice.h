// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The control endpoint (EP0) of the DFU bootloader: the control transfer
// stages, the standard requests, the DFU class requests and the MS OS 2.0
// vendor request. The device has no other endpoint.
//
// The USB driver of the MCU (Hw) moves the packets. It calls setup(),
// out() and inComplete() from its poll loop, and busReset() after a bus
// reset. Hw has these functions:
//
//   void send(const uint8_t *data, uint16_t size); // one IN packet, size <= ep0Size
//   void receive();                                // accept the next OUT packet
//   void stall();                                  // STALL both directions until the next SETUP
//   void setAddress(uint8_t address);              // the address after SET_ADDRESS
//
// No hardware access: the host tests run it with a fake Hw.

#include <cstdint>
#include <cstring>

#include "protocol/DfuProtocol.h"
#include "usb/DfuDescriptors.h"

namespace usb {

template <typename Hw>
class DfuDevice {
   public:
   DfuDevice(Hw &hw, dfu::Protocol &dfu, const uint8_t *serialString)
      : hw_{ hw }
      , dfu_{ dfu }
      , serial_{ serialString } {
   }

   void busReset() {
      stage_ = Stage::idle;
      configuration_ = 0;
      pendingAddress_ = 0;
      dfu_.usbReset();
      hw_.receive();
   }

   bool configured() const {
      return configuration_ != 0;
   }

   void setup(const uint8_t (&packet)[8]) {
      const Setup s{ packet[0], packet[1], static_cast<uint16_t>(packet[2] | packet[3] << 8),
                     static_cast<uint16_t>(packet[4] | packet[5] << 8), static_cast<uint16_t>(packet[6] | packet[7] << 8) };
      stage_ = Stage::idle;
      pendingAddress_ = 0;
      const uint8_t type = s.bmRequestType & 0x60;
      bool ok = false;
      if(type == 0x00) {
         ok = standardRequest(s);
      } else if(type == 0x20) {
         ok = classRequest(s);
      } else if(type == 0x40) {
         ok = vendorRequest(s);
      }
      if(!ok) {
         stage_ = Stage::idle;
         hw_.stall();
         return;
      }
      hw_.receive();
   }

   void out(const uint8_t *data, uint16_t size) {
      if(stage_ == Stage::dataOut) {
         const uint16_t n = size < outLeft_ ? size : outLeft_;
         std::memcpy(outData_, data, n);
         outData_ += n;
         outLeft_ = static_cast<uint16_t>(outLeft_ - n);
         // A short packet ends the data stage early: stall, the host sent
         // less than wLength.
         if(outLeft_ > 0 && size < descriptors::ep0Size) {
            dfu_.stall();
            stage_ = Stage::idle;
            hw_.stall();
            return;
         }
         if(outLeft_ == 0) {
            dfu_.endDownload();
            sendStatus();
            return;
         }
         hw_.receive();
         return;
      }
      // The status stage of a request with IN data. The host can also end
      // the data stage early with it.
      if((stage_ == Stage::dataIn || stage_ == Stage::statusOut) && size == 0) {
         stage_ = Stage::idle;
         dfu_.requestDone();
      }
      hw_.receive();
   }

   void inComplete() {
      if(stage_ == Stage::dataIn) {
         if(inLeft_ > 0 || inZlp_) {
            sendNext();
         } else {
            stage_ = Stage::statusOut;
         }
      } else if(stage_ == Stage::statusIn) {
         if(pendingAddress_) {
            hw_.setAddress(pendingAddress_);
            pendingAddress_ = 0;
         }
         stage_ = Stage::idle;
         dfu_.requestDone();
      }
   }

   private:
   struct Setup {
      uint8_t bmRequestType;
      uint8_t bRequest;
      uint16_t wValue;
      uint16_t wIndex;
      uint16_t wLength;
   };

   enum class Stage : uint8_t { idle, dataIn, statusOut, dataOut, statusIn };

   void reply(const uint8_t *data, uint16_t size, uint16_t wLength) {
      inData_ = data;
      inLeft_ = size < wLength ? size : wLength;
      // A reply shorter than wLength that is a multiple of the packet size
      // ends with a zero-length packet.
      inZlp_ = inLeft_ < wLength && inLeft_ % descriptors::ep0Size == 0;
      stage_ = Stage::dataIn;
      sendNext();
   }

   void sendNext() {
      const uint16_t n = inLeft_ < descriptors::ep0Size ? inLeft_ : descriptors::ep0Size;
      hw_.send(inData_, n);
      inData_ += n;
      inLeft_ = static_cast<uint16_t>(inLeft_ - n);
      if(n < descriptors::ep0Size) {
         inZlp_ = false;
      }
   }

   void sendStatus() {
      stage_ = Stage::statusIn;
      hw_.send(nullptr, 0);
   }

   const uint8_t *string(uint8_t index) const {
      switch(index) {
      case 0:
         return descriptors::language;
      case 1:
         return descriptors::manufacturer;
      case 2:
         return descriptors::product;
      case 3:
         return serial_;
      case 4:
         return descriptors::interface;
      default:
         return nullptr;
      }
   }

   bool standardRequest(const Setup &s) {
      const uint8_t recipient = s.bmRequestType & 0x1F;
      switch(s.bRequest) {
      case 0x06: { // GET_DESCRIPTOR
         const auto type = static_cast<uint8_t>(s.wValue >> 8);
         const auto index = static_cast<uint8_t>(s.wValue);
         if(type == 1) {
            reply(descriptors::device, sizeof descriptors::device, s.wLength);
         } else if(type == 2 && index == 0) {
            reply(descriptors::configuration, sizeof descriptors::configuration, s.wLength);
         } else if(type == 15) {
            reply(descriptors::bos, sizeof descriptors::bos, s.wLength);
         } else if(const uint8_t *d = type == 3 ? string(index) : nullptr) {
            reply(d, d[0], s.wLength);
         } else {
            return false;
         }
         return true;
      }
      case 0x05: // SET_ADDRESS: the address is valid after the status stage
         pendingAddress_ = s.wValue & 0x7F;
         sendStatus();
         return true;
      case 0x09: // SET_CONFIGURATION
         if(s.wValue > 1) {
            return false;
         }
         configuration_ = static_cast<uint8_t>(s.wValue);
         sendStatus();
         return true;
      case 0x08: // GET_CONFIGURATION
         small_[0] = configuration_;
         reply(small_, 1, s.wLength);
         return true;
      case 0x00: // GET_STATUS: device, interface 0, EP0
         if((recipient == 1 && (!configuration_ || s.wIndex != 0)) || (recipient == 2 && (s.wIndex & 0x7F) != 0) || recipient > 2) {
            return false;
         }
         small_[0] = 0;
         small_[1] = 0;
         reply(small_, 2, s.wLength);
         return true;
      case 0x01: // CLEAR_FEATURE
      case 0x03: // SET_FEATURE
         // ENDPOINT_HALT of EP0: a STALL of EP0 ends with the next SETUP, so
         // there is nothing to do. Remote wakeup and test mode: not supported.
         if(recipient == 2 && s.wValue == 0 && (s.wIndex & 0x7F) == 0) {
            sendStatus();
            return true;
         }
         return false;
      case 0x0A: // GET_INTERFACE
         if(!configuration_ || s.wIndex != 0) {
            return false;
         }
         small_[0] = 0;
         reply(small_, 1, s.wLength);
         return true;
      case 0x0B: // SET_INTERFACE: only alternate setting 0
         if(!configuration_ || s.wIndex != 0 || s.wValue != 0) {
            return false;
         }
         sendStatus();
         return true;
      default:
         return false;
      }
   }

   bool classRequest(const Setup &s) {
      // DFU requests go to interface 0
      if((s.bmRequestType & 0x1F) != 1 || s.wIndex != 0 || !configuration_) {
         return false;
      }
      const bool in = s.bmRequestType & 0x80;
      switch(s.bRequest) {
      case dfu::request::dnload:
         if(in || !dfu_.beginDownload(s.wValue, s.wLength)) {
            return false;
         }
         if(s.wLength == 0) {
            sendStatus();
         } else {
            outData_ = dfu_.buffer().data();
            outLeft_ = s.wLength;
            stage_ = Stage::dataOut;
         }
         return true;
      case dfu::request::upload: {
         const int n = in ? dfu_.upload(s.wValue, s.wLength, dfu_.buffer()) : -1;
         if(n < 0) {
            return false;
         }
         reply(dfu_.buffer().data(), static_cast<uint16_t>(n), s.wLength);
         return true;
      }
      case dfu::request::getStatus:
         if(!in || s.wLength < sizeof status_) {
            break;
         }
         dfu_.getStatus(status_);
         reply(status_, sizeof status_, s.wLength);
         return true;
      case dfu::request::getState:
         if(!in || s.wLength < 1) {
            break;
         }
         small_[0] = static_cast<uint8_t>(dfu_.state());
         reply(small_, 1, s.wLength);
         return true;
      case dfu::request::clrStatus:
         if(in || !dfu_.clearStatus()) {
            return false;
         }
         sendStatus();
         return true;
      case dfu::request::abort:
         if(in || !dfu_.abort()) {
            return false;
         }
         sendStatus();
         return true;
      default:
         break;
      }
      // DFU_DETACH and unknown requests
      dfu_.stall();
      return false;
   }

   bool vendorRequest(const Setup &s) {
      if(s.bmRequestType != 0xC0 || s.bRequest != descriptors::msOs20VendorCode || s.wIndex != descriptors::msOs20DescriptorIndex) {
         return false;
      }
      reply(descriptors::msOs20Set, sizeof descriptors::msOs20Set, s.wLength);
      return true;
   }

   Hw &hw_;
   dfu::Protocol &dfu_;
   const uint8_t *serial_;
   Stage stage_ = Stage::idle;
   uint8_t configuration_ = 0;
   uint8_t pendingAddress_ = 0;
   const uint8_t *inData_ = nullptr;
   uint16_t inLeft_ = 0;
   bool inZlp_ = false;
   uint8_t *outData_ = nullptr;
   uint16_t outLeft_ = 0;
   uint8_t small_[2] = {};
   uint8_t status_[6] = {};
};

} // namespace usb
