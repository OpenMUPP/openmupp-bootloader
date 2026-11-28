// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// A minimal driver of the STM32F1 USB device peripheral for the DFU
// bootloader (RM0008 chapter 23): EP0 only, no interrupt. poll() runs the
// device from the main loop and calls usb::DfuDevice. The register code
// comes from the CDC driver of the AN3155 bootloader.
//
// The peripheral sets the endpoint to NAK after each packet, so a packet
// waits until poll() handles the one before it. A SETUP packet is
// accepted also while the endpoint is NAK or STALL (RM0008 23.4.2).

#include <cmsis_device.h>

#include <cstdint>

namespace hw {

class UsbFs {
   public:
   static constexpr uint16_t ep0Size = 64;

   // Peripheral reset, EP0. The USB clock (48 MHz) must be on. The D+
   // pull-up is the task of the caller.
   void start();
   // Peripheral reset and USB clock off: the GPIO drives D+ again
   // (RM0008, GPIO configurations for device peripherals).
   void stop();

   template <typename Device>
   void poll(Device &device) {
      const uint16_t istr = USB->ISTR;
      if(istr & USB_ISTR_RESET) {
         USB->ISTR = static_cast<uint16_t>(~USB_ISTR_RESET);
         resetEp0();
         device.busReset();
         return;
      }
      if(istr & (USB_ISTR_SUSP | USB_ISTR_WKUP)) {
         USB->ISTR = static_cast<uint16_t>(~(USB_ISTR_SUSP | USB_ISTR_WKUP));
      }
      while(USB->ISTR & USB_ISTR_CTR) {
         const auto ep = static_cast<uint8_t>(USB->ISTR & USB_ISTR_EP_ID);
         const uint16_t reg = epr(ep);
         if(ep != 0) {
            // No other endpoint is open: clear the flags, or the loop does
            // not end.
            clearCtr(ep, static_cast<uint16_t>(reg & (USB_EP_CTR_TX | USB_EP_CTR_RX)));
            continue;
         }
         if(reg & USB_EP_CTR_TX) {
            clearCtr(0, USB_EP_CTR_TX);
            device.inComplete();
         }
         if(reg & USB_EP_CTR_RX) {
            uint16_t size = static_cast<uint16_t>(bdt(countRx) & 0x3FF);
            if(size > ep0Size) {
               size = ep0Size;
            }
            // Copy before the clear: after it, the peripheral can put a new
            // SETUP into the buffer.
            uint8_t packet[ep0Size];
            pmaRead(ep0Rx, packet, size);
            clearCtr(0, USB_EP_CTR_RX);
            if(reg & USB_EP_SETUP) {
               uint8_t setup[8] = {};
               for(uint16_t i = 0; i < sizeof setup && i < size; ++i) {
                  setup[i] = packet[i];
               }
               device.setup(setup);
            } else {
               device.out(packet, size);
            }
         }
      }
   }

   // The driver interface of usb::DfuDevice
   void send(const uint8_t *data, uint16_t size) {
      pmaWrite(ep0Tx, data, size);
      bdt(countTx) = size;
      setStat(statTx, USB_EP_TX_VALID);
   }
   void receive() {
      setStat(statRx, USB_EP_RX_VALID);
   }
   void stall() {
      setStat(statTx | statRx, USB_EP_TX_STALL | USB_EP_RX_STALL);
   }
   void setAddress(uint8_t address) {
      USB->DADDR = static_cast<uint16_t>(USB_DADDR_EF | (address & 0x7F));
   }

   private:
   // The packet memory (PMA): 512 bytes. The CPU sees each 16-bit word at
   // a 32-bit address step. The buffer descriptor table is at offset 0.
   static constexpr uint16_t ep0Tx = 0x40;
   static constexpr uint16_t ep0Rx = 0x80;
   // The BDT fields of EP0: ADDR_TX, COUNT_TX, ADDR_RX, COUNT_RX
   static constexpr uint8_t addrTx = 0, countTx = 1, addrRx = 2, countRx = 3;

   // The CTR flags are "write 0 to clear", the STAT and DTOG bits are
   // "write 1 to toggle". A write keeps these bits.
   static constexpr uint16_t epKeep = USB_EP_CTR_RX | USB_EP_SETUP | USB_EP_T_FIELD | USB_EP_KIND | USB_EP_CTR_TX | USB_EPADDR_FIELD;
   static constexpr uint16_t statTx = 0x0030;
   static constexpr uint16_t statRx = 0x3000;

   static volatile uint32_t *pma(uint16_t offset) {
      return reinterpret_cast<volatile uint32_t *>(USB_PMAADDR + 2u * offset);
   }
   static volatile uint32_t &bdt(uint8_t field) {
      return *pma(static_cast<uint16_t>(field * 2));
   }
   static volatile uint16_t &epr(uint8_t ep) {
      return (&USB->EP0R)[2 * ep];
   }

   static void pmaWrite(uint16_t offset, const uint8_t *data, uint16_t size) {
      volatile uint32_t *word = pma(offset);
      for(uint16_t i = 0; i < size; i += 2) {
         uint16_t value = data[i];
         if(i + 1 < size) {
            value = static_cast<uint16_t>(value | data[i + 1] << 8);
         }
         *word++ = value;
      }
   }

   static void pmaRead(uint16_t offset, uint8_t *data, uint16_t size) {
      const volatile uint32_t *word = pma(offset);
      for(uint16_t i = 0; i < size; i += 2) {
         const uint32_t value = *word++;
         data[i] = static_cast<uint8_t>(value);
         if(i + 1 < size) {
            data[i + 1] = static_cast<uint8_t>(value >> 8);
         }
      }
   }

   // Sets the STAT bits of EP0 in mask (statTx and/or statRx) to value.
   static void setStat(uint16_t mask, uint16_t value) {
      const uint16_t reg = epr(0);
      epr(0) = static_cast<uint16_t>(((reg & (epKeep | mask)) ^ value) | USB_EP_CTR_RX | USB_EP_CTR_TX);
   }

   static void clearCtr(uint8_t ep, uint16_t flag) {
      epr(ep) = static_cast<uint16_t>((epr(ep) & epKeep & ~flag) | ((USB_EP_CTR_RX | USB_EP_CTR_TX) & ~flag));
   }

   static void resetEp0();
};

} // namespace hw
