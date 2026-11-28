// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The flash of the CH32V307 for the bootloader (CH32FV2x_V3xRM 32.2).
//
// The application area ends at the end of the zero-wait flash. With the
// default user option (CODE-256KB + RAM-64KB), that is 256 KB. The
// firmware keeps its settings page in the non-zero-wait flash behind it
// (0x08077F00), so a download never reaches the settings. A part with
// another split gives a smaller zero-wait area: the bootloader then does
// not support it.

#include <cstdint>

#include "Layout.h"

namespace ch32::memory {

// The application is linked at its address in the flash alias at 0, and
// the bootloader jumps there.
constexpr uint32_t applicationAlias = layout::applicationStart - layout::flashBase;
constexpr uint32_t applicationEnd = layout::flashBase + 256 * 1024;
// The erase unit: the 4 KB sector of the standard erase. It takes as long
// as one 256-byte fast erase (16 ms). The programming uses 256-byte pages.
constexpr uint32_t pageSize = 4096;

static_assert(layout::applicationStart % pageSize == 0 && applicationEnd % pageSize == 0);

} // namespace ch32::memory
