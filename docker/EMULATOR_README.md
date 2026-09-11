# Headless Flycast emulator for visual QA

This documents how the Dreamcast emulator used for automated visual QA
(screenshotting a running build to check for missing backgrounds, broken
sprites, flickering text, etc.) was set up in this container, exactly how
to redo it from scratch, and how to invoke it once a real disc image exists.

**Emulator used: [Flycast](https://github.com/flyinghead/flycast), built from source.**
redream was not used — see "Why not redream" below.

## TL;DR — once a build exists

```
tools/run_flycast_headless.sh build/beelzfight.cdi
```

Screenshots land in `build/qa_screenshots/shot_01.png` … `shot_04.png`.
See "Using the wrapper script" below for all the options (ELF loading,
forcing BIOS-less HLE boot, timing, resolution, etc).

## Status: proven working, end-to-end, in this container

- Flycast builds cleanly from source with OpenGL-only (no Vulkan), targeting
  desktop Linux.
- It runs under Xvfb with Mesa's `llvmpipe` software OpenGL renderer (there
  is no `/dev/dri` / GPU in this container) — confirmed `OpenGL version 4.5
  (Core Profile) Mesa 25.2.8`, `direct rendering: Yes`.
- `tools/run_flycast_headless.sh` launches it, waits for boot, and captures
  PNG screenshots via ImageMagick's `import -window root`.
- Verified non-blank output: with no content loaded, the captured
  1280x720 screenshot clearly shows Flycast's own frontend — a dark-themed
  "GAMES" browser panel with a search/Filter bar, a "Settings" button
  (gear icon), and centered text "Your game list is empty" above a blue
  "Add Game Folder" button, over a black backdrop. This is not a black or
  garbage frame — real GL-rendered UI (text, buttons, borders) is visible,
  proving the Xvfb → GL(llvmpipe) → SDL2 → ImageMagick capture chain works.
- The wrapper script exits cleanly (exit code 0), kills flycast and Xvfb,
  and has a hard wall-clock timeout (`timeout -k 10s`) so it can never hang
  a CI job even if the emulator or X server wedges.

## BIOS requirement — no copyrighted BIOS file was sourced or used

No Dreamcast BIOS ROM was downloaded, copied, or created for this task, and
none is needed to prove or use this pipeline:

- **Flycast's own frontend/menu UI needs no BIOS at all.** It's just the
  SDL/imgui game browser; this is what the validated screenshot above
  shows.
- **Homebrew `.elf` files need no BIOS either.** Flycast's own command-line
  parser (`core/cfg/cl.cpp`, function `parseCommandLine`) special-cases the
  `.elf` extension: passing an ELF automatically sets the transient config
  `bios.UseReios=yes`, which switches Flycast to its **reios** HLE (high
  level emulation) BIOS — a from-scratch reimplementation of the Dreamcast
  boot ROM's behavior, not a copy of Sega's firmware. This is the
  documented, legal, no-piracy path for booting self-built homebrew, and is
  exactly what `docs/` in the upstream repo and its wiki describe.
- **Real `.cdi`/`.gdi`/`.chd`/`.cue` disc images default to requiring a
  real BIOS file** (`dc_boot.bin` / `dc_flash.bin` in Flycast's data dir),
  same as any accurate Dreamcast emulator, because those formats don't
  carry an ELF entrypoint Flycast can HLE-boot on its own by default.
  You can still force reios/HLE-boot for a disc image (compatibility varies
  — reios is primarily tuned for homebrew, so test it on the actual build
  once it exists):
  ```
  tools/run_flycast_headless.sh build/beelzfight.cdi -- -config config:UseReios=yes
  ```
  If that doesn't boot cleanly for a particular build, the alternative is
  to point the QA script directly at the pre-disc `.elf` your toolchain
  produces (before `mkdcdisc`/GDI-building), since that path is
  auto-HLE'd and has the broadest homebrew compatibility. **We did not
  source a real BIOS file and are not recommending doing so** — that would
  require a copyrighted Sega ROM dump, which is explicitly out of scope
  here.

## Why not redream

`redream.io` (and its `.io` domain generally) is blocked by this
container's outbound network policy (`403` at the proxy — see "Network
notes" below), so a prebuilt redream binary could not even be downloaded to
evaluate it. This was moot in practice: Flycast built and ran successfully,
so no fallback was needed.

## Why build from source instead of a prebuilt release

The plan was to prefer a prebuilt Linux AppImage/binary from Flycast's
GitHub Releases. That turned out to be unreachable from this container:

- `github.com/<owner>/<repo>/releases`, `api.github.com`,
  `codeload.github.com`, `objects.githubusercontent.com`, and
  `flyinghead.github.io` (the project's own "builds page") all return
  `403` — blocked by this environment's outbound proxy/GitHub-access
  policy (these hosts require the session's repo to be "attached" via a
  different mechanism than plain HTTPS browsing allows, or are not
  reachable at all).
- What **does** work anonymously: `git clone`/`fetch` of public repos over
  `https://github.com/<owner>/<repo>.git` (a dedicated "git read" lane in
  this container's proxy), and `raw.githubusercontent.com`.
- Flathub/Flatpak (`flathub.org`) is also blocked, ruling out the Linux
  Flatpak distribution route mentioned in Flycast's README.

Given that, building from source (which only needs `git clone` + the
Ubuntu package mirror, both available) was the only viable path — and it
worked without much trouble once the correct GitHub org was found
(**`flyinghead/flycast`**, not `flycast/flycast` as named in the task —
that name 404s; flyinghead is the actual/current maintainer's org).

## Exact rebuild steps (from a clean container)

### 1. System packages

```bash
apt-get update
apt-get install -y \
  build-essential cmake pkg-config git \
  libcurl4-openssl-dev libudev-dev libsdl2-dev \
  libgl1-mesa-dev libglu1-mesa-dev \
  libfreetype-dev libusb-1.0-0-dev \
  libasound2-dev libpulse-dev liblua5.4-dev \
  libzip-dev zlib1g-dev libminiupnpc-dev \
  xvfb imagemagick ffmpeg libsdl2-2.0-0 mesa-utils libosmesa6 \
  alsa-utils libasound2t64 pulseaudio-utils x11-utils
```

`xvfb`/`x11-utils` (for `Xvfb`/`xdpyinfo`) give the headless X display;
`libgl1-mesa-dev`/`libgl1-mesa-dri` give Mesa's `llvmpipe` software GL
renderer (there's no GPU/`/dev/dri` here); `imagemagick` gives `import` for
screenshotting; the rest are Flycast's documented Linux build deps
(`libcurl`, `libudev`, `SDL2`) plus what its CMake script additionally
needs once Vulkan/Discord/breakpad/tests are turned off (freetype, Lua,
ALSA/PulseAudio, libusb for its DreamLink/DreamPicoPort controller support,
libzip, miniupnpc for its netplay UPnP code).

### 2. Clone Flycast and its required submodules

```bash
git clone https://github.com/flyinghead/flycast /home/user/flyinghead/flycast
cd /home/user/flyinghead/flycast

# Only the submodules actually needed for a Linux/OpenGL/no-Vulkan/
# no-breakpad/no-discord/no-ctest desktop build (skips SDL — we use the
# system libsdl2-dev instead — and skips Vulkan-Headers/VMA/glslang/
# libadrenotools/oboe/Syphon/Spout/discord-rpc/googletest, none of which
# this configuration needs):
git submodule update --init --depth 1 -- \
  core/deps/libchdr core/deps/luabridge core/deps/rcheevos \
  core/deps/asio core/deps/xbyak core/deps/libjuice \
  core/deps/websocketpp core/deps/DreamPicoPort-API core/deps/tinygettext

# DreamPicoPort-API and tinygettext each pull in one more nested submodule:
git -C core/deps/DreamPicoPort-API submodule update --init --depth 1 -- ext/libusb-cmake
git -C core/deps/tinygettext submodule update --init --depth 1 -- external/tinycmmc
```

**freetype is a special case.** Flycast pins `core/deps/freetype` to a
submodule hosted on `gitlab.freedesktop.org`, which is blocked by this
container's network policy. Freetype has an official GitHub mirror that
*is* reachable, so clone that instead and check out the exact commit
Flycast's tree pins (verify with
`git ls-tree HEAD core/deps/freetype`; it was
`0a0221a1347e2f1e07c395263540026e9a0aa7c7` — "Version 2.14.3" — at the time
of writing):

```bash
rmdir core/deps/freetype  # empty submodule placeholder
git clone https://github.com/freetype/freetype.git core/deps/freetype
git -C core/deps/freetype checkout <commit-from-git-ls-tree-above>
```

### 3. Configure and build

```bash
mkdir -p build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_VULKAN=OFF -DUSE_DX9=OFF -DUSE_DX11=OFF \
  -DUSE_BREAKPAD=OFF -DUSE_DISCORD=OFF -DENABLE_CTEST=OFF \
  -DUSE_HOST_SDL=ON -DUSE_HOST_LIBZIP=ON -DUSE_HOST_LIBCHDR=OFF
make -j"$(nproc)"
```

This produced `build/flycast` (~27 MB, dynamically linked, no missing
libs per `ldd`) in a few minutes on 4 cores. Vulkan/DX9/DX11/breakpad/
Discord/tests are all off because: there's no GPU here so Vulkan buys
nothing over the OpenGL/llvmpipe path already proven to work; DX9/DX11 are
Windows-only anyway; breakpad (crash reporting) and Discord presence add
build cost/dependencies with zero QA value; unit tests aren't needed to
run the emulator.

### 4. Install it somewhere stable

```bash
cp build/flycast /usr/local/bin/flycast
```

`tools/run_flycast_headless.sh` looks for `flycast` on `PATH`, then
`/usr/local/bin/flycast`, then the dev build tree above, in that order (or
respects `FLYCAST_BIN=/exact/path` if set).

**Important — this install does not persist across a fresh container.**
Per this task's scope, nothing was added to `docker/Dockerfile.romdev` or
any other file under `docker/`/`scripts/` — those belong to the game build
pipeline being worked on in parallel. That means the Flycast source
checkout (`/home/user/flyinghead/flycast`, ~350 MB) and the installed
binary (`/usr/local/bin/flycast`) live only in *this* container instance.
**If QA needs to run in a new/rebuilt container, redo steps 1–4 above**
(all of it is scripted above and took a few minutes total). If this
becomes a recurring need, the natural fix is for whoever owns `docker/` to
bake an equivalent build (or these same steps) into a QA-specific
image/layer — intentionally left undone here since it's outside this
task's allowed paths.

## Using the wrapper script

`tools/run_flycast_headless.sh` — see the comment header in the script
itself for the full reference; summary:

```
tools/run_flycast_headless.sh [content-path] [-- extra flycast args...]
```

- `content-path` — a `.cdi`/`.gdi`/`.chd`/`.cue` disc image or a homebrew
  `.elf`. **Optional**: omit it to just boot Flycast's own game-browser UI
  (this is what was used to validate the pipeline — see below).
- Anything after a literal `--` is forwarded verbatim to the `flycast`
  binary, e.g. to force BIOS-less HLE boot on a disc image:
  `tools/run_flycast_headless.sh build/beelzfight.cdi -- -config config:UseReios=yes`

It starts (or reuses) an Xvfb X display, launches flycast against it with
`SDL_VIDEODRIVER=x11` and `SDL_AUDIODRIVER=dummy` (no real audio device
needed/used), waits `BOOT_WAIT_SECS` (default 6s) for boot, then captures
`SHOT_COUNT` (default 4) screenshots `SHOT_INTERVAL_SECS` (default 3s)
apart into `build/qa_screenshots/shot_01.png …`, and tears everything down
(kills flycast, kills Xvfb if it started it, exit code 0). The whole
invocation is wrapped in `timeout -k 10s $HARD_TIMEOUT_SECS` (default
120s) as a hard safety net so it can never hang a CI job.

Key environment overrides (all optional): `FLYCAST_BIN`, `DISPLAY_NUM`,
`SCREEN_RES`, `BOOT_WAIT_SECS`, `SHOT_COUNT`, `SHOT_INTERVAL_SECS`,
`HARD_TIMEOUT_SECS`, `OUT_DIR`. Exit codes: `0` success, `2` flycast binary
not found, `3` Xvfb didn't come up, `4` flycast died immediately, `5` no
screenshots captured.

## Pipeline validation actually performed

```
tools/run_flycast_headless.sh
```

(no content — proves the capture pipeline itself, per the task's own
guidance that Flycast's built-in menu with nothing loaded is sufficient).

Result: exit code `0`, four 1280x720 PNGs in `build/qa_screenshots/`
(`flycast_stdout.log` alongside them has flycast's own log — confirms
`OpenGL version 4.5`, `Vendor 'Mesa' Renderer 'llvmpipe...'`,
`glBlitFramebuffer test successful`, no errors/crashes). Screenshot content
described above under "Status" — a real, legible, GL-rendered UI, not a
black or corrupt frame.

## Network notes for whoever revisits this

This container's outbound HTTPS goes through a policy-enforcing proxy.
Observed during this task:

- **Works anonymously, no setup needed:** `git clone`/`fetch` of *any*
  public GitHub repo over HTTPS, and `raw.githubusercontent.com`.
- **Blocked (`403`):** `github.com/<owner>/<repo>` web paths like
  `/releases`, `api.github.com`, `codeload.github.com`,
  `objects.githubusercontent.com`, any `*.github.io` page,
  `gitlab.freedesktop.org`, `flathub.org`, `redream.io`.
- The Ubuntu package mirrors (`archive.ubuntu.com`,
  `security.ubuntu.com`) work fine; some third-party PPAs
  (`ppa.launchpadcontent.net`) are blocked.

Net effect: anything shippable as "clone the source and build it" works
here; anything that requires fetching a pre-built release asset, browsing
a project's GitHub Pages site, or hitting a non-GitHub project download
domain generally does not, and needs a GitHub-mirror or apt-package
workaround (as done above for freetype).
