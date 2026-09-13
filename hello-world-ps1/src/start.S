/* -----------------------------------------------------------------------
 * Minimal MIPS I (R3000A) entry point for a bare-metal PS1 program.
 *
 * By the time this runs, the BIOS (here, PCSX-Redux's open-source
 * OpenBIOS) has already parsed the PS-EXE header, loaded the "text"
 * blob to the address it names, set $gp/$sp from the header's fields,
 * and jumped to the header's entry point. We still set our own $sp
 * explicitly (same defensive habit as every other platform in this
 * series) before calling into C.
 * ----------------------------------------------------------------------- */

    .set noreorder
    .section .start
    .globl _start

_start:
    lui     $sp, 0x801f
    ori     $sp, $sp, 0xff00

    jal     main
    nop

halt:
    b       halt
    nop
