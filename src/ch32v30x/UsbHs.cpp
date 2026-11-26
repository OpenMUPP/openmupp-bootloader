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
   USBHS->UEP_CONFIG = 0;
   USBHS->UEP_TYPE = 0;
   USBHS->UEP_BUF_MOD = 0;
   USBHS->UEP0_DMA = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer_));
   USBHS->UEP_MAX_LEN[0].MAX_LEN = ep0Size;
   ep0().T_LEN = 0;
   ep0().TX_CTRL = USBHS_UEP_T_RES_NAK;
   ep0().RX_CTRL = USBHS_UEP_R_RES_ACK;
   USBHS->DEV_AD = 0;
}

void UsbHs::start() {
   // The clock sequence of the WCH USBHS examples: PHY PLL from HSE at
   // 4 MHz, 48 MHz USB clock from the PHY, PLL on, then the AHB clock.
   RCC->CFGR2 = (RCC->CFGR2 & ~(RCC_CFGR2_USBHSDIV_MASK | RCC_CFGR2_USBHSPLLSRC | RCC_CFGR2_USBHSCLK_MASK)) | usbHsDiv |
                RCC_CFGR2_USBHSCLK_4MHZ | RCC_CFGR2_USBFSSRC;
   RCC->CFGR2 |= RCC_CFGR2_USBHSPLL;
   RCC->AHBPCENR |= RCC_USBHSEN;

   // RST_SIE must be cleared by software (RM 22.2.1.1): at least 10 us.
   USBHS->CTRL = USBHS_UC_CLR_ALL | USBHS_UC_RST_SIE;
   delayCycles(2000);
   USBHS->CTRL = 0;
   USBHS->HOST_CTRL = USBHS_UH_PHY_SUSPENDM;
   USBHS->CTRL = USBHS_UC_DMA_EN | USBHS_UC_INT_BUSY | USBHS_UC_SPEED_FULL;
   USBHS->INT_EN = 0;
   USBHS->INT_FG = 0xFF;
   resetEp0();
   USBHS->CTRL |= USBHS_UC_DEV_PU_EN;
}

void UsbHs::stop() {
   USBHS->CTRL &= static_cast<uint8_t>(~USBHS_UC_DEV_PU_EN);
   USBHS->CTRL = USBHS_UC_CLR_ALL | USBHS_UC_RST_SIE;
   USBHS->HOST_CTRL = 0;
   RCC->AHBPCENR &= ~RCC_USBHSEN;
   RCC->CFGR2 &= ~RCC_CFGR2_USBHSPLL;
}

} // namespace ch32
