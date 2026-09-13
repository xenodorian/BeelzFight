/* Minimal linker script for a bare-metal N64 program, loaded as a flat
 * binary by libdragon's IPL3 "compat" bootcode.
 *
 * 0x80000400 matches this project's ROM header (see tools/make_rom.py,
 * and libdragon's ipl3_compat.z64 template, which already defaults its
 * header's PC field to this address): IPL3 compat mode loads the flat
 * binary to, and jumps to, whatever address the ROM header names -- so
 * this is a build-time choice, not a hardware-fixed one. 0x400 is the
 * classic convention: it leaves the low RDRAM area (where IPL3 writes
 * its boot-flags block, e.g. 0x80000300-0x8000031f) undisturbed.
 */

OUTPUT_FORMAT("elf32-tradbigmips")
OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY {
    ram : ORIGIN = 0x80000400, LENGTH = 0x00300000
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

    __rom_end = .;
}
