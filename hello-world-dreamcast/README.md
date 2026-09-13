# Hello World (Dreamcast)

A minimal, from-scratch bare-metal Sega Dreamcast program (no KallistiOS,
no BIOS calls). It talks directly to the PowerVR2 (PVR) display hardware
to bring up a 320x240 RGB565 framebuffer, then plots "HELLO WORLD" into
it with a hand-drawn 8x8 font (the same glyph bitmaps used in the
companion `hello-world-snes` project) and halts.

![screenshot](docs/screenshot.png)

Verified running in [Flycast](https://github.com/flyinghead/flycast),
built from source and booted headless via `tools/run_flycast_headless.sh`
— the screenshot above is straight from that run, loaded as a homebrew
`.elf` (Flycast auto-enables its from-scratch `reios` HLE BIOS for `.elf`
files, so no copyrighted Sega BIOS is needed to boot or test this).

## Layout

- `src/start.S` — the SH4 entry point: disables interrupts, sets up a
  stack pointer at the top of main RAM, and jumps into C.
- `src/hello.c` — PVR2 register setup (mirrors KallistiOS's `vid_init()`
  for its `DM_320x240_NTSC` mode — see `video.c` in
  [KallistiOS](https://github.com/KallistiOS/KallistiOS), which this was
  checked against), a framebuffer clear, the font data, and the draw
  loop.
- `dc.ld` — linker script placing code at `0x8c010000` (the conventional
  homebrew load address in the Dreamcast's 16MB main RAM).
- `Makefile` — builds `hello.elf` with `sh-elf-gcc`/binutils, and `hello.cdi`
  with `mkdcdisc`.
- `hello.cdi` — a bootable disc image wrapping `hello.elf`, built with
  [mkdcdisc](https://gitlab.com/simulant/mkdcdisc) (the same tool this
  repo's own `scripts/build_cdi.sh` uses for the main game), padding
  disabled (`-N`) so it's a ~1.5MB file rather than a full ~740MB GD-ROM
  image. Add `--allow-overwrite` and drop `-N` (see the `Makefile`'s `cdi`
  target) if you need a real, full-size disc for burning/an ODE.

## Building

Requires an `sh-elf` cross toolchain (Ubuntu/Debian ship one):

```sh
sudo apt-get install gcc-sh-elf binutils-sh-elf libnewlib-sh-elf-dev
make
```

This produces `hello.elf`. `make cdi` additionally produces `hello.cdi`,
and requires `mkdcdisc` on `PATH` (see its
[BUILDING.md](https://gitlab.com/simulant/mkdcdisc/-/blob/main/BUILDING.md)
— it's a small meson/C++ project, not part of the SH4 toolchain).

## Running

- **Flycast** (or another Dreamcast emulator with homebrew ELF support):
  load `hello.elf` directly — this is the path actually verified (see the
  screenshot above and "A note on `hello.cdi` and emulation" below).
- **Real hardware**: load `hello.elf` over a serial/broadband adapter with
  [dcload](https://github.com/dcload-ip/dcload-ip)/`dc-tool`, or burn/mount
  `hello.cdi` (or an ODE-loaded copy of it) — a real console's real BIOS
  boots a disc image the standard way, which is the well-trodden path
  `hello.elf`-via-reios is a convenience shortcut around, not the other
  way around.
- **Flycast with a real BIOS** (`dc_boot.bin`/`dc_flash.bin` in its data
  dir — not provided or sourced here, see `docker/EMULATOR_README.md`):
  load `hello.cdi` directly, the same as any other disc image.

### A note on `hello.cdi` and emulation

`hello.cdi` was built with the same tool and flow this repo's real game
uses (`mkdcdisc` -> `scripts/build_cdi.sh`), and its ISO9660/IP.BIN
structure was not hand-rolled — no reason to expect it's malformed. What
*was* tried and didn't pan out: booting it through Flycast's `reios` HLE
BIOS headlessly, the same way `hello.elf` was verified above. Passing a
disc image to `-config config:UseReios=yes` gets as far as reios loading
the disc's IP.BIN bootstrap and showing its (own, non-Sega,
`mkdcdisc`-generated) license/logo screen, then never progresses past it
even after minutes of wall-clock time at sustained CPU usage — i.e. it
hangs somewhere inside that bootstrap, not inside this project's own
code, which never gets a chance to run. `docker/EMULATOR_README.md`
already flags exactly this ahead of time: *"reios is primarily tuned for
homebrew [ELF loading], so test it on the actual build... If that
doesn't boot cleanly... the alternative is to point... directly at the
pre-disc `.elf`"* — which is what was done instead. A real BIOS (real
hardware, or Flycast configured with one) is the standard, well-tested
way to boot a disc image and should have no trouble with `hello.cdi`;
what's untested is specifically the reios-HLE-boots-a-disc-image
combination, in this environment.

## How it works

- `_start` (in `start.S`) disables interrupts, sets SR to a known value,
  points the stack at the top of the standard 16MB Dreamcast RAM window
  (`0x8cfffff0`), and calls `main()`.
- `video_init()` configures the PVR2's scan-out registers directly
  (pixel format, line stride, framebuffer address/size, timing) for a
  320x240, non-interlaced, RGB565 mode — no 3D/tile-accelerator setup is
  needed since this only ever writes a flat framebuffer.
- The framebuffer (at the start of VRAM, `0xa5000000`) is cleared to
  black, then "HELLO WORLD" is plotted into it, glyph by glyph, at 3x
  scale, centered on screen.
- `main()` never returns; it spins forever once the frame is drawn, since
  nothing else needs to happen.
