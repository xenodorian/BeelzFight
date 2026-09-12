#!/usr/bin/env python3
"""Generates all pixel-art sprite/background sheets for the BeelzFight GBA
game as indexed PNGs ready for `grit`. Regenerate with:
    python3 gen_assets/gen_sprites.py
Magenta (255,0,255) is the transparent color grit expects by default.
"""
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "data")
os.makedirs(OUT, exist_ok=True)

TRANSPARENT = (255, 0, 255)

# Player palette
SKIN = (240, 195, 155)
SKIN_D = (205, 155, 115)
HAIR = (84, 48, 38)
HAIR_HI = (120, 72, 54)
DRESS = (66, 92, 176)
DRESS_HI = (94, 124, 208)
DRESS_D = (44, 62, 128)
OUTLINE = (28, 22, 30)
BLADE = (222, 224, 232)
BLADE_HI = (255, 255, 255)
BLADE_D = (160, 162, 178)
HILT = (140, 100, 46)

# Enemy (imp) palette
IMP = (170, 40, 160)
IMP_D = (110, 20, 110)
IMP_EYE = (255, 230, 60)
IMP_HORN = (40, 20, 20)

# Boss (demon) palette -- muted/desaturated so it reads as menacing, not neon
DEMON = (94, 34, 38)
DEMON_HI = (132, 58, 54)
DEMON_D = (61, 20, 26)
DEMON_BELLY = (150, 90, 66)
DEMON_BELLY_D = (110, 62, 48)
DEMON_HORN = (43, 34, 40)
DEMON_HORN_HI = (70, 56, 60)
DEMON_EYE = (233, 200, 90)
DEMON_CLAW = (224, 214, 196)


def row_span(d, y0, y1, x0, x1, color):
    """Fills scanlines y0..y1-1 across x0..x1-1. The basic brush for building
    an organic silhouette out of many varying-width horizontal strips,
    instead of stacking ellipses/rectangles into a snowman."""
    d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=color)

# Background palette
SKY_TOP = (40, 30, 70)
SKY_BOT = (110, 60, 110)
GROUND = (60, 35, 40)
GROUND_D = (40, 22, 26)
GROUND_HI = (90, 55, 55)
CLOUD = (150, 110, 150)


def new_img(w, h):
    img = Image.new("RGB", (w, h), TRANSPARENT)
    return img, ImageDraw.Draw(img)


def save_indexed(img, name, colors=16):
    """Builds an exact-color indexed PNG (no quantization drift) so the
    transparent magenta and every hand-picked color survive byte-for-byte
    for grit to key off of."""
    rgb = img.convert("RGB")
    used = [c for _, c in rgb.getcolors(maxcolors=1 << 20)]
    if TRANSPARENT in used:
        used.remove(TRANSPARENT)
    palette = [TRANSPARENT] + used
    if len(palette) > colors:
        raise ValueError(f"{name}: {len(palette)} colors exceeds cap {colors}")
    color_to_idx = {c: i for i, c in enumerate(palette)}
    p = Image.new("P", rgb.size)
    flat_pal = []
    for c in palette:
        flat_pal.extend(c)
    flat_pal.extend([0, 0, 0] * (256 - len(palette)))
    p.putpalette(flat_pal)
    px_src = rgb.load()
    px_dst = p.load()
    w, h = rgb.size
    for y in range(h):
        for x in range(w):
            px_dst[x, y] = color_to_idx[px_src[x, y]]
    path = os.path.join(OUT, name)
    p.save(path)
    print("wrote", path, p.size, f"{len(palette)} colors")


# ---------------------------------------------------------------------------
# Player: 32x32 per frame, stacked vertically.
# Frames: 0 idle, 1 walk1, 2 walk2, 3 light1, 4 light2, 5 heavy1, 6 heavy2,
#         7 block, 8 parry, 9 hurt
# ---------------------------------------------------------------------------
PLAYER_FRAMES = 10
PW, PH = 32, 32


def draw_body(d, leg_off=0, lean=0, arm_up=False, tint=None):
    sk = tint or SKIN
    dr = DRESS if not tint else tint
    # legs
    d.rectangle([12 + lean, 24, 15 + lean, 30], fill=OUTLINE)
    d.rectangle([13 + lean, 24, 14 + lean, 29], fill=SKIN_D)
    d.rectangle([17 + lean - leg_off, 24, 20 + lean - leg_off, 30], fill=OUTLINE)
    d.rectangle([18 + lean - leg_off, 24, 19 + lean - leg_off, 29], fill=SKIN_D)
    # dress/torso
    d.rectangle([10 + lean, 14, 22 + lean, 25], fill=OUTLINE)
    d.rectangle([11 + lean, 15, 21 + lean, 24], fill=dr)
    d.rectangle([11 + lean, 15, 15 + lean, 18], fill=DRESS_HI if not tint else tint)
    d.rectangle([11 + lean, 22, 21 + lean, 24], fill=DRESS_D if not tint else tint)
    # head
    d.ellipse([11 + lean, 4, 21 + lean, 14], fill=OUTLINE)
    d.ellipse([12 + lean, 5, 20 + lean, 13], fill=sk)
    # hair
    d.pieslice([11 + lean, 3, 21 + lean, 13], 180, 360, fill=HAIR)
    d.rectangle([11 + lean, 6, 13 + lean, 12], fill=HAIR)
    d.rectangle([19 + lean, 6, 21 + lean, 12], fill=HAIR)
    d.pieslice([12 + lean, 3, 18 + lean, 9], 200, 340, fill=HAIR_HI)
    # arm (near side)
    ay = 15 if not arm_up else 12
    d.rectangle([9 + lean, ay, 11 + lean, ay + 8], fill=sk)


def draw_sword(d, angle, lean=0):
    """angle: 'down' idle rest, 'light', 'heavy', 'guard'"""
    if angle == "down":
        d.rectangle([21 + lean, 6, 25 + lean, 29], fill=OUTLINE)
        d.rectangle([22 + lean, 7, 24 + lean, 27], fill=BLADE)
        d.rectangle([22 + lean, 7, 22 + lean, 27], fill=BLADE_HI)
        d.rectangle([24 + lean, 7, 24 + lean, 27], fill=BLADE_D)
        d.rectangle([20 + lean, 20, 26 + lean, 23], fill=HILT)
    elif angle == "light":
        d.rectangle([20 + lean, 12, 31, 16], fill=OUTLINE)
        d.rectangle([21 + lean, 13, 30, 15], fill=BLADE)
        d.rectangle([16 + lean, 13, 21 + lean, 17], fill=HILT)
    elif angle == "heavy":
        # big diagonal sweep
        d.polygon([(18 + lean, 26), (30, 6), (32, 8), (20 + lean, 29)], fill=OUTLINE)
        d.polygon([(19 + lean, 25), (29, 8), (30, 9), (20 + lean, 27)], fill=BLADE)
        d.rectangle([15 + lean, 21, 21 + lean, 27], fill=HILT)
    elif angle == "guard":
        d.rectangle([6 + lean, 10, 10 + lean, 26], fill=OUTLINE)
        d.rectangle([7 + lean, 11, 9 + lean, 25], fill=BLADE)
        d.rectangle([6 + lean, 17, 12 + lean, 20], fill=HILT)


def make_player_frame(kind):
    img, d = new_img(PW, PH)
    if kind == "idle":
        draw_body(d)
        draw_sword(d, "down")
    elif kind == "walk1":
        draw_body(d, leg_off=3)
        draw_sword(d, "down")
    elif kind == "walk2":
        draw_body(d, leg_off=-2)
        draw_sword(d, "down")
    elif kind == "light1":
        draw_body(d, lean=-1, arm_up=True)
        draw_sword(d, "down", lean=-1)
    elif kind == "light2":
        draw_body(d, lean=1, arm_up=True)
        draw_sword(d, "light", lean=1)
    elif kind == "heavy1":
        draw_body(d, lean=-2, arm_up=True)
        draw_sword(d, "guard", lean=-2)
    elif kind == "heavy2":
        draw_body(d, lean=2, arm_up=True)
        draw_sword(d, "heavy", lean=0)
    elif kind == "block":
        draw_body(d, arm_up=True)
        draw_sword(d, "guard")
    elif kind == "parry":
        draw_body(d, arm_up=True, tint=(230, 230, 255))
        draw_sword(d, "guard")
    elif kind == "hurt":
        draw_body(d, lean=-2, tint=(255, 150, 150))
        draw_sword(d, "down", lean=-2)
    return img


def gen_player():
    sheet = Image.new("RGB", (PW, PH * PLAYER_FRAMES), TRANSPARENT)
    order = ["idle", "walk1", "walk2", "light1", "light2", "heavy1", "heavy2",
             "block", "parry", "hurt"]
    for i, kind in enumerate(order):
        frame = make_player_frame(kind)
        sheet.paste(frame, (0, i * PH))
    save_indexed(sheet, "player.png", colors=16)


# ---------------------------------------------------------------------------
# Enemy imp: 16x16 per frame. Frames: walk1, walk2, attack, hurt
# ---------------------------------------------------------------------------
EW, EH = 16, 16


def make_imp_frame(kind):
    img, d = new_img(EW, EH)
    leg_off = 0
    tint = IMP
    if kind == "walk1":
        leg_off = 1
    elif kind == "walk2":
        leg_off = -1
    elif kind == "attack":
        leg_off = 0
    elif kind == "hurt":
        tint = (255, 150, 150)
    # legs
    d.rectangle([5, 12, 6, 15], fill=OUTLINE)
    d.rectangle([9 - leg_off, 12, 10 - leg_off, 15], fill=OUTLINE)
    # body
    d.ellipse([3, 6, 12, 14], fill=OUTLINE)
    d.ellipse([4, 7, 11, 13], fill=tint)
    # horns
    d.polygon([(4, 6), (5, 2), (6, 6)], fill=IMP_HORN)
    d.polygon([(9, 6), (10, 2), (11, 6)], fill=IMP_HORN)
    # head
    d.ellipse([4, 3, 11, 9], fill=IMP_D if kind != "hurt" else tint)
    # eyes
    if kind == "attack":
        d.rectangle([3, 6, 5, 7], fill=IMP_EYE)
        d.rectangle([10, 6, 12, 7], fill=IMP_EYE)
        d.polygon([(11, 8), (15, 7), (11, 10)], fill=OUTLINE)
    else:
        d.rectangle([5, 6, 6, 7], fill=IMP_EYE)
        d.rectangle([9, 6, 10, 7], fill=IMP_EYE)
    return img


def gen_enemy():
    order = ["walk1", "walk2", "attack", "hurt"]
    sheet = Image.new("RGB", (EW, EH * len(order)), TRANSPARENT)
    for i, kind in enumerate(order):
        sheet.paste(make_imp_frame(kind), (0, i * EH))
    save_indexed(sheet, "enemy.png", colors=16)


# ---------------------------------------------------------------------------
# Boss demon: 64x64 per frame. Frames: idle, shoot, hurt
# ---------------------------------------------------------------------------
BW, BH = 64, 64


def make_boss_frame(kind):
    """A hunched, top-heavy demon silhouette built from varying-width row
    spans (classic pixel-art scanline technique), not stacked circles --
    wide shoulders, a jutting jaw, and angled claw-arms instead of a
    round torso with rectangle arms bolted on."""
    img, d = new_img(BW, BH)
    body = DEMON if kind != "hurt" else DEMON_HI
    belly = DEMON_BELLY if kind != "hurt" else (200, 150, 130)
    arm_out = kind == "shoot"

    # --- silhouette outline pass (drawn 1px larger, then base fill on top) ---
    def clampx(x):
        return max(0, min(BW, x))

    def clampy(y):
        return max(0, min(BH, y))

    def draw_silhouette(col, bellycol, hornCol, pad):
        def rs(y0, y1, x0, x1, c):
            row_span(d, clampy(y0 - pad), clampy(y1 + pad), clampx(x0 - pad), clampx(x1 + pad), c)

        # horns (asymmetric, swept back)
        d.polygon([(16 - pad, 10), (10 - pad, 0 - pad), (22 + pad, 9)], fill=hornCol)
        d.polygon([(46 + pad, 10), (54 + pad, 1 - pad), (40 - pad, 9)], fill=hornCol)
        # head: narrow crown widening into a heavy jaw
        rs(8, 14, 21, 43, col)
        rs(14, 20, 18, 46, col)
        rs(20, 24, 16, 48, col)  # jowls
        # shoulders / upper torso -- wide, hunched
        rs(24, 30, 10, 54, col)
        rs(30, 38, 13, 51, col)
        # belly bulge
        rs(38, 48, 15, 49, bellycol if pad == 0 else col)
        rs(48, 52, 18, 46, col)
        # legs, slightly bowed
        rs(52, 60, 17, 27, col)
        rs(52, 60, 37, 47, col)
        # clawed feet
        rs(60, 64, 14, 30, col)
        rs(60, 64, 34, 50, col)
        # arms: angled forward, thick at shoulder tapering to a clawed fist
        if arm_out:
            rs(22, 28, 52, 62, col)
            rs(26, 32, 58, 64, col)
            rs(8, 16, 2, 12, col)
        else:
            rs(26, 34, 4, 14, col)
            rs(32, 44, 2, 12, col)
            rs(26, 34, 50, 60, col)
            rs(32, 44, 52, 62, col)

    draw_silhouette(OUTLINE, OUTLINE, DEMON_HORN, pad=1)   # dark outline, 1px larger
    draw_silhouette(body, belly, DEMON_HORN_HI, pad=0)     # base fill

    # highlight strip along the top-left of head/torso (fixed light source)
    row_span(d, 9, 13, 22, 40, DEMON_HI if kind != "hurt" else (255, 200, 190))
    row_span(d, 25, 29, 14, 30, DEMON_HI if kind != "hurt" else (255, 200, 190))

    # eyes + claws on top
    ey = 13 if kind != "hurt" else 15
    d.rectangle([25, ey, 30, ey + 3], fill=DEMON_EYE)
    d.rectangle([34, ey, 39, ey + 3], fill=DEMON_EYE)
    if arm_out:
        d.polygon([(58, 24), (63, 26), (58, 30)], fill=DEMON_CLAW)
        d.polygon([(6, 8), (1, 11), (6, 15)], fill=DEMON_CLAW)
    else:
        d.polygon([(5, 32), (0, 36), (5, 40)], fill=DEMON_CLAW)
        d.polygon([(59, 32), (63, 36), (58, 40)], fill=DEMON_CLAW)
    return img


def gen_boss():
    order = ["idle", "shoot", "hurt"]
    sheet = Image.new("RGB", (BW, BH * len(order)), TRANSPARENT)
    for i, kind in enumerate(order):
        sheet.paste(make_boss_frame(kind), (0, i * BH))
    save_indexed(sheet, "boss.png", colors=16)


# ---------------------------------------------------------------------------
# Projectile: 8x8 single frame fireball.
# ---------------------------------------------------------------------------
def gen_projectile():
    img, d = new_img(8, 8)
    d.ellipse([0, 0, 7, 7], fill=OUTLINE)
    d.ellipse([1, 1, 6, 6], fill=(255, 160, 40))
    d.ellipse([2, 2, 5, 5], fill=(255, 230, 100))
    save_indexed(img, "projectile.png", colors=8)


# ---------------------------------------------------------------------------
# HUD heart: 16x16, 2 frames (full, empty)
# ---------------------------------------------------------------------------
def gen_heart():
    sheet = Image.new("RGB", (16, 32), TRANSPARENT)
    for i, full in enumerate([True, False]):
        img, d = new_img(16, 16)
        color = (220, 30, 40) if full else (70, 70, 70)
        d.polygon([(8, 13), (1, 6), (1, 3), (4, 1), (8, 5),
                   (12, 1), (15, 3), (15, 6)], fill=OUTLINE)
        d.polygon([(8, 11), (3, 6), (3, 4), (5, 3), (8, 6),
                   (11, 3), (13, 4), (13, 6)], fill=color)
        sheet.paste(img, (0, i * 16))
    save_indexed(sheet, "heart.png", colors=8)


# ---------------------------------------------------------------------------
# Boss HP bar segment: 8x8 single frame.
# ---------------------------------------------------------------------------
def gen_hpseg():
    img, d = new_img(8, 8)
    d.rectangle([0, 0, 7, 7], fill=OUTLINE)
    d.rectangle([1, 1, 6, 6], fill=(220, 30, 40))
    save_indexed(img, "hpseg.png", colors=4)


# ---------------------------------------------------------------------------
# Background: 256x256 (32x32 tiles), seamless horizontal wrap.
# ---------------------------------------------------------------------------
def gen_background():
    img, d = new_img(256, 256)
    BANDS = 5
    band_h = 160 // BANDS
    for band in range(BANDS):
        t = band / (BANDS - 1)
        r = int(SKY_TOP[0] + (SKY_BOT[0] - SKY_TOP[0]) * t)
        g = int(SKY_TOP[1] + (SKY_BOT[1] - SKY_TOP[1]) * t)
        b = int(SKY_TOP[2] + (SKY_BOT[2] - SKY_TOP[2]) * t)
        y0 = band * band_h
        y1 = 160 if band == BANDS - 1 else y0 + band_h
        d.rectangle([0, y0, 255, y1 - 1], fill=(r, g, b))
    # clouds (wrap-safe: keep away from x=0/x=255 seam, symmetric shapes)
    for cx, cy in [(40, 30), (140, 20), (200, 45)]:
        d.ellipse([cx - 18, cy - 6, cx + 18, cy + 6], fill=CLOUD)
        d.ellipse([cx - 10, cy - 12, cx + 10, cy], fill=CLOUD)
    # ground
    d.rectangle([0, 140, 255, 255], fill=GROUND_D)
    d.rectangle([0, 140, 255, 146], fill=GROUND_HI)
    d.rectangle([0, 146, 255, 160], fill=GROUND)
    for x in range(0, 256, 16):
        d.line([(x, 146), (x, 160)], fill=GROUND_D)
    save_indexed(img, "bg.png", colors=16)


# ---------------------------------------------------------------------------
# Font: one 8x8 tile per printable ASCII char (32=space .. 126=~), stacked
# vertically -- tile index = ord(ch) - 32. Used for our own hand-rolled BG
# text renderer (source/text.c) instead of libgba's consoleDemoInit(), which
# turned out not to render on at least one real-world GBA emulator (the
# backdrop color showed but no glyphs) -- rolling our own means the same
# plain-memcpy tile/palette loading path already proven for the background
# and sprites is what draws the title/win/lose text too.
# ---------------------------------------------------------------------------
from PIL import ImageFont

FONT_FIRST = 32
FONT_LAST = 126
FONT_COUNT = FONT_LAST - FONT_FIRST + 1


def gen_font():
    pil_font = ImageFont.load_default()
    sheet = Image.new("RGB", (8, 8 * FONT_COUNT), TRANSPARENT)
    for i in range(FONT_COUNT):
        ch = chr(FONT_FIRST + i)
        # Render to a grayscale layer and threshold -- PIL antialiases text
        # even for bitmap fonts, which would blow past our tiny color cap.
        mask = Image.new("L", (8, 8), 0)
        if ch != " ":
            # Every glyph in this bitmap font occupies exactly an 8px-tall
            # band, but *which* 8 rows varies: cap-height letters sit at
            # y=2..10, descenders (g, p, y, ...) at y=4..12, etc. A single
            # fixed offset for all of them clips whichever glyphs it isn't
            # tuned for -- tried -1 (clipped every glyph's bottom row,
            # turning 'E' into an 'F' shape and 'L' into a bare stroke),
            # then a uniform -2 (still clipped descenders' tails, e.g. 'y'
            # rendering as 'v'). Shifting each glyph by its own measured
            # top instead fits all of them with no clipping either way.
            top = pil_font.getbbox(ch)[1]
            ImageDraw.Draw(mask).text((0, -top), ch, font=pil_font, fill=255)
        img = Image.new("RGB", (8, 8), TRANSPARENT)
        px = img.load()
        mpx = mask.load()
        for y in range(8):
            for x in range(8):
                if mpx[x, y] >= 128:
                    px[x, y] = (255, 255, 255)
        sheet.paste(img, (0, i * 8))
    save_indexed(sheet, "font.png", colors=4)


if __name__ == "__main__":
    gen_player()
    gen_enemy()
    gen_boss()
    gen_projectile()
    gen_heart()
    gen_hpseg()
    gen_background()
    gen_font()
