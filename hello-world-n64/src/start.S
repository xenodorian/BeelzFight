/* -----------------------------------------------------------------------
 * Minimal MIPS entry point for a bare-metal N64 program.
 *
 * By the time this runs, libdragon's open-source IPL3 (in "compat" mode)
 * has already sized and cleared RDRAM, DMA'd this program's flat binary
 * from ROM to the address given in the ROM header, and jumped here. CPU
 * state otherwise isn't guaranteed, so before touching any C code we set
 * up our own known-good stack pointer, then call main().
 * ----------------------------------------------------------------------- */

    .set noreorder
    .section .text.start
    .globl _start

_start:
    /* Stack pointer: near the top of the guaranteed-present first 4MB of
       RDRAM (0x80000000-0x803FFFFF), well clear of this program's own
       code/data near 0x80001000. Every retail N64 has at least 4MB. */
    lui     $sp, 0x8040
    addiu   $sp, $sp, -256

    jal     main
    nop

halt:
    b       halt
    nop
