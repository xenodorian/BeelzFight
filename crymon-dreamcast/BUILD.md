# CryMon — shared source, two ports

Web is the base. Dreamcast consumes the same content and the same sprites.
Do not keep a second copy of the game in C tables.

```
content/                  JSON source of truth
public/sprites/           art source of truth
src/game/                 web engine (loads JSON)
ports/dreamcast/          Dreamcast runtime (bakes JSON + sprites to C)
backups/                  frozen snapshots of removed ports
```

Repos:

- **xenodorian/CryMon** — canonical. This tree.
- **xenodorian/BeelzFight** `crymon-dreamcast/` — DC working copy. Must not
  drift from CryMon `content/` + `public/sprites/`.

## What is shared vs what is not

**Shared (edit once):**

- `content/species.json` — stats, moves, spells
- `content/items.json` — bag/shop
- `content/maps.json` — ASCII maps, solid tiles, tile art keys
- `content/dialogue.json` — every talk beat, intro, ending, speaker names
- `content/world.json` — start bag, map names, warps, encounters, trainer kits, NPC marks, formulas
- `public/sprites/` — walk cycles, portraits, monsters, items, props

**Per port (do not try to unify):**

- Rendering (canvas vs PVR framebuffer)
- Input (keyboard / touch vs Maple)
- Audio
- The battle/menu state machine implementation (same formulas, different code)

Formulas in `world.json` `formulas` are the contract. Ports implement them;
they do not invent a second capture/XP rule.

## Web build

```
npm run dev        # 0.0.0.0:8080
npm run typecheck
npm run build
```

`src/game/data.ts` imports the JSON. `src/game/engine.ts` is the loop.
Change dialogue → save JSON → preview refreshes.

## Dreamcast build

Toolchain: `sh-elf-gcc`, `mkdcdisc`.

```
cd ports/dreamcast
python3 ../../tools/bake_content.py --content ../../content --out src
python3 tools/gen_sprites.py ../../public/sprites
make
make cdi
```

Baker uppercases talk text and folds punctuation for the 8×8 bitmap font.
That is a display transform, not a second script.

`src/content_*.inc` and `src/sprites.h` are generated. Never author them.

## Workflow for a content change

1. Edit `content/*.json` and/or `public/sprites/`.
2. Confirm web preview.
3. Bake DC includes (and sprites.h if art changed).
4. Rebuild DC only if you need a new CDI this turn.
5. If you are on BeelzFight, copy the JSON back to CryMon the same turn.

## Abandoned

- SNES / `.sfc` / 65816. Do not restore.
- LÖVE2D (`love/`). Do not restore. Lua snapshot is in `backups/love-port/`.
- SDL2 / R36S PortMaster (`native/`). Do not restore. C snapshot is in `backups/sdl-port/`.

## Backups

`backups/dreamcast-pre-fuse/` holds the last DC `main.c` with hardcoded
tables, Claude's overnight reports, the old `gen_sprites.py`, and a tar
of `art/` + placeholder sprites. Use it to remember a DC-only quirk.
Do not copy those tables forward.

`backups/love-port/` holds the Lua sources of the removed LÖVE port.
Reference only.

`backups/sdl-port/` holds the C sources of the removed SDL2/R36S port.
Reference only.
