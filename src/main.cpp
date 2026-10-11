// Entry point of the bootloader (issue #4).
//
// hw/Boot.cpp decides after the reset: start the application, or continue
// here in bootloader mode. In bootloader mode, main():
//
// 1. Write-protects the bootloader area, if a WRP bit of it is clear
//    (plan section 3.6). This needs a reset.
// 2. Sets the clock to 72 MHz from the 12 MHz crystal, and starts the USB
//    CDC device (usb/UsbCdc.h).
// 3. Runs the AN3155 protocol (protocol/An3155Protocol.h) on the CDC port.
//    The main loop polls the USB device; there is no USB interrupt.
//
// The red LED is on in bootloader mode, and flickers on USB activity.

#include <cmsis_device.h>
#include <peripherals/Rcc.h>
#include <peripherals/Uid.h>
#include <startup-hooks.h>
#include <utils/Timekeeping.h>

#include "boot/BootDecision.h"
#include "boot/FlashRules.h"
#include "hw/Board.h"
#include "hw/Boot.h"
#include "hw/Flash.h"
#include "protocol/An3155Protocol.h"
#include "usb/UsbCdc.h"

namespace {

// Written before the reset of the self-protection. At the next start, the
// bootloader does not try again: a failed option byte update must not give
// a reset loop.
constexpr uint16_t selfProtectToken = 0x5E1F;

// LED: one toggle each flickerMs while there is activity. idleMs after
// the last activity, the LED is on again.
constexpr uint32_t flickerMs = 50;
constexpr uint32_t idleMs = 100;

// Wait for the last reply before a reset, and the time that the host sees
// the disconnect.
constexpr uint32_t drainMs = 100;
constexpr uint32_t disconnectMs = 20;

// SysTick runs at 1 kHz: the ticks of system-stm32 are milliseconds.
uint32_t milliseconds() {
   return static_cast<uint32_t>(getUptimeTicks());
}

class SysTickClock : public protocol::Clock {
   public:
   uint32_t milliseconds() const override {
      return ::milliseconds();
   }
};

class Memory : public protocol::MemoryReader {
   public:
   void read(uint32_t address, std::span<uint8_t> data) override {
      const volatile uint8_t *source = reinterpret_cast<const volatile uint8_t *>(address);
      for(uint8_t &byte : data) {
         byte = *source++;
      }
   }
};

void setPullUp(bool on) {
   board::usbPullUp.set(on);
}

void wait(uint32_t ms) {
   const uint32_t start = milliseconds();
   while(milliseconds() - start < ms) {
   }
}

// Resets the MCU. dr1 and dr2 go to the backup registers first: the boot
// request, or the GO token.
[[noreturn]] void restart(uint16_t dr1, uint16_t dr2) {
   const uint32_t start = milliseconds();
   while(!usbcdc::sent() && milliseconds() - start < drainMs) {
      usbcdc::poll();
   }
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
   const bool clockOk = Rcc::setClock72MhzHSE(12'000'000);
   SystemCoreClockUpdate();
   SysTick_Config(SystemCoreClock / 1000);

   RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | board::led.clockBit() | board::usbPullUp.clockBit();
   // PA15 (LED) is JTDI after the reset. SWD stays on.
   AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
   board::led.set(true);
   board::led.configure(board::Pin::outputPushPull2MHz);
   board::usbPullUp.set(false);
   board::usbPullUp.configure(board::Pin::outputPushPull2MHz);

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
   SysTickClock clock;
   protocol::An3155Protocol protocol{ map, { flash, memory, flash, application, clock } };

   uint8_t uid[hw::Uid::SizeBytes];
   hw::Uid::read(uid);
   Rcc::enableUSBClock();
   usbcdc::start(uid, &setPullUp);

   Led led;
   uint32_t resets = usbcdc::resetCount();
   bool configured = false;
   uint8_t received[usbcdc::packetSize];
   std::size_t receivedSize = 0;
   std::size_t taken = 0;

   for(;;) {
      usbcdc::poll();
      const uint32_t now = milliseconds();

      // A USB reset or a disconnect: the protocol restarts.
      if(usbcdc::resetCount() != resets || usbcdc::configured() != configured) {
         resets = usbcdc::resetCount();
         configured = usbcdc::configured();
         protocol.reset();
         receivedSize = taken = 0;
      }

      if(taken == receivedSize) {
         receivedSize = usbcdc::read(received, sizeof received);
         taken = 0;
      }
      if(taken < receivedSize) {
         taken += protocol.receive({ received + taken, receivedSize - taken });
         led.activity(now);
      }

      protocol.poll();

      const auto output = protocol.output();
      if(!output.empty()) {
         protocol.consumeOutput(usbcdc::write(output.data(), output.size()));
         led.activity(now);
      }

      switch(protocol.action()) {
      case protocol::An3155Protocol::Action::none:
         break;
      case protocol::An3155Protocol::Action::reset:
         // New option bytes: the host connects to the bootloader again.
         restart(boot::bootRequest, 0);
      case protocol::An3155Protocol::Action::startApplication:
         restart(0, boot::goToken);
      }

      led.update(now);
   }
}
