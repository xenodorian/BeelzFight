# Overnight autonomous run — progress report

Branch: `claude/relaxed-rubin-u950jh` (pushed, commit `574acc3`)
Build: clean (`make` — no warnings), `crymon.cdi` rebuilt and attached.

## Done

1. **Battle enter/withdraw/faint animations.** A fresh CryMon (new battle,
   or a bench monster swapping in) rises into view over ~0.3s; a fainted
   or captured one fades to black in place. One shared blitter
   (`blit_sprite_anim`) drives both, timed off `frame_count` deltas
   tracked in `main()` — no changes needed anywhere mint_monster()/
   party[lead] assignments happen.

2. **Populated starting town.** VELD now has 4 extra houses (solid
   HH-blocks, no door, not interactive — decoration only) placed in open
   ground, verified by script to not touch any existing NPC mark, path
   tile, or collision. Also moved the Camp door off the main north-south
   corridor onto its own branching spur so it reads as a fork rather
   than a second door stacked above the Forest exit.

3. **Map-name fade banners.** Every door/warp tile used to show a
   one-line dialogue box ("THE TREES CLOSE OVER THE PATH," etc.) —
   that's gone. `do_warp()` now arms a plain text fade-in/hold/fade-out
   banner naming the destination, with no A-press required.

4. **Full story rewrite**, cohesive start to finish:
   - CryMon = Crystal Monsters is explained in the opening scene; "the
     war" is now named — the Weeping Army, occupying Crytown's front
     line, referenced consistently across NPC lines, Calder, the camp
     officer, and the cart letter.
   - Leaving the house triggers Mason's ambush conversation directly
     (no more generic flavor text first). He reveals nothing; **Max**
     reveals her plan — fight through to Shinigami (imprisoned for
     necromancy), free him, and take power from him. Mason fights her
     to *stop* her, explicitly because he thinks she can't win.
   - Cathleen's dialogue says outright that she guards Shinigami's door
     and holds its key. Beating her — not just capturing her — now
     opens the door too (new `beat_cathleen` flag). Fixed a real
     pre-existing bug in the process: closing her post-win dialogue
     left a stale `post_action` that would silently restart the fight.
   - Beating Shinigami no longer cuts instantly to an ending. He hands
     over a scroll (**Legendary Reanimation**) and vanishes from the
     map, then the story continues.
   - Anne approaches a second time afterward to tell Max her father
     died, and that the scroll can undo it.
   - A real interactive 2-option choice screen lets the player pick
     **resurrect father** or **resurrect Heavenfall**. Either path
     resolves into the same ending, which now carries a GAME OVER
     label and a "thank you for playing" close.
   - Removed the old Calder/camp-commander early "win" screen that used
     to kick the player back to the title screen mid-story — that was
     incompatible with a single continuous narrative, since the real
     story path (Forest → Grove → Shinigami) never depended on it.

## Flagged, not implemented — needs your call

**The father/Heavenfall resurrection is narrative-only.** The choice
screen, the reveal, and the ending all work, but neither resurrected
character mechanically joins the party as a real fighter with its own
sprite and moves.

Why I stopped there: this port has a hard rule from earlier in our
sessions never to fabricate new sprite art — only ever recover/reuse
the real art shipping in `xenodorian/CryMon`. I checked
`public/sprites/` there for anything usable as "Father" or "Heavenfall"
battle art and there's nothing — no father sprite at all (he's only
ever been an offscreen bed prop), and obviously no "ancient legendary
meteor CryMon" since that species doesn't exist in the source game.
Inventing pixel art for either would break that rule; reusing an
unrelated sprite (Bram's portrait for "Father," some existing monster
recolored for "Heavenfall") felt like a real enough change to the
game's content that I didn't want to make it unilaterally overnight.

Options, if you want this to be a real mechanic:
- Point me at (or approve me reusing) specific existing art for each —
  e.g. Bram's sprite for Father, a recolored/scaled existing monster
  for Heavenfall — and I'll wire them up as real `SPECIES` entries with
  battle stats and one move each (musket / meteor).
- Commission or drop in new sprite files yourself under
  `public/sprites/`, matching the existing per-species folder
  convention, and I'll run them through `gen_sprites.py`.
- Leave it narrative-only, as shipped now.

Nothing else was skipped — every other part of both messages (battle
animations, town population, map banners, path branching, and the full
dialogue audit/rewrite) is implemented, built, and pushed.
