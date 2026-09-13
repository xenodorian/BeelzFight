/* Minimal linker script for a bare-metal PS1 program, producing a raw
 * PS-EXE (the executable format the BIOS's LoadExec/Exec kernel calls,
 * and CD boot, both load): a 2048-byte header followed by a flat
 * "text" blob (code + rodata + data all concatenated).
 *
 * Header layout (all fields little-endian, PS1 is little-endian MIPS)
 * per PCSX-Redux's psxlibc/psxexe.h / ps-exe.ld, which this was checked
 * against:
 *   0x00  "PS-X EXE" magic (8 bytes)
 *   0x08  8 bytes, unused (text_off/data_off -- not read by the BIOS)
 *   0x10  pc0        -- initial PC (entry point)
 *   0x14  gp0        -- initial $gp (0: this code has no gp-relative
 *                        references at all, see -G0/-mno-abicalls)
 *   0x18  t_addr     -- where to load the "text" blob
 *   0x1C  t_size     -- how many bytes of "text" blob follow the header
 *   0x20  d_addr/d_size, 0x28 b_addr/b_size -- unused, left zero
 *   0x30  s_addr     -- initial stack pointer (also set explicitly in
 *                        start.S, belt and suspenders)
 *   0x34  s_size     -- 0 (unused)
 *   0x38-0x7FF       -- reserved/padding, left zero
 */

OUTPUT_FORMAT("elf32-tradlittlemips")
OUTPUT_ARCH(mips)
ENTRY(_start)

TLOAD_ADDR = 0x80010000;
STACK_ADDR = 0x801ffff0;

MEMORY {
    header : ORIGIN = TLOAD_ADDR - 0x800, LENGTH = 0x800
    ram    : ORIGIN = TLOAD_ADDR, LENGTH = 0x100000
}

SECTIONS {
    .header : {
        BYTE(0x50); BYTE(0x53); BYTE(0x2d); BYTE(0x58); /* "PS-X" */
        BYTE(0x20); BYTE(0x45); BYTE(0x58); BYTE(0x45); /* " EXE" */
        LONG(0); LONG(0);
        LONG(ABSOLUTE(_start));
        LONG(0);
        LONG(TLOAD_ADDR);
        LONG(__text_size);
        LONG(0); LONG(0);
        LONG(0); LONG(0);
        LONG(STACK_ADDR);
        LONG(0);
        . = . + 1992;
    } > header

    __text_start = TLOAD_ADDR;

    .text TLOAD_ADDR : {
        *(.start)
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

    __text_end = .;
    __text_size = __text_end - __text_start;

    .bss (NOLOAD) : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } > ram
}
