// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Start-up of the bootloader on the CH32V307 (OpenMUPP v1).
//
// os_startup_initialize_hardware_early() (ch32v00x-system) runs before the
// RAM initialisation and before SystemInit(). The bootloader replaces it
// with the boot decision (boot/BootDecision.h), as on the STM32F1. To start
// the application, it sets the peripherals that the decision used back to
// their reset state and jumps to the application. The application thus
// starts with the clocks of the reset (HSI) and sets up its core registers
// (gp, sp, mtvec) itself.
//
// The backup registers BKP_DATAR1 (boot request) and BKP_DATAR2 (GO token)
// have the layout of BKP_DR1 and BKP_DR2 of the STM32F1. The key of
// OpenMUPP v1 is on BOOT0: a key that is down at the reset starts the ROM
// bootloader of WCH, so the decision has no key.
//
// The decision leaves the clocks of PWR, BKP and CRC on in bootloader
// mode. The functions below use them.

#include <cstdint>

#include "protocol/Hardware.h"

namespace ch32 {

// CH32FV2x_V3xRM: BKP_DATARx (5.3), PWR_CTLR.DBP (4.4.1), CRC (12.3).
// ch32v30x.h of ch32v00x-system has no BKP, PWR and CRC.
// Functions, not references: the boot decision runs before the C++
// start-up code, so no object with a dynamic initialisation is ready.
inline volatile uint32_t &reg32(uintptr_t address) {
   return *reinterpret_cast<volatile uint32_t *>(address);
}
inline volatile uint32_t &bkpDatar1() {
   return reg32(0x4000'6C04);
}
inline volatile uint32_t &bkpDatar2() {
   return reg32(0x4000'6C08);
}

// Writes a backup data register (BKP_DATARx keep their value over a reset).
void writeBackup(volatile uint32_t &reg, uint16_t value);

// The application image check with the CRC unit.
class ApplicationImage : public protocol::Application {
   public:
   bool isValid() const override;
};

} // namespace ch32
