/* Minimal application at 0x08002800 (behind the 10 KB bootloader) for the
 * stamp tool tests. .rodata is a separate output section with a 64-byte
 * alignment, so there is a gap between .text and .rodata. */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08002800, LENGTH = 54K
    RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 20K
}
ENTRY(reset_handler)
SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) } > FLASH
    .rodata : { *(.rodata*) } > FLASH
    .data : { *(.data*) } > RAM AT > FLASH
}
