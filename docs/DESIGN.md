# BeelzFight — Design Notes

Sega Dreamcast homebrew, side-scrolling action game built on KallistiOS (KOS).

## Premise
A small girl wielding a sword far too large for her fights her way through
waves of weak demons and closes each level against a boss.

## Technical targets
- Platform: Sega Dreamcast (SH4 CPU, PowerVR2 GPU)
- Video mode: 640x480 VGA, progressive scan ("480p"), RGB565
- Toolchain: KallistiOS (KOS) + sh4-elf / arm-eabi cross compilers
- Rendering: PowerVR PVR 2D sprite/quad list, hardware alpha blending
- Output: bootable .cdi (GD-ROM image), runnable in Flycast/redream

## Controls
| Input          | Action                         |
|----------------|---------------------------------|
| D-Pad / Analog | Move left/right, crouch          |
| A              | Attack 1 — horizontal slash      |
| B              | Attack 2 — overhead slash        |
| X              | Attack 3 — thrust                |
| Y              | Attack 4 — spin/heavy finisher   |
| L Trigger      | Parry (short window, punishes)   |
| R Trigger      | Block (reduces/nullifies damage) |
| Start          | Pause                            |

Attacks can be chained into short combos (A->B->X->Y) with a combo-window
timer; missing the window resets to attack 1.

## Entities
- **Player**: health, stamina (blocking drains it, parry does not), combo
  state, i-frames on successful parry.
- **Weak enemy (Imp)**: low HP, walks toward player, short melee swipe.
- **Weak enemy (Thrall)**: low HP, lunges in short bursts.
- **Boss (end of level)**: multi-phase, telegraphed attacks (wide sweep,
  slam, projectile toss), parryable windows on specific attacks.

## Level flow
1. Intro walk-in, first wave of Imps spawns when player crosses trigger X.
2. Second wave (mixed Imps + Thralls) further along.
3. Arena gate closes, boss spawns, 2-phase fight.
4. Boss defeated -> level clear screen.

## Asset pipeline
1. `scripts/gen_sprites.py` (Pillow) procedurally paints pixel-art sprite
   sheets (no external network asset service used — this environment has
   no image-generation API available) for player, enemies, boss, and
   parallax background layers. Output: PNG, indexed where useful.
2. `tools/` KOS texture conversion (kmgenc / pvrtex) turns PNGs into `.dt`
   / `.pvr` textures baked into `romdisc/`.
3. `romdisc/` is packaged into the CD image via KOS's `mkdcdisc` (or
   scramble + genisoimage fallback) as `1ST_READ.BIN` + IP.BIN + data.
