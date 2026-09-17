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

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'src', 'sprites.h')
KEY = 0xF81F  # magenta

# ----------------------------------------------------------------------
# Placeholder art: any entity below tagged PLACEHOLDER_ART (grep for it)
# has no real source art in xenodorian/CryMon yet. Rather than fail the
# build, open_or_placeholder() synthesizes a "missing texture" PNG (a
# magenta/black checkerboard with the entity's short tag stamped on it)
# on first run and caches it under PLACEHOLDER_DIR, at the exact
# relative path real art would use -- so dropping a real PNG in at that
# same path (in the CryMon checkout) is a straight replacement, no code
# changes needed. Every placeholder actually used in a given run is
# collected into `manifest` and written out as ART_NEEDED.md at the end,
# so the manifest can never drift out of sync with what's actually
# missing.
# ----------------------------------------------------------------------
PLACEHOLDER_DIR = os.path.join(HERE, 'placeholder_sprites')


def _placeholder_font(size):
    try:
        return ImageFont.truetype(
            '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', size)
    except Exception:
        return ImageFont.load_default()


def make_placeholder(w, h, tag):
    im = Image.new('RGBA', (w, h), (0, 0, 0, 255))
    d = ImageDraw.Draw(im)
    cell = max(4, min(w, h) // 8)
    for y in range(0, h, cell):
        for x in range(0, w, cell):
            if ((x // cell) + (y // cell)) % 2 == 0:
                d.rectangle([x, y, x + cell - 1, y + cell - 1], fill=(255, 0, 255, 255))
    d.rectangle([0, 0, w - 1, h - 1], outline=(255, 255, 0, 255), width=2)
    font = _placeholder_font(max(8, min(w, h) // 6))
    text = tag.upper()
    try:
        bbox = d.textbbox((0, 0), text, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    except Exception:
        tw, th = d.textsize(text, font=font)
    tx, ty = max(0, (w - tw) // 2), max(0, (h - th) // 2)
    d.rectangle([tx - 2, ty - 2, tx + tw + 2, ty + th + 2], fill=(0, 0, 0, 255))
    d.text((tx, ty), text, fill=(255, 255, 0, 255), font=font)
    return im


def open_or_placeholder(root, relpath, w, h, tag, manifest, note=''):
    # Prefer the shared web sprite pack, then a leftover local art/
    # override, then the synthesized placeholder cache.
    shared = os.path.join(root, relpath)
    local = os.path.join(HERE, '..', 'art', 'sprites', relpath)
    if os.path.exists(shared):
        return Image.open(shared)
    if os.path.exists(local):
        return Image.open(local)
    cached = os.path.join(PLACEHOLDER_DIR, relpath)
    if not os.path.exists(cached):
        os.makedirs(os.path.dirname(cached), exist_ok=True)
        make_placeholder(w, h, tag).save(cached)
    manifest.append((relpath, w, h, tag, note))
    return Image.open(cached)

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
    'oren':    'npc/oren-%d.png',
    'tessa':   'npc/tessa-%d.png',
    'birch':   'npc/birch-%d.png',
    'sable':   'npc/sable-%d.png',
    # Unique Weeping Army NPCs (were reusing the generic soldier sprite).
    'cross':      'npc/cross-%d.png',
    'commander':  'npc/commander-%d.png',
    'conscript':  'npc/conscript-%d.png',
    'enforcer':   'npc/enforcer-%d.png',
    'sentry':     'npc/sentry-%d.png',
    'father':     'npc/father-%d.png',
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
    'emberling', 'frostail', 'boulderam', 'stormwing',
    'sableclaw', 'thornhide', 'glasswisp', 'ashenmaw',
    'heavenfall',
]
MONSTER_FRAMES = [1, 2, 3, 4]
MONSTER_W, MONSTER_H = 92, 92

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
ITEM_ICONS = [
    'salve', 'bandage', 'bitterroot', 'dust', 'gem',
    'sunbalm', 'warroot', 'smokebomb', 'greatcrystal',
]
ITEM_ICON_W, ITEM_ICON_H = 14, 14

# Dialogue-box character portraits (public/sprites/portraits/<name>.png,
# source art 160x200 to 225x225 depending on character -- not all the
# same aspect ratio), sized to fill as much of PORTRAIT_BOX_W x
# PORTRAIT_BOX_H as possible without cropping or stretching: each
# portrait is scaled by the SAME factor on both axes (min of the two
# box/source ratios, "contain" scaling), so a 160x200 portrait and a
# 225x225 one both end up as large as they can while keeping their
# own real proportions -- see draw_dialogue_box() in main.c, which
# centers whatever size comes out of this in that box. Only the
# speakers TALK's beats actually use (SPK_* in main.c) are pulled;
# the reference has portraits for every battle species too
# (port-quillpup etc, shown on the battle-intro screen this port
# doesn't have), not needed here.
PORTRAITS = [
    'max', 'anne', 'mason', 'wren', 'mae', 'ivo', 'nell', 'pike',
    'calder', 'bram', 'cathleen', 'shinigami',
    'oren', 'tessa', 'birch', 'sable',
    'cross', 'commander', 'conscript', 'enforcer', 'sentry',
    'father', 'heavenfall',
]
PORTRAIT_BOX_W, PORTRAIT_BOX_H = 312, 176

def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def encode(im, dst_w, dst_h, resample=Image.NEAREST):
    im = im.convert('RGBA').resize((dst_w, dst_h), resample)
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

def default_sprite_root():
    env = os.environ.get('CRYMON_SPRITES')
    if env:
        return env
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = []
    for up in (2, 3, 4):
        base = os.path.join(here, *(['..'] * up))
        candidates.append(os.path.join(base, 'public', 'sprites'))
        candidates.append(os.path.join(base, 'CryMon', 'public', 'sprites'))
    candidates.append(os.path.join(here, '..', 'art', 'sprites'))
    for cand in candidates:
        cand = os.path.normpath(cand)
        if os.path.isdir(cand):
            return cand
    return os.path.normpath(os.path.join(here, '..', '..', '..', 'public', 'sprites'))


def main():
    if len(sys.argv) == 2:
        arg = sys.argv[1]
        root = os.path.join(arg, 'public', 'sprites') if os.path.isdir(os.path.join(arg, 'public', 'sprites')) else arg
    elif len(sys.argv) == 1:
        root = default_sprite_root()
    else:
        sys.exit('usage: gen_sprites.py [path-to-CryMon-checkout-or-public/sprites]')
    if not os.path.isdir(root):
        sys.exit('sprite root not found: %s\nSet CRYMON_SPRITES or pass the CryMon checkout.' % root)
    manifest = []  # (relpath, w, h, tag, note) for every placeholder actually used this run

    lines = []
    lines.append('/* Generated by ports/dreamcast/tools/gen_sprites.py from')
    lines.append(' * the shared public/sprites pack. Do not hand-edit. */')
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
            im = open_or_placeholder(root, pattern % f, ACTOR_DST_W, ACTOR_DST_H,
                                      name, manifest, 'world sprite, idle frame %d/4' % f)
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
            im = open_or_placeholder(root, 'monsters/%s/%d.png' % (name, f), MONSTER_W, MONSTER_H,
                                      name, manifest, 'battle sprite, frame %d/4 (all 4 may be identical)' % f)
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
        im = open_or_placeholder(root, 'items/%s.png' % name, ITEM_ICON_W, ITEM_ICON_H,
                                  name, manifest, 'bag/shop/battle item icon')
        pixels = encode(im, ITEM_ICON_W, ITEM_ICON_H)
        emit_array(lines, 'icon_%s' % name, pixels, ITEM_ICON_W, ITEM_ICON_H)

    lines.append('#define PORTRAIT_BOX_W %d' % PORTRAIT_BOX_W)
    lines.append('#define PORTRAIT_BOX_H %d' % PORTRAIT_BOX_H)
    lines.append('')
    for name in PORTRAITS:
        im = open_or_placeholder(root, 'portraits/%s.png' % name, PORTRAIT_BOX_W, PORTRAIT_BOX_H,
                                  name, manifest, 'dialogue-box portrait')
        sw, sh = im.size
        scale = min(PORTRAIT_BOX_W / sw, PORTRAIT_BOX_H / sh)
        dw, dh = max(1, round(sw * scale)), max(1, round(sh * scale))
        pixels = encode(im, dw, dh, resample=Image.LANCZOS)
        lines.append('#define PORT_%s_W %d' % (name.upper(), dw))
        lines.append('#define PORT_%s_H %d' % (name.upper(), dh))
        emit_array(lines, 'port_%s' % name, pixels, dw, dh)

    with open(OUT, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote', OUT)

    manifest_path = os.path.join(HERE, '..', 'ART_NEEDED.md')
    if manifest:
        md = []
        md.append('# Art needed\n')
        md.append(
            'Auto-generated by `tools/gen_sprites.py` -- every row below is a real\n'
            'gap: no source art exists for it in `xenodorian/CryMon`, so the game is\n'
            'currently running a synthesized placeholder (a magenta/black checker\n'
            'tile with the entity\'s tag stamped on it, cached under\n'
            '`tools/placeholder_sprites/<path>` at the exact path below). Regenerate\n'
            'this file any time by re-running `gen_sprites.py` -- it reflects\n'
            'whatever is actually still missing, never hand-edited.\n'
        )
        md.append(
            '## Instructions for generating replacement art\n\n'
            '1. Draw/generate a PNG matching the **pixel size** given for each row\n'
            '   below (or any larger size with the same aspect ratio -- everything\n'
            '   is downscaled at build time, never upscaled).\n'
            '2. Style: match the existing CryMon art in `public/sprites/` -- simple,\n'
            '   readable silhouettes, flat-ish shading, small enough to read at 24-92px\n'
            '   on screen. Battle sprites (`monsters/<name>/`) are the most detailed;\n'
            '   world sprites (`npc/<name>-N.png`) and item icons are simpler.\n'
            '3. Background: fully transparent (alpha channel), not a solid color --\n'
            '   anything under alpha 128 is treated as see-through at build time.\n'
            '4. Save the PNG at **exactly** the "expected path" column below, rooted\n'
            '   at `public/sprites/` inside a checkout of `xenodorian/CryMon` (the\n'
            '   same repo this port\'s art already comes from). That\'s the only path\n'
            '   `tools/gen_sprites.py <path-to-CryMon-checkout>` ever looks at --\n'
            '   dropping a real file there automatically replaces the placeholder\n'
            '   next time sprites are regenerated, no code changes needed.\n'
            '5. Monsters and NPCs need one file per frame (1-4); it\'s fine for all 4\n'
            '   to be pixel-identical at first (no animation) -- a real idle cycle can\n'
            '   follow later.\n'
        )
        md.append('## Missing (%d files)\n' % len(manifest))
        md.append('| Expected path (under `public/sprites/`) | Size | Entity | Note |')
        md.append('|---|---|---|---|')
        for relpath, w, h, tag, note in manifest:
            md.append('| `%s` | %dx%d | %s | %s |' % (relpath, w, h, tag, note))
        md.append('')
        with open(manifest_path, 'w') as f:
            f.write('\n'.join(md) + '\n')
        print('wrote %s (%d missing files)' % (manifest_path, len(manifest)))
    elif os.path.exists(manifest_path):
        os.remove(manifest_path)
        print('removed %s (nothing missing)' % manifest_path)

if __name__ == '__main__':
    main()
