# BeelzFight — Game Boy Advance

BeelzFight is switching target from Dreamcast to GBA (see `../docs/DESIGN.md`
and the Dreamcast-era code under `../src` for the game design this is meant
to eventually carry over — none of that code is GBA-portable as-is, since
GBA has no PVR-style texture hardware and runs on a very different CPU).

`gba/hello` is the original minimal toolchain smoke test. `gba/game` is the
actual game: a side-scrolling action game (see `gba/game/README.md`).

## Toolchain

Unlike the old Dreamcast setup (`../docker/Dockerfile.romdev`, a from-source
KallistiOS build needed to work around this sandbox's restricted network
egress), GBA development uses devkitPro's official, prebuilt `devkitARM`
Docker image directly — no custom toolchain build required. See
`../docker/Dockerfile.gbadev` for why it's still pulled through
`mirror.gcr.io` rather than `docker.io` directly.

```sh
docker/build-gba.sh                  # build the beelzfight-gbadev image (seconds)
docker/compile-gba.sh gba/hello       # compile gba/hello/ -> beelzfight_hello.gba
```

Output lands in `gba/hello/beelzfight_hello.gba` (also copied to
`docker/build/`). Load it in any GBA emulator (mGBA recommended) or on
real hardware via a flashcart.

## gba/hello

A minimal smoke test: boots into text mode via libgba's `consoleDemoInit()`
and prints a banner. Confirmed booting correctly in mGBA (both the Qt and
SDL frontends) — building this is enough to prove devkitARM + libgba are
wired up correctly before any real game code is ported over.
