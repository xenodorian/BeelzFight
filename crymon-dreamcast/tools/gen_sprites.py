#!/usr/bin/env python3
# Converts CryMon's sprite art into a C header of raw RGB565 pixel
# arrays for the bare-metal Dreamcast port's put_pixel-based
# framebuffer: the player (max, 4 walk frames per direction), the 4
# HOUSE furniture props, single standing-frame sprites for every
# world NPC (Wren, Mae, Ivo, Nell, Pike, Bram, Calder, Mason,
# soldiers, Shinigami, Cathleen), and single-frame battle art for
# every fightable species.
#
# Each sprite is downscaled with nearest-neighbor resampling to a
# fixed target size and color-keyed for transparency: any source
# pixel with alpha < 128 becomes the key color (0xF81F, magenta, one
# per sprite -- picked to not collide with that sprite's own opaque
# colors, checked below) and is skipped by the blitter at draw time.
# The source art has anti-aliased edges (alpha spans the full 0-255
# range, not just 0/255), so this hard cutoff is an approximation --
# edge pixels end up fully opaque or fully see-through, not blended.
# Good enough to verify sprites load and draw; true alpha blending
# isn't attempted here.
#
# Usage: gen_sprites.py <path-to-xenodorian/CryMon-checkout>
# Requires Pillow (pip install pillow).

import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'src', 'sprites.h')
KEY = 0xF81F  # magenta

PLAYER_DIRS = ['down', 'up', 'left', 'right']
PLAYER_FRAMES = [1, 2, 3, 4]
ACTOR_SRC_W, ACTOR_SRC_H = 48, 64
ACTOR_DST_W, ACTOR_DST_H = 24, 32

# Target sizes picked to roughly match the room's 20px tiles while
# keeping each prop's real aspect ratio (the sizes previously
# hardcoded as flat-rect placeholders in main.c did not match the
# actual art's proportions).
PROPS = {
    'bed_father': ('props/bed-father.png', 42, 40),
    'bed_empty':  ('props/bed-empty.png', 42, 40),
    'shelf':      ('props/shelf.png', 34, 31),
    'crate':      ('props/crate.png', 24, 25),
}

# name -> source PNG pattern (%d substitutes the frame number 1-4)
# relative to public/sprites, down-facing where the source has
# directions. All 4 idle frames are pulled now (drawWorld()'s
# `Math.floor(this.clock * 4) % 4 + 1` for these, `* 3` for Shinigami
# -- see main.c's draw_npcs for the per-frame timing), not just frame
# 1: these NPCs stand still but still idle-animate in the reference.
# Mason/Anne/soldier are NOT here -- they actually walk (approach/
# patrol/chase), so they get the same full 4-direction x 4-frame
# treatment as the player instead (see WALKERS below).
NPCS = {
    'wren':    'npc/wren-%d.png',
    'mae':     'npc/mae-%d.png',
    'ivo':     'npc/ivo-%d.png',
    'nell':    'npc/nell-%d.png',
    'pike':    'npc/pike-%d.png',
    'bram':    'npc/bram-%d.png',
    'calder':  'npc/calder-%d.png',
    'shinigami': 'shinigami/down-%d.png',
}
NPC_FRAMES = [1, 2, 3, 4]

# Walking actors: full walk cycle like the player, for the ones that
# actually move (Mason and Anne approach the player, soldiers patrol/
# chase -- see the world-actors section in main.c).
WALKERS = {
    'mason':   'mason',
    'anne':    'anne',
    'soldier': 'npc/soldier',
}

# Cathleen has no small walk sprite (she "fights as herself" -- her
# only art is the same battle portrait used both on the GROVE map and
# in battle), downscaled to a squarer box than the rectangular actor
# convention since the source itself is square.
CATHLEEN_WORLD_SRC = 'monsters/cathleen/1.png'
CATHLEEN_WORLD_W, CATHLEEN_WORLD_H = 28, 28

# species id -> monsters/<id>/%d.png, all 4 frames (drawBattle()'s own
# `Math.floor(b.t * 4) % 4 + 1`, shared by both the foe and the
# player's own sprite -- see main.c's draw_battle_sprites). Sizes vary
# per species in the source art (112x91 up to 200x200); all downscaled
# to one fixed battle-sprite box for a consistent battle-screen
# layout, accepting minor aspect squish on the non-square ones.
MONSTERS = [
    'quillpup', 'glimmoth', 'tortcask', 'razorbat', 'mossback',
    'briarfox', 'fenwisp', 'duskhorn', 'needleroot', 'cathleen', 'crymare',
]
MONSTER_FRAMES = [1, 2, 3, 4]
MONSTER_W, MONSTER_H = 56, 56

# render.lua's drawBattle() draws this (sprites.lua's "bg" key) behind
# everything else, full-screen, before the status boxes and menu; the
# reference's own fallback when it's missing is a flat fill, which is
# what this port's own battle screen did before this asset was wired
# in. No transparency in the source (plain RGB), so no color key
# needed -- every pixel is opaque.
BATTLE_BG_SRC = 'battle-bg.png'
BATTLE_BG_W, BATTLE_BG_H = 320, 240

# Bag/shop/battle item-menu icons, one per data.ITEMS entry (id ->
# items/<id>.png), downscaled to a small square that fits next to a
# MENU_ROW_H=16 text row.
ITEM_ICONS = ['salve', 'bandage', 'bitterroot', 'dust', 'gem']
ITEM_ICON_W, ITEM_ICON_H = 14, 14

# Dialogue-box character portraits (public/sprites/portraits/<name>.png,
# source art 160x200 to 225x225 depending on character), downscaled to
# a small column that fits next to the wrapped text -- see
# draw_dialogue_box()/PORTRAIT_W/PORTRAIT_H in main.c. Only the
# speakers TALK's beats actually use (SPK_* in main.c) are pulled;
# the reference has portraits for every battle species too
# (port-quillpup etc, shown on the battle-intro screen this port
# doesn't have), not needed here.
PORTRAITS = [
    'max', 'anne', 'mason', 'wren', 'mae', 'ivo', 'nell', 'pike',
    'calder', 'bram', 'cathleen', 'shinigami',
]
PORTRAIT_W, PORTRAIT_H = 32, 40

def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def encode(im, dst_w, dst_h):
    im = im.convert('RGBA').resize((dst_w, dst_h), Image.NEAREST)
    px = im.load()
    out = []
    for y in range(dst_h):
        for x in range(dst_w):
            r, g, b, a = px[x, y]
            if a < 128:
                out.append(KEY)
            else:
                v = rgb565(r, g, b)
                if v == KEY:
                    v ^= 0x0001  # nudge off the key color, imperceptible
                out.append(v)
    return out

def emit_array(lines, name, pixels, w, h):
    lines.append('static const unsigned short %s[%d * %d] = {' % (name, w, h))
    for row in range(h):
        vals = pixels[row * w:(row + 1) * w]
        lines.append('    ' + ', '.join('0x%04X' % v for v in vals) + ',')
    lines.append('};')
    lines.append('')

def main():
    if len(sys.argv) != 2:
        sys.exit('usage: gen_sprites.py <path-to-xenodorian/CryMon-checkout>')
    root = os.path.join(sys.argv[1], 'public', 'sprites')

    lines = []
    lines.append('/* Generated by tools/gen_sprites.py from xenodorian/CryMon')
    lines.append(' * public/sprites/ PNGs. Do not hand-edit -- regenerate from')
    lines.append(' * source art instead. */')
    lines.append('')
    lines.append('#define SPRITE_KEY 0x%04X' % KEY)
    lines.append('')

    lines.append('#define MAX_SPRITE_W %d' % ACTOR_DST_W)
    lines.append('#define MAX_SPRITE_H %d' % ACTOR_DST_H)
    lines.append('')
    for d in PLAYER_DIRS:
        for f in PLAYER_FRAMES:
            im = Image.open(os.path.join(root, 'max', '%s-%d.png' % (d, f)))
            assert im.size == (ACTOR_SRC_W, ACTOR_SRC_H), (d, f, im.size)
            pixels = encode(im, ACTOR_DST_W, ACTOR_DST_H)
            emit_array(lines, 'max_%s_%d' % (d, f), pixels, ACTOR_DST_W, ACTOR_DST_H)

    for name, (relpath, w, h) in PROPS.items():
        im = Image.open(os.path.join(root, relpath))
        pixels = encode(im, w, h)
        lines.append('#define PROP_%s_W %d' % (name.upper(), w))
        lines.append('#define PROP_%s_H %d' % (name.upper(), h))
        emit_array(lines, 'prop_%s' % name, pixels, w, h)

    lines.append('#define NPC_SPRITE_W %d' % ACTOR_DST_W)
    lines.append('#define NPC_SPRITE_H %d' % ACTOR_DST_H)
    lines.append('')
    for name, pattern in NPCS.items():
        for f in NPC_FRAMES:
            im = Image.open(os.path.join(root, pattern % f))
            pixels = encode(im, ACTOR_DST_W, ACTOR_DST_H)
            emit_array(lines, 'npc_%s_%d' % (name, f), pixels, ACTOR_DST_W, ACTOR_DST_H)

    for name, reldir in WALKERS.items():
        for d in PLAYER_DIRS:
            for f in PLAYER_FRAMES:
                im = Image.open(os.path.join(root, reldir, '%s-%d.png' % (d, f)))
                assert im.size == (ACTOR_SRC_W, ACTOR_SRC_H), (name, d, f, im.size)
                pixels = encode(im, ACTOR_DST_W, ACTOR_DST_H)
                emit_array(lines, 'npc_%s_%s_%d' % (name, d, f), pixels, ACTOR_DST_W, ACTOR_DST_H)

    lines.append('#define CATHLEEN_WORLD_W %d' % CATHLEEN_WORLD_W)
    lines.append('#define CATHLEEN_WORLD_H %d' % CATHLEEN_WORLD_H)
    im = Image.open(os.path.join(root, CATHLEEN_WORLD_SRC))
    pixels = encode(im, CATHLEEN_WORLD_W, CATHLEEN_WORLD_H)
    emit_array(lines, 'npc_cathleen', pixels, CATHLEEN_WORLD_W, CATHLEEN_WORLD_H)

    lines.append('#define MONSTER_SPRITE_W %d' % MONSTER_W)
    lines.append('#define MONSTER_SPRITE_H %d' % MONSTER_H)
    lines.append('')
    for name in MONSTERS:
        for f in MONSTER_FRAMES:
            im = Image.open(os.path.join(root, 'monsters', name, '%d.png' % f))
            pixels = encode(im, MONSTER_W, MONSTER_H)
            emit_array(lines, 'monster_%s_%d' % (name, f), pixels, MONSTER_W, MONSTER_H)

    lines.append('#define BATTLE_BG_W %d' % BATTLE_BG_W)
    lines.append('#define BATTLE_BG_H %d' % BATTLE_BG_H)
    im = Image.open(os.path.join(root, BATTLE_BG_SRC))
    pixels = encode(im, BATTLE_BG_W, BATTLE_BG_H)
    emit_array(lines, 'battle_bg', pixels, BATTLE_BG_W, BATTLE_BG_H)

    lines.append('#define ITEM_ICON_W %d' % ITEM_ICON_W)
    lines.append('#define ITEM_ICON_H %d' % ITEM_ICON_H)
    lines.append('')
    for name in ITEM_ICONS:
        im = Image.open(os.path.join(root, 'items', '%s.png' % name))
        pixels = encode(im, ITEM_ICON_W, ITEM_ICON_H)
        emit_array(lines, 'icon_%s' % name, pixels, ITEM_ICON_W, ITEM_ICON_H)

    lines.append('#define PORTRAIT_SPRITE_W %d' % PORTRAIT_W)
    lines.append('#define PORTRAIT_SPRITE_H %d' % PORTRAIT_H)
    lines.append('')
    for name in PORTRAITS:
        im = Image.open(os.path.join(root, 'portraits', '%s.png' % name))
        pixels = encode(im, PORTRAIT_W, PORTRAIT_H)
        emit_array(lines, 'port_%s' % name, pixels, PORTRAIT_W, PORTRAIT_H)

    with open(OUT, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote', OUT)

if __name__ == '__main__':
    main()
