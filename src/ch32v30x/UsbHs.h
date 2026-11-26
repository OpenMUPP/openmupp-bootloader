// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// A minimal USBHS device driver for the DFU bootloader on the CH32V307
// (CH32FV2x_V3xRM chapter 22): EP0 only, full speed, no interrupt. poll()
// runs the device from the main loop and calls usb::DfuDevice. The driver
// of ch32v00x-system (UsbDevice.cpp) is the reference for the register
// sequences; it is too large for the 8 KB bootloader.
//
// INT_BUSY is on: the controller answers NAK to each token while
// INT_FG.TRANSFER is set, so a packet waits until poll() handles the one
// before it.

#include <wch_device.h>

#include <cstdint>
#include <cstring>

namespace ch32 {

class UsbHs {
   public:
   static constexpr uint16_t ep0Size = 64;

   // Clocks, SIE reset, full speed, EP0. Then the pull-up on D+.
   void start();
   // Pull-up off, controller reset, clocks off: the host sees a disconnect.
   void stop();

   template <typename Device>
   void poll(Device &device) {
      const uint8_t flags = USBHS->INT_FG;
      if(flags & USBHS_UIF_BUS_RST) {
         USBHS->INT_FG = USBHS_UIF_BUS_RST | USBHS_UIF_TRANSFER | USBHS_UIF_SETUP_ACT;
         resetEp0();
         device.busReset();
         return;
      }
      if(flags & USBHS_UIF_TRANSFER) {
         // INT_ST first: a SETUP that completes after this read shows in
         // INT_FG, and its OUT token must not count as data.
         const uint8_t status = USBHS->INT_ST;
         const bool setupPending = (USBHS->INT_FG & USBHS_UIF_SETUP_ACT) != 0;
         const uint8_t token = status & USBHS_UIS_TOKEN_MASK;
         if((status & USBHS_UIS_ENDP_MASK) == 0 && token == USBHS_UIS_TOKEN_IN) {
            ep0().TX_CTRL = static_cast<uint8_t>((ep0().TX_CTRL ^ USBHS_UEP_T_TOG_DATA1) & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
            USBHS->INT_FG = USBHS_UIF_TRANSFER;
            device.inComplete();
         } else if((status & USBHS_UIS_ENDP_MASK) == 0 && token == USBHS_UIS_TOKEN_OUT && !setupPending && (status & USBHS_UIS_TOG_OK)) {
            uint16_t size = USBHS->RX_LEN;
            if(size > ep0Size) {
               size = ep0Size;
            }
            uint8_t packet[ep0Size];
            std::memcpy(packet, buffer_, size);
            ep0().RX_CTRL = static_cast<uint8_t>((ep0().RX_CTRL ^ USBHS_UEP_R_TOG_DATA1) & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
            USBHS->INT_FG = USBHS_UIF_TRANSFER;
            device.out(packet, size);
         } else {
            // A repeated OUT packet (the host lost the ACK), a SOF, or a
            // token of an endpoint that does not exist
            USBHS->INT_FG = USBHS_UIF_TRANSFER;
         }
      }
      if(USBHS->INT_FG & USBHS_UIF_SETUP_ACT) {
         uint8_t setup[8];
         std::memcpy(setup, buffer_, sizeof setup);
         // The data stage and the status stage start with DATA1
         ep0().TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
         ep0().RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;
         USBHS->INT_FG = USBHS_UIF_SETUP_ACT;
         device.setup(setup);
      }
      if(flags & USBHS_UIF_SUSPEND) {
         USBHS->INT_FG = USBHS_UIF_SUSPEND;
      }
   }

   // The driver interface of usb::DfuDevice
   void send(const uint8_t *data, uint16_t size) {
      if(size > 0) {
         std::memcpy(buffer_, data, size);
      }
      ep0().T_LEN = size;
      ep0().TX_CTRL = static_cast<uint8_t>((ep0().TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK);
   }
   void receive() {
      ep0().RX_CTRL = static_cast<uint8_t>((ep0().RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK);
   }
   void stall() {
      ep0().TX_CTRL = static_cast<uint8_t>((ep0().TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL);
      ep0().RX_CTRL = static_cast<uint8_t>((ep0().RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_STALL);
   }
   void setAddress(uint8_t address) {
      USBHS->DEV_AD = address & USBHS_DEV_AD_MASK;
   }

   private:
   static USBHS_EpCtrl_TypeDef &ep0() {
      return USBHS->UEP[0];
   }
   void resetEp0();

   // The EP0 DMA buffer, for IN and OUT. The DMA needs a 4-byte address.
   alignas(4) uint8_t buffer_[ep0Size];
};

} // namespace ch32
