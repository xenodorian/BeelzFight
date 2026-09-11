# BeelzFight romdev environment

A Docker image with a complete, working Sega Dreamcast homebrew SDK:
KallistiOS (KOS), its `sh-elf` (SH4) and `arm-eabi` (AICA) cross-compilers,
the `pvrtex`/`kmgenc` PNG→PVR texture converters, and `mkdcdisc` +
`genisoimage`/`mkisofs` for packaging a bootable disc image.

## TL;DR

```sh
docker/build.sh                     # build the image (30-90+ min first time)
docker/compile.sh                   # compile this repo's game with it
```

## Toolchain provenance (read this if you rebuild this image)

The **intended, canonical way** to get this SDK is KallistiOS's own toolchain
builder, `utils/kos-chain` (the modern successor to the old `dc-chain`
scripts), which builds `sh-elf`/`arm-eabi` GCC+Binutils+Newlib from pristine
upstream source. That is what this Dockerfile tried first.

It could not be made to work **in the sandboxed environment this image was
built in**, because that sandbox's network egress policy allows git/HTTPS to
`github.com`, `gitlab.com`, npm/PyPI/crates.io/the Go proxy, Ubuntu's package
archive (`archive.ubuntu.com`), and a short list of container registries
(`mcr.microsoft.com`, `gcr.io`, `mirror.gcr.io`) — but denies essentially
every host GNU toolchain sources are normally fetched from
(`ftpmirror.gnu.org`, `mirrors.kernel.org`, `ftp.gnu.org`, `gcc.gnu.org`,
`sourceware.org`), **and** Docker Hub's own blob CDN
(`production.cloudfront.docker.com`), so not even a `docker.io`-hosted
prebuilt KOS image could be pulled directly.

The one avenue that stayed open was `mirror.gcr.io`, Google's public
read-through mirror of the entirety of Docker Hub. Through it this image
pulls the only prebuilt KOS toolchain image that could be found at all,
[`einsteinx2/dcdev-kos-toolchain`](https://hub.docker.com/r/einsteinx2/dcdev-kos-toolchain)
(GCC 9.3.0 for `sh-elf` / GCC 8.4.0 for `arm-eabi`, built ~2020-2021,
Alpine/musl-linked) — stale, but a real, working `sh4-elf-gcc`, which was
otherwise unobtainable in that sandbox. Everything *above* the raw
cross-compiler — KallistiOS itself, kos-ports, mkdcdisc — is still built
fresh from pinned upstream source (all reachable via github.com/gitlab.com),
so the actively-developed parts of the SDK stay current; only the
GCC/Binutils/Newlib layer is the older prebuilt one.

**If you are rebuilding this image somewhere with normal internet access**
(a real workstation, most CI runners), you should instead build the
toolchain from source via `utils/kos-chain` for a fully current,
reproducible-from-source SDK — see
[KallistiOS's own toolchain docs](https://github.com/KallistiOS/KallistiOS/blob/master/utils/kos-chain/README.md).
That is the better approach in every way except "works with no GNU-mirror
access at all." Two smaller consequences of the current setup only apply if
you keep the prebuilt toolchain:

- **musl compatibility shim.** The prebuilt toolchain's binaries are
  musl-linked (Alpine); this image is Ubuntu (glibc), because
  `archive.ubuntu.com` is reachable here while `deb.debian.org` is not. The
  `musl` Ubuntu package supplies just enough of a musl runtime (loader +
  default library search path) for the copied cross-compiler binaries —
  and the two musl-built shared libraries their C++-implemented pieces
  (`cc1`/`cc1plus`/`lto1`) need — to run correctly alongside the image's own
  glibc tooling. See the `COPY --from=toolchain` block in
  `Dockerfile.romdev`.
- **`docker/gcc9-compat.h` compiler shim.** Modern KOS calls a few
  identifiers (`__align_up`, `__is_aligned`, `__builtin_is_aligned`,
  `__builtin_set_thread_pointer`) that KallistiOS's own from-source toolchain
  builder patches directly into GCC as real compiler builtins. This
  prebuilt toolchain predates those patches, so this header supplies
  portable C equivalents and is force-included into every KOS compile via
  `KOS_CFLAGS` (see `environ.sh` generation in the Dockerfile). At the
  KOS revision currently pinned (see "KOS/toolchain version match" below)
  it is a no-op — nothing in that era's KOS sources references these
  identifiers at all — and is kept only so bumping `KOS_COMMIT` forward
  later needs no other changes here.
- **`addons/` (optional extra libraries, e.g. `libkosext2fs`) are not
  built.** They aren't needed for core KOS, PVR/2D-sprite rendering, or
  anything in this repo. `kernel` (`libkallisti.a`) and the `utils` this
  image needs (kmgenc, scramble, makeip, bin2c, bin2o, genromfs) build and
  link cleanly.
- **kos-ports was not populated.** kos-ports' own build system downloads
  each port's upstream source tarball directly from that port's own site at
  build time (zlib from zlib.net, etc) — again not reachable under the same
  policy. The `kos-ports` checkout itself is present in the image (at
  `$KOS_PORTS`, cloned at a pinned commit) and `make install` works for any
  individual port the moment this image runs somewhere with normal internet
  access. This is not required for anything in this repo: the PVR API used
  for 2D sprite rendering is part of core KOS, no port needed.

### KOS/toolchain version match (read this if things crash at runtime)

**Update, 2026-09-11: the original `KOS_COMMIT` pin (bleeding-edge KOS as of
that date) produced binaries that built and looked structurally valid but
crashed instantly when actually *run* in an emulator.** This was only
caught once Flycast became available in this environment and someone
booted the resulting `hello.elf`:

```
E[COMMON]: Flycast has stopped: Fatal: SH4 branch instruction in delay slot
```

That message is Flycast's SH4 block decoder refusing to execute a real
ISA-level illegality (a branch instruction sitting in another branch's
delay slot — forbidden on every real SH4). Instrumenting Flycast's decoder
to log the faulting address (`core/hw/sh4/dyna/decoder.cpp`, right before
it throws `FlycastException("Fatal: SH4 branch instruction in delay slot")`)
showed execution landing squarely on `_irq_srt_addr`
(`kernel/arch/dreamcast/kernel/entry.s`) — a plain *data* word (a saved
register-context pointer), not code — meaning something had jumped to the
address of that variable instead of through it, almost certainly during
the very first hardware interrupt/exception delivery (the crash reliably
hit ~400-700ms after REIOS handoff, consistent with KOS's first
timer-preemption tick). `entry.s`'s copyright header reads "2025 Falco
Girgis": it was substantially rewritten for faster IRQ context save/restore
in Dec 2023 and again in Aug 2025, and per KallistiOS's own `utils/dc-chain`
patch history, KOS-patched GCC builds gained the custom compiler builtins
`gcc9-compat.h` above works around only in Jan 2023 — i.e. bleeding-edge
KOS's exception-entry code was written against calling-convention/codegen
assumptions roughly two years newer than this prebuilt toolchain (built
2021-03-15, confirmed via `docker inspect` on the toolchain image).

**Fix: `KOS_COMMIT` is now pinned to `057d05fe4f2b6fc3e2c93215d3b83c871ae4e23c`
(2021-04-24)** — the newest commit reachable from a
`--shallow-since=2021-03-01` fetch of KOS's master branch, i.e. right at
this toolchain's own build date, carrying the original (c)2000-2001 Dan
Potter `entry.s`, well before either IRQ rewrite, and from before any of
`gcc9-compat.h`'s builtins existed in KOS at all. Confirmed: both KOS's own
`hello.elf` (stock, and a `dbgio_dev_select("fb")` variant — see the
Validation section below for why stock `hello.c` alone isn't a conclusive
visual test) and this repo's `beelzfight.elf` now boot past REIOS and run
real KOS/game code without that crash.

**Trade-off knowingly accepted:** this KOS revision predates the `pvrtex`
PNG→PVR texture converter (added Aug 2024) and `elf2bin` (added Nov 2023),
so neither is built into this image anymore (`utils/Makefile`'s `DIRS` list
and the tree layout at this commit don't have them at all). This is
confirmed harmless for this specific repo: BeelzFight's own asset pipeline
(`scripts/png_to_pvr.py`) deliberately hand-writes PVR-ready
ARGB4444/RGB565 texture blobs instead of depending on KOS's
`pvrtex`/`kmgenc`, the textures are pre-generated and already committed
under `romdisc/textures/*.pvr`, and disc packaging goes through `mkdcdisc`
(built fresh from source, independent of the KOS revision) rather than the
manual `elf2bin`+`scramble`+`genisoimage` fallback below. A project that
does need `pvrtex`/`elf2bin` should bump `KOS_COMMIT` forward past this
era instead — and would then need to work through (or wait out) the
entry.s incompatibility described above first.

**If `beelzfight.elf`/`beelzfight.cdi` themselves still fail after this
fix**, that points at a bug in this repo's own game code rather than the
toolchain/KOS pin — see "Validation performed" below for the specific
failure last observed there, which is exactly that kind of bug (not a
delay-slot/ISA-legality crash, and not present in KOS's own examples).

Pinned revisions (see `ARG`s at the top of `Dockerfile.romdev`):

| Component  | Source                                              | Commit / version |
|------------|------------------------------------------------------|-------------------|
| KallistiOS | https://github.com/KallistiOS/KallistiOS              | `057d05fe4f2b6fc3e2c93215d3b83c871ae4e23c` (2021-04-24) |
| kos-ports  | https://github.com/KallistiOS/kos-ports                | `f4faacc42faaf552625777b7709e871a827e1055` |
| mkdcdisc   | https://gitlab.com/simulant/mkdcdisc                   | `4d74e40dd2122e14389a305ed1d86dd024201389` |
| sh-elf / arm-eabi toolchain | `mirror.gcr.io/einsteinx2/dcdev-kos-toolchain:gcc-9` | GCC 9.3.0 / 8.4.0 (built 2021-03-15) |

## Building the image

```sh
docker/build.sh
```

This is a straight wrapper around:

```sh
docker build -t beelzfight-romdev -f docker/Dockerfile.romdev docker/
```

It builds KOS from source against the prebuilt cross-toolchain, so most of
the time is spent compiling `libkallisti.a` and the PC-side utils — a few
minutes on a normal machine, even with cold caches (there is no
GCC/Binutils/Newlib source build in this path; see "Toolchain provenance"
above for what to do instead if you want one).

**Building behind a TLS-inspecting proxy** (corporate network, some CI
sandboxes): if `git`/`curl`/`meson` inside the build fail with certificate
errors, point `EXTRA_CA_BUNDLE` at that proxy's CA certificate and re-run —
it's passed to the build as an ephemeral BuildKit secret (`--secret
id=extra_ca,src=...`), never written into the image or this repo:

```sh
EXTRA_CA_BUNDLE=/path/to/proxy-ca.pem docker/build.sh
```

## Compiling the game

Any normal KOS project Makefile (one that includes
`$(KOS_BASE)/Makefile.rules`, same as every example under
`examples/dreamcast/` in a KOS checkout) builds the same way: source
`environ.sh`, then `make`.

```sh
docker/compile.sh                       # compiles ./ (this repo)
docker/compile.sh path/to/other/project # or a different directory
docker/compile.sh . clean all           # custom make target(s)
```

Under the hood, that's:

```sh
docker run --rm \
    -v "$(pwd):/src" \
    -w /src \
    beelzfight-romdev \
    bash -lc 'source ${KOS_BASE}/environ.sh && make'
```

`environ.sh` lives at `/opt/toolchains/dc/kos/environ.sh` (the canonical
path used throughout the KOS docs/scene; `/opt/kos` is also symlinked there
for convenience). It sets `KOS_BASE`, `KOS_CC`/`KOS_CFLAGS`/..., and puts
`sh-elf-gcc`, `arm-eabi-gcc`, and all the PC-side tools below on `PATH`.
Interactive shells (`docker run -it beelzfight-romdev bash`) source it
automatically; non-interactive `RUN`/`docker run cmd` invocations need the
explicit `source`, as above.

## Converting a PNG sprite sheet to a PVR texture

`pvrtex` (KOS's PNG/JPEG → PVR texture converter, built and installed to
`/opt/toolchains/dc/bin/pvrtex`, already on `PATH`) is the tool to use:

```sh
docker run --rm -v "$(pwd):/src" -w /src beelzfight-romdev \
    pvrtex -i sprite.png -o sprite.dt -f RGB565
```

Run `pvrtex --help` for the full option list (compression, mipmaps, twiddling,
palette formats, etc). `kmgenc` (the older KMG-container converter) is also
available on `PATH` if a project specifically needs that format instead.

## Packaging a bootable disc image

Two ways, both validated against the `hello.elf` example this image ships
(`examples/dreamcast/hello` under the KOS checkout at `$KOS_BASE`).

### 1. `mkdcdisc` (recommended)

One command turns an ELF straight into a bootable `.cdi`, computing
IP.BIN/scrambling/padding/ECC for you:

```sh
docker run --rm -v "$(pwd):/src" -w /src beelzfight-romdev \
    mkdcdisc -e build/game.elf -d romdisc/ -n "BeelzFight" -a "Your Name" -o build/beelzfight.cdi
```

- `-e` the ELF to boot, `-d` a directory tree to add to the data track root
  (repeatable; use `-D` instead to add its *contents* without the directory
  itself), `-n`/`-a` the disc title/author shown in IP.BIN, `-o` the output
  file (format inferred from its extension: `.cdi`, `.gdi`, `.mds`, `.nrg`).
- Validated end-to-end: `mkdcdisc -e hello.elf -n "BeelzFight Hello" -a
  "BeelzFight" -o hello.cdi` produced a 740MB `.cdi` (Padus DiscJuggler
  format, MIL-CD audio+data layout, padded to full-disc size by default);
  with `-I` alongside `-o hello.cdi` it also dumps the plain data-track
  `.iso`, whose first 15 bytes read `SEGA SEGAKATANA` — the exact IP.BIN
  boot signature the Dreamcast BIOS checks for — confirmed byte-for-byte.
  The same signature was also located (at a later offset, past the MIL-CD
  audio session and CDI container header, as expected) inside the `.cdi`
  file itself.
- `mkdcdisc --help` / `docs/cli-reference.md` in its own repo
  (https://gitlab.com/simulant/mkdcdisc) covers GD-ROM (`.gdi`) output,
  CDDA audio tracks, and more disc-layout options.

### 2. Manual: scramble + makeip + genisoimage (fallback)

The classic, tool-by-tool sequence `mkdcdisc` replaces — useful if you want
to hand-build the ISO9660 tree yourself. Also validated against `hello.elf`:

```sh
# 1. ELF -> raw binary
elf2bin hello.elf hello.bin

# 2. Scramble the binary (Dreamcast's bootstrap loader requires this)
scramble hello.bin 1ST_READ.BIN

# 3. Build IP.BIN (the boot sector KOS's bootstrap reads)
mkdir -p cd_root && cp 1ST_READ.BIN cd_root/
makeip -g "BEELZFIGHT HELLO" -c "BeelzFight" -n "T-00000" IP.BIN

# 4. Wrap it all into an ISO9660 image, with IP.BIN inserted as the boot sector
genisoimage -G IP.BIN -C 0,11702 -V "BEELZHELLO" -joliet -rock -l \
    -o game.iso cd_root/
```

- `elf2bin`/`scramble`/`makeip` all come from KOS's own `utils/` and are on
  `PATH` once `environ.sh` is sourced. **`elf2bin` is not built into this
  image at the currently-pinned KOS revision** (see "KOS/toolchain version
  match" above) — use `kos-objcopy -O binary in.elf out.bin` directly
  instead (that's all `elf2bin` itself does; `kos-objcopy` is unaffected,
  it comes from the cross-toolchain, not this KOS checkout) if you need
  this manual fallback path.
- `-G IP.BIN` tells `genisoimage` to write `IP.BIN`'s contents into the
  volume's boot sector; `-C 0,11702` is the conventional
  session-offset/padding pair used for Dreamcast self-boot discs;
  `-joliet -rock -l` keep long/mixed-case filenames usable from both KOS's
  ISO9660 driver and other tools.
- Verified: the resulting `game.iso`'s first 15 bytes are `SEGA SEGAKATANA`,
  same as with `mkdcdisc`.
- This produces a plain `.iso` (bootable in Flycast/redream as-is, or
  burnable/ODE-loadable with an appropriate wrapper); it is not the
  DiscJuggler `.cdi` container format `mkdcdisc` produces directly.

## Files in this directory

| File | Purpose |
|---|---|
| `Dockerfile.romdev` | The image definition (see comments throughout for the reasoning behind each step). |
| `build.sh` | Builds the image. |
| `compile.sh` | Runs `make` for a project inside the image. |
| `gcc9-compat.h` | Compiler compatibility shim for the older prebuilt toolchain (see "Toolchain provenance" above). |

## Validation performed

Static checks (a binary that merely *looks* right):

- `sh-elf-gcc --version` / `arm-eabi-gcc --version` and a trivial
  `sh-elf-gcc -c` compile, run as part of the image build itself (so a
  broken toolchain fails the build, not silently ships).
- Full KallistiOS `kernel` + `utils` build from the pinned commit above
  (`libkallisti.a`, `kmgenc`, `scramble`, `makeip`, `bin2c`, `bin2o`,
  `genromfs`), including the ARM sound driver (`stream.drv`, built with
  `arm-eabi-gcc` during the KOS build itself).
- `examples/dreamcast/hello` (KOS's own "Hello world!" example) and this
  repo's own `beelzfight.elf` both compile and link cleanly, producing
  genuine SH4 ELF binaries (`file`-confirmed) and, via `mkdcdisc`, bootable
  `.cdi`s with the `SEGA SEGAKATANA` IP.BIN signature at the correct offset.

**These alone are not sufficient** — an earlier version of this image
passed every check above and still crashed instantly when actually run;
see "KOS/toolchain version match" above. Runtime validation, in
[Flycast](../tools/run_flycast_headless.sh) (headless, screenshot-capturing):

- **KOS's stock `hello.elf`**: boots cleanly through REIOS with no crash.
  Note: a screenshot of *stock* `hello.c` alone is black and proves
  little either way — KOS's `dbgio` console auto-selects `"scif"` (serial)
  over `"fb"` (on-screen text) by priority order, and `scif_detected()`
  unconditionally returns 1 ("we are always detected, though we might end
  up realizing there's no cable connected later") — so `printf`'s "Hello
  world!" goes out an unconnected serial port and the screen is black even
  on a fully correct boot. A one-line variant that calls
  `dbgio_dev_select("fb")` before printing (same `hello.c`, otherwise
  unmodified, built the same way) was used for an actual visual pass/fail
  signal instead, and does render "Hello world!" on screen, confirmed via
  screenshot, with no crash across several seconds (including live thread
  preemption/timer IRQs, via a `thd_sleep()` loop) — i.e. exception
  delivery, not just straight-line code, is confirmed working correctly.
- **This repo's `beelzfight.elf`**, built via `docker/compile.sh` then
  packaged via `scripts/build_cdi.sh` (`mkdcdisc`) exactly as documented in
  the main README: boots through REIOS with **no delay-slot/ISA crash**
  (the bug above is fixed), but currently still crashes about 3.5-3.7
  seconds into boot, before anything is ever drawn to screen (screenshots
  taken as early as 2s in are black; the crash consistently happens before
  the first one would show anything). The failure is a *different*, later
  kind of fault:

  ```
  E[COMMON]: Flycast has stopped: Fatal: SH4 exception when blocked
  ```

  This is Flycast refusing to take a second real CPU exception (illegal
  instruction, `expEvn=0x180`, not a delay-slot issue) while the SH4 was
  already inside another exception's handler (`SR.BL=1` — a double-fault,
  which resets real hardware; Flycast just stops instead). Instrumenting
  `Do_Exception()` in `core/hw/sh4/sh4_interrupts.cpp` to log addresses
  showed: the *outer* exception (already in progress, `BL=1`) interrupted
  code inside KOS's `scif_flush()` (i.e. a normal `printf`/dbgio flush, most
  likely preempted by KOS's ordinary timer-tick thread-switch IRQ — routine
  and expected); the *inner*, fatal one is an illegal-instruction fault at
  `epc=0x8c00e0a2` — an address **13-14 KB below `beelzfight.elf`'s own
  lowest loaded segment** (its `.text` starts at `0x8c010000`; nothing in
  the ELF maps below that). That address range is where the disc's
  scramble/bootstrap loader stub normally lives before KOS's own code takes
  over, i.e. by the time this fires, the CPU is executing through a pointer
  into stale/leftover memory that was never legitimately code at this point
  in execution — the signature of a stray/uninitialized function pointer,
  corrupted return address, or similar memory-safety bug, not a toolchain
  or KOS-version issue (KOS's own thread/interrupt machinery was just
  exercised cleanly for several seconds by the `hello.c`/`fb` test above
  with no such fault). This looks like a bug in this repo's own `src/`
  (`assets.c`/`texture.c`'s malloc/free + `pvr_mem_malloc` texture-loading
  path, and/or the main loop in `main.c`, are the most likely places to
  start — nothing else in `src/` registers custom IRQ handlers or holds
  function pointers) rather than something further toolchain/KOS work can
  fix; see the parent README/task notes for how to reproduce and iterate on
  it directly.
