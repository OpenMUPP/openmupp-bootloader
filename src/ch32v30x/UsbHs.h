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

#include <ch32v30x_usb.h>

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
      const uint8_t flags = USBHSD->INT_FG;
      if(flags & USBHS_UIF_BUS_RST) {
         USBHSD->INT_FG = USBHS_UIF_BUS_RST | USBHS_UIF_TRANSFER | USBHS_UIF_SETUP_ACT;
         resetEp0();
         device.busReset();
         return;
      }
      if(flags & USBHS_UIF_TRANSFER) {
         // INT_ST first: a SETUP that completes after this read shows in
         // INT_FG, and its OUT token must not count as data.
         const uint8_t status = USBHSD->INT_ST;
         const bool setupPending = (USBHSD->INT_FG & USBHS_UIF_SETUP_ACT) != 0;
         const uint8_t token = status & USBHS_UIS_TOKEN_MASK;
         if((status & USBHS_UIS_ENDP_MASK) == 0 && token == USBHS_UIS_TOKEN_IN) {
            USBHSD->UEP0_TX_CTRL =
            static_cast<uint8_t>((USBHSD->UEP0_TX_CTRL ^ USBHS_UEP_T_TOG_DATA1) & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
            USBHSD->INT_FG = USBHS_UIF_TRANSFER;
            device.inComplete();
         } else if((status & USBHS_UIS_ENDP_MASK) == 0 && token == USBHS_UIS_TOKEN_OUT && !setupPending && (status & USBHS_UIS_TOG_OK)) {
            uint16_t size = USBHSD->RX_LEN;
            if(size > ep0Size) {
               size = ep0Size;
            }
            uint8_t packet[ep0Size];
            std::memcpy(packet, buffer_, size);
            USBHSD->UEP0_RX_CTRL =
            static_cast<uint8_t>((USBHSD->UEP0_RX_CTRL ^ USBHS_UEP_R_TOG_DATA1) & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
            USBHSD->INT_FG = USBHS_UIF_TRANSFER;
            device.out(packet, size);
         } else {
            // A repeated OUT packet (the host lost the ACK), a SOF, or a
            // token of an endpoint that does not exist
            USBHSD->INT_FG = USBHS_UIF_TRANSFER;
         }
      }
      if(USBHSD->INT_FG & USBHS_UIF_SETUP_ACT) {
         uint8_t setup[8];
         std::memcpy(setup, buffer_, sizeof setup);
         // The data stage and the status stage start with DATA1
         USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
         USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;
         USBHSD->INT_FG = USBHS_UIF_SETUP_ACT;
         device.setup(setup);
      }
      if(flags & USBHS_UIF_SUSPEND) {
         USBHSD->INT_FG = USBHS_UIF_SUSPEND;
      }
   }

   // The driver interface of usb::DfuDevice
   void send(const uint8_t *data, uint16_t size) {
      if(size > 0) {
         std::memcpy(buffer_, data, size);
      }
      USBHSD->UEP0_TX_LEN = size;
      USBHSD->UEP0_TX_CTRL = static_cast<uint8_t>((USBHSD->UEP0_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK);
   }
   void receive() {
      USBHSD->UEP0_RX_CTRL = static_cast<uint8_t>((USBHSD->UEP0_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK);
   }
   void stall() {
      USBHSD->UEP0_TX_CTRL = static_cast<uint8_t>((USBHSD->UEP0_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL);
      USBHSD->UEP0_RX_CTRL = static_cast<uint8_t>((USBHSD->UEP0_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_STALL);
   }
   void setAddress(uint8_t address) {
      USBHSD->DEV_AD = address & USBHS_MASK_USB_ADDR;
   }

   private:
   void resetEp0();

   // The EP0 DMA buffer, for IN and OUT. The DMA needs a 4-byte address.
   alignas(4) uint8_t buffer_[ep0Size];
};

} // namespace ch32
