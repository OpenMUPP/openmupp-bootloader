// SPDX-License-Identifier: GPL-3.0-or-later

/* Minimal application for the stamp tool tests. The reserved vector table
 * words (0x1C-0x2B) are zero. The 64-byte aligned table makes a gap before
 * .rodata (see app.ld). .data makes the image include a load region after
 * the code. */
void reset_handler(void);

volatile unsigned int counter = 5;

__attribute__((aligned(64))) const unsigned char table[4] = { 1, 2, 3, 4 };

__attribute__((section(".isr_vector"), used))
void (*const vectors[16])(void) = {
    (void (*)(void))0x20005000, reset_handler, reset_handler, reset_handler,
};

void reset_handler(void)
{
    for (;;)
        counter += table[counter & 3];
}
