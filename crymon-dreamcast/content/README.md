# CryMon shared content

This folder is the **single source of truth** for story, maps, species,
items, warps, and encounters. Edit JSON here — do not duplicate tables
in the web engine or the Dreamcast C port.

| File | Owns |
|---|---|
| `species.json` | CryMon stats, moves, spells |
| `items.json` | Bag/shop defs and display order |
| `maps.json` | ASCII maps, solid tiles, tile art keys |
| `dialogue.json` | Speakers, intro, ending, every talk beat |
| `world.json` | Start bag, map names, warps, wild pools, trainer kits, combat formulas |

## Web

`src/game/data.ts` loads these files at build time. Change a line of
dialogue here and the preview picks it up on refresh.

## Dreamcast

```
python3 tools/bake_content.py
```

writes `src/content_*.inc` (uppercase, folded punctuation for the
bitmap font). `make` depends on those includes. Rebuild the CDI after
baking.

Runtime loops, rendering, and input stay native on each platform.
Battle formulas in `world.json` `formulas` are the contract both
engines implement.
