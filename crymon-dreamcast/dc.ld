/* Minimal linker script for a bare-metal Dreamcast ELF.
 *
 * Main RAM is 0x8c000000-0x8cffffff (16MB). By convention homebrew and
 * KOS binaries load starting at 0x8c010000, leaving the first 64KB free
 * (low system/bootstrap area); the stack (set up in start.S) lives at
 * the very top of RAM, growing down.
 */

OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)

MEMORY {
    ram : ORIGIN = 0x8c010000, LENGTH = 0x00fe0000
}

SECTIONS {
    .text : {
        *(.text.start)
        *(.text)
        *(.text.*)
    } > ram

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } > ram

    .data : {
        *(.data)
        *(.data.*)
    } > ram

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } > ram
}
