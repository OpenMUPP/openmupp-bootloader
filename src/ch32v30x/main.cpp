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

#include <Bkp.h>
#include <peripherals/CycleCounter.h>
#include <peripherals/GpioPin.h>
#include <peripherals/Rcc.h>
#include <peripherals/Uid.h>
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

// The DFU work of one block (CH32V307DS0 table 4-20, typical): four page
// writes of 2 ms, 8 ms plus a margin. Each fourth block also starts a 4 KB
// sector erase of 16 ms: 24 ms, plus a margin. The CRC of 248 KB takes
// about 2 ms. Not measured on the board yet: these values come from the
// typical values of the datasheet (it gives no maximum).
constexpr dfu::Config dfuConfig{ layout::applicationStart, memory::applicationEnd, memory::pageSize, 12, 30, 20 };
static_assert(dfuConfig.valid());

// The time that dfu-util needs to read the last GETSTATUS reply, and the
// time that the host sees the disconnect.
constexpr uint32_t drainMs = 20;
constexpr uint32_t disconnectMs = 20;

// The pins of OpenMUPP v1 (OpenMUPP src/board/boards/OpenMuppV1.h): the
// USB LED on PA9 (green) and the state LED on PA10 (red), active high.
constexpr uint8_t usbLedPin = 9;
constexpr uint8_t stateLedPin = 10;

const hw::GpioPin usbLed{ GPIOA, usbLedPin };
const hw::GpioPin stateLed{ GPIOA, stateLedPin };

template <typename Poll>
void wait(uint32_t ms, Poll poll) {
   // The counter wraps after 2^32 cycles (30 s at 144 MHz): compare differences
   const uint32_t cycles = ms * (SystemCoreClock / 1000);
   const uint32_t start = hw::CycleCounter::count();
   while(hw::CycleCounter::count() - start < cycles) {
      poll();
   }
}

void wait(uint32_t ms) {
   wait(ms, [] {});
}

[[noreturn]] void restart(UsbHs &usb, uint16_t dr1, uint16_t dr2) {
   usb.stop();
   wait(disconnectMs);
   hw::Bkp::writeDR1(dr1);
   hw::Bkp::writeDR2(dr2);
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
   hw::CycleCounter::start();
}

int main() {
   // Here, not in os_startup_initialize_hardware(): the GpioPin objects get
   // their values from the static constructors, which run after that hook.
   Rcc::enableGPIOAClock();
   usbLed.write(false);
   stateLed.write(true);
   usbLed.configure(hw::GpioPin::Mode::Output);
   stateLed.configure(hw::GpioPin::Mode::Output);

   // SystemInit() keeps HSI when the crystal does not start
   if(Rcc::getSystemClockSource() != Rcc::SystemClockSource::PLL) {
      for(bool on = false;; on = !on) {
         usbLed.write(on);
         stateLed.write(on);
         wait(250);
      }
   }

   uint8_t uid[hw::Uid::SizeBytes];
   hw::Uid::read(uid);
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
      usbLed.write(device.configured());
      if(protocol.resetRequested()) {
         wait(drainMs, [&] { usbHs.poll(device); });
         restart(usbHs, 0, boot::goToken);
      }
   }
}
