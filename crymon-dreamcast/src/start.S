! -----------------------------------------------------------------------
! Minimal SH4 entry point for a bare-metal Dreamcast ELF.
!
! Boot loaders (real BIOS or Flycast's reios HLE BIOS) load this ELF's
! segments into main RAM and jump to its entry point with CPU state
! otherwise unspecified, so before touching any C code we: disable
! interrupts, pick a known-good status register value, and set up a
! stack pointer at the top of the standard 16MB Dreamcast RAM. This
! mirrors what KallistiOS's own startup.s does, minus the parts that
! are irrelevant here (dcload exit handling, 32MB RAM auto-detection,
! FPU/VBR save-and-restore for returning to a monitor).
! -----------------------------------------------------------------------

    .section .text.start
    .globl _start

_start:
    ! Disable interrupts, block levels, keep a sane FPU/register bank state
    mov.l   sr_init, r0
    ldc     r0, sr

    ! Stack pointer: top of the 16MB main RAM window (0x8c000000-0x8cffffff)
    mov.l   stack_top, r15

    ! Jump into C
    mov.l   main_addr, r0
    jsr     @r0
    nop

    ! main() never returns, but if it somehow did: spin forever
halt:
    bra     halt
    nop

    .align  4
sr_init:
    .long   0x500000f0
stack_top:
    .long   0x8cfffff0
main_addr:
    .long   _main
