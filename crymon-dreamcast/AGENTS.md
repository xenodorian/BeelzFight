# CryMon — project standing orders

This file is the game-specific contract. Platform rules in `AGENTS.md` still apply.

**Canonical tree is this repo (xenodorian/CryMon), web first.**
Claude's Dreamcast work in `xenodorian/BeelzFight` is a port of this tree, not a second game.

Read [`docs/CRYMON.md`](docs/CRYMON.md) before changing story, maps, stats, or art.
Read [`CLAUDE.md`](CLAUDE.md) if you are Claude; same rules, DC-focused build steps.

## Do

- Edit **`content/*.json`** for species, items, maps, dialogue, warps, encounters, NPC marks, trainer kits.
- Edit **`public/sprites/`** for art. Do not duplicate sprite packs per port.
- Keep web playable: `src/game/` loads JSON directly. Preview must keep running.
- After JSON edits that should hit Dreamcast: `python3 tools/bake_content.py --content content --out ports/dreamcast/src`
- After sprite edits that should hit Dreamcast: `python3 ports/dreamcast/tools/gen_sprites.py`

## Do not

- Do not re-author maps, talk beats, or species tables inside `src/game/engine.ts`, `native/crymon.c`, or `ports/dreamcast/src/main.c`.
- Do not revive the SNES port. It was abandoned; do not add `.sfc` / 65816 sources.
- Do not invent a second dialogue file "just for DC". Bake from JSON (uppercase happens in the baker).
- Do not hand-edit `ports/dreamcast/src/content_*.inc` or `ports/dreamcast/src/sprites.h`.

## If you need an old DC-only version

See [`backups/dreamcast-pre-fuse/README.md`](backups/dreamcast-pre-fuse/README.md). That snapshot is reference-only.
