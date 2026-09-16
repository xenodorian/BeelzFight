# Art needed

All previously missing unique art is now filled. `tools/gen_sprites.py`
loads real PNGs from `crymon-dreamcast/art/sprites/` first, then from a
`xenodorian/CryMon` checkout. Re-run the generator after dropping in
replacements; this file is only a status note now.

## Filled (was PLACEHOLDER_ART)

| Entity | Kind |
|---|---|
| Oren, Tessa, Birch, Sable | world idle 24×32 (4 frames) + portraits |
| Emberling, Frostail, Boulderam, Stormwing, Sableclaw, Thornhide, Glasswisp, Ashenmaw | battle 92×92 (4 frames) |
| Sunbalm, Warroot, Smoke Bomb, Greater Crystal | item icons 14×14 |

## Also filled (was unmarked / shared art)

| Entity | Kind | Notes |
|---|---|---|
| Warden Cross | unique idle + portrait | Grove mark `K` — no longer generic soldier |
| Camp commander | unique idle + portrait | Camp mark `I` |
| Conscript | unique idle + portrait | Camp mark `K` |
| Enforcer | unique idle + portrait | Camp mark `A` |
| Cliffs sentry | unique idle + portrait | Cliffs mark `V` |
| Father | portrait + standing idle | dialogue uses `SPK_FATHER` |
| Heavenfall | portrait + battle 4-frame + `SP_HEAVENFALL` | choice / legendary sprite |

Forest patrol soldiers still share the original generic Weeping Army walk cycle on purpose.
