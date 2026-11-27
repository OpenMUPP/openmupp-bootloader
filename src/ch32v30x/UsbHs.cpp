// SPDX-License-Identifier: GPL-3.0-or-later

#include "ch32v30x/UsbHs.h"

namespace ch32 {

namespace {

// The USB PHY PLL takes a 4 MHz reference: HSE / USBHSDIV (RM 3.4.12).
constexpr uint32_t phyReferenceHz = 4'000'000;
static_assert(HSE_VALUE % phyReferenceHz == 0 && HSE_VALUE / phyReferenceHz >= 1 && HSE_VALUE / phyReferenceHz <= 8);
constexpr uint32_t usbHsDiv = (HSE_VALUE / phyReferenceHz - 1) << 24;

void delayCycles(uint32_t cycles) {
   for(volatile uint32_t i = 0; i < cycles; ++i) {
   }
}

} // namespace

void UsbHs::resetEp0() {
   USBHSD->ENDP_CONFIG = 0;
   USBHSD->ENDP_TYPE = 0;
   USBHSD->BUF_MODE = 0;
   USBHSD->UEP0_DMA = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer_));
   USBHSD->UEP0_MAX_LEN = ep0Size;
   USBHSD->UEP0_TX_LEN = 0;
   USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
   USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK;
   USBHSD->DEV_AD = 0;
}

void UsbHs::start() {
   // The clock sequence of the WCH USBHS examples: PHY PLL from HSE at
   // 4 MHz, 48 MHz USB clock from the PHY, PLL on, then the AHB clock.
   RCC->CFGR2 = (RCC->CFGR2 & ~(RCC_USBHSDIV | RCC_USBHSPLLSRC | RCC_USBHSCLK)) | usbHsDiv | USBHS_PLL_CKREF_4M | RCC_USBFSSRC;
   RCC->CFGR2 |= RCC_USBHSPLL;
   RCC->AHBPCENR |= RCC_USBHSEN;

   // RST_SIE must be cleared by software (RM 22.2.1.1): at least 10 us.
   USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
   delayCycles(2000);
   USBHSD->CONTROL = 0;
   USBHSD->HOST_CTRL = USBHS_UH_PHY_SUSPENDM;
   USBHSD->CONTROL = USBHS_UC_DMA_EN | USBHS_UC_INT_BUSY | USBHS_UC_SPEED_FULL;
   USBHSD->INT_EN = 0;
   USBHSD->INT_FG = 0xFF;
   resetEp0();
   USBHSD->CONTROL |= USBHS_UC_DEV_PU_EN;
}

void UsbHs::stop() {
   USBHSD->CONTROL &= static_cast<uint8_t>(~USBHS_UC_DEV_PU_EN);
   USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
   USBHSD->HOST_CTRL = 0;
   RCC->AHBPCENR &= ~RCC_USBHSEN;
   RCC->CFGR2 &= ~RCC_USBHSPLL;
}

} // namespace ch32
