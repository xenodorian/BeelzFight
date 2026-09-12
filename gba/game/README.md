# BeelzFight (GBA)

A single-level side-scrolling action game: a girl with an oversized sword
cuts through waves of imps and finishes the level against a demon boss.

## Controls

| Button   | Action                                   |
|----------|-------------------------------------------|
| D-pad    | Move left / right                        |
| A        | Light attack (fast, short reach, 1 dmg)  |
| B        | Heavy attack (slower, longer reach, 3 dmg, hits everything in range) |
| L        | Parry — destroys an incoming projectile with no damage taken |
| R (hold) | Block — negates melee contact damage while held |
| START    | Begin / retry from the title and end screens |

## Structure

- `include/game.h` — shared constants (tile/palette layout, level size) and entity structs.
- `source/main.c` — init, the title/play/win/lose state machine, camera, HUD, enemy wave spawner.
- `source/player.c` — player state machine and combat (attacks, block, parry, hit reactions).
- `source/enemy.c` — imp grunts: approach the player, contact damage, hurt/death.
- `source/boss.c` — the boss (stationary, periodic projectile attack) and its projectiles.
- `source/oam.c` — small OAM shadow-buffer helper (set/hide sprites, load tiles+palette into VRAM).
- `data/` — generated sprite/background tile data (see below) plus the source PNGs.
- `gen_assets/gen_sprites.py` — regenerates every PNG in `data/` from scratch.

## Regenerating art

The PNGs in `data/` are checked in, but if you edit `gen_assets/gen_sprites.py`:

```sh
python3 gen_assets/gen_sprites.py
```

Then reconvert to GBA tile data with `grit` (bundled in the devkitARM image;
each PNG becomes a `gfx_<name>.c`/`.h` pair with tile + 16-color-palette
data — see any existing `data/gfx_*.c` for the exact flags used):

```sh
docker run --rm -v "$PWD/data:/data" -w /data beelzfight-gbadev bash -lc '
  grit player.png -gt -gB4 -pn16 -m! -ft c -fh -o gfx_player
  # ...same pattern for enemy/boss/projectile/heart/hpseg (all -m!, sprites)
  grit bg.png -gt -gB4 -pn16 -m -mRtpf -ft c -fh -o gfx_bg   # background needs -m for the tilemap
'
```

Sprite sheet PNGs are laid out as one animation frame per row-band (frame
width = sprite width, frames stacked vertically) so grit's tile ordering
lines up with each frame's starting tile index — see `TILE_*`/`*_FRAME_TILES`
in `game.h`.

## Building

```sh
../../docker/build-gba.sh              # once, if beelzfight-gbadev isn't built yet
../../docker/compile-gba.sh .
```

Produces `beelzfight.gba` in this directory.

## Notes from QA in mGBA

Playtested headless in mGBA (both Qt and SDL frontends, under Xvfb) with
screenshots checked at each stage (title, movement, enemy waves, HUD, boss,
win/lose). Three real bugs were caught and fixed this way:

- The win/lose/title text screens didn't reset `BG_OFFSET`, so leftover
  gameplay camera scroll made the console text tear/wrap around the
  background's 256px-wide tilemap.
- `consoleDemoInit()` doesn't reset the console's cursor row across repeated
  calls, so title/win/lose text drifted down a row on every cycle; fixed by
  positioning each line with an explicit ANSI cursor-move instead of
  relative `\n`s.
- The boss's HP bar and the player's heart icons overlapped in the HUD.
- The boss always fires leftward, but the player's reachable range extended
  slightly past the boss's fixed position, so projectiles could end up
  flying away from the player after walking past it. Fixed by capping the
  player's position once the boss is engaged.
