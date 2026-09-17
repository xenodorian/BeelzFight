# CryMon — instructions for Claude

You are working on **CryMon**. The **web tree in xenodorian/CryMon is the game.**
The Dreamcast port is `ports/dreamcast/` in that repo (mirrored under
`crymon-dreamcast/` on `xenodorian/BeelzFight` when needed).

Full contract: [`docs/CRYMON.md`](docs/CRYMON.md)

## Source of truth (edit these)

| What | Where |
|---|---|
| Maps, species, items, talk, warps, wild pools, trainers, NPC marks | `content/*.json` |
| Pixel art | `public/sprites/` |
| Web engine / UI | `src/game/`, `src/components/gemwar-app.tsx` |
| Dreamcast runtime (video, Maple, battle loop) | `ports/dreamcast/src/main.c` |
| Dreamcast sprite bake | `ports/dreamcast/tools/gen_sprites.py` |
| Content bake (JSON → C) | `tools/bake_content.py` |

If JSON and `main.c` disagree, **JSON wins.** Re-bake. Do not patch the `.inc` files.

## Build web

From the CryMon repo root (this workspace):

```
npm run dev          # preview on 0.0.0.0:8080 — leave it running
npm run typecheck
```

`src/game/data.ts` imports `content/*.json`. No bake step.

## Build Dreamcast

From `ports/dreamcast/` (or `crymon-dreamcast/` on BeelzFight):

```
python3 ../../tools/bake_content.py --content ../../content --out src
python3 tools/gen_sprites.py          # uses ../../public/sprites, or $CRYMON_SPRITES
make                                  # needs sh-elf-gcc
make cdi                              # needs mkdcdisc
```

On BeelzFight, if this folder is `crymon-dreamcast/` at repo root:

```
python3 tools/bake_content.py --content content --out src
CRYMON_SPRITES=/path/to/CryMon/public/sprites python3 tools/gen_sprites.py
make
```

If CryMon sprites are missing, leftover `art/sprites/` is a fallback only.

## Standing rules

1. **Do not fork the story.** Weeping Army, Shinigami scroll, father/Heavenfall choice, Anne crystals ×5, Mason leave after battle, map-name banners (not dialogue on warp).
2. **Heavenfall / father resurrection is narrative-only** unless the user asks to make them party members.
3. **Do not touch Max's walk cycle** unless the user names Max.
4. **No SNES port.** Abandoned.
5. **Placeholders:** if art is missing, tag `PLACEHOLDER_ART` and keep going. Do not silently reuse another character's sprite as a stand-in for a named NPC.
6. Pre-fusion DC sources (hardcoded tables, old art pack) live in CryMon `backups/dreamcast-pre-fuse/`. Reference only.

## When you finish a DC change

- If you changed story/maps/stats: put it in `content/*.json` first, bake, then any `main.c` logic.
- Sync JSON back to CryMon if you edited a BeelzFight copy, or the web preview will drift.
