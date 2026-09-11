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
  `KOS_CFLAGS` (see `environ.sh` generation in the Dockerfile). It is not
  needed, and stops being included automatically, once KOS is built against
  its own from-source, KOS-patched toolchain.
- **`addons/` (optional extra libraries, e.g. `libkosext2fs`) are not
  built.** They aren't needed for core KOS, PVR/2D-sprite rendering, or
  anything in this repo, and at least one of them compiles with `-Werror`
  and turns the same missing-builtins situation the compat shim works
  around elsewhere back into a hard error for that one library specifically.
  `kernel` (`libkallisti.a`) and all of `utils` (pvrtex, kmgenc, scramble,
  makeip, ...) build and link cleanly.
- **kos-ports was not populated.** kos-ports' own build system downloads
  each port's upstream source tarball directly from that port's own site at
  build time (zlib from zlib.net, etc) — again not reachable under the same
  policy. The `kos-ports` checkout itself is present in the image (at
  `$KOS_PORTS`, cloned at a pinned commit) and `make install` works for any
  individual port the moment this image runs somewhere with normal internet
  access. This is not required for anything in this repo: the PVR API used
  for 2D sprite rendering is part of core KOS, no port needed.

Pinned revisions (see `ARG`s at the top of `Dockerfile.romdev`):

| Component  | Source                                              | Commit / version |
|------------|------------------------------------------------------|-------------------|
| KallistiOS | https://github.com/KallistiOS/KallistiOS              | `caf8fbfa8464af5701e000b47c2bb910055a3206` |
| kos-ports  | https://github.com/KallistiOS/kos-ports                | `f4faacc42faaf552625777b7709e871a827e1055` |
| mkdcdisc   | https://gitlab.com/simulant/mkdcdisc                   | `4d74e40dd2122e14389a305ed1d86dd024201389` |
| sh-elf / arm-eabi toolchain | `mirror.gcr.io/einsteinx2/dcdev-kos-toolchain:gcc-9` | GCC 9.3.0 / 8.4.0 |

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
  `PATH` once `environ.sh` is sourced.
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

- `sh-elf-gcc --version` / `arm-eabi-gcc --version` and a trivial
  `sh-elf-gcc -c` compile, run as part of the image build itself (so a
  broken toolchain fails the build, not silently ships).
- Full KallistiOS `kernel` + `utils` build from the pinned commit above
  (`libkallisti.a`, `pvrtex`, `kmgenc`, `scramble`, `makeip`, ...),
  including the ARM sound driver (`stream.drv`, built with `arm-eabi-gcc`
  during the KOS build itself).
- `examples/dreamcast/hello` (KOS's own "Hello world!" example) compiled
  inside a container from the built image via `source environ.sh && make`,
  producing `hello.elf`; confirmed with `file hello.elf`:
  `ELF 32-bit LSB executable, Renesas SH, version 1 (SYSV), statically
  linked, ...` — a genuine SH4 target binary.
- `hello.elf` packaged into a bootable disc image both ways documented
  above (`mkdcdisc` -> `.cdi`/`.iso`, and the manual
  `elf2bin`+`scramble`+`makeip`+`genisoimage` sequence -> `.iso`), with the
  `SEGA SEGAKATANA` IP.BIN signature confirmed present at the correct
  offset in every resulting image.
