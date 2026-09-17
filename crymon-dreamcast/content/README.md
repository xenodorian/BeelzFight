# CryMon shared content

**Single source of truth** for story, maps, species, items, warps,
encounters, trainer kits, and NPC marks. Web loads these files.
Dreamcast bakes them to `ports/dreamcast/src/content_*.inc`.

Full build rules: [`../docs/CRYMON.md`](../docs/CRYMON.md)

| File | Owns |
|---|---|
| `species.json` | CryMon stats, moves, spells |
| `items.json` | Bag/shop defs and display order |
| `maps.json` | ASCII maps, solid tiles, tile art keys |
| `dialogue.json` | Speakers, intro, ending, every talk beat |
| `world.json` | Start bag, map names, warps, wild pools, trainers, NPC marks, formulas |

Edit JSON here. Do not duplicate tables in `engine.ts` or `main.c`.
