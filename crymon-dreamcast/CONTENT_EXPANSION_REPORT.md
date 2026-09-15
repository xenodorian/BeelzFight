# Content expansion — progress report

Branch: `claude/relaxed-rubin-u950jh` (pushed, commit `828e42b`)
Build: clean (`make` — no warnings), `crymon.cdi` rebuilt and attached.

## Done, in full

- **Roof tiles.** New `r` tile (solid, its own color) caps the top row of
  every real house block: Max's own on VELD, plus the 4 decorative
  houses. Camp's H-pairs were left alone — in a military camp they read
  as tents/watchtower posts, not houses, so roofing them would've been
  a stretch. Flagging this as a judgment call, not an oversight.

- **Placeholder art pipeline + manifest.** `tools/gen_sprites.py` now
  falls back to a synthesized "missing texture" PNG (checkerboard +
  the entity's tag) for any source file that isn't in
  `xenodorian/CryMon` yet, cached under `tools/placeholder_sprites/`
  at the *exact* path real art would use — dropping a real file there
  is a straight replacement, no code changes needed. `ART_NEEDED.md`
  (repo root) is auto-written every run listing exactly what's still
  missing, with instructions aimed at Grok (or anyone) on size/style/
  format/path. `PLACEHOLDER_ART` is grep-able across `main.c` and
  `gen_sprites.py` — 12 tagged spots total.

- **8 new monsters** (Emberling, Frostail, Boulderam, Stormwing,
  Sableclaw, Thornhide, Glasswisp, Ashenmaw), each with real stats/
  moves. Shiny variants needed no extra work — `mint_shiny()`/
  `roll_shiny()` were already species-agnostic. Wild-encounterable:
  Emberling/Ashenmaw mixed into VELD's tall grass, the other 6 (plus
  Stormwing/Sableclaw/Frostail) in a new Cliffs encounter pool.

- **4 new items**: Sunbalm (+40 HP, joins the outside-battle
  heal-target flow alongside Salve/Wrap), Warroot (AGL+4, mirrors
  Bitterroot), Smoke Bomb (flees a wild battle outright), Greater
  Crystal (+25 capture chance). The mid-combat item menu now pages in
  fixed 6-row chunks, since 9 possible item types no longer fit the
  battle layout's original PASS+5 sizing — carrying one of everything
  no longer overflows the box.

- **2 new maps**: **The Cliffs** (off VELD, a real place now instead
  of just Pike's old throwaway line about them) and **The Ruins** (off
  GROVE, only reachable once Shinigami is gone — the way south was
  blocked by whatever kept him caged). Both were BFS-validated for
  reachability before shipping, same methodology as every map edit
  this whole project.

- **8 new NPCs**: Oren (merchant, reuses the existing shop UI/ITEMS
  table), Tessa/Birch/Sable (friendly, each with Weeping-Army-flavored
  exposition and a free item), and 4 Weeping Army soldiers (Cliffs, 2
  in Camp, 1 in Grove), each fielding 3 CryMon at increasing levels
  via the existing bench mechanic. Every soldier has its own opening
  line and a response from Max that digs into a different facet of
  the Weeping Army — scorched-earth tactics, forced conscription,
  plain cruelty, and (tying straight into the main plot) why they
  wanted Shinigami's necromancy in the first place.

- **Every map now has ≥3 interactables.** Camp and Grove were under
  the minimum before this pass (1 and 2 respectively) — the 2 new Camp
  soldiers and Grove's Warden Cross fix that directly; Cliffs and
  Ruins ship with exactly 3 each by design.

- **Treasure chest**, on the Cliffs (reuses the existing crate prop
  rather than needing new art) — grants marks plus a Sunbalm and a
  Greater Crystal on first open, flavor text on repeat visits.

## Judgment calls (not blockers, just flagging)

- Camp's tent/post blocks weren't roofed (see above).
- VELD's wild-encounter split (west=Glimmoth, east=Tortcask) became a
  flat 4-way random roll to fit Emberling/Ashenmaw in — the old
  geographic split was cosmetic flavor, not load-bearing, so I judged
  this a reasonable simplification rather than adding a second
  geographic axis.
- The 4 new soldiers' sprites stay on their maps after being beaten
  (dialogue changes to an aftermath line instead) — matches Calder's
  existing precedent rather than despawning, for consistency.

Nothing was skipped outright. Every part of the request — maps,
monsters, NPCs (merchant/friendly/soldiers), items, roofs, the
3-interactable minimum, the chest, and the placeholder-art manifest —
is implemented, built, and pushed.
