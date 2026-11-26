/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Memory of the DFU bootloader on the CH32V307 (default user option:
 * CODE-256KB + RAM-64KB). The bootloader is linked at the flash alias at 0,
 * as the ROM boot maps the user flash there.
 *
 * Give the length of the bootloader area with
 * -Wl,--defsym=__flash_length=<bytes>, before the scripts (-T). The link
 * fails when the bootloader does not fit.
 */
MEMORY
{
	FLASH (rx) : ORIGIN = 0x00000000, LENGTH = DEFINED(__flash_length) ? __flash_length : 8K
	RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 64K
}
