// SPDX-License-Identifier: GPL-3.0-or-later

#include "hw/UsbFs.h"

#include <peripherals/Rcc.h>

namespace hw {

void UsbFs::resetEp0() {
   // COUNT_RX: 2 blocks of 32 bytes (BL_SIZE) = 64 bytes
   constexpr uint16_t rxCount64 = 0x8000 | (1 << 10);
   USB->BTABLE = 0;
   bdt(addrTx) = ep0Tx;
   bdt(addrRx) = ep0Rx;
   bdt(countRx) = rxCount64;
   // Control endpoint 0, DTOG bits clear, TX NAK, RX VALID. The STAT and
   // DTOG bits toggle on a 1: write the difference to the current value.
   const uint16_t reg = epr(0);
   epr(0) = static_cast<uint16_t>(((reg & (USB_EP_DTOG_TX | USB_EP_DTOG_RX | statTx | statRx)) ^ (USB_EP_TX_NAK | USB_EP_RX_VALID)) |
                                  USB_EP_CONTROL | USB_EP_CTR_RX | USB_EP_CTR_TX);
   USB->DADDR = USB_DADDR_EF;
}

void UsbFs::start() {
   // Power-up (RM0008 23.4.2): clear PDWN, wait tSTARTUP (1 us), release FRES.
   USB->CNTR = USB_CNTR_FRES;
   for(int i = 0; i < 100; ++i) {
      __NOP();
   }
   USB->CNTR = 0;
   USB->ISTR = 0;
   resetEp0();
}

void UsbFs::stop() {
   RCC->APB1RSTR |= RCC_APB1RSTR_USBRST;
   RCC->APB1RSTR &= ~RCC_APB1RSTR_USBRST;
   Rcc::disableUSBClock();
}

} // namespace hw
