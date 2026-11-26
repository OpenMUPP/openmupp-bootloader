// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The USB descriptors of the DFU bootloader. One configuration, one
// interface (DFU mode, alternate setting 0 "Application"), only EP0.
//
// Windows 8.1 and later bind WinUSB without an INF file: bcdUSB 2.01 makes
// Windows read the BOS descriptor, and the MS OS 2.0 descriptor set gives
// the device the compatible ID "WINUSB". dfu-util uses libusb, which finds
// WinUSB devices without a device interface GUID.
//
// No hardware access: the host tests use this header too.

#include <cstdint>

#include "protocol/DfuProtocol.h"

namespace usb::descriptors {

// The pid.codes test ID until the bootloader has its own PID. The host
// tool also checks the product string.
constexpr uint16_t vendorId = 0x1209;
constexpr uint16_t productId = 0x0001;
constexpr uint16_t deviceRelease = 0x0300;
constexpr uint8_t ep0Size = 64;

// bRequest of the vendor request that reads the MS OS 2.0 descriptor set
constexpr uint8_t msOs20VendorCode = 0x20;
// wIndex of that request (MS_OS_20_DESCRIPTOR_INDEX)
constexpr uint16_t msOs20DescriptorIndex = 7;

// clang-format off
inline constexpr uint8_t device[18] = {
   18, 1, 0x01, 0x02,    // bLength, DEVICE, bcdUSB 2.01 (BOS)
   0x00, 0x00, 0x00,     // class in the interface
   ep0Size,              // bMaxPacketSize0
   vendorId & 0xFF, vendorId >> 8, productId & 0xFF, productId >> 8,
   deviceRelease & 0xFF, deviceRelease >> 8,
   1, 2, 3, 1,           // iManufacturer, iProduct, iSerialNumber, bNumConfigurations
};
// clang-format on

// bitCanDnload, bitCanUpload, bitManifestationTolerant: after the
// manifestation the device answers GETSTATUS with dfuIDLE, as dfu-util 0.11
// needs for a clean end (protocol/DfuProtocol.h). bitWillDetach is clear:
// the device has no DFU_DETACH, and dfu-util -R resets the bus instead.
constexpr uint8_t dfuAttributes = 0x01 | 0x02 | 0x04;

// clang-format off
inline constexpr uint8_t configuration[27] = {
   9, 2, 27, 0, 1, 1, 0, 0x80, 50,                    // CONFIGURATION, 1 interface, bus powered, 100 mA
   9, 4, 0, 0, 0, 0xFE, 0x01, 0x02, 4,                // INTERFACE 0: DFU, DFU mode, iInterface 4
   9, 0x21, dfuAttributes, 0xFF, 0x00,                // DFU FUNCTIONAL, wDetachTimeOut 255 ms
   dfu::transferSize & 0xFF, dfu::transferSize >> 8,  // wTransferSize
   0x10, 0x01,                                        // bcdDFUVersion 1.10
};

// The MS OS 2.0 descriptor set: the header and the compatible ID WINUSB
// for the whole device.
inline constexpr uint8_t msOs20Set[30] = {
   10, 0, 0x00, 0, 0x00, 0x00, 0x03, 0x06, 30, 0,     // SET HEADER, Windows 8.1, total length
   20, 0, 0x03, 0, 'W', 'I', 'N', 'U', 'S', 'B', 0, 0, // COMPATIBLE ID
   0, 0, 0, 0, 0, 0, 0, 0,                            // sub-compatible ID
};

inline constexpr uint8_t bos[33] = {
   5, 15, 33, 0, 1,                                   // BOS, total length, 1 capability
   28, 16, 5, 0,                                      // PLATFORM capability
   0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,    // MS OS 2.0 UUID D8DD60DF-4589-4CC7-9CD2-659D9E648A9F
   0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
   0x00, 0x00, 0x03, 0x06,                            // dwWindowsVersion 8.1
   sizeof msOs20Set, 0,                               // wMSOSDescriptorSetTotalLength
   msOs20VendorCode, 0,                               // bMS_VendorCode, bAltEnumCode
};
// clang-format on

inline constexpr uint8_t language[4] = { 4, 3, 0x09, 0x04 };
inline constexpr uint8_t manufacturer[] = { 18, 3, 'O', 0, 'p', 0, 'e', 0, 'n', 0, 'M', 0, 'U', 0, 'P', 0, 'P', 0 };
// The host tool finds the bootloader by this string (tools/mupp_probe.py)
inline constexpr uint8_t product[] = { 40,  3, 'O', 0, 'p', 0, 'e', 0, 'n', 0, 'M', 0, 'U', 0, 'P', 0, 'P', 0, ' ', 0,
                                       'B', 0, 'o', 0, 'o', 0, 't', 0, 'l', 0, 'o', 0, 'a', 0, 'd', 0, 'e', 0, 'r', 0 };
inline constexpr uint8_t interface[] = { 24,  3, 'A', 0, 'p', 0, 'p', 0, 'l', 0, 'i', 0,
                                         'c', 0, 'a', 0, 't', 0, 'i', 0, 'o', 0, 'n', 0 };

static_assert(sizeof manufacturer == manufacturer[0] && sizeof product == product[0] && sizeof interface == interface[0]);
static_assert(sizeof bos == bos[2] && sizeof msOs20Set == msOs20Set[8] && sizeof configuration == configuration[2]);

// The serial number string: the chip UID as 24 hex digits, as in the
// firmware, so the host tool finds the same dongle in both modes.
struct SerialString {
   uint8_t bytes[2 + 2 * 24];

   explicit SerialString(const uint8_t (&uid)[12]) {
      static constexpr char hex[] = "0123456789ABCDEF";
      bytes[0] = sizeof bytes;
      bytes[1] = 3;
      for(int i = 0; i < 24; ++i) {
         bytes[2 + 2 * i] = static_cast<uint8_t>(hex[(uid[i / 2] >> (i & 1 ? 0 : 4)) & 0xF]);
         bytes[3 + 2 * i] = 0;
      }
   }
};

} // namespace usb::descriptors
