#!/usr/bin/env python3
"""High-detail procedural art generator for BeelzFight.

No image-generation service is reachable from this build environment, so
every pixel here is painted programmatically. Unlike the first pass (flat
shapes drawn on a tiny grid and nearest-upscaled, which looked blocky and
lost all facial/material detail), this renders at 4x supersample with
proper form shading, then box-downsamples to the native texture size, so
the sprites carry real anti-aliased detail at 1:1 on screen.

Pipeline per frame:
  1. draw shapes at SUPER x resolution (light comes from the upper-left)
  2. grow the silhouette by 1px and fill it dark -> crisp read-anywhere outline
  3. box-downsample to native size (this is what creates the anti-aliasing)
  4. bleed edge colors into the transparent margin, so bilinear filtering /
     ARGB4444 quantization can never pull a dark halo out of the alpha edge

Texture budget matters: the PowerVR has 8MB of VRAM shared with the
framebuffer, so frame sizes below are chosen to land exactly on tight
power-of-two sheets (see make_sheet).
"""
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPR_DIR = os.path.join(ROOT, "assets", "sprites")
BG_DIR = os.path.join(ROOT, "assets", "backgrounds")
os.makedirs(SPR_DIR, exist_ok=True)
os.makedirs(BG_DIR, exist_ok=True)

SUPER = 4
manifest = {"sheets": {}, "backgrounds": {}}

LIGHT = (-1.0, -1.0)  # light direction (upper-left), normalized-ish


# ----------------------------------------------------------------- canvas

class Canvas:
    """Draws in final-resolution coordinates onto a supersampled buffer."""

    # Every canvas is drawn with this much slack on all four sides. A pose
    # that reaches past the nominal frame (a raised greatsword, a lunge) is
    # therefore drawn in full rather than chopped; fit_frames() then scales
    # the whole animation down just enough to bring the widest pose back
    # inside the frame, so clipping is structurally impossible instead of
    # something that has to be caught by eye every time a pose changes.
    PAD = 48

    def __init__(self, w, h, pad=None):
        # backgrounds are authored at exact tile sizes and pass pad=0
        self.PAD = self.__class__.PAD if pad is None else pad
        self.w, self.h = w + 2 * self.PAD, h + 2 * self.PAD
        self.im = Image.new("RGBA", (self.w * SUPER, self.h * SUPER), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def _p(self, pts):
        k = self.PAD
        return [((x + k) * SUPER, (y + k) * SUPER) for x, y in pts]

    def poly(self, pts, fill):
        if len(pts) >= 3:
            self.d.polygon(self._p(pts), fill=fill)

    def ell(self, cx, cy, rx, ry, fill):
        k = self.PAD
        self.d.ellipse([(cx - rx + k) * SUPER, (cy - ry + k) * SUPER,
                        (cx + rx + k) * SUPER, (cy + ry + k) * SUPER], fill=fill)

    def arc_poly(self, cx, cy, r0, r1, a0, a1, fill, steps=24):
        """Filled annulus wedge -- used for swing arcs, capes, wing membranes."""
        pts = []
        for i in range(steps + 1):
            a = math.radians(a0 + (a1 - a0) * i / steps)
            pts.append((cx + math.cos(a) * r1, cy + math.sin(a) * r1))
        for i in range(steps, -1, -1):
            a = math.radians(a0 + (a1 - a0) * i / steps)
            pts.append((cx + math.cos(a) * r0, cy + math.sin(a) * r0))
        self.poly(pts, fill)

    def cap(self, p0, p1, r0, r1, fill):
        """Tapered capsule (a limb segment)."""
        (x0, y0), (x1, y1) = p0, p1
        dx, dy = x1 - x0, y1 - y0
        L = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / L, dx / L
        self.poly([(x0 + nx * r0, y0 + ny * r0), (x1 + nx * r1, y1 + ny * r1),
                   (x1 - nx * r1, y1 - ny * r1), (x0 - nx * r0, y0 - ny * r0)], fill)
        self.ell(x0, y0, r0, r0, fill)
        self.ell(x1, y1, r1, r1, fill)

    def limb(self, p0, p1, r0, r1, base, sh, hi=None):
        """Capsule with form shading: shadow body, lit core, specular sliver."""
        self.cap(p0, p1, r0, r1, sh)
        o = (LIGHT[0] * r0 * 0.26, LIGHT[1] * r0 * 0.26)
        self.cap((p0[0] + o[0], p0[1] + o[1]), (p1[0] + o[0], p1[1] + o[1]),
                 r0 * 0.84, r1 * 0.84, base)
        if hi:
            o2 = (LIGHT[0] * r0 * 0.52, LIGHT[1] * r0 * 0.52)
            self.cap((p0[0] + o2[0], p0[1] + o2[1]), (p1[0] + o2[0], p1[1] + o2[1]),
                     r0 * 0.38, r1 * 0.38, hi)

    def shaded(self, pts, base, sh, hi=None, amt=1.0):
        """Polygon with the same 3-tone form shading as limb()."""
        self.poly(pts, sh)
        cx = sum(p[0] for p in pts) / len(pts)
        cy = sum(p[1] for p in pts) / len(pts)
        inner = [(cx + (x - cx) * 0.88 + LIGHT[0] * amt,
                  cy + (y - cy) * 0.88 + LIGHT[1] * amt) for x, y in pts]
        self.poly(inner, base)
        if hi:
            core = [(cx + (x - cx) * 0.5 + LIGHT[0] * amt * 2.0,
                     cy + (y - cy) * 0.5 + LIGHT[1] * amt * 2.0) for x, y in pts]
            self.poly(core, hi)

    def stroke(self, pts, fill, w=1.0):
        self.d.line(self._p(pts), fill=fill, width=max(1, int(round(w * SUPER))), joint="curve")

    def finish(self, outline=(16, 10, 20, 255), outline_px=1):
        im = self.im
        if outline is not None and outline_px > 0:
            a = im.split()[3]
            grown = a.filter(ImageFilter.MaxFilter(2 * outline_px * SUPER + 1))
            ol = Image.new("RGBA", im.size, outline)
            ol.putalpha(grown)
            im = Image.alpha_composite(ol, im)
        return im.resize((self.w, self.h), Image.Resampling.BOX)


def dilate_edges(img, iters=3):
    """Copy edge colors outward into fully-transparent pixels. Straight-alpha
    textures otherwise carry RGB=0 outside the silhouette, which bilinear
    filtering (and 4-bit alpha quantization) drags in as a black fringe."""
    arr = np.array(img)
    rgb = arr[..., :3].astype(np.float32)
    a = arr[..., 3]
    known = a > 0
    for _ in range(iters):
        if known.all():
            break
        sums = np.zeros_like(rgb)
        cnts = np.zeros(a.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1),
                       (1, 1), (1, -1), (-1, 1), (-1, -1)):
            sums += np.roll(rgb, (dy, dx), axis=(0, 1)) * np.roll(known, (dy, dx), axis=(0, 1))[..., None]
            cnts += np.roll(known, (dy, dx), axis=(0, 1))
        newly = (~known) & (cnts > 0)
        if not newly.any():
            break
        rgb[newly] = sums[newly] / cnts[newly][..., None]
        known = known | newly
    return Image.fromarray(np.dstack([rgb.astype(np.uint8), a]), "RGBA")


def tint(img, color, strength):
    """Hit-flash: blend toward `color` only where the sprite is drawn."""
    arr = np.array(img).astype(np.float32)
    tgt = np.array(color, np.float32)
    m = (arr[..., 3:4] > 0).astype(np.float32) * strength
    arr[..., :3] = arr[..., :3] * (1 - m) + tgt * m
    return Image.fromarray(arr.astype(np.uint8), "RGBA")


def fade(img, f):
    r, g, b, a = img.split()
    return Image.merge("RGBA", (r, g, b, a.point(lambda v: int(v * f))))


def fit_frames(frames, fw, fh, anchor, zoom):
    """Scale a whole animation about its anchor so that its most extreme
    pose fits inside fw x fh, then crop away the work margin.

    Scaling is uniform across every frame of the sheet (never per-frame),
    so the character does not visibly change size mid-animation. Returns
    (cropped frames, draw_w, draw_h): draw_w/draw_h are the on-screen quad
    size that restores the intended `zoom` despite the shrink.
    """
    pad = Canvas.PAD
    ax, ay = anchor[0] + pad, anchor[1] + pad
    L = R = T = B = 0.0
    for fr in frames:
        bb = fr.getbbox()  # alpha-aware: fully transparent pixels excluded
        if bb is None:
            continue
        L = max(L, ax - bb[0]); T = max(T, ay - bb[1])
        R = max(R, bb[2] - ax);  B = max(B, bb[3] - ay)
    # one texel of guaranteed empty margin on every side, so bilinear
    # filtering at a frame's edge can never reach into the neighbouring
    # frame packed next to it on the sheet
    g = 1.0
    lim = [(anchor[0] - g, L), (fw - anchor[0] - g, R),
           (anchor[1] - g, T), (fh - anchor[1] - g, B)]
    s = min([1.0] + [room / need for room, need in lim if need > 0.01])
    out = []
    for fr in frames:
        if s < 1.0:
            nw, nh = max(1, int(round(fr.width * s))), max(1, int(round(fr.height * s)))
            sc = fr.resize((nw, nh), Image.Resampling.LANCZOS)
            ox, oy = anchor[0] - ax * s, anchor[1] - ay * s
        else:
            sc, ox, oy = fr, -pad, -pad
        cell = Image.new("RGBA", (fw, fh), (0, 0, 0, 0))
        cell.paste(sc, (int(round(ox)), int(round(oy))))
        out.append(dilate_edges(cell))
    return out, fw * zoom / s, fh * zoom / s, s


def make_sheet(frames, fw, fh, name, anchor, zoom=1.0):
    """Pack frames into the smallest power-of-two sheet that fits them."""
    frames, draw_w, draw_h, fit = fit_frames(frames, fw, fh, anchor, zoom)
    n = len(frames)
    best = None
    for cols in range(1, n + 1):
        rows = math.ceil(n / cols)
        w = 1 << (cols * fw - 1).bit_length()
        h = 1 << (rows * fh - 1).bit_length()
        # PVR textures top out at 1024 in each dimension -- a 2048-tall
        # sheet is not merely wasteful, pvr_poly_cxt_txr cannot encode its
        # size and every load of it fails, so the sprite silently vanishes.
        if w > 1024 or h > 1024:
            continue
        if best is None or w * h < best[0] * best[1]:
            best = (w, h, cols)
    sheet_w, sheet_h, _ = best
    cols = sheet_w // fw
    sheet = Image.new("RGBA", (sheet_w, sheet_h), (0, 0, 0, 0))
    for i, fr in enumerate(frames):
        # plain copy: cells start empty and never overlap, and a masked
        # paste would blend translucent pixels against the transparent
        # black backdrop, darkening them.
        sheet.paste(fr, ((i % cols) * fw, (i // cols) * fh))
    path = os.path.join(SPR_DIR, f"{name}.png")
    sheet.save(path)
    print(f"  {name:10s} {fw}x{fh} frames -> {sheet_w}x{sheet_h} sheet"
          f"   fit {fit:.3f}  draw {draw_w:.0f}x{draw_h:.0f}")
    return {"sheet_w": sheet_w, "sheet_h": sheet_h, "cols": cols,
            "frame_w": fw, "frame_h": fh, "file": f"{name}.png",
            "anchor_x": anchor[0], "anchor_y": anchor[1],
            "draw_w": round(draw_w, 1), "draw_h": round(draw_h, 1)}


class AnimBuilder:
    def __init__(self):
        self.frames = []
        self.anims = {}

    def add(self, name, images, fps, loop=True):
        self.anims[name] = {"start": len(self.frames), "count": len(images),
                            "fps": fps, "loop": 1 if loop else 0}
        self.frames.extend(images)


# ================================================================= PLAYER
# 112x112 frames; she stands left-of-centre so the greatsword has room to
# swing right/up without clipping (the renderer anchors on ANCHOR, so
# facing left mirrors cleanly instead of making her jump sideways).
P_W = P_H = 112
P_CX = 42.0
P_FY = 102.0
P_ANCHOR = (P_CX, P_FY)

PL = dict(
    skin=(255, 221, 194), skin_sh=(219, 163, 142), skin_hi=(255, 243, 228),
    hair=(62, 27, 52), hair_sh=(38, 14, 34), hair_hi=(126, 54, 100),
    hair_gloss=(186, 104, 156),
    dress=(58, 26, 82), dress_sh=(33, 13, 50), dress_hi=(96, 50, 128),
    corset=(26, 12, 36), corset_hi=(52, 28, 66),
    trim=(244, 240, 250), trim_sh=(186, 180, 200),
    cape=(120, 24, 52), cape_sh=(72, 12, 32), cape_hi=(178, 48, 82),
    sock=(88, 62, 104), sock_sh=(52, 32, 66),
    boot=(64, 40, 46), boot_sh=(38, 22, 28), boot_hi=(104, 70, 76),
    steel=(214, 222, 236), steel_sh=(124, 136, 160), steel_hi=(255, 255, 255),
    gold=(236, 192, 92), gold_sh=(152, 112, 44), gold_hi=(255, 242, 188),
    gem=(236, 62, 86), gem_hi=(255, 168, 184),
    eye=(158, 92, 226), eye_dk=(74, 34, 120),
    mouth=(150, 70, 84),
)


def draw_sword(c, grip, ang, length, width=13.0, glow=False):
    """The oversized slab greatsword: tapered blade with a bevel highlight
    and a dark fuller, a winged gold crossguard, wrapped grip, gem pommel."""
    a = math.radians(ang)
    dx, dy = math.cos(a), math.sin(a)
    nx, ny = -dy, dx
    gx, gy = grip

    def P(t, o):
        return (gx + dx * t + nx * o, gy + dy * t + ny * o)

    tipw = width * 0.34
    base = 6.0                       # blade starts just past the guard
    tip = base + length
    # blade slab (slight forward-swept belly, angled chisel tip)
    c.poly([P(base, -width * 0.44), P(base + length * 0.16, -width * 0.5),
            P(tip - 5, -tipw), P(tip, -tipw * 0.2), P(tip - 2.5, tipw * 0.75),
            P(base + length * 0.16, width * 0.5), P(base, width * 0.44)],
           PL["steel_sh"])
    # lit face of the blade (upper-left side)
    c.poly([P(base + 1, -width * 0.34), P(base + length * 0.16, -width * 0.40),
            P(tip - 5.5, -tipw * 0.8), P(tip - 1.5, -tipw * 0.1),
            P(tip - 4, tipw * 0.2), P(base + length * 0.16, width * 0.12),
            P(base + 1, width * 0.1)], PL["steel"])
    # bright bevel along the cutting edge
    c.poly([P(base + 2, -width * 0.34), P(base + length * 0.18, -width * 0.40),
            P(tip - 5, -tipw * 0.78), P(tip - 3, -tipw * 0.5),
            P(base + length * 0.18, -width * 0.22), P(base + 2, -width * 0.18)],
           PL["steel_hi"])
    # fuller groove
    c.poly([P(base + 5, -width * 0.06), P(tip - 9, -tipw * 0.22),
            P(tip - 9, tipw * 0.12), P(base + 5, width * 0.1)], PL["steel_sh"])
    if glow:
        c.poly([P(base + 5, -width * 0.05), P(tip - 10, -tipw * 0.18),
                P(tip - 10, tipw * 0.04), P(base + 5, width * 0.02)],
               (255, 150, 190))
    # engraved notches near the ricasso
    for t in (base + 7, base + 12):
        c.poly([P(t, -width * 0.30), P(t + 1.6, -width * 0.30),
                P(t + 1.6, -width * 0.16), P(t, -width * 0.16)], PL["steel_sh"])

    # crossguard: swept wings
    c.poly([P(base - 1, -width * 0.95), P(base + 2.5, -width * 0.72),
            P(base + 1.5, -width * 0.1), P(base - 2.5, -width * 0.2)], PL["gold_sh"])
    c.poly([P(base - 1, width * 0.95), P(base + 2.5, width * 0.72),
            P(base + 1.5, width * 0.1), P(base - 2.5, width * 0.2)], PL["gold"])
    c.poly([P(base - 0.5, -width * 0.8), P(base + 1.6, -width * 0.62),
            P(base + 1.0, -width * 0.16)], PL["gold_hi"])
    # grip + pommel
    c.cap(P(base - 2, 0), P(-7.0, 0), width * 0.17, width * 0.15, PL["boot_sh"])
    for k in range(3):
        t = -1.0 - k * 2.0
        c.stroke([P(t, -width * 0.16), P(t - 1.2, width * 0.16)], PL["boot_hi"], 0.7)
    c.ell(*P(-7.8, 0), width * 0.22, width * 0.22, PL["gold"])
    c.ell(*P(-7.8, 0), width * 0.10, width * 0.10, PL["gem"])


def draw_girl(c, pose):
    """A small girl in a gothic battle dress hauling a slab greatsword."""
    bob = pose.get("bob", 0.0)
    lean = pose.get("lean", 0.0)
    legp = pose.get("legp", 0.0)     # -1..1 stride phase
    crouch = pose.get("crouch", 0.0)
    expr = pose.get("expr", "normal")
    fall = pose.get("fall", 0.0)      # death: 0 upright .. 1 collapsed

    fy = P_FY + bob
    hipy = fy - 25 + crouch + fall * 9
    shy = hipy - 23 + fall * 5
    cx = P_CX + lean * 0.9 + fall * 5
    hx = cx + lean * 1.4              # head drifts with the lean
    heady = shy - 15 + fall * 5

    # ---- back leg + back arm first (they read as shadowed depth) --------
    bfoot = (cx - 4 - legp * 9, fy - abs(legp) * 2)
    bknee = ((cx - 4 + bfoot[0]) / 2 - 1.5, (hipy + fy) / 2 + 1)
    c.limb((cx - 4, hipy), bknee, 4.2, 3.4, PL["sock_sh"], PL["sock_sh"])
    c.limb(bknee, bfoot, 3.4, 3.0, PL["sock_sh"], PL["sock_sh"])
    c.limb((bfoot[0] - 1, bfoot[1] - 2.5), (bfoot[0] + 3.5, bfoot[1] - 1.5),
           3.4, 3.0, PL["boot_sh"], PL["boot_sh"])

    # ---- cape behind the body ------------------------------------------
    sway = lean * 1.6 + legp * 2.5
    c.shaded([(cx - 9, shy - 2), (cx + 8, shy - 2),
              (cx + 6 - sway, hipy + 12), (cx - 10 - sway * 1.4, hipy + 10)],
             PL["cape"], PL["cape_sh"], PL["cape_hi"], amt=1.4)

    # ---- hair mass behind the head --------------------------------------
    c.ell(hx - 1, heady + 5, 13.5, 15.0, PL["hair_sh"])
    c.poly([(hx - 13, heady + 2), (hx - 15 - sway * 0.6, heady + 20),
            (hx - 9 - sway, heady + 30), (hx - 4, heady + 16)], PL["hair_sh"])
    c.poly([(hx + 12, heady + 2), (hx + 15 - sway * 0.6, heady + 21),
            (hx + 8 - sway, heady + 31), (hx + 3, heady + 16)], PL["hair"])

    # ---- torso: corset over dress --------------------------------------
    c.shaded([(cx - 8, shy - 1), (cx + 8, shy - 1), (cx + 6.5, hipy - 1),
              (cx - 6.5, hipy - 1)], PL["dress"], PL["dress_sh"], PL["dress_hi"])
    c.shaded([(cx - 7, shy + 6), (cx + 7, shy + 6), (cx + 6, hipy - 2),
              (cx - 6, hipy - 2)], PL["corset"], (18, 8, 26), PL["corset_hi"], amt=0.8)
    for k in range(3):  # corset lacing
        y = shy + 8.5 + k * 3.4
        c.stroke([(cx - 4.5, y), (cx + 4.5, y + 1.6)], PL["trim"], 0.55)
        c.stroke([(cx - 4.5, y + 1.6), (cx + 4.5, y)], PL["trim_sh"], 0.55)
    c.poly([(cx - 8.4, shy - 1.5), (cx + 8.4, shy - 1.5),
            (cx + 8, shy + 1.2), (cx - 8, shy + 1.2)], PL["trim"])

    # ---- skirt ----------------------------------------------------------
    sk = hipy
    c.shaded([(cx - 7, sk - 3), (cx + 7, sk - 3), (cx + 13 - sway, sk + 13),
              (cx - 13 - sway, sk + 13)], PL["dress"], PL["dress_sh"],
             PL["dress_hi"], amt=1.6)
    c.poly([(cx - 13 - sway, sk + 13), (cx + 13 - sway, sk + 13),
            (cx + 12.4 - sway, sk + 10.6), (cx - 12.4 - sway, sk + 10.6)], PL["trim"])
    for k in (-6, 0, 6):  # fold lines
        c.stroke([(cx + k * 0.55, sk - 1), (cx + k - sway, sk + 12)],
                 PL["dress_sh"], 0.6)

    # ---- front leg ------------------------------------------------------
    ffoot = (cx + 3 + legp * 9, fy - abs(legp) * 1.2)
    fknee = ((cx + 3 + ffoot[0]) / 2 + 1.5, (hipy + fy) / 2)
    c.limb((cx + 3, hipy + 2), fknee, 4.6, 3.6, PL["sock"], PL["sock_sh"], (58, 34, 70))
    c.limb(fknee, (ffoot[0], ffoot[1] - 3), 3.6, 3.1, PL["sock"], PL["sock_sh"])
    c.stroke([(fknee[0] - 3.6, fknee[1] - 4), (fknee[0] + 3.6, fknee[1] - 4.8)],
             PL["trim"], 0.8)
    # buckled boot
    c.limb((ffoot[0] - 2, ffoot[1] - 3.2), (ffoot[0] + 4, ffoot[1] - 2),
           3.8, 3.2, PL["boot"], PL["boot_sh"], PL["boot_hi"])
    c.stroke([(ffoot[0] - 2.4, ffoot[1] - 4.4), (ffoot[0] + 2.6, ffoot[1] - 3.8)],
             PL["gold"], 0.7)

    # ---- back arm --------------------------------------------------------
    bha = pose.get("backarm", 40.0)
    bsh = (cx - 7.5, shy + 4)
    bel = (bsh[0] - math.cos(math.radians(bha)) * 7, bsh[1] + math.sin(math.radians(bha)) * 7)
    bhand = pose.get("backhand")
    if bhand is None:
        bhand = (bel[0] - 4, bel[1] + 6)
    c.limb(bsh, bel, 3.4, 2.9, PL["skin_sh"], PL["skin_sh"])
    c.limb(bel, bhand, 2.9, 2.5, PL["skin_sh"], PL["skin_sh"])
    c.ell(bhand[0], bhand[1], 2.8, 2.8, PL["boot_sh"])

    # ---- head ------------------------------------------------------------
    hr = 11.5
    c.ell(hx, heady, hr, hr * 1.06, PL["skin_sh"])
    c.ell(hx - 1.2, heady - 1.2, hr * 0.94, hr * 0.99, PL["skin"])
    c.ell(hx - 3.5, heady - 4.0, hr * 0.42, hr * 0.36, PL["skin_hi"])
    # shadow cast by the fringe, kept up at the hairline so it does not
    # wash over the eyes and turn the whole upper face into one dark band
    c.poly([(hx - hr * 0.95, heady - 6.5), (hx + hr * 0.95, heady - 6.5),
            (hx + hr * 0.88, heady - 4.2), (hx - hr * 0.88, heady - 4.2)],
           PL["skin_sh"])

    # eyes
    ey = heady + 1.8
    for sx in (-4.4, 4.2):
        w, h = 3.1, 3.2
        if expr == "hurt":
            h = 2.0
        if expr == "dead":
            h = 1.1
        c.ell(hx + sx, ey, w, h, (250, 248, 252))
        c.ell(hx + sx + 0.5, ey + 0.3, w * 0.72, h * 0.82, PL["eye"])
        c.ell(hx + sx + 0.5, ey + 0.7, w * 0.42, h * 0.5, PL["eye_dk"])
        c.ell(hx + sx - 0.7, ey - 1.4, w * 0.34, h * 0.3, (255, 255, 255))
        # lash line (short + thin: a long thick one merges with the other
        # eye's lash and the brows into a single black bar across the face)
        c.stroke([(hx + sx - w * 0.9, ey - h * 0.95), (hx + sx + w * 0.55, ey - h * 1.05)],
                 PL["hair_sh"], 0.55)
    # brows
    bw = -1.0 if expr in ("attack", "hurt") else 0.0
    c.stroke([(hx - 7.0, ey - 5.8 + bw), (hx - 2.4, ey - 6.6)], PL["hair_hi"], 0.6)
    c.stroke([(hx + 2.2, ey - 6.6), (hx + 6.8, ey - 5.8 + bw)], PL["hair_hi"], 0.6)
    # nose + mouth
    c.stroke([(hx + 0.2, ey + 3.0), (hx + 1.0, ey + 3.6)], PL["skin_sh"], 0.6)
    if expr == "attack":
        c.ell(hx + 0.4, ey + 6.0, 2.4, 1.9, PL["mouth"])
        c.ell(hx + 0.4, ey + 5.4, 2.0, 0.9, (255, 255, 255))
    elif expr == "dead":
        c.stroke([(hx - 1.6, ey + 6.2), (hx + 2.4, ey + 6.2)], PL["mouth"], 0.7)
    else:
        c.stroke([(hx - 1.4, ey + 5.6), (hx + 0.4, ey + 6.4), (hx + 2.2, ey + 5.6)],
                 PL["mouth"], 0.7)

    # fringe / front hair
    c.poly([(hx - 12.6, heady - 2), (hx - 11, heady - 9.5), (hx - 4, heady - 12.6),
            (hx + 5, heady - 12.4), (hx + 11.4, heady - 8), (hx + 12.6, heady - 1),
            (hx + 8.5, heady - 5.5), (hx + 4.5, heady - 2.0), (hx + 1, heady - 6),
            (hx - 3.5, heady - 2.4), (hx - 8, heady - 6.0)], PL["hair"])
    c.poly([(hx - 10.5, heady - 6.5), (hx - 4.5, heady - 11.0),
            (hx + 3, heady - 10.6), (hx + 1, heady - 6.5), (hx - 4, heady - 4.2)],
           PL["hair_hi"])
    c.stroke([(hx - 8.2, heady - 8.6), (hx - 2.5, heady - 10.6), (hx + 3.2, heady - 9.6)],
             PL["hair_gloss"], 0.8)
    # ahoge
    c.stroke([(hx - 0.5, heady - 12.0), (hx + 2.5, heady - 17.5), (hx + 6.5, heady - 15.5)],
             PL["hair"], 1.1)
    # side locks framing the face
    c.poly([(hx - 12.6, heady - 2), (hx - 14.2, heady + 9), (hx - 10.2, heady + 8),
            (hx - 9.6, heady - 1)], PL["hair"])
    c.poly([(hx + 12.6, heady - 1), (hx + 14.0, heady + 10), (hx + 10.0, heady + 9),
            (hx + 9.6, heady)], PL["hair_sh"])

    # ---- pauldron + front arm + sword ------------------------------------
    grip = pose.get("grip", (cx + 13, shy + 7))
    ang = pose.get("ang", 38.0)
    blade = pose.get("blade", 46.0)

    fsh = (cx + 7.5, shy + 3)
    # elbow bends toward the grip, biased outward for a readable pose
    ex = (fsh[0] + grip[0]) / 2 + 2.0
    ey2 = (fsh[1] + grip[1]) / 2 + 3.0
    c.limb(fsh, (ex, ey2), 3.8, 3.2, PL["skin"], PL["skin_sh"], PL["skin_hi"])
    c.limb((ex, ey2), grip, 3.2, 2.7, PL["skin"], PL["skin_sh"], PL["skin_hi"])
    # steel pauldron
    c.shaded([(cx + 2.5, shy - 2.5), (cx + 11.5, shy - 1), (cx + 12, shy + 6),
              (cx + 3, shy + 5)], PL["steel"], PL["steel_sh"], PL["steel_hi"], amt=1.2)
    c.stroke([(cx + 3.5, shy + 1.4), (cx + 11.6, shy + 2.6)], PL["gold"], 0.7)

    draw_sword(c, grip, ang, blade, glow=pose.get("glow", False))
    # gauntlet hand over the grip
    c.ell(grip[0], grip[1], 3.2, 3.2, PL["boot"])
    c.ell(grip[0] - 0.8, grip[1] - 0.9, 2.1, 2.1, PL["boot_hi"])
    if pose.get("twohand"):
        g2 = (grip[0] - math.cos(math.radians(ang)) * 5.5,
              grip[1] - math.sin(math.radians(ang)) * 5.5)
        c.limb(bsh, (g2[0] - 5, g2[1] + 3), 3.4, 2.9, PL["skin_sh"], PL["skin_sh"])
        c.limb((g2[0] - 5, g2[1] + 3), g2, 2.9, 2.6, PL["skin"], PL["skin_sh"])
        c.ell(g2[0], g2[1], 3.0, 3.0, PL["boot"])


def player_frame(pose):
    c = Canvas(P_W, P_H)
    draw_girl(c, pose)
    img = c.finish()
    if pose.get("flash"):
        img = tint(img, (255, 120, 130), 0.55)
    if pose.get("fadev") is not None:
        img = fade(img, pose["fadev"])
    return img


def build_player():
    b = AnimBuilder()

    # idle: sword resting point-down in front, subtle breathing
    idle = []
    for k, (bb, aa) in enumerate([(0, 44), (-1, 42), (0, 44), (1, 46)]):
        idle.append(player_frame({"bob": bb, "ang": aa, "blade": 44,
                                  "grip": (P_CX + 13, P_FY - 46 + bb)}))
    b.add("idle", idle, 5)

    run = []
    for k, lp in enumerate([1.0, 0.35, -1.0, -0.35]):
        run.append(player_frame({"legp": lp, "lean": 3.2, "bob": -abs(lp) * 1.2,
                                 "ang": 30 + k * 4, "blade": 44,
                                 "grip": (P_CX + 14, P_FY - 44)}))
    b.add("run", run, 11)

    # A: horizontal cleave, wind up behind then sweep through
    a1 = [
        {"ang": 208, "blade": 46, "grip": (P_CX + 4, P_FY - 56), "lean": -3, "expr": "attack"},
        {"ang": 150, "blade": 50, "grip": (P_CX + 12, P_FY - 60), "lean": 1, "expr": "attack"},
        {"ang": 60, "blade": 50, "grip": (P_CX + 16, P_FY - 52), "lean": 4, "expr": "attack", "glow": True},
        {"ang": 10, "blade": 48, "grip": (P_CX + 17, P_FY - 44), "lean": 3, "expr": "attack"},
    ]
    b.add("attack1", [player_frame(p) for p in a1], 14, loop=False)

    # B: overhead chop
    a2 = [
        {"ang": 256, "blade": 46, "grip": (P_CX + 8, P_FY - 58), "bob": -2, "twohand": True, "expr": "attack"},
        {"ang": 300, "blade": 50, "grip": (P_CX + 12, P_FY - 62), "bob": -3, "twohand": True, "expr": "attack"},
        {"ang": 20, "blade": 48, "grip": (P_CX + 15, P_FY - 50), "bob": 1, "lean": 3, "expr": "attack", "glow": True},
        {"ang": 52, "blade": 48, "grip": (P_CX + 16, P_FY - 44), "bob": 2, "crouch": 2, "lean": 2, "expr": "attack"},
    ]
    b.add("attack2", [player_frame(p) for p in a2], 14, loop=False)

    # X: thrust
    a3 = [
        {"ang": 6, "blade": 40, "grip": (P_CX + 2, P_FY - 50), "lean": -3, "twohand": True, "expr": "attack"},
        {"ang": 2, "blade": 50, "grip": (P_CX + 12, P_FY - 50), "lean": 2, "twohand": True, "expr": "attack"},
        {"ang": 0, "blade": 46, "grip": (P_CX + 14, P_FY - 49), "lean": 5, "legp": 0.5, "expr": "attack", "glow": True},
        {"ang": 8, "blade": 44, "grip": (P_CX + 10, P_FY - 48), "lean": 1, "expr": "attack"},
    ]
    b.add("attack3", [player_frame(p) for p in a3], 16, loop=False)

    # Y: heavy rising cleave finisher (stays in the forward hemisphere so
    # the blade never leaves the frame)
    a4 = [
        {"ang": 130, "blade": 46, "grip": (P_CX + 6, P_FY - 42), "crouch": 3, "expr": "attack"},
        {"ang": 60, "blade": 52, "grip": (P_CX + 14, P_FY - 48), "lean": 2, "expr": "attack"},
        {"ang": 348, "blade": 46, "grip": (P_CX + 15, P_FY - 52), "lean": 4, "bob": -2, "expr": "attack", "glow": True},
        {"ang": 300, "blade": 48, "grip": (P_CX + 14, P_FY - 54), "bob": -3, "expr": "attack", "glow": True},
        {"ang": 264, "blade": 44, "grip": (P_CX + 8, P_FY - 52), "bob": -1, "expr": "attack"},
    ]
    b.add("attack4", [player_frame(p) for p in a4], 14, loop=False)

    par = [
        {"ang": 288, "blade": 44, "grip": (P_CX + 12, P_FY - 52), "twohand": True, "expr": "attack"},
        {"ang": 280, "blade": 44, "grip": (P_CX + 13, P_FY - 54), "twohand": True,
         "expr": "attack", "glow": True, "flash": 0.3},
    ]
    b.add("parry", [player_frame(p) for p in par], 12, loop=False)

    blk = [
        {"ang": 284, "blade": 44, "grip": (P_CX + 11, P_FY - 50), "twohand": True, "crouch": 2},
        {"ang": 284, "blade": 44, "grip": (P_CX + 11, P_FY - 50), "twohand": True,
         "crouch": 2, "flash": 0.10},
    ]
    b.add("block", [player_frame(p) for p in blk], 6)

    hit = [
        {"ang": 70, "blade": 42, "grip": (P_CX + 8, P_FY - 40), "lean": -5,
         "expr": "hurt", "flash": 1.0, "crouch": 2},
        {"ang": 76, "blade": 42, "grip": (P_CX + 7, P_FY - 38), "lean": -6,
         "expr": "hurt", "crouch": 3},
    ]
    b.add("hit", [player_frame(p) for p in hit], 9, loop=False)

    dth = []
    # unlike the enemies, her last death frame is held for as long as the
    # game-over screen is up, so it must not fade out to nothing
    for k, (f, fv) in enumerate([(0.15, 1.0), (0.45, 1.0), (0.75, 1.0),
                                 (1.0, 0.95), (1.0, 0.9)]):
        # the blade sweeps to flat as she drops, so it lies on the ground
        # rather than pointing down through it
        dth.append(player_frame({"fall": f, "ang": 120 + f * 55, "blade": 40,
                                 "grip": (P_CX + 10 + f * 8, P_FY - 44 + f * 32),
                                 "expr": "dead", "fadev": fv, "crouch": f * 3}))
    b.add("death", dth, 6, loop=False)

    meta = make_sheet(b.frames, P_W, P_H, "player", P_ANCHOR, zoom=1.34)
    meta["anims"] = b.anims
    manifest["sheets"]["player"] = meta


# ================================================================== IMP
E_W = E_H = 96
I_CX, I_FY = 48.0, 86.0

IMP = dict(
    skin=(186, 52, 42), skin_sh=(112, 24, 24), skin_hi=(232, 104, 72),
    belly=(226, 156, 96), belly_sh=(170, 100, 60),
    horn=(238, 224, 186), horn_sh=(176, 158, 122),
    claw=(28, 20, 24), claw_hi=(84, 70, 72),
    eye=(255, 224, 70), eye_glow=(255, 250, 200),
    wing=(96, 24, 30), wing_sh=(58, 12, 18),
)


def draw_imp(c, pose):
    crouch = pose.get("crouch", 0.0)
    leg = pose.get("leg", 0.0)
    lunge = pose.get("lunge", 0.0)
    arm = pose.get("arm", 0.0)      # -1 arms back/up .. +1 arms forward/down
    cx = I_CX + pose.get("lean", 0.0)
    fy = I_FY
    hipy = fy - 17 + crouch
    bodyy = hipy - 13
    heady = bodyy - 11 + crouch * 0.3

    # tail
    c.stroke([(cx - 7, hipy - 2), (cx - 17, hipy - 6), (cx - 19, hipy - 15)],
             IMP["skin_sh"], 2.2)
    c.poly([(cx - 19, hipy - 15), (cx - 23, hipy - 20), (cx - 16, hipy - 20)],
           IMP["claw"])
    # wing nubs
    for s in (-1, 1):
        c.poly([(cx + 8 * s, bodyy - 1), (cx + 20 * s, bodyy - 11),
                (cx + 17 * s, bodyy + 3)], IMP["wing_sh"] if s < 0 else IMP["wing"])

    # legs
    for s, ph in ((-1, leg), (1, -leg)):
        fx = cx + s * 5 + ph * 5
        c.limb((cx + s * 4.5, hipy), (fx, fy - 4), 3.6, 3.0,
               IMP["skin"] if s > 0 else IMP["skin_sh"], IMP["skin_sh"])
        c.poly([(fx - 4, fy - 4), (fx + 5, fy - 4), (fx + 4, fy), (fx - 3.5, fy)],
               IMP["claw"])
        for k in range(3):
            c.poly([(fx - 3 + k * 3, fy), (fx - 1.6 + k * 3, fy + 1.6),
                    (fx - 0.4 + k * 3, fy)], IMP["claw_hi"])

    # pot-bellied torso
    c.shaded([(cx - 9, bodyy - 2), (cx + 9, bodyy - 2), (cx + 11, hipy),
              (cx - 11, hipy)], IMP["skin"], IMP["skin_sh"], IMP["skin_hi"], amt=1.5)
    c.ell(cx + 0.5, hipy - 5, 8.0, 6.4, IMP["belly_sh"])
    c.ell(cx, hipy - 6, 7.0, 5.4, IMP["belly"])
    for k in range(3):  # belly plating
        c.stroke([(cx - 6 + k * 0.6, hipy - 9 + k * 3.4), (cx + 6 - k * 0.6, hipy - 9 + k * 3.4)],
                 IMP["belly_sh"], 0.6)

    # arms + claws
    for s in (-1, 1):
        sh = (cx + s * 9, bodyy + 2)
        reach = lunge * (7 if s > 0 else 3)
        # arm swings opposite the leg on that side, so the walk cycle reads
        swing = arm * (1 if s > 0 else -1)
        el = (sh[0] + s * (6 + reach * 0.6) + swing * 1.5,
              sh[1] + 6 - reach * 0.8 - swing * 3.0)
        hd = (el[0] + s * (6 + reach) + swing * 2.5,
              el[1] + 4 - reach * 0.9 - swing * 5.0)
        col = IMP["skin"] if s > 0 else IMP["skin_sh"]
        c.limb(sh, el, 3.2, 2.7, col, IMP["skin_sh"])
        c.limb(el, hd, 2.7, 2.2, col, IMP["skin_sh"])
        for k in (-1, 0, 1):
            c.poly([(hd[0], hd[1]), (hd[0] + s * 6, hd[1] + k * 3.2 - 1),
                    (hd[0] + s * 2.4, hd[1] + k * 1.6 + 1.4)], IMP["claw"])

    # head
    c.ell(cx, heady, 11.0, 9.6, IMP["skin_sh"])
    c.ell(cx - 1, heady - 1, 10.0, 8.6, IMP["skin"])
    c.ell(cx - 3.5, heady - 3.5, 4.2, 3.2, IMP["skin_hi"])
    # horns
    for s in (-1, 1):
        c.poly([(cx + s * 6.5, heady - 6.5), (cx + s * 8.5, heady - 17),
                (cx + s * 11.5, heady - 19.5), (cx + s * 9.5, heady - 12),
                (cx + s * 10.5, heady - 5)], IMP["horn_sh"])
        c.poly([(cx + s * 7.0, heady - 7.5), (cx + s * 8.8, heady - 16),
                (cx + s * 10.6, heady - 18), (cx + s * 9.2, heady - 9)], IMP["horn"])
    # ears
    for s in (-1, 1):
        c.poly([(cx + s * 9.5, heady - 1), (cx + s * 17, heady - 4),
                (cx + s * 10, heady + 5)], IMP["skin_sh"])
    # eyes (glow)
    for s in (-1, 1):
        c.ell(cx + s * 4.2 - 0.6, heady - 1.2, 3.6, 3.0, (40, 10, 10))
        c.ell(cx + s * 4.2 - 0.6, heady - 1.2, 2.8, 2.3, IMP["eye"])
        c.ell(cx + s * 4.2 - 1.2, heady - 1.9, 1.2, 1.0, IMP["eye_glow"])
        c.poly([(cx + s * 4.2 - 0.6, heady - 3.4), (cx + s * 4.2 + 1.0, heady - 0.6),
                (cx + s * 4.2 - 2.2, heady - 0.6)], (30, 8, 8))
    # grin
    c.poly([(cx - 6, heady + 4.2), (cx + 6, heady + 4.2), (cx + 4.4, heady + 7.4),
            (cx - 4.4, heady + 7.4)], (48, 10, 14))
    for k in range(5):
        x = cx - 5 + k * 2.5
        c.poly([(x, heady + 4.4), (x + 1.6, heady + 4.4), (x + 0.8, heady + 6.6)],
               (250, 244, 226))


def imp_frame(pose):
    c = Canvas(E_W, E_H)
    draw_imp(c, pose)
    img = c.finish()
    if pose.get("flash"):
        img = tint(img, (255, 200, 200), float(pose["flash"]) * 0.6)
    if pose.get("fadev") is not None:
        img = fade(img, pose["fadev"])
    return img


def build_imp():
    b = AnimBuilder()
    b.add("idle", [imp_frame({"crouch": c, "arm": a})
                   for c, a in ((0, 0.0), (1.6, 0.35), (0, 0.0))], 5)
    b.add("walk", [imp_frame({"leg": v, "arm": -v * 0.9, "lean": v * 1.2,
                              "crouch": abs(v) * -1.6})
                   for v in (1.0, 0.0, -1.0, 0.0)], 10)
    b.add("attack", [imp_frame({"lunge": -0.35, "arm": -0.9, "crouch": 1.5,
                                "lean": -2.0}),
                     imp_frame({"lunge": 0.55, "arm": 0.4, "crouch": -1.0,
                                "lean": 1.5}),
                     imp_frame({"lunge": 1.0, "arm": 0.9, "crouch": -2.0,
                                "lean": 3.0})], 12, loop=False)
    b.add("death", [imp_frame({"crouch": 3, "fadev": 0.9, "flash": 1.0}),
                    imp_frame({"crouch": 7, "fadev": 0.55}),
                    imp_frame({"crouch": 10, "fadev": 0.2})], 7, loop=False)
    meta = make_sheet(b.frames, E_W, E_H, "imp", (I_CX, I_FY), zoom=0.86)
    meta["anims"] = b.anims
    manifest["sheets"]["imp"] = meta


# ================================================================ THRALL
T_CX, T_FY = 48.0, 88.0
THR = dict(
    skin=(142, 128, 158), skin_sh=(88, 76, 106), skin_hi=(184, 172, 200),
    robe=(58, 44, 74), robe_sh=(34, 24, 46), robe_hi=(88, 70, 108),
    hood=(44, 32, 58), hood_sh=(24, 16, 34),
    eye=(228, 96, 240), eye_glow=(255, 214, 255),
    bone=(226, 218, 206), claw=(30, 24, 34),
)


def draw_thrall(c, pose):
    leg = pose.get("leg", 0.0)
    lunge = pose.get("lunge", 0.0)
    sway = pose.get("sway", 0.0)
    arm = pose.get("arm", 0.0)
    bob = pose.get("bob", 0.0)
    cx, fy = T_CX + sway * 0.4, T_FY
    hipy = fy - 22 + bob
    shy = hipy - 22
    heady = shy - 9

    # legs (gaunt)
    for s, ph in ((-1, leg), (1, -leg)):
        fx = cx + s * 4 + ph * 6
        col = THR["skin"] if s > 0 else THR["skin_sh"]
        c.limb((cx + s * 3.5, hipy), (fx, fy - 3), 3.0, 2.4, col, THR["skin_sh"])
        c.poly([(fx - 3.4, fy - 3), (fx + 4.4, fy - 3), (fx + 3.6, fy), (fx - 3, fy)],
               THR["claw"])

    # tattered robe
    hem = [(cx - 12 + sway, hipy + 11), (cx - 8 + sway, hipy + 6),
           (cx - 4 + sway, hipy + 13), (cx + 1 + sway, hipy + 5),
           (cx + 5 + sway, hipy + 12), (cx + 9 + sway, hipy + 6),
           (cx + 12 + sway, hipy + 10)]
    c.shaded([(cx - 8, shy + 2), (cx + 8, shy + 2)] + list(reversed(hem)),
             THR["robe"], THR["robe_sh"], THR["robe_hi"], amt=1.4)
    for k in (-4, 2):  # rag folds
        c.stroke([(cx + k, shy + 6), (cx + k * 1.6 + sway, hipy + 8)],
                 THR["robe_sh"], 0.7)

    # ribcage showing through the torn robe
    c.shaded([(cx - 6, shy + 1), (cx + 6, shy + 1), (cx + 5, shy + 12),
              (cx - 5, shy + 12)], THR["skin"], THR["skin_sh"], THR["skin_hi"])
    for k in range(3):
        y = shy + 3.4 + k * 3.0
        c.stroke([(cx - 4.6, y), (cx + 4.6, y)], THR["bone"], 0.6)

    # long arms with talons
    for s in (-1, 1):
        sh = (cx + s * 7.5, shy + 3)
        reach = lunge * (10 if s > 0 else 5)
        swing = arm * (1 if s > 0 else -1)
        el = (sh[0] + s * (8 + reach * 0.5) + swing * 2.0,
              sh[1] + 8 - reach * 0.9 - swing * 3.5)
        hd = (el[0] + s * (8 + reach * 0.7) + swing * 3.0,
              el[1] + 6 - reach * 1.1 - swing * 6.0)
        col = THR["skin"] if s > 0 else THR["skin_sh"]
        c.limb(sh, el, 2.8, 2.3, col, THR["skin_sh"])
        c.limb(el, hd, 2.3, 1.9, col, THR["skin_sh"])
        for k in (-1, 0, 1):
            c.poly([(hd[0], hd[1]), (hd[0] + s * 7.5, hd[1] + k * 3.4),
                    (hd[0] + s * 3, hd[1] + k * 1.5 + 1.6)], THR["claw"])

    # hooded head: shadowed void with two burning eyes
    c.ell(cx, heady, 8.4, 9.0, THR["skin_sh"])
    c.poly([(cx - 10, heady + 6), (cx - 10.5, heady - 5), (cx - 4, heady - 12),
            (cx + 4, heady - 12), (cx + 10.5, heady - 5), (cx + 10, heady + 6),
            (cx + 6, heady + 2), (cx - 6, heady + 2)], THR["hood"])
    c.poly([(cx - 9.0, heady - 4), (cx - 3.6, heady - 10.4), (cx + 2, heady - 10.2),
            (cx + 5, heady - 6)], THR["robe_hi"])
    c.poly([(cx - 6.6, heady - 2.5), (cx + 6.6, heady - 2.5), (cx + 5, heady + 5),
            (cx - 5, heady + 5)], (16, 10, 22))
    for s in (-1, 1):
        c.ell(cx + s * 3.2, heady + 0.6, 2.6, 1.9, THR["eye"])
        c.ell(cx + s * 3.2, heady + 0.6, 1.3, 0.9, THR["eye_glow"])
    # hood shoulders
    c.poly([(cx - 11, heady + 5), (cx + 11, heady + 5), (cx + 9, shy + 4),
            (cx - 9, shy + 4)], THR["hood_sh"])


def thrall_frame(pose):
    c = Canvas(E_W, E_H)
    draw_thrall(c, pose)
    img = c.finish()
    if pose.get("flash"):
        img = tint(img, (255, 210, 255), float(pose["flash"]) * 0.6)
    if pose.get("fadev") is not None:
        img = fade(img, pose["fadev"])
    return img


def build_thrall():
    b = AnimBuilder()
    b.add("idle", [thrall_frame({"sway": 0.0, "bob": 0.0, "arm": 0.0}),
                   thrall_frame({"sway": 1.4, "bob": -1.4, "arm": 0.3})], 3)
    b.add("walk", [thrall_frame({"leg": v, "sway": -v * 1.8, "arm": -v * 0.8,
                                 "bob": -abs(v) * 1.6})
                   for v in (1.0, 0.0, -1.0, 0.0)], 9)
    b.add("attack", [thrall_frame({"lunge": -0.3, "arm": -1.0, "bob": 1.5}),
                     thrall_frame({"lunge": 0.5, "arm": 0.3}),
                     thrall_frame({"lunge": 1.0, "arm": 0.9, "bob": -1.5})],
          11, loop=False)
    b.add("death", [thrall_frame({"fadev": 0.85, "flash": 1.0}),
                    thrall_frame({"sway": 3, "fadev": 0.5}),
                    thrall_frame({"sway": 5, "fadev": 0.18})], 7, loop=False)
    meta = make_sheet(b.frames, E_W, E_H, "thrall", (T_CX, T_FY), zoom=0.98)
    meta["anims"] = b.anims
    manifest["sheets"]["thrall"] = meta


# ================================================================== BOSS
B_W = B_H = 144
B_CX, B_FY = 72.0, 134.0

BS = dict(
    skin=(104, 26, 54), skin_sh=(58, 12, 30), skin_hi=(150, 48, 78),
    armor=(176, 138, 62), armor_sh=(104, 76, 30), armor_hi=(244, 214, 138),
    horn=(48, 38, 44), horn_hi=(96, 82, 88), horn_tip=(212, 196, 190),
    wing=(52, 16, 36), wing_sh=(30, 8, 22), wing_mem=(92, 26, 52),
    eye=(255, 96, 48), eye_glow=(255, 226, 170),
    claw=(22, 16, 20),
    ember=(255, 140, 52),
)


def draw_boss(c, pose):
    bob = pose.get("bob", 0.0)
    crouch = pose.get("crouch", 0.0)
    wf = pose.get("wflap", 0.0)
    aL = pose.get("armL", 0.0)
    aR = pose.get("armR", 0.0)
    fall = pose.get("fall", 0.0)
    cx = B_CX
    fy = B_FY
    hipy = fy - 34 + crouch + fall * 14
    shy = hipy - 30 + fall * 6
    heady = shy - 13 + bob

    # ---- wings (behind everything) -------------------------------------
    for s in (-1, 1):
        base = (cx + s * 14, shy - 2)
        tipx = cx + s * (56 + wf * 0.4)
        tipy = shy - 30 - wf
        col = BS["wing"] if s > 0 else BS["wing_sh"]
        c.poly([base, (tipx, tipy), (cx + s * 50, shy + 6), (cx + s * 30, shy + 20)], col)
        # membrane panels
        for k in range(3):
            f = 0.3 + k * 0.28
            c.stroke([base, (base[0] + (tipx - base[0]) * (0.55 + k * 0.18),
                             base[1] + ((shy + 16) - base[1]) * f)], BS["wing_mem"], 1.4)
        c.stroke([base, (tipx, tipy)], BS["wing_mem"], 1.6)

    # ---- legs / hooves ---------------------------------------------------
    for s in (-1, 1):
        kx = cx + s * 13
        c.limb((cx + s * 10, hipy), (kx, hipy + 18), 8.0, 6.0, BS["skin"], BS["skin_sh"],
               BS["skin_hi"])
        c.limb((kx, hipy + 18), (kx + s * 2, fy - 6), 6.0, 5.0, BS["skin"], BS["skin_sh"])
        c.poly([(kx + s * 2 - 7, fy - 8), (kx + s * 2 + 7, fy - 8),
                (kx + s * 2 + 6, fy), (kx + s * 2 - 6, fy)], BS["horn"])
        c.poly([(kx + s * 2 - 7, fy - 8), (kx + s * 2 + 7, fy - 8),
                (kx + s * 2 + 6, fy - 5), (kx + s * 2 - 6, fy - 5)], BS["horn_hi"])

    # ---- torso -----------------------------------------------------------
    c.shaded([(cx - 22, shy - 4), (cx + 22, shy - 4), (cx + 17, hipy + 4),
              (cx - 17, hipy + 4)], BS["skin"], BS["skin_sh"], BS["skin_hi"], amt=2.0)
    # armored chest plate with a burning rune
    c.shaded([(cx - 20, shy + 2), (cx + 20, shy + 2), (cx + 16, shy + 26),
              (cx - 16, shy + 26)], BS["armor"], BS["armor_sh"], BS["armor_hi"], amt=1.6)
    c.stroke([(cx - 19, shy + 6), (cx + 19, shy + 6)], BS["armor_hi"], 1.0)
    c.ell(cx, shy + 15, 7.5, 7.5, BS["armor_sh"])
    c.ell(cx, shy + 15, 5.4, 5.4, BS["ember"])
    c.ell(cx, shy + 15, 2.6, 2.6, BS["eye_glow"])
    for k in range(4):  # abdominal plates
        c.stroke([(cx - 13 + k * 0.9, hipy - 8 + k * 3.4),
                  (cx + 13 - k * 0.9, hipy - 8 + k * 3.4)], BS["skin_sh"], 0.9)
    # shoulder pauldrons with spikes
    for s in (-1, 1):
        c.shaded([(cx + s * 14, shy - 8), (cx + s * 27, shy - 3),
                  (cx + s * 25, shy + 9), (cx + s * 13, shy + 6)],
                 BS["armor"], BS["armor_sh"], BS["armor_hi"], amt=1.4)
        c.poly([(cx + s * 20, shy - 6), (cx + s * 26, shy - 18),
                (cx + s * 27, shy - 3)], BS["horn"])

    # ---- arms ------------------------------------------------------------
    for s, a in ((-1, aL), (1, aR)):
        sh = (cx + s * 20, shy + 6)
        el = (sh[0] + s * (14 + a * 0.5), sh[1] + 16 - a * 0.9)
        hd = (el[0] + s * (12 + a), el[1] + 12 - a * 1.3)
        col = BS["skin"] if s > 0 else BS["skin_sh"]
        c.limb(sh, el, 7.2, 5.6, col, BS["skin_sh"], BS["skin_hi"])
        c.limb(el, hd, 5.6, 4.4, col, BS["skin_sh"])
        c.ell(hd[0], hd[1], 5.0, 5.0, BS["skin_sh"])
        for k in (-1, 0, 1):
            c.poly([(hd[0], hd[1]), (hd[0] + s * 9, hd[1] + k * 4.4 + 1),
                    (hd[0] + s * 4, hd[1] + k * 2.4 + 3)], BS["claw"])

    # ---- head: horned skull ---------------------------------------------
    c.shaded([(cx - 13, heady - 8), (cx + 13, heady - 8), (cx + 10, heady + 12),
              (cx - 10, heady + 12)], BS["skin"], BS["skin_sh"], BS["skin_hi"], amt=1.4)
    # snout
    c.shaded([(cx - 7, heady + 6), (cx + 7, heady + 6), (cx + 5, heady + 16),
              (cx - 5, heady + 16)], BS["skin"], BS["skin_sh"], BS["skin_hi"])
    c.ell(cx - 2.4, heady + 14, 1.4, 1.0, (20, 8, 14))
    c.ell(cx + 2.4, heady + 14, 1.4, 1.0, (20, 8, 14))
    # jaw + fangs
    c.poly([(cx - 7, heady + 15), (cx + 7, heady + 15), (cx + 5.5, heady + 19),
            (cx - 5.5, heady + 19)], (34, 10, 20))
    for k in range(4):
        x = cx - 5 + k * 3.2
        c.poly([(x, heady + 15.5), (x + 2.0, heady + 15.5), (x + 1.0, heady + 19.5)],
               (238, 230, 214))
    # great horns
    for s in (-1, 1):
        c.poly([(cx + s * 11, heady - 6), (cx + s * 20, heady - 22),
                (cx + s * 30, heady - 34), (cx + s * 27, heady - 20),
                (cx + s * 17, heady - 5)], BS["horn"])
        c.poly([(cx + s * 12.5, heady - 8), (cx + s * 20, heady - 21),
                (cx + s * 26, heady - 30), (cx + s * 23, heady - 19)], BS["horn_hi"])
        c.poly([(cx + s * 28, heady - 31), (cx + s * 31, heady - 35),
                (cx + s * 27.5, heady - 26)], BS["horn_tip"])
    # burning eyes under a heavy brow
    c.poly([(cx - 12, heady - 4), (cx + 12, heady - 4), (cx + 10, heady + 2),
            (cx - 10, heady + 2)], BS["skin_sh"])
    for s in (-1, 1):
        c.ell(cx + s * 6, heady + 2.5, 4.4, 3.0, (60, 12, 10))
        c.ell(cx + s * 6, heady + 2.5, 3.2, 2.1, BS["eye"])
        c.ell(cx + s * 6 - 0.8, heady + 1.9, 1.4, 1.0, BS["eye_glow"])


def boss_frame(pose):
    c = Canvas(B_W, B_H)
    draw_boss(c, pose)
    img = c.finish()
    if pose.get("flash"):
        img = tint(img, (255, 240, 220), float(pose["flash"]) * 0.5)
    if pose.get("fadev") is not None:
        img = fade(img, pose["fadev"])
    return img


def build_boss():
    b = AnimBuilder()
    b.add("idle", [boss_frame({"bob": 0, "wflap": 0}),
                   boss_frame({"bob": -2, "wflap": 6}),
                   boss_frame({"bob": 0, "wflap": 2})], 5)
    b.add("walk", [boss_frame({"crouch": 0, "wflap": 2, "armL": 2, "armR": -2}),
                   boss_frame({"crouch": 2, "wflap": 5, "armL": -2, "armR": 2}),
                   boss_frame({"crouch": 0, "wflap": 2, "armL": 2, "armR": -2})], 7)
    b.add("attack_sweep", [boss_frame({"armR": -4, "wflap": 2}),
                           boss_frame({"armR": 7, "wflap": 8}),
                           boss_frame({"armR": 12, "wflap": 10, "flash": 0.3})],
          10, loop=False)
    b.add("attack_slam", [boss_frame({"crouch": -6, "armL": -6, "armR": -6, "wflap": 8}),
                          boss_frame({"crouch": -10, "armL": -9, "armR": -9, "wflap": 12}),
                          boss_frame({"crouch": 8, "armL": 6, "armR": 6, "flash": 0.3})],
          9, loop=False)
    b.add("attack_cast", [boss_frame({"armL": -8, "armR": -8, "wflap": -2}),
                          boss_frame({"armL": -12, "armR": -12, "wflap": 10, "flash": 0.35})],
          8, loop=False)
    b.add("hurt", [boss_frame({"crouch": 3, "flash": 1.0}),
                   boss_frame({"crouch": 1})], 9, loop=False)
    b.add("death", [boss_frame({"fall": 0.2, "fadev": 1.0, "flash": 1.0}),
                    boss_frame({"fall": 0.5, "fadev": 0.8}),
                    boss_frame({"fall": 0.8, "fadev": 0.5}),
                    boss_frame({"fall": 1.0, "fadev": 0.15})], 5, loop=False)
    meta = make_sheet(b.frames, B_W, B_H, "boss", (B_CX, B_FY), zoom=1.75)
    meta["anims"] = b.anims
    manifest["sheets"]["boss"] = meta


# ============================================================== FX / UI
def build_fireball():
    F = 48
    frames = []
    for i in range(6):
        c = Canvas(F, F)
        ph = i / 6.0 * math.tau
        cx = cy = F / 2
        # trailing flame tail
        c.poly([(cx + 14, cy - 7), (cx + 22, cy), (cx + 14, cy + 7)], (180, 40, 20))
        for k in range(5):
            a = ph + k * 1.26
            r = 9 + math.sin(a) * 2.2
            c.ell(cx + math.cos(a) * 3, cy + math.sin(a) * 3, r * 0.6, r * 0.6,
                  (236, 92, 28))
        c.ell(cx, cy, 9.5, 9.5, (250, 148, 40))
        c.ell(cx - 1, cy - 1, 6.4, 6.4, (255, 214, 96))
        c.ell(cx - 1.5, cy - 1.5, 3.4, 3.4, (255, 252, 224))
        frames.append(dilate_edges(c.finish(outline=(70, 16, 8, 255))))
    meta = make_sheet(frames, F, F, "fireball", (F / 2, F / 2), zoom=0.95)
    meta["anims"] = {"spin": {"start": 0, "count": 6, "fps": 14, "loop": 1}}
    manifest["sheets"]["fireball"] = meta


def build_shadow():
    """Soft elliptical drop shadow so actors read as standing on the ground."""
    W, H = 64, 32
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = img.load()
    for y in range(H):
        for x in range(W):
            dx = (x - W / 2) / (W / 2 - 1)
            dy = (y - H / 2) / (H / 2 - 1)
            d = math.hypot(dx, dy)
            a = max(0.0, 1.0 - d)
            px[x, y] = (0, 0, 0, int((a ** 1.6) * 150))
    img.save(os.path.join(SPR_DIR, "shadow.png"))
    manifest["sheets"]["shadow"] = {"file": "shadow.png", "frame_w": W, "frame_h": H,
                                     "sheet_w": W, "sheet_h": H, "cols": 1,
                                     "anchor_x": W / 2, "anchor_y": H / 2,
                                     "draw_w": W, "draw_h": H}


def build_ui_icons():
    S = 40
    specs = [("A", (40, 190, 96)), ("B", (226, 58, 54)), ("X", (58, 110, 232)),
             ("Y", (242, 206, 56)), ("L", (206, 210, 222)), ("R", (206, 210, 222))]
    frames = []
    for letter, col in specs:
        c = Canvas(S, S)
        r = S / 2 - 3
        c.ell(S / 2, S / 2, r, r, tuple(int(v * 0.45) for v in col))
        c.ell(S / 2 - 0.8, S / 2 - 1.2, r * 0.88, r * 0.88, col)
        c.ell(S / 2 - r * 0.3, S / 2 - r * 0.42, r * 0.42, r * 0.3,
              tuple(min(255, int(v * 1.35) + 40) for v in col))
        img = c.finish(outline=(18, 14, 22, 255))
        d = ImageDraw.Draw(img)
        # blocky glyph, drawn at final res so it stays razor sharp
        _draw_glyph(d, letter, S, Canvas.PAD)
        frames.append(dilate_edges(img))
    meta = make_sheet(frames, S, S, "ui_icons", (S / 2, S / 2))
    meta["icons"] = {s[0]: i for i, s in enumerate(specs)}
    manifest["sheets"]["ui_icons"] = meta


def _draw_glyph(d, ch, S, off=0):
    """Minimal 5x7-style vector glyphs for the button icons."""
    c = (22, 16, 24, 255)
    w = max(2, int(S * 0.075))
    x0, x1 = S * 0.32, S * 0.68
    y0, y1 = S * 0.28, S * 0.72
    ym = (y0 + y1) / 2
    segs = {
        "A": [((x0, y1), (S / 2, y0)), ((S / 2, y0), (x1, y1)),
              ((x0 + S * 0.06, ym + S * 0.08), (x1 - S * 0.06, ym + S * 0.08))],
        "B": [((x0, y0), (x0, y1)), ((x0, y0), (x1 - S * 0.04, y0 + S * 0.06)),
              ((x1 - S * 0.04, y0 + S * 0.06), (x0, ym)), ((x0, ym), (x1, ym + S * 0.06)),
              ((x1, ym + S * 0.06), (x0, y1))],
        "X": [((x0, y0), (x1, y1)), ((x1, y0), (x0, y1))],
        "Y": [((x0, y0), (S / 2, ym)), ((x1, y0), (S / 2, ym)), ((S / 2, ym), (S / 2, y1))],
        "L": [((x0 + S * 0.04, y0), (x0 + S * 0.04, y1)), ((x0 + S * 0.04, y1), (x1, y1))],
        "R": [((x0, y1), (x0, y0)), ((x0, y0), (x1, y0 + S * 0.08)),
              ((x1, y0 + S * 0.08), (x0, ym)), ((x0, ym), (x1, y1))],
    }
    for p0, p1 in segs[ch]:
        d.line([(p0[0] + off, p0[1] + off), (p1[0] + off, p1[1] + off)],
               fill=c, width=w)


# =========================================================== BACKGROUNDS
def vgrad(w, h, stops):
    """Vertical gradient from (pos, rgb) stops, with a touch of ordered
    dithering so 16-bit RGB565 output doesn't band."""
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        lo = stops[0]
        hi = stops[-1]
        for i in range(len(stops) - 1):
            if stops[i][0] <= t <= stops[i + 1][0]:
                lo, hi = stops[i], stops[i + 1]
                break
        span = max(1e-6, hi[0] - lo[0])
        f = (t - lo[0]) / span
        col = [lo[1][k] + (hi[1][k] - lo[1][k]) * f for k in range(3)]
        for x in range(w):
            dither = ((x & 1) ^ (y & 1)) * 1.2 - 0.6
            px[x, y] = tuple(max(0, min(255, int(col[k] + dither))) for k in range(3))
    return img


def build_backgrounds():
    # ---- sky: full-height gradient so nothing can show through as black --
    sky = vgrad(64, 256, [(0.0, (12, 8, 30)), (0.35, (44, 16, 54)),
                          (0.62, (122, 32, 52)), (0.82, (206, 78, 44)),
                          (1.0, (250, 152, 66))])
    sky.save(os.path.join(BG_DIR, "sky.png"))
    sky_b = vgrad(64, 256, [(0.0, (26, 4, 20)), (0.32, (76, 8, 28)),
                            (0.6, (158, 22, 32)), (0.82, (236, 86, 32)),
                            (1.0, (255, 190, 96))])
    sky_b.save(os.path.join(BG_DIR, "sky_boss.png"))

    # ---- far layer: blood moon, cloud bands, jagged peaks ---------------
    # Everything above the peaks used to be empty, which left the top half
    # of a 480p screen as a bare gradient. The moon is repeated twice across
    # the tile so one is almost always on screen, and the cloud bands give
    # the upper sky something to parallax against.
    FW, FH = 1024, 256
    c = Canvas(FW, FH, pad=0)
    for mx in (176, 704):
        for r, col in ((52, (120, 26, 34)), (48, (208, 74, 48)),
                       (44, (255, 152, 96)), (41, (255, 206, 158))):
            c.ell(mx, 84, r, r, col)
        for (dx, dy, dr) in ((-14, -12, 9), (11, 9, 7), (3, -18, 5),
                             (16, -8, 4), (-6, 14, 6)):
            c.ell(mx + dx, 84 + dy, dr, dr, (236, 172, 130))
    # fixed-seed LCG, so the backgrounds are byte-identical on every run
    # (a texture that changes every build makes real diffs impossible to see)
    seed = [20260911]

    def rnd(n):
        seed[0] = (seed[0] * 1103515245 + 12345) & 0x7FFFFFFF
        return seed[0] % n

    # cloud bands: long flat lozenges, darker than the sky, lit underneath
    for band in range(7):
        by = 26 + band * 22
        x = -60 - rnd(120)
        while x < FW + 60:
            w = 90 + rnd(190)
            h = 7 + rnd(7)
            c.ell(x + w * 0.5, by, w * 0.5, h, (58, 22, 52))
            c.ell(x + w * 0.5, by + h * 0.45, w * 0.44, h * 0.5, (96, 34, 62))
            c.ell(x + w * 0.42, by + h * 0.8, w * 0.3, h * 0.28, (154, 56, 72))
            x += w + 40 + rnd(120)

    # three depths of peaks, palest and lowest at the back
    for depth, (base_h, spread, col, lit) in enumerate((
            (150, 120, (44, 20, 56), (70, 32, 78)),
            (118, 100, (32, 15, 44), (54, 24, 62)),
            (92, 84, (22, 11, 34), (40, 18, 50)))):
        x = -60 - depth * 37
        while x < FW + 60:
            wpk = spread + rnd(spread)
            hpk = base_h - 30 + rnd(60)
            top = FH - hpk
            c.poly([(x, FH), (x + wpk * 0.5, top), (x + wpk, FH)], col)
            c.poly([(x + wpk * 0.5, top), (x + wpk * 0.5 + 9, top + 22),
                    (x + wpk * 0.18, FH)], lit)
            if hpk > base_h:  # snow/ash cap on the tallest
                c.poly([(x + wpk * 0.5, top), (x + wpk * 0.62, top + 14),
                        (x + wpk * 0.5, top + 9), (x + wpk * 0.38, top + 14)],
                       (128, 74, 96))
            x += wpk * 0.58
    far = dilate_edges(c.finish(outline=None))
    far.save(os.path.join(BG_DIR, "layer_far.png"))

    # ---- mid layer: ruined fortress silhouette with lit windows ----------
    c = Canvas(FW, FH, pad=0)
    base = FH - 6
    x = 0
    while x < FW:
        kind = rnd(3)
        w = 84 + rnd(76)
        h = 120 + rnd(110)          # tall enough to fill the layer
        top = base - h
        c.poly([(x, base), (x, top), (x + w, top), (x + w, base)], (20, 12, 28))
        c.poly([(x + 4, base), (x + 4, top + 5), (x + w - 4, top + 5), (x + w - 4, base)],
               (30, 18, 40))
        # ruined battlements: merlons at random heights, some missing
        for k in range(int(w // 18)):
            if rnd(9) < 2:
                continue
            bx = x + 5 + k * 18
            mh = 6 + rnd(12)
            c.poly([(bx, top + 5), (bx, top - mh), (bx + 9, top - mh),
                    (bx + 9, top + 5)], (24, 14, 32))
        if kind == 0:                    # spired tower
            tx = x + w * 0.5
            ttop = max(8.0, top - 62)
            c.poly([(tx - 19, top + 6), (tx - 19, ttop + 34), (tx + 19, ttop + 34),
                    (tx + 19, top + 6)], (26, 15, 36))
            c.poly([(tx - 26, ttop + 34), (tx, ttop), (tx + 26, ttop + 34)], (18, 10, 26))
            c.poly([(tx - 5, ttop + 46), (tx + 5, ttop + 46), (tx + 5, ttop + 60),
                    (tx - 5, ttop + 60)], (255, 176, 84))
        # tall arched windows, sparse and irregular -- a dense even grid of
        # them reads as an office block rather than a fortress
        cols_w = max(1, int(w // 34))
        for row in range(max(1, int(h // 64))):
            for k in range(cols_w):
                if rnd(10) < 5:
                    continue
                wx = x + 14 + k * 34
                wy = top + 34 + row * 64
                if wy + 30 > base - 6:
                    continue
                c.poly([(wx - 2, wy - 2), (wx + 13, wy - 2), (wx + 13, wy + 30),
                        (wx - 2, wy + 30)], (14, 8, 20))
                c.ell(wx + 5.5, wy + 5, 7.5, 7.5, (212, 112, 46))
                c.poly([(wx, wy + 5), (wx + 11, wy + 5), (wx + 11, wy + 27),
                        (wx, wy + 27)], (212, 112, 46))
                c.ell(wx + 5.5, wy + 7, 4.5, 4.5, (255, 214, 138))
                c.poly([(wx + 2, wy + 7), (wx + 9, wy + 7), (wx + 9, wy + 24),
                        (wx + 2, wy + 24)], (255, 214, 138))
        # buttress shadows down the face
        for k in range(1, max(2, int(w // 40))):
            c.stroke([(x + k * 40, top + 6), (x + k * 40, base)], (14, 8, 20), 1.2)
        c.stroke([(x + 2, top + 6), (x + 2, base)], (78, 38, 60), 1.0)
        x += w + 6
    mid = dilate_edges(c.finish(outline=None))
    mid.save(os.path.join(BG_DIR, "layer_mid.png"))


    # ---- ground: cobbles, cracks and lava seams, irregular top edge ------
    GW, GH = 256, 128
    c = Canvas(GW, GH, pad=0)
    surf = 14
    # irregular rubble line along the top
    top_pts = [(0, surf + 3)]
    s = 5150
    for i in range(1, 17):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        top_pts.append((i * GW / 16.0, surf - 2 + (s % 7)))
    top_pts.append((GW, surf + 3))
    c.poly(top_pts + [(GW, GH), (0, GH)], (46, 28, 34))
    c.poly([(0, surf + 6), (GW, surf + 6), (GW, GH), (0, GH)], (36, 21, 27))
    # lit top faces of the cobbles
    for i in range(16):
        bx = i * 16
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        hh = 3 + (s % 4)
        c.poly([(bx + 1, surf + hh), (bx + 15, surf + hh - 1), (bx + 15, surf + hh + 4),
                (bx + 1, surf + hh + 5)], (74, 46, 52))
        c.stroke([(bx + 1, surf + hh), (bx + 15, surf + hh - 1)], (108, 70, 74), 0.8)
    # cobble grid below
    for row in range(4):
        yy = surf + 12 + row * 16
        for col in range(9):
            s = (s * 1103515245 + 12345) & 0x7FFFFFFF
            xx = col * 29 + (row % 2) * 14 + (s % 5)
            c.poly([(xx, yy), (xx + 24, yy + 1), (xx + 23, yy + 12), (xx - 1, yy + 11)],
                   (52, 32, 38))
            c.stroke([(xx + 1, yy + 1.5), (xx + 22, yy + 2.5)], (70, 44, 50), 0.7)
    # lava seams
    for (sx, ex, yy) in ((10, 74, surf + 26), (96, 150, surf + 52), (168, 238, surf + 34),
                          (40, 118, surf + 80), (150, 246, surf + 92)):
        pts = [(sx, yy)]
        n = 5
        for i in range(1, n + 1):
            s = (s * 1103515245 + 12345) & 0x7FFFFFFF
            pts.append((sx + (ex - sx) * i / n, yy - 4 + (s % 9)))
        c.stroke(pts, (140, 40, 16), 1.8)
        c.stroke(pts, (255, 136, 44), 0.8)
    ground = dilate_edges(c.finish(outline=None))
    ground.save(os.path.join(BG_DIR, "ground.png"))

    # ---- arena gate -------------------------------------------------------
    QW, QH = 128, 256
    c = Canvas(QW, QH, pad=0)
    c.poly([(6, 0), (QW - 6, 0), (QW - 6, QH), (6, QH)], (30, 22, 26))
    for i in range(7):
        bx = 12 + i * 16
        c.shaded([(bx, 6), (bx + 9, 6), (bx + 9, QH), (bx, QH)],
                 (86, 72, 78), (48, 38, 44), (128, 112, 118), amt=1.2)
        for k in range(9):
            c.ell(bx + 4.5, 22 + k * 26, 2.0, 2.0, (150, 134, 140))
        c.poly([(bx - 1, 6), (bx + 10, 6), (bx + 4.5, -6)], (58, 48, 54))
    for yy in (10, 128, 246):
        c.shaded([(4, yy), (QW - 4, yy), (QW - 4, yy + 12), (4, yy + 12)],
                 (96, 80, 86), (54, 44, 50), (140, 124, 130), amt=1.0)
    gate = dilate_edges(c.finish(outline=None))
    gate.save(os.path.join(BG_DIR, "gate.png"))

    manifest["backgrounds"] = {
        "sky": {"file": "sky.png", "w": 64, "h": 256, "opaque": 1},
        "sky_boss": {"file": "sky_boss.png", "w": 64, "h": 256, "opaque": 1},
        "layer_far": {"file": "layer_far.png", "w": FW, "h": FH, "tile_x": 1},
        "layer_mid": {"file": "layer_mid.png", "w": FW, "h": FH, "tile_x": 1},
        "ground": {"file": "ground.png", "w": GW, "h": GH, "tile_x": 1},
        "gate": {"file": "gate.png", "w": QW, "h": QH},
    }


# ============================================================ C HEADER
def cid(name):
    return name.upper().replace("-", "_")


def emit_header(path):
    L = ["/* Auto-generated by scripts/gen_sprites.py -- do not edit. */",
         "#ifndef BEELZ_SPRITE_DATA_H", "#define BEELZ_SPRITE_DATA_H", "",
         "typedef struct { int start; int count; int fps; int loop; } bz_anim_t;",
         "typedef struct {",
         "    const char *pvr_file;",
         "    int frame_w, frame_h, sheet_w, sheet_h, cols;",
         "    float anchor_x, anchor_y;   /* where the actor's feet/centre sit */",
         "    float draw_w, draw_h;       /* on-screen quad size at 480p    */",
         "} bz_sheet_t;",
         "typedef struct { const char *pvr_file; int w, h, tile_x; } bz_bg_t;", ""]
    for name, sd in manifest["sheets"].items():
        i = cid(name)
        pvr = os.path.splitext(sd["file"])[0] + ".pvr"
        L.append(f"static const bz_sheet_t SHEET_{i} = {{")
        L.append(f'    "/cd/textures/{pvr}", {sd["frame_w"]}, {sd["frame_h"]}, '
                 f'{sd["sheet_w"]}, {sd["sheet_h"]}, {sd["cols"]}, '
                 f'{sd["anchor_x"]:.1f}f, {sd["anchor_y"]:.1f}f, '
                 f'{sd["draw_w"]:.1f}f, {sd["draw_h"]:.1f}f')
        L.append("};")
        for an, ad in sd.get("anims", {}).items():
            L.append(f'static const bz_anim_t {cid(name + "_ANIM_" + an)} = '
                     f'{{{ad["start"]}, {ad["count"]}, {ad["fps"]}, {ad["loop"]}}};')
        for ic, idx in sd.get("icons", {}).items():
            L.append(f"#define UI_ICON_{ic} {idx}")
        L.append("")
    L.append("/* backgrounds */")
    for name, bd in manifest["backgrounds"].items():
        pvr = os.path.splitext(bd["file"])[0] + ".pvr"
        L.append(f'static const bz_bg_t BG_{cid(name)} = {{"/cd/textures/{pvr}", '
                 f'{bd["w"]}, {bd["h"]}, {bd.get("tile_x", 0)}}};')
    L += ["", "#endif /* BEELZ_SPRITE_DATA_H */", ""]
    open(path, "w").write("\n".join(L))


def build_preview():
    """Contact sheet for eyeballing everything at once."""
    tiles = []
    for n in ("player", "imp", "thrall", "boss", "fireball", "ui_icons"):
        tiles.append(Image.open(os.path.join(SPR_DIR, f"{n}.png")).convert("RGBA"))
    W = max(t.width for t in tiles)
    H = sum(t.height for t in tiles) + 10 * len(tiles)
    out = Image.new("RGBA", (W, H), (86, 86, 92, 255))
    y = 0
    for t in tiles:
        out.paste(t, (0, y), t)
        y += t.height + 10
    out.save(os.path.join(SPR_DIR, "_preview.png"))


def main():
    build_player()
    build_imp()
    build_thrall()
    build_boss()
    build_fireball()
    build_shadow()
    build_ui_icons()
    build_backgrounds()
    json.dump(manifest, open(os.path.join(SPR_DIR, "manifest.json"), "w"), indent=2)
    emit_header(os.path.join(ROOT, "include", "sprite_data.h"))
    build_preview()
    total = 0
    for n, sd in manifest["sheets"].items():
        total += sd["sheet_w"] * sd["sheet_h"] * 2
    for n, bd in manifest["backgrounds"].items():
        total += bd["w"] * bd["h"] * 2
    print(f"VRAM for textures: {total/1024/1024:.2f} MB")


if __name__ == "__main__":
    main()
