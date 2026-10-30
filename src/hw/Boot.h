#pragma once

// Start-up of the bootloader (issue #10, plan sections 3.1 and 3.4).
//
// os_startup_initialize_hardware_early() (system-stm32) runs straight
// after the reset, before the RAM initialisation and the clock setup. The
// bootloader replaces it with the boot decision (boot/BootDecision.h). To
// start the application, it sets the peripherals that the decision used
// back to their reset state and jumps to the application. The application
// thus starts in the reset state, with all interrupts masked (PRIMASK
// set), as OpenMUPP expects (docs/firmware-update.md).
//
// The decision leaves the clocks of PWR, BKP and CRC on in bootloader
// mode. The functions below use them.

#include <cstdint>

#include "protocol/Hardware.h"

namespace hw::startup {

// Writes a backup data register (BKP_DRx keep their value over a reset).
void writeBackup(volatile uint32_t &reg, uint16_t value);

// The flash size in bytes, from the flash size register.
uint32_t flashSize();

// The application image check with the CRC unit.
class ApplicationImage : public protocol::Application {
   public:
   bool isValid() const override;
};

} // namespace hw::startup
