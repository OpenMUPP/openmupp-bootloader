// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Minimal USB CDC-ACM device for the STM32F1 USB peripheral. This is the
// USB stack of the rewritten bootloader (issue #4): the libArT USB stack
// does not fit in the 8 KB bootloader area (issue #6). One configuration,
// one CDC-ACM function, no interrupt: poll() runs the device from the main
// loop. It needs only CMSIS (cmsis_device.h of system-stm32).
//
// Usage (an echo device, as in the size spike on branch spike/cdc-size):
//
//   uint8_t uid[12];
//   hw::Uid::read(uid);
//   Rcc::enableUSBClock();          // after the 72 MHz clock setup
//   usbcdc::start(uid, &setPullUp);
//   for(;;) {
//      usbcdc::poll();
//      uint8_t buffer[usbcdc::packetSize];
//      const std::size_t n = usbcdc::read(buffer, sizeof buffer);
//      usbcdc::write(buffer, n);
//   }
//
// Timing: the device answers only when poll() runs. Call poll() at least
// every 1 ms in normal operation. The longest gap between two poll() calls
// must stay below 10 ms (RM0008 23.4.2: EP0 must work 10 ms after a bus
// reset) except for one flash page erase (20-40 ms; the CPU stalls on the
// flash). Erase several pages one page at a time, with poll() between the
// pages. A SETUP that comes during a gap waits in the hardware; the USB 2.0
// control request time limits (50 ms, 500 ms) allow one page erase.
//
// Known deviations: the device is bus-powered (100 mA) but it does not go
// into low-power mode on a suspend. It ignores the suspend, as a
// bootloader that the host uses actively. No remote wakeup.
//
// Status: compile-tested and size-measured (3.2 KB image with start-up,
// LTO). Not yet tested on a board; #10 integrates it and #11 tests it.
//
// Endpoints: EP0 control (64 bytes), EP1 IN interrupt notification (never
// sent), EP2 OUT/IN bulk data (64 bytes).
//
// Flow control: the OUT endpoint stays NAK after a packet until read() has
// taken all bytes of the packet. The host waits; no byte is dropped.
// write() queues up to txCapacity bytes. poll() sends them as 64-byte
// packets, with a zero-length packet after a transfer that ends on a full
// packet.

#include <cstddef>
#include <cstdint>

namespace usbcdc {

constexpr std::size_t packetSize = 64;
constexpr std::size_t txCapacity = 320;

// Sets the serial number string from the 12-byte chip UID, then starts the
// peripheral. The USB clock (48 MHz) must be on. pullUp connects the device
// (D+ pull-up), or is nullptr for a fixed pull-up.
void start(const uint8_t (&uid)[12], void (*pullUp)(bool on));

// Runs the device. Call it often from the main loop.
void poll();

// Takes received bytes. Returns the number of bytes copied.
std::size_t read(uint8_t *data, std::size_t size);

// Queues bytes for sending. Returns the number of bytes queued (less than
// size when the queue is full).
std::size_t write(const uint8_t *data, std::size_t size);

// True when all queued bytes are sent, with the zero-length packet if
// one is necessary. Call it before a reset that must not lose a reply.
bool sent();

// True when the host has configured the device.
bool configured();

// Counts the USB bus resets. The protocol restarts when it changes.
uint32_t resetCount();

} // namespace usbcdc
