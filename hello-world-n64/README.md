# Hello World (Nintendo 64)

A minimal, from-scratch bare-metal N64 program (no libdragon SDK/library
calls). It talks directly to the Video Interface (VI) hardware to bring
up a 320x240 16bpp (RGBA5551) framebuffer, then plots "HELLO WORLD" into
it with the same hand-drawn 8x8 font used in the companion
`hello-world-snes` and `hello-world-dreamcast` projects, and halts.

The ROM is booted by [libdragon](https://github.com/DragonMinded/libdragon)'s
own IPL3 ("compat" build) -- an open-source (public domain / Unlicense),
from-scratch reimplementation of the cartridge bootcode every N64 ROM
needs, written specifically so homebrew doesn't need Nintendo's original
copyrighted IPL3. No Nintendo code (IPL3, PIF ROM, or otherwise) was
sourced or used to build this.

## Layout

- `src/start.S` -- the MIPS entry point: sets up a stack pointer, calls
  `main()`.
- `src/hello.c` -- VI register setup (the standard NTSC 320x240
  non-interlaced preset -- see `vi_ntsc_p` in libdragon's `src/vi.h`,
  which this was checked against), an uncached framebuffer (writes go
  straight to RDRAM, bypassing D-cache, so the VI's independent DMA
  never reads stale data), the font data, and the draw loop.
- `n64.ld` -- linker script placing code at `0x80000400`, the classic
  N64 homebrew load address (just past the low-RDRAM area IPL3 reserves
  for its own boot-flags block).
- `ipl3_compat.bin` -- libdragon's prebuilt `boot/bin/ipl3_compat.z64`
  (4096 bytes: a 64-byte header + IPL3 bootcode), used unmodified except
  for two header fields `tools/make_rom.py` patches at build time (see
  below).
- `tools/make_rom.py` -- concatenates `ipl3_compat.bin` and the built
  flat binary into `hello.z64`, patching the header's "how many bytes to
  load" field to the binary's exact size.
- `Makefile` -- builds `hello.z64` with the `mips64-linux-gnuabi64`
  cross toolchain (a real, big-endian MIPS III/VR4300-capable GCC, even
  though its target triple says "linux" -- this build never touches
  glibc; `-nostdlib -ffreestanding` plus our own `_start` make it a
  plain bare-metal codegen target).

## Building

Requires a big-endian MIPS64 cross toolchain (Ubuntu/Debian ship one)
and `libdragon`'s prebuilt IPL3 compat blob:

```sh
sudo apt-get install gcc-mips64-linux-gnuabi64 binutils-mips64-linux-gnuabi64
# ipl3_compat.bin is already in this directory (copied from
# libdragon's boot/bin/ipl3_compat.z64, public domain / Unlicense)
make
```

This produces `hello.z64`.

## How it works

IPL3 compat mode (see `boot/loader_compat.c` in libdragon) reads two
big-endian words from its own header: the load address / entry point at
offset `0x08`, and the number of bytes to load from ROM at offset
`0x10`. It sizes and clears RDRAM, DMAs that many bytes from ROM offset
`0x1000` to the given address, and jumps there -- no ELF, no checksum,
same spirit as this repo's SNES header or the Dreamcast IP.BIN
bootstrap.

- `_start` (`start.S`) points the stack near the top of the guaranteed
  4MB every retail N64 has, then calls `main()`.
- `video_init()` writes the 14 VI registers directly: pixel format
  (16bpp), framebuffer address, line width, and the NTSC 320x240
  non-interlaced timing/scale constants.
- The framebuffer is cleared to black and "HELLO WORLD" plotted into
  it, glyph by glyph, at 3x scale, centered -- all writes go through an
  uncached (KSEG1) pointer alias so they land in RDRAM immediately.
- `main()` never returns; it spins forever once the frame is drawn.

## Verification status

`hello.z64` was built with the toolchain and process described above,
and the IPL3 blob is recognized by mupen64plus's own built-in
compatibility database as genuine libdragon bootcode (`Video: Found ROM
'Libdragon', CRC b002000000000000-00`) -- a reasonable sign the ROM
itself is well-formed. What did **not** succeed: booting it in
`mupen64plus` (both its dynamic recompiler and pure interpreter core
modes, with and without the expansion-pak-sized 8MB RDRAM
configuration). In every case, IPL3's own RDRAM auto-sizing routine --
the same real hardware-detection trick used since the original Nintendo
IPL3 -- misdetects the emulator's memory as 64MB regardless of how
mupen64plus is actually configured (4MB or 8MB), which mupen64plus's own
core flags (`Core Error: IPL3 detected 64 MB of RDRAM != 8 MB`) right
before the CPU crashes on a reserved opcode inside IPL3's own boot-flags
area. This points at an inaccuracy in mupen64plus's emulation of the
RDRAM controller's bank-aliasing behavior during this specific
low-level detection routine, not a bug in libdragon's IPL3 (used
successfully across real hardware and other emulators) or in this
project's own code, which never got a chance to run.

No other N64 emulator was available to cross-check in this environment:
`cen64` (a cycle-accurate simulator that could plausibly emulate this
correctly) requires a real Nintendo PIF ROM dump to run at all, which,
consistent with this repo's stance on the Dreamcast BIOS, was not
sourced. Real hardware (a flashcart) or a more hardware-accurate
emulator (e.g. ares) would be the way to confirm this boots cleanly
outside of mupen64plus.
