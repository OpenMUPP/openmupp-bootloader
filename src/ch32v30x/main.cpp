// SPDX-License-Identifier: GPL-3.0-or-later
// The DFU bootloader on the CH32V307 (OpenMUPP v1).
//
// ch32v30x/Boot.cpp decides after the reset: start the application, or
// continue here in bootloader mode. In bootloader mode, main():
//
// 1. Starts the USBHS device at full speed (ch32v30x/UsbHs.h) with the DFU
//    interface (usb/DfuDevice.h). The main loop polls it; there is no
//    interrupt.
// 2. Runs the flash work of the DFU protocol between two polls.
// 3. After a valid download, resets into the application with the GO
//    token.
//
// The red LED (state) is on in bootloader mode. The green LED (USB) is on
// while the host has configured the device. Both blink when the crystal
// does not start: USBHS needs it.
//
// The write protection of the bootloader area is not in this version.

#include <startup-hooks.h>
#include <system_ch32v30x.h>
#include <wch_device.h>

#include "Layout.h"
#include "boot/BootDecision.h"
#include "ch32v30x/Boot.h"
#include "ch32v30x/Flash.h"
#include "ch32v30x/Memory.h"
#include "ch32v30x/UsbHs.h"
#include "protocol/DfuProtocol.h"
#include "usb/DfuDescriptors.h"
#include "usb/DfuDevice.h"

namespace {

using namespace ch32;

// The DFU work of one block: four fast erases and four fast page writes
// of 256 bytes, each less than 5 ms. The CRC of 248 KB takes about 2 ms.
constexpr dfu::Config dfuConfig{ layout::applicationStart, memory::applicationEnd, memory::pageSize, 40, 20 };
static_assert(dfuConfig.valid());

// The time that dfu-util needs to read the last GETSTATUS reply, and the
// time that the host sees the disconnect.
constexpr uint32_t drainMs = 20;
constexpr uint32_t disconnectMs = 20;

// The pins of OpenMUPP v1 (OpenMUPP src/board/boards/OpenMuppV1.h): the
// USB LED on PA9 (green) and the state LED on PA10 (red), active high.
constexpr uint8_t usbLedPin = 9;
constexpr uint8_t stateLedPin = 10;

void configureLed(uint8_t pin) {
   // CFGHR: pins 8 to 15. MODE 10 (2 MHz), CNF 00 (push-pull)
   const uint32_t shift = 4u * (pin - 8u);
   GPIOA->CFGHR = (GPIOA->CFGHR & ~(0xFu << shift)) | (0x2u << shift);
}

void setLed(uint8_t pin, bool on) {
   GPIOA->BSHR = on ? 1u << pin : 1u << (pin + 16);
}

// The SysTick counts up at HCLK from start-up on (CH32FV2x_V3xRM 9.5.5).
// CNT wraps after 2^32 / HCLK (30 s at 144 MHz), so the code compares
// differences of cycles.
template <typename Poll>
void wait(uint32_t ms, Poll poll) {
   const uint32_t cycles = ms * (SystemCoreClock / 1000);
   const uint32_t start = SysTick->CNT;
   while(SysTick->CNT - start < cycles) {
      poll();
   }
}

void wait(uint32_t ms) {
   wait(ms, [] {});
}

[[noreturn]] void restart(UsbHs &usb, uint16_t dr1, uint16_t dr2) {
   usb.stop();
   wait(disconnectMs);
   writeBackup(bkpDatar1(), dr1);
   writeBackup(bkpDatar2(), dr2);
   NVIC_SystemReset();
   __builtin_unreachable();
}

UsbHs usbHs;
Flash flash;
ApplicationImage application;

} // namespace

// The bootloader must never hang: a fault resets the MCU.
extern "C" void HardFault_Handler() {
   NVIC_SystemReset();
}

void os_startup_initialize_hardware() {
   SystemCoreClockUpdate();
   SysTick->CTLR = 0;
   SysTick->CNT = 0;
   SysTick->CTLR = (1u << 2) | (1u << 0); // STCLK: HCLK, STE: on, count up

   RCC->APB2PCENR |= RCC_IOPAEN;
   setLed(usbLedPin, false);
   setLed(stateLedPin, true);
   configureLed(usbLedPin);
   configureLed(stateLedPin);

   // SystemInit() keeps HSI when the crystal does not start
   if((RCC->CFGR0 & RCC_CFGR0_SWS_MASK) != RCC_CFGR0_SWS_PLL) {
      for(bool on = false;; on = !on) {
         setLed(usbLedPin, on);
         setLed(stateLedPin, on);
         wait(250);
      }
   }
}

int main() {
   uint8_t uid[12];
   for(uint32_t i = 0; i < sizeof uid; ++i) {
      uid[i] = *reinterpret_cast<const volatile uint8_t *>(0x1FFF'F7E8 + i); // UID, RM 31.2
   }
   const usb::descriptors::SerialString serial{ uid };
   dfu::Protocol protocol{ dfuConfig, flash, flash, application };
   usb::DfuDevice<UsbHs> device{ usbHs, protocol, serial.bytes };

   // The host must see the disconnect also after a reset from the
   // application.
   wait(disconnectMs);
   usbHs.start();

   for(;;) {
      usbHs.poll(device);
      protocol.runWork();
      setLed(usbLedPin, device.configured());
      if(protocol.resetRequested()) {
         wait(drainMs, [&] { usbHs.poll(device); });
         restart(usbHs, 0, boot::goToken);
      }
   }
}
