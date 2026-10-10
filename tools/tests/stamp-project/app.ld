/* Minimal application at 0x08002000 for the stamp tool tests. */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08002000, LENGTH = 52K
    RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 20K
}
ENTRY(reset_handler)
SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) } > FLASH
    .data : { *(.data*) } > RAM AT > FLASH
}
