# Hello World (PlayStation 1)

A minimal, from-scratch bare-metal PS1 program (no PsyQ/PSn00bSDK
library calls). It talks directly to the GPU's two I/O ports to bring
up a 320x240 NTSC 15bpp display mode, then draws "HELLO WORLD" with the
GPU's monochrome-rectangle draw command, using the same hand-drawn 8x8
font used in the companion SNES/Dreamcast/N64 hello-world projects.

![screenshot](docs/screenshot.png)

Booted and verified with [Mednafen](https://mednafen.github.io/), using
[OpenBIOS](https://github.com/grumpycoders/pcsx-redux/tree/main/src/mips/openbios)
(part of the PCSX-Redux project) as the BIOS -- an open-source (MIT
licensed), from-scratch reimplementation of the PS1 BIOS, written
specifically so homebrew doesn't need a real, copyrighted Sony BIOS
dump. No Sony code was sourced or used to build or run this.

## Layout

- `src/start.S` -- the MIPS entry point: sets up a stack pointer,
  calls `main()`.
- `src/hello.c` -- GPU setup (reset, NTSC 320x240 15bpp mode, display
  enable, drawing area/offset) and the font/draw code. The register
  addresses and command encodings were checked against OpenBIOS's own
  source (`common/hardware/{gpu,hwregs}.h`, `psyqo/primitives/
  rectangles.hh`, `main/splash.c`) rather than hand-derived from memory
  -- see the comment at the top of `hello.c` for specifics, including
  why the monochrome-rectangle command (`0x60`) was used instead of the
  GPU's "fast fill" command (`0x02`), which turned out to round
  coordinates to 16-pixel boundaries and would have mangled glyphs this
  small.
- `ps1.ld` -- linker script producing a PS-EXE: a 2048-byte header
  (entry point, load address, size, initial stack) followed by a flat
  code/data blob, loaded at the conventional `0x80010000`.
- `Makefile` -- builds `hello.exe` (a PS-EXE, despite the extension --
  this is the file's real conventional name in the PS1 homebrew scene)
  with the `mipsel-linux-gnu` cross toolchain (a real, little-endian
  MIPS I-capable GCC, even though its target triple says "linux" --
  this build never touches glibc; `-nostdlib -ffreestanding` plus our
  own `_start` make it a plain bare-metal codegen target, the same
  trick used for the Dreamcast and N64 projects' toolchains).
- `openbios.bin` -- a prebuilt copy of PCSX-Redux's OpenBIOS (see
  "Building OpenBIOS" below to rebuild it), for emulators that need a
  BIOS file path configured.

## Building

Requires a little-endian MIPS cross toolchain (Ubuntu/Debian ship one):

```sh
sudo apt-get install gcc-mipsel-linux-gnu binutils-mipsel-linux-gnu
make
```

This produces `hello.exe`.

### Building OpenBIOS

`openbios.bin` here was built from PCSX-Redux's source, unmodified,
with the same cross toolchain (no need for their full documented
`mipsel-none-elf` toolchain -- the Makefile's `PREFIX`/`FORMAT`
variables just need overriding):

```sh
git clone https://github.com/grumpycoders/pcsx-redux.git
cd pcsx-redux
git submodule update --init --depth 1 -- third_party/uC-sdk
make -C src/mips/openbios PREFIX=mipsel-linux-gnu FORMAT=elf32-tradlittlemips
```

## Running

- **Mednafen**: point its `psx.bios_na`/`psx.bios_jp`/`psx.bios_eu`
  setting (in `mednafen.cfg`, or `-psx.bios_na <path>` on the command
  line) at `openbios.bin`, set `psx.bios_sanity 0` (it isn't a genuine
  Sony BIOS, so its checksum won't match Mednafen's sanity check), then
  run `mednafen hello.exe` directly -- Mednafen loads a raw PS-EXE
  without needing a disc image.
- **PCSX-Redux**: ships with OpenBIOS built in; open `hello.exe`
  directly, or use its `BOOT_MODE=psexe` build option to skip straight
  to PS-EXE loading.
- **Real hardware**: PS-EXE is also what real BIOS's `Exec`/`LoadExec`
  kernel calls take (e.g. loaded over serial with a devkit/modchip, or
  from a memory card via an existing bootloader); real hardware needs
  the real BIOS, not `openbios.bin` (OpenBIOS targets emulators and
  flashable replacement chips, not sideloading onto stock firmware).

## How it works

- `_start` (`start.S`) points the stack near the top of PS1's 2MB main
  RAM, then calls `main()`. (The BIOS's PS-EXE loader already sets `$sp`
  from the header before jumping here, per the header's `s_addr` field
  -- this is just the same defensive "set it again explicitly" habit
  used in every other platform in this series.)
- `video_init()` issues GPU control-port (GP1) commands directly:
  reset, display area, horizontal/vertical timing range, 320x240 NTSC
  15bpp mode, then (after setting a full-screen drawing area/offset via
  the data port, GP0) enables the display.
- The screen is cleared to black and "HELLO WORLD" plotted into it,
  one monochrome rectangle per contiguous run of lit pixels in each
  font row, at 3x scale, centered.
- `main()` never returns; it spins forever once the frame is drawn.
