// SPDX-License-Identifier: GPL-3.0-or-later
// The DFU bootloader on the STM32F1 boards.
//
// hw/Boot.cpp decides after the reset: start the application, or continue
// here in bootloader mode. In bootloader mode, main():
//
// 1. Write-protects the bootloader area, if a WRP bit of it is clear
//    (plan section 3.6). This needs a reset.
// 2. Sets the clock to 72 MHz from the crystal (HSE_VALUE), and starts the
//    USB device (hw/UsbFs.h) with the DFU interface (usb/DfuDevice.h). The
//    main loop polls it; there is no USB interrupt.
// 3. Runs the flash work of the DFU protocol between two polls.
// 4. After a valid download, resets into the application with the GO
//    token.
//
// The LED is on in bootloader mode, and flickers during a download or an
// upload.

#include <cmsis_device.h>
#include <peripherals/Rcc.h>
#include <peripherals/Uid.h>
#include <startup-hooks.h>
#include <utils/Timekeeping.h>

#include "Layout.h"
#include "boot/BootDecision.h"
#include "boot/FlashRules.h"
#include "hw/Board.h"
#include "hw/Boot.h"
#include "hw/Flash.h"
#include "hw/UsbFs.h"
#include "protocol/DfuProtocol.h"
#include "protocol/MemoryMap.h"
#include "usb/DfuDescriptors.h"
#include "usb/DfuDevice.h"

namespace {

// Written before the reset of the self-protection. At the next start, the
// bootloader does not try again: a failed option byte update must not give
// a reset loop.
constexpr uint16_t selfProtectToken = 0x5E1F;

// LED: one toggle each flickerMs while there is activity. idleMs after
// the last activity, the LED is on again.
constexpr uint32_t flickerMs = 50;
constexpr uint32_t idleMs = 100;

// The DFU work of one block (STM32F103x8/xB datasheet, table 28): a 1 KB
// page erase takes 20 to 40 ms, and the 512 half-word writes of a block
// take 27 ms (52.5 us typical each). Each block starts a new page, so each
// block has the erase poll time. A shorter poll time is not an error: the
// peripheral NAKs the data stage of the next GETSTATUS until the work is
// done. The CRC of 120 KB takes about 3 ms.
constexpr uint32_t blockPollMs = 30;
constexpr uint32_t erasePollMs = 60;
constexpr uint32_t manifestPollMs = 10;

// The time that dfu-util needs to read the last GETSTATUS reply, and the
// time that the host sees the disconnect.
constexpr uint32_t drainMs = 20;
constexpr uint32_t disconnectMs = 20;

// SysTick runs at 1 kHz: the ticks of system-stm32 are milliseconds.
uint32_t milliseconds() {
   return static_cast<uint32_t>(getUptimeTicks());
}

class Memory : public protocol::MemoryReader {
   public:
   void read(uint32_t address, std::span<uint8_t> data) override {
      const volatile uint8_t *source = reinterpret_cast<const volatile uint8_t *>(address);
      for(uint8_t &byte : data) {
         byte = *source++;
      }
   }
};

// With WDG_SW = 0 in the USER option byte, the independent watchdog runs
// from the reset (about 0.4 s). The bootloader keeps the USER byte, so it
// reloads the watchdog. The reload has no effect when the watchdog is off.
void reloadWatchdog() {
   IWDG->KR = 0xAAAA;
}

// Without a pull-up switch, D+ low as an output disconnects. As an input,
// D+ belongs to the USB transceiver again.
void setPullUp(bool on) {
   if constexpr(board::usbPullUp) {
      board::usbPullUp->set(on);
   } else {
      board::usbDp.set(on);
      board::usbDp.configure(on ? board::Pin::inputFloating : board::Pin::outputPushPull2MHz);
   }
}

void wait(uint32_t ms) {
   const uint32_t start = milliseconds();
   while(milliseconds() - start < ms) {
      reloadWatchdog();
   }
}

// Resets the MCU. dr1 and dr2 go to the backup registers first: the boot
// request, or the GO token.
[[noreturn]] void restart(hw::UsbFs &usb, uint16_t dr1, uint16_t dr2) {
   usb.stop();
   setPullUp(false);
   wait(disconnectMs);
   hw::startup::writeBackup(BKP->DR1, dr1);
   hw::startup::writeBackup(BKP->DR2, dr2);
   NVIC_SystemReset();
}

// Plan section 3.6: the bootloader area is always write-protected.
void protectBootloader(hw::Flash &flash) {
   const bool triedBefore = BKP->DR3 == selfProtectToken;
   if(triedBefore) {
      hw::startup::writeBackup(BKP->DR3, 0);
   }
   const uint32_t groups = flash.writeProtectedGroups();
   if(flashrules::selfProtection(triedBefore, groups, flash.readoutProtected()) != flashrules::SelfProtection::protect ||
      !flash.setWriteProtectedGroups(groups)) {
      return;
   }
   // The new WRP bits take effect after the reset. Stay in the bootloader.
   hw::startup::writeBackup(BKP->DR3, selfProtectToken);
   hw::startup::writeBackup(BKP->DR1, boot::bootRequest);
   NVIC_SystemReset();
}

class Led {
   public:
   void activity(uint32_t now) {
      if(now - lastToggle_ >= flickerMs) {
         lit_ = !lit_;
         board::led.set(lit_);
         lastToggle_ = now;
      }
      lastActivity_ = now;
   }

   void update(uint32_t now) {
      if(!lit_ && now - lastActivity_ >= idleMs) {
         lit_ = true;
         board::led.set(true);
      }
   }

   private:
   bool lit_ = true;
   uint32_t lastToggle_ = 0;
   uint32_t lastActivity_ = 0;
};

} // namespace

// The bootloader must never hang: a fault resets the MCU.
extern "C" void HardFault_Handler() {
   NVIC_SystemReset();
}
extern "C" void MemManage_Handler() __attribute__((alias("HardFault_Handler")));
extern "C" void BusFault_Handler() __attribute__((alias("HardFault_Handler")));
extern "C" void UsageFault_Handler() __attribute__((alias("HardFault_Handler")));

void os_startup_initialize_hardware() {
   const bool clockOk = Rcc::setClock72MhzHSE();
   SystemCoreClockUpdate();
   SysTick_Config(SystemCoreClock / 1000);

   RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | board::led.clockBit() |
                   (board::usbPullUp ? board::usbPullUp->clockBit() : board::usbDp.clockBit());
   // PA15 (LED of the Mini, USB pull-up of the ST-Link/V2-1) is JTDI after
   // the reset. SWD stays on.
   AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
   board::led.set(true);
   board::led.configure(board::Pin::outputPushPull2MHz);
   setPullUp(false);
   if constexpr(board::usbPullUp) {
      board::usbPullUp->configure(board::usbPullUp->openDrain ? board::Pin::outputOpenDrain2MHz : board::Pin::outputPushPull2MHz);
   }

   // No USB without the crystal: the LED blinks and the host sees no device.
   if(!clockOk) {
      for(bool on = false;; on = !on) {
         board::led.set(on);
         wait(250);
      }
   }
}

int main() {
   const protocol::MemoryMap map{ hw::startup::flashSize() };
   hw::Flash flash{ map.flashEnd() };
   protectBootloader(flash);

   Memory memory;
   hw::startup::ApplicationImage application;
   const dfu::Config config{
      layout::applicationStart, map.flashEnd(), protocol::memory::pageSize, blockPollMs, erasePollMs, manifestPollMs
   };
   // Static: keeps the 1 KB block buffer off the 2 KB main stack
   static dfu::Protocol protocol{ config, flash, memory, application };

   uint8_t uid[hw::Uid::SizeBytes];
   hw::Uid::read(uid);
   const usb::descriptors::SerialString serial{ uid };
   hw::UsbFs usb;
   usb::DfuDevice<hw::UsbFs> device{ usb, protocol, serial.bytes };

   // The host must see the disconnect also after a reset from the
   // application, when no pull-up switch disconnected it.
   wait(disconnectMs);
   Rcc::enableUSBClock();
   usb.start();
   setPullUp(true);

   Led led;
   for(;;) {
      reloadWatchdog();
      usb.poll(device);
      protocol.runWork();
      const uint32_t now = milliseconds();
      if(protocol.state() != dfu::State::idle && protocol.state() != dfu::State::error) {
         led.activity(now);
      }
      led.update(now);
      if(protocol.resetRequested()) {
         const uint32_t start = milliseconds();
         while(milliseconds() - start < drainMs) {
            reloadWatchdog();
            usb.poll(device);
         }
         restart(usb, 0, boot::goToken);
      }
   }
}
