#!/usr/bin/env python3
"""Procedural pixel-art asset generator for BeelzFight.

No external image-generation service is available in this build
environment, so every sprite here is painted programmatically with
PIL's vector primitives (rect/polygon/line) at a small "logical" pixel
grid, then nearest-neighbour scaled up -- the standard way to get a
crisp, hard-edged pixel-art look without anti-aliasing artifacts.

Outputs:
  assets/sprites/player.png      (+ manifest entry)
  assets/sprites/imp.png
  assets/sprites/thrall.png
  assets/sprites/boss.png
  assets/sprites/fireball.png
  assets/sprites/ui_icons.png
  assets/backgrounds/*.png
  assets/sprites/manifest.json   (frame grids + animation ranges, consumed by the C renderer)

All sheet / texture dimensions are kept power-of-two, as required by
the PowerVR PVR texture hardware.
"""
import json
import math
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPR_DIR = os.path.join(ROOT, "assets", "sprites")
BG_DIR = os.path.join(ROOT, "assets", "backgrounds")
os.makedirs(SPR_DIR, exist_ok=True)
os.makedirs(BG_DIR, exist_ok=True)

manifest = {"sheets": {}, "backgrounds": {}}

# ---------------------------------------------------------------- helpers

def blank(w, h):
    return Image.new("RGBA", (w, h), (0, 0, 0, 0))


def rotp(cx, cy, x, y, deg):
    a = math.radians(deg)
    dx, dy = x - cx, y - cy
    return (cx + dx * math.cos(a) - dy * math.sin(a),
            cy + dx * math.sin(a) + dy * math.cos(a))


def thick_line(draw, p0, p1, color, width):
    draw.line([p0, p1], fill=color, width=max(1, int(round(width))))
    r = width / 2.0
    draw.ellipse([p0[0] - r, p0[1] - r, p0[0] + r, p0[1] + r], fill=color)
    draw.ellipse([p1[0] - r, p1[1] - r, p1[0] + r, p1[1] + r], fill=color)


def make_sheet(frames, frame_w, frame_h, name):
    """Pack a list of PIL frame images into a power-of-two sheet, write PNG,
    and return (sheet_w, sheet_h, columns)."""
    n = len(frames)
    cols = 1
    while cols * cols < n:
        cols *= 2
    cols = max(cols, 1)
    rows = math.ceil(n / cols)

    def npow2(v):
        p = 1
        while p < v:
            p *= 2
        return p

    sheet_w = npow2(cols * frame_w)
    sheet_h = npow2(rows * frame_h)
    cols = sheet_w // frame_w
    sheet = blank(sheet_w, sheet_h)
    for i, fr in enumerate(frames):
        cx = (i % cols) * frame_w
        cy = (i // cols) * frame_h
        sheet.paste(fr, (cx, cy), fr)
    path = os.path.join(SPR_DIR, f"{name}.png")
    sheet.save(path)
    return sheet_w, sheet_h, cols, path


# ================================================================== PLAYER
# 32x32 logical grid, scaled x2 -> 64x64 frame slot.
P_SCALE = 2
P_LOGICAL = 32
P_FRAME = P_LOGICAL * P_SCALE

PAL_PLAYER = dict(
    outline=(18, 12, 16, 255),
    skin=(246, 206, 172, 255),
    skin_sh=(214, 170, 140, 255),
    hair=(46, 20, 34, 255),
    hair_hi=(78, 38, 56, 255),
    dress=(58, 18, 70, 255),
    dress_hi=(92, 36, 104, 255),
    trim=(232, 226, 236, 255),
    boot=(32, 24, 28, 255),
    blade=(228, 232, 238, 255),
    blade_sh=(168, 176, 184, 255),
    blade_edge=(255, 255, 255, 255),
    hilt=(96, 62, 30, 255),
    guard=(198, 164, 64, 255),
    eye=(30, 20, 24, 255),
    flash=(255, 90, 90, 200),
)


def draw_player_base(d, pal, leg_off=0, arm_off=0, bob=0, lean=0, expr="normal"):
    """Draw the girl's body (everything except the sword) at logical coords."""
    cx = 16
    hip_y = 22 + bob
    # legs
    lf = 14 + lean * 0.3
    rf = 18 + lean * 0.3
    d.rectangle([lf - 1 + leg_off, hip_y, lf + 1 + leg_off, hip_y + 6], fill=pal["dress"])
    d.rectangle([rf - 1 - leg_off, hip_y, rf + 1 - leg_off, hip_y + 6], fill=pal["dress"])
    d.rectangle([lf - 1 + leg_off, hip_y + 6, lf + 1 + leg_off, hip_y + 8], fill=pal["boot"])
    d.rectangle([rf - 1 - leg_off, hip_y + 6, rf + 1 - leg_off, hip_y + 8], fill=pal["boot"])
    # dress / hips (trapezoid skirt)
    skirt_y = hip_y - 7
    d.polygon([(cx - 6, skirt_y + 7), (cx + 6, skirt_y + 7),
               (cx + 3, skirt_y), (cx - 3, skirt_y)], fill=pal["dress"])
    d.line([(cx - 5, skirt_y + 6), (cx + 5, skirt_y + 6)], fill=pal["trim"], width=1)
    # torso
    ty = skirt_y - 6
    d.rectangle([cx - 3, ty, cx + 3, skirt_y], fill=pal["dress_hi"])
    d.line([(cx - 3, ty), (cx + 3, ty)], fill=pal["trim"], width=1)
    # off-hand arm
    ax = cx - 4 - arm_off * 0.4
    d.rectangle([ax, ty + 1, ax + 2, ty + 6], fill=pal["skin_sh"])
    # head
    hy = ty - 8 + lean * 0.15
    d.ellipse([cx - 4, hy, cx + 4, hy + 8], fill=pal["skin"])
    # hair back
    d.pieslice([cx - 5, hy - 2, cx + 5, hy + 9], 10, 170, fill=pal["hair"])
    d.polygon([(cx - 5, hy + 3), (cx - 7, hy + 10), (cx - 3, hy + 6)], fill=pal["hair"])
    d.polygon([(cx + 5, hy + 3), (cx + 7, hy + 10), (cx + 3, hy + 6)], fill=pal["hair_hi"])
    # hair top / fringe
    d.pieslice([cx - 4, hy - 3, cx + 4, hy + 5], 180, 360, fill=pal["hair"])
    d.polygon([(cx - 1, hy - 4), (cx + 1, hy - 6), (cx + 1, hy - 2)], fill=pal["hair"])  # ahoge
    # face
    if expr == "hit":
        d.line([(cx - 2, hy + 4), (cx - 1, hy + 3)], fill=pal["eye"], width=1)
        d.line([(cx + 1, hy + 3), (cx + 2, hy + 4)], fill=pal["eye"], width=1)
    else:
        d.point((cx - 2, hy + 4), fill=pal["eye"])
        d.point((cx + 2, hy + 4), fill=pal["eye"])
    return cx, ty  # shoulder anchor for sword hand


def sword_points(shoulder, angle_deg, length, curve=0):
    sx, sy = shoulder
    hilt = rotp(sx, sy, sx + 2, sy + 2, 0)
    tip = rotp(sx, sy, sx + 2 + length, sy + 2 + curve, angle_deg)
    return hilt, tip


def draw_sword(d, pal, shoulder, angle_deg, length=26, width=7.5):
    """A big, wide slab-blade greatsword -- deliberately oversized vs. her body."""
    hilt, tip = sword_points(shoulder, angle_deg, length)
    a = math.radians(angle_deg)
    dirx, diry = math.cos(a), math.sin(a)
    nx, ny = -diry, dirx
    base_w = width
    tip_w = width * 0.4
    shoulder_pt = (length * 0.18)  # slight forward-swept widest point
    bx, by = hilt[0] + dirx * shoulder_pt, hilt[1] + diry * shoulder_pt
    base_l = (bx + nx * base_w / 2, by + ny * base_w / 2)
    base_r = (bx - nx * base_w / 2, by - ny * base_w / 2)
    hilt_l = (hilt[0] + nx * base_w * 0.3, hilt[1] + ny * base_w * 0.3)
    hilt_r = (hilt[0] - nx * base_w * 0.3, hilt[1] - ny * base_w * 0.3)
    tip_l = (tip[0] + nx * tip_w / 2, tip[1] + ny * tip_w / 2)
    tip_r = (tip[0] - nx * tip_w / 2, tip[1] - ny * tip_w / 2)
    # shadow pass (slightly offset) then main blade slab
    d.polygon([hilt_l, base_l, tip_l, tip, tip_r, base_r, hilt_r], fill=pal["blade_sh"])
    inset = 1.1
    base_l2 = (bx + nx * (base_w / 2 - inset), by + ny * (base_w / 2 - inset))
    base_r2 = (bx - nx * (base_w / 2 - inset), by - ny * (base_w / 2 - inset))
    tip_l2 = (tip[0] + nx * (tip_w / 2 - inset * 0.5), tip[1] + ny * (tip_w / 2 - inset * 0.5))
    tip_r2 = (tip[0] - nx * (tip_w / 2 - inset * 0.5), tip[1] - ny * (tip_w / 2 - inset * 0.5))
    d.polygon([base_l2, tip_l2, tip, tip_r2, base_r2], fill=pal["blade"])
    # center edge-line highlight running the length of the blade
    mid1 = (bx, by)
    d.line([mid1, tip], fill=pal["blade_edge"], width=1)
    # cross-guard (wide, perpendicular to blade at the hilt)
    gw = base_w * 0.9 + 2
    gx1, gy1 = hilt[0] + nx * gw / 2, hilt[1] + ny * gw / 2
    gx2, gy2 = hilt[0] - nx * gw / 2, hilt[1] - ny * gw / 2
    thick_line(d, (gx1, gy1), (gx2, gy2), pal["guard"], 2.2)
    # grip + pommel
    grip_end = (hilt[0] - dirx * 4.5, hilt[1] - diry * 4.5)
    thick_line(d, hilt, grip_end, pal["hilt"], 2.4)
    pommel = (hilt[0] - dirx * 5.5, hilt[1] - diry * 5.5)
    d.ellipse([pommel[0] - 1.2, pommel[1] - 1.2, pommel[0] + 1.2, pommel[1] + 1.2], fill=pal["guard"])


def player_frame(pose):
    img = blank(P_LOGICAL, P_LOGICAL)
    d = ImageDraw.Draw(img)
    pal = PAL_PLAYER
    leg_off = pose.get("leg_off", 0)
    bob = pose.get("bob", 0)
    lean = pose.get("lean", 0)
    expr = pose.get("expr", "normal")
    shoulder, torso_top = draw_player_base(d, pal, leg_off=leg_off, bob=bob, lean=lean, expr=expr)
    sh_pt = (shoulder + 3, torso_top + 2)
    if pose.get("guard"):
        draw_sword(d, pal, sh_pt, pose.get("angle", -20), length=pose.get("length", 20),
                   width=pose.get("width", 7.5))
    else:
        draw_sword(d, pal, sh_pt, pose.get("angle", 40), length=pose.get("length", 22),
                   width=pose.get("width", 7.5))
    if pose.get("flash"):
        ov = blank(P_LOGICAL, P_LOGICAL)
        ImageDraw.Draw(ov).rectangle([0, 0, P_LOGICAL - 1, P_LOGICAL - 1], fill=pal["flash"])
        img = Image.alpha_composite(img, ov)
    if pose.get("fade") is not None:
        r, g, b, a = img.split()
        a = a.point(lambda v: int(v * pose["fade"]))
        img = Image.merge("RGBA", (r, g, b, a))
    return img.resize((P_FRAME, P_FRAME), Image.NEAREST)


def build_player():
    frames = []
    anims = {}

    def add(name, poses, fps=8):
        start = len(frames)
        for p in poses:
            frames.append(player_frame(p))
        anims[name] = {"start": start, "count": len(poses), "fps": fps, "loop": True}

    add("idle", [
        {"angle": 35, "bob": 0}, {"angle": 33, "bob": -1},
        {"angle": 35, "bob": 0}, {"angle": 37, "bob": 1},
    ], fps=5)

    add("run", [
        {"angle": 20, "leg_off": 3, "lean": 3, "bob": 0},
        {"angle": 30, "leg_off": 0, "lean": 3, "bob": -1},
        {"angle": 20, "leg_off": -3, "lean": 3, "bob": 0},
        {"angle": 10, "leg_off": 0, "lean": 3, "bob": -1},
    ], fps=10)

    # Attack 1 (A) - horizontal slash, left to right
    add("attack1", [
        {"angle": 150, "length": 20, "lean": -2},
        {"angle": 90, "length": 24, "lean": 1},
        {"angle": 10, "length": 26, "lean": 3},
        {"angle": -30, "length": 22, "lean": 2},
    ], fps=14)
    anims["attack1"]["loop"] = False

    # Attack 2 (B) - overhead chop
    add("attack2", [
        {"angle": -110, "length": 20, "bob": -2},
        {"angle": -70, "length": 24, "bob": -1},
        {"angle": -20, "length": 26, "bob": 0},
        {"angle": 20, "length": 22, "bob": 1},
    ], fps=14)
    anims["attack2"]["loop"] = False

    # Attack 3 (X) - forward thrust
    add("attack3", [
        {"angle": 10, "length": 14, "lean": -1},
        {"angle": 5, "length": 28, "lean": 2},
        {"angle": 5, "length": 30, "lean": 3},
        {"angle": 8, "length": 18, "lean": 0},
    ], fps=16)
    anims["attack3"]["loop"] = False

    # Attack 4 (Y) - spin finisher, full rotation
    add("attack4", [
        {"angle": 0, "length": 24, "bob": -1},
        {"angle": 90, "length": 26, "bob": 0},
        {"angle": 180, "length": 26, "bob": 1},
        {"angle": 270, "length": 26, "bob": 0},
        {"angle": 360, "length": 24, "bob": -1},
    ], fps=14)
    anims["attack4"]["loop"] = False

    # Parry (L) - quick raised guard
    add("parry", [
        {"angle": -60, "length": 18, "guard": True, "width": 4},
        {"angle": -80, "length": 20, "guard": True, "width": 5},
    ], fps=12)
    anims["parry"]["loop"] = False

    # Block (R) - sword held up, static hold
    add("block", [
        {"angle": -70, "length": 18, "guard": True, "width": 4},
        {"angle": -70, "length": 18, "guard": True, "width": 4, "flash": True},
    ], fps=6)

    # Hit reaction
    add("hit", [
        {"angle": 60, "length": 18, "lean": -4, "expr": "hit", "flash": True},
        {"angle": 60, "length": 18, "lean": -4, "expr": "hit"},
    ], fps=8)
    anims["hit"]["loop"] = False

    # Death
    add("death", [
        {"angle": 40, "bob": 0, "lean": -2, "expr": "hit"},
        {"angle": 70, "bob": 2, "lean": -5, "expr": "hit"},
        {"angle": 100, "bob": 6, "lean": -8, "expr": "hit", "fade": 0.85},
        {"angle": 120, "bob": 9, "lean": -10, "expr": "hit", "fade": 0.6},
        {"angle": 130, "bob": 9, "lean": -10, "expr": "hit", "fade": 0.3},
    ], fps=6)
    anims["death"]["loop"] = False

    w, h, cols, path = make_sheet(frames, P_FRAME, P_FRAME, "player")
    manifest["sheets"]["player"] = {
        "file": os.path.basename(path), "frame_w": P_FRAME, "frame_h": P_FRAME,
        "sheet_w": w, "sheet_h": h, "cols": cols, "anims": anims,
    }


# =============================================================== ENEMIES
E_SCALE = 2
E_LOGICAL = 32
E_FRAME = E_LOGICAL * E_SCALE


def imp_frame(pose, pal):
    img = blank(E_LOGICAL, E_LOGICAL)
    d = ImageDraw.Draw(img)
    cx = 16
    crouch = pose.get("crouch", 0)
    leg = pose.get("leg", 0)
    lunge = pose.get("lunge", 0)
    hy = 10 + crouch
    # legs
    d.rectangle([cx - 4 + leg, 22 + crouch, cx - 2 + leg, 27 + crouch], fill=pal["dark"])
    d.rectangle([cx + 2 - leg, 22 + crouch, cx + 4 - leg, 27 + crouch], fill=pal["dark"])
    # body (hunched)
    d.polygon([(cx - 6, 22 + crouch), (cx + 6, 22 + crouch),
               (cx + 5, hy + 4), (cx - 5, hy + 4)], fill=pal["skin"])
    d.polygon([(cx - 5, hy + 4), (cx + 5, hy + 4), (cx + 3, hy), (cx - 3, hy)], fill=pal["skin"])
    # tail
    d.line([(cx - 5, 24 + crouch), (cx - 9, 20 + crouch), (cx - 8, 16 + crouch)],
           fill=pal["dark"], width=2)
    # head
    d.ellipse([cx - 5, hy - 6, cx + 5, hy + 3], fill=pal["skin"])
    # horns
    d.polygon([(cx - 4, hy - 5), (cx - 6, hy - 10), (cx - 2, hy - 4)], fill=pal["horn"])
    d.polygon([(cx + 4, hy - 5), (cx + 6, hy - 10), (cx + 2, hy - 4)], fill=pal["horn"])
    # eyes
    d.point((cx - 2, hy - 1), fill=pal["eye"])
    d.point((cx + 2, hy - 1), fill=pal["eye"])
    # arms / claws
    axo = pose.get("arm", 0) + lunge
    d.line([(cx - 5, hy + 2), (cx - 9 - axo, hy + 6)], fill=pal["dark"], width=2)
    d.line([(cx + 5, hy + 2), (cx + 9 + axo, hy + 6 - lunge * 0.6)], fill=pal["dark"], width=2)
    d.polygon([(cx + 9 + axo, hy + 6 - lunge * 0.6), (cx + 11 + axo, hy + 4 - lunge * 0.6),
               (cx + 12 + axo, hy + 8 - lunge * 0.6)], fill=pal["claw"])
    if pose.get("fade") is not None:
        r, g, b, a = img.split()
        a = a.point(lambda v: int(v * pose["fade"]))
        img = Image.merge("RGBA", (r, g, b, a))
    return img.resize((E_FRAME, E_FRAME), Image.NEAREST)


def build_imp():
    pal = dict(skin=(176, 44, 44, 255), dark=(118, 24, 24, 255),
               horn=(232, 220, 182, 255), eye=(255, 228, 64, 255), claw=(28, 18, 18, 255))
    frames, anims = [], {}

    def add(name, poses, fps=8, loop=True):
        start = len(frames)
        for p in poses:
            frames.append(imp_frame(p, pal))
        anims[name] = {"start": start, "count": len(poses), "fps": fps, "loop": loop}

    add("idle", [{"crouch": 0}, {"crouch": 1}, {"crouch": 0}], fps=4)
    add("walk", [{"leg": 2}, {"leg": 0}, {"leg": -2}, {"leg": 0}], fps=9)
    add("attack", [{"lunge": 0}, {"lunge": 4}, {"lunge": 7}], fps=12, loop=False)
    add("death", [{"crouch": 2, "fade": 0.9}, {"crouch": 5, "fade": 0.5}, {"crouch": 6, "fade": 0.15}],
        fps=6, loop=False)

    w, h, cols, path = make_sheet(frames, E_FRAME, E_FRAME, "imp")
    manifest["sheets"]["imp"] = {"file": os.path.basename(path), "frame_w": E_FRAME,
                                  "frame_h": E_FRAME, "sheet_w": w, "sheet_h": h,
                                  "cols": cols, "anims": anims}


def thrall_frame(pose, pal):
    img = blank(E_LOGICAL, E_LOGICAL)
    d = ImageDraw.Draw(img)
    cx = 16
    leg = pose.get("leg", 0)
    lunge = pose.get("lunge", 0)
    hy = 6
    d.rectangle([cx - 3 + leg, 21, cx - 1 + leg, 29], fill=pal["dark"])
    d.rectangle([cx + 1 - leg, 21, cx + 3 - leg, 29], fill=pal["dark"])
    d.rectangle([cx - 4, hy + 4, cx + 4, 22], fill=pal["skin"])
    d.polygon([(cx - 5, hy + 4), (cx - 3, hy + 2), (cx - 3, hy + 9), (cx - 6, hy + 10)],
              fill=pal["rag"])
    d.polygon([(cx + 5, hy + 4), (cx + 3, hy + 2), (cx + 3, hy + 9), (cx + 6, hy + 10)],
              fill=pal["rag"])
    d.ellipse([cx - 4, hy - 5, cx + 4, hy + 4], fill=pal["skin"])
    d.line([(cx - 2, hy - 1), (cx - 1, hy - 1)], fill=pal["eye"], width=1)
    d.line([(cx + 1, hy - 1), (cx + 2, hy - 1)], fill=pal["eye"], width=1)
    ax = lunge
    d.line([(cx - 4, hy + 6), (cx - 8 - ax, hy + 3 - ax * 0.4)], fill=pal["dark"], width=2)
    d.line([(cx + 4, hy + 6), (cx + 8 + ax, hy + 3 - ax * 0.4)], fill=pal["dark"], width=2)
    if pose.get("fade") is not None:
        r, g, b, a = img.split()
        a = a.point(lambda v: int(v * pose["fade"]))
        img = Image.merge("RGBA", (r, g, b, a))
    return img.resize((E_FRAME, E_FRAME), Image.NEAREST)


def build_thrall():
    pal = dict(skin=(112, 92, 134, 255), dark=(76, 60, 96, 255),
               rag=(48, 38, 58, 255), eye=(206, 62, 206, 255))
    frames, anims = [], {}

    def add(name, poses, fps=8, loop=True):
        start = len(frames)
        for p in poses:
            frames.append(thrall_frame(p, pal))
        anims[name] = {"start": start, "count": len(poses), "fps": fps, "loop": loop}

    add("idle", [{"leg": 0}, {"leg": 1}], fps=3)
    add("walk", [{"leg": 3}, {"leg": 0}, {"leg": -3}, {"leg": 0}], fps=8)
    add("attack", [{"lunge": 0}, {"lunge": 5}, {"lunge": 8}], fps=11, loop=False)
    add("death", [{"fade": 0.9}, {"fade": 0.5}, {"fade": 0.15}], fps=6, loop=False)

    w, h, cols, path = make_sheet(frames, E_FRAME, E_FRAME, "thrall")
    manifest["sheets"]["thrall"] = {"file": os.path.basename(path), "frame_w": E_FRAME,
                                     "frame_h": E_FRAME, "sheet_w": w, "sheet_h": h,
                                     "cols": cols, "anims": anims}


# ================================================================== BOSS
B_SCALE = 2
B_LOGICAL = 64
B_FRAME = B_LOGICAL * B_SCALE

PAL_BOSS = dict(
    skin=(78, 16, 48, 255), skin_sh=(50, 8, 30, 255),
    horn=(35, 28, 32, 255), eye=(255, 70, 45, 255),
    wing=(34, 10, 26, 235), wing_hi=(60, 18, 42, 235),
    armor=(126, 96, 42, 255), armor_hi=(168, 130, 60, 255),
    claw=(18, 12, 14, 255),
)


def boss_frame(pose):
    img = blank(B_LOGICAL, B_LOGICAL)
    d = ImageDraw.Draw(img)
    pal = PAL_BOSS
    cx = 32
    hy = 18 + pose.get("bob", 0)
    wflap = pose.get("wflap", 0)
    armL = pose.get("armL", 0)
    armR = pose.get("armR", 0)
    crouch = pose.get("crouch", 0)
    fade = pose.get("fade")

    # wings (behind body)
    d.polygon([(cx - 10, hy + 6), (cx - 30, hy - 6 - wflap), (cx - 26, hy + 4 - wflap),
               (cx - 14, hy + 14)], fill=pal["wing"])
    d.polygon([(cx + 10, hy + 6), (cx + 30, hy - 6 - wflap), (cx + 26, hy + 4 - wflap),
               (cx + 14, hy + 14)], fill=pal["wing_hi"])
    # legs
    d.rectangle([cx - 9, 46 + crouch, cx - 3, 58 + crouch], fill=pal["skin_sh"])
    d.rectangle([cx + 3, 46 + crouch, cx + 9, 58 + crouch], fill=pal["skin_sh"])
    d.rectangle([cx - 10, 56 + crouch, cx - 2, 60 + crouch], fill=pal["claw"])
    d.rectangle([cx + 2, 56 + crouch, cx + 10, 60 + crouch], fill=pal["claw"])
    # torso
    d.polygon([(cx - 14, 24 + crouch), (cx + 14, 24 + crouch),
               (cx + 11, 50 + crouch), (cx - 11, 50 + crouch)], fill=pal["skin"])
    d.rectangle([cx - 11, 30 + crouch, cx + 11, 40 + crouch], fill=pal["armor"])
    d.line([(cx - 11, 30 + crouch), (cx + 11, 30 + crouch)], fill=pal["armor_hi"], width=2)
    # arms
    lx = cx - 14 + armL
    rx = cx + 14 + armR
    d.line([(cx - 12, hy + 10), (lx, hy + 22)], fill=pal["skin_sh"], width=5)
    d.line([(cx + 12, hy + 10), (rx, hy + 22)], fill=pal["skin_sh"], width=5)
    d.polygon([(lx, hy + 22), (lx - 5, hy + 20), (lx - 4, hy + 28), (lx + 2, hy + 27)],
              fill=pal["claw"])
    d.polygon([(rx, hy + 22), (rx + 5, hy + 20), (rx + 4, hy + 28), (rx - 2, hy + 27)],
              fill=pal["claw"])
    # head
    d.polygon([(cx - 9, hy), (cx + 9, hy), (cx + 6, hy + 14), (cx - 6, hy + 14)], fill=pal["skin"])
    d.polygon([(cx - 8, hy), (cx - 14, hy - 14), (cx - 4, hy - 2)], fill=pal["horn"])
    d.polygon([(cx + 8, hy), (cx + 14, hy - 14), (cx + 4, hy - 2)], fill=pal["horn"])
    d.rectangle([cx - 5, hy + 6, cx - 2, hy + 8], fill=pal["eye"])
    d.rectangle([cx + 2, hy + 6, cx + 5, hy + 8], fill=pal["eye"])

    if pose.get("flash"):
        ov = blank(B_LOGICAL, B_LOGICAL)
        ImageDraw.Draw(ov).rectangle([0, 0, B_LOGICAL - 1, B_LOGICAL - 1], fill=(255, 255, 255, 90))
        img = Image.alpha_composite(img, ov)
    if fade is not None:
        r, g, b, a = img.split()
        a = a.point(lambda v: int(v * fade))
        img = Image.merge("RGBA", (r, g, b, a))
    return img.resize((B_FRAME, B_FRAME), Image.NEAREST)


def build_boss():
    frames, anims = [], {}

    def add(name, poses, fps=8, loop=True):
        start = len(frames)
        for p in poses:
            frames.append(boss_frame(p))
        anims[name] = {"start": start, "count": len(poses), "fps": fps, "loop": loop}

    add("idle", [{"bob": 0, "wflap": 0}, {"bob": -2, "wflap": 3},
                 {"bob": 0, "wflap": 0}, {"bob": 1, "wflap": -2}], fps=4)
    add("walk", [{"bob": 0, "crouch": 0}, {"bob": -2, "crouch": 1},
                 {"bob": 0, "crouch": 0}, {"bob": -1, "crouch": -1}], fps=6)
    add("attack_sweep", [{"armR": -4}, {"armR": 10, "wflap": 6}, {"armR": 22, "wflap": 10},
                          {"armR": 8}], fps=10, loop=False)
    add("attack_slam", [{"crouch": -4, "armL": -2, "armR": -2},
                         {"crouch": -8, "armL": -6, "armR": -6},
                         {"crouch": 6, "armL": 4, "armR": 4, "flash": True},
                         {"crouch": 2}], fps=8, loop=False)
    add("attack_cast", [{"armL": -6, "armR": -6, "wflap": -4},
                         {"armL": -10, "armR": -10, "wflap": 4},
                         {"armL": -10, "armR": -10, "wflap": 8, "flash": True},
                         {"armL": -4, "armR": -4}], fps=8, loop=False)
    add("hurt", [{"crouch": 2, "flash": True}, {"crouch": 1}], fps=8, loop=False)
    add("death", [{"crouch": 0, "fade": 1.0}, {"crouch": 4, "fade": 0.8},
                   {"crouch": 8, "fade": 0.6}, {"crouch": 12, "fade": 0.4},
                   {"crouch": 14, "fade": 0.2}, {"crouch": 14, "fade": 0.0}],
        fps=5, loop=False)

    w, h, cols, path = make_sheet(frames, B_FRAME, B_FRAME, "boss")
    manifest["sheets"]["boss"] = {"file": os.path.basename(path), "frame_w": B_FRAME,
                                   "frame_h": B_FRAME, "sheet_w": w, "sheet_h": h,
                                   "cols": cols, "anims": anims}


def build_fireball():
    logical, scale = 16, 4
    frame = logical * scale
    frames = []
    for i in range(4):
        img = blank(logical, logical)
        d = ImageDraw.Draw(img)
        r = 5 + (i % 2)
        d.ellipse([8 - r, 8 - r, 8 + r, 8 + r], fill=(255, 140, 30, 255))
        d.ellipse([8 - r + 2, 8 - r + 2, 8 + r - 2, 8 + r - 2], fill=(255, 220, 90, 255))
        frames.append(img.resize((frame, frame), Image.NEAREST))
    w, h, cols, path = make_sheet(frames, frame, frame, "fireball")
    manifest["sheets"]["fireball"] = {"file": os.path.basename(path), "frame_w": frame,
                                       "frame_h": frame, "sheet_w": w, "sheet_h": h,
                                       "cols": cols,
                                       "anims": {"spin": {"start": 0, "count": 4, "fps": 12, "loop": True}}}


# ==================================================================== UI
def build_ui_icons():
    cell = 32
    labels = [("A", (30, 190, 90)), ("B", (210, 40, 40)), ("X", (40, 90, 220)),
              ("Y", (235, 200, 30)), ("L", (200, 200, 210)), ("R", (200, 200, 210))]
    try:
        font = ImageFont.load_default(size=18)
    except TypeError:
        font = ImageFont.load_default()
    frames = []
    for letter, color in labels:
        img = blank(cell, cell)
        d = ImageDraw.Draw(img)
        d.ellipse([1, 1, cell - 2, cell - 2], fill=color + (255,))
        d.ellipse([1, 1, cell - 2, cell - 2], outline=(20, 20, 20, 255), width=2)
        bbox = d.textbbox((0, 0), letter, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        d.text(((cell - tw) / 2 - bbox[0], (cell - th) / 2 - bbox[1]), letter,
               font=font, fill=(20, 20, 20, 255))
        frames.append(img)
    # bar frame + fill swatch (9-slice-free: game scales fill rect at runtime)
    bar = blank(cell * 4, cell // 2)
    bd = ImageDraw.Draw(bar)
    bd.rectangle([0, 0, cell * 4 - 1, cell // 2 - 1], outline=(230, 230, 230, 255), width=2)
    frames.append(bar.resize((cell * 4, cell), Image.NEAREST) if False else
                  Image.new("RGBA", (cell, cell), (0, 0, 0, 0)))
    w, h, cols, path = make_sheet(frames[:6], cell, cell, "ui_icons")
    manifest["sheets"]["ui_icons"] = {"file": os.path.basename(path), "frame_w": cell,
                                       "frame_h": cell, "sheet_w": w, "sheet_h": h, "cols": cols,
                                       "icons": {"A": 0, "B": 1, "X": 2, "Y": 3, "L": 4, "R": 5}}
    # standalone bar textures (health / stamina / boss)
    def bar_texture(name, w_px, h_px, border, fill):
        img = blank(w_px, h_px)
        d = ImageDraw.Draw(img)
        d.rectangle([0, 0, w_px - 1, h_px - 1], outline=border, width=2)
        d.rectangle([3, 3, w_px - 4, h_px - 4], fill=fill)
        img.save(os.path.join(SPR_DIR, f"{name}.png"))
        manifest["sheets"][name] = {"file": f"{name}.png", "frame_w": w_px, "frame_h": h_px,
                                     "sheet_w": w_px, "sheet_h": h_px, "cols": 1}

    bar_texture("bar_health", 256, 16, (230, 230, 230, 255), (200, 30, 40, 255))
    bar_texture("bar_stamina", 256, 16, (230, 230, 230, 255), (60, 170, 210, 255))
    bar_texture("bar_boss", 512, 16, (230, 200, 120, 255), (120, 20, 140, 255))


# =============================================================== BACKGROUND
def seamless_h_noise_band(w, h, base_color, hi_color, seed_shapes, alpha=255):
    img = blank(w, h)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w - 1, h - 1], fill=base_color + (alpha,))
    for (fx, fy, fw, fh, color) in seed_shapes:
        x0 = int(fx * w)
        x1 = int((fx + fw) * w)
        y0 = int(fy * h)
        y1 = int((fy + fh) * h)
        for dup in (-w, 0, w):  # draw shifted copies so it tiles seamlessly
            d.polygon([(x0 + dup, h), (x0 + dup, y1), ((x0 + x1) / 2 + dup, y0),
                       (x1 + dup, y1), (x1 + dup, h)], fill=color + (alpha,))
    return img


def build_backgrounds():
    # sky gradient (hellish twilight)
    sw, sh = 512, 256
    sky = blank(sw, sh)
    top = (28, 10, 46)
    mid = (120, 34, 40)
    bot = (232, 120, 46)
    for y in range(sh):
        t = y / (sh - 1)
        if t < 0.6:
            tt = t / 0.6
            c = tuple(int(top[i] + (mid[i] - top[i]) * tt) for i in range(3))
        else:
            tt = (t - 0.6) / 0.4
            c = tuple(int(mid[i] + (bot[i] - mid[i]) * tt) for i in range(3))
        ImageDraw.Draw(sky).line([(0, y), (sw - 1, y)], fill=c + (255,))
    sky.save(os.path.join(BG_DIR, "sky.png"))

    far = seamless_h_noise_band(1024, 256, (18, 8, 22), None, [
        (0.02, 0.35, 0.14, 0.65, (14, 6, 18)),
        (0.20, 0.20, 0.16, 0.80, (16, 7, 20)),
        (0.42, 0.40, 0.12, 0.60, (14, 6, 18)),
        (0.60, 0.15, 0.18, 0.85, (16, 7, 20)),
        (0.82, 0.30, 0.15, 0.70, (14, 6, 18)),
    ], alpha=235)
    far.save(os.path.join(BG_DIR, "layer_far.png"))

    mid_l = seamless_h_noise_band(1024, 256, (34, 14, 30), None, [
        (0.05, 0.55, 0.10, 0.45, (46, 18, 40)),
        (0.24, 0.45, 0.09, 0.55, (42, 16, 36)),
        (0.45, 0.60, 0.11, 0.40, (46, 18, 40)),
        (0.66, 0.50, 0.09, 0.50, (42, 16, 36)),
        (0.86, 0.55, 0.10, 0.45, (46, 18, 40)),
    ], alpha=255)
    mid_l.save(os.path.join(BG_DIR, "layer_mid.png"))

    # ground tile (cracked stone), tileable 128x64
    gw, gh = 128, 64
    ground = blank(gw, gh)
    gd = ImageDraw.Draw(ground)
    gd.rectangle([0, 0, gw - 1, gh - 1], fill=(58, 30, 26, 255))
    gd.rectangle([0, 0, gw - 1, 10], fill=(78, 42, 34, 255))
    crack_pts = [(10, 12), (30, 28), (55, 14), (78, 34), (100, 18), (120, 30)]
    for i in range(len(crack_pts) - 1):
        gd.line([crack_pts[i], crack_pts[i + 1]], fill=(38, 18, 16, 255), width=1)
    for x in range(0, gw, 16):
        gd.line([(x, 11), (x, gh - 1)], fill=(46, 24, 20, 255), width=1)
    ground.save(os.path.join(BG_DIR, "ground.png"))

    # boss arena variant sky (more intense fire glow)
    arena = blank(sw, sh)
    top2 = (30, 6, 20)
    mid2 = (150, 20, 20)
    bot2 = (255, 120, 20)
    for y in range(sh):
        t = y / (sh - 1)
        if t < 0.55:
            tt = t / 0.55
            c = tuple(int(top2[i] + (mid2[i] - top2[i]) * tt) for i in range(3))
        else:
            tt = (t - 0.55) / 0.45
            c = tuple(int(mid2[i] + (bot2[i] - mid2[i]) * tt) for i in range(3))
        ImageDraw.Draw(arena).line([(0, y), (sw - 1, y)], fill=c + (255,))
    arena.save(os.path.join(BG_DIR, "sky_boss.png"))

    # arena gate (closes off the boss room)
    gw2, gh2 = 128, 256
    gate = blank(gw2, gh2)
    gd2 = ImageDraw.Draw(gate)
    gd2.rectangle([0, 0, gw2 - 1, gh2 - 1], fill=(40, 26, 22, 255))
    for x in range(8, gw2, 16):
        gd2.rectangle([x, 0, x + 6, gh2 - 1], fill=(60, 40, 30, 255))
    gd2.rectangle([0, 0, gw2 - 1, 10], fill=(90, 60, 30, 255))
    gate.save(os.path.join(BG_DIR, "gate.png"))

    manifest["backgrounds"] = {
        "sky": {"file": "sky.png", "w": sw, "h": sh},
        "sky_boss": {"file": "sky_boss.png", "w": sw, "h": sh},
        "layer_far": {"file": "layer_far.png", "w": 1024, "h": 256, "tile_x": True},
        "layer_mid": {"file": "layer_mid.png", "w": 1024, "h": 256, "tile_x": True},
        "ground": {"file": "ground.png", "w": gw, "h": gh, "tile_x": True},
        "gate": {"file": "gate.png", "w": gw2, "h": gh2},
    }


def c_ident(name):
    return name.upper().replace("-", "_")


def emit_c_header(out_path):
    """Bake sheet/animation metadata into a C header so the game never needs
    a runtime JSON/text parser on-device -- everything is a const table."""
    lines = []
    lines.append("/* Auto-generated by scripts/gen_sprites.py -- do not edit by hand. */")
    lines.append("#ifndef BEELZ_SPRITE_DATA_H")
    lines.append("#define BEELZ_SPRITE_DATA_H")
    lines.append("")
    lines.append("typedef struct { int start; int count; int fps; int loop; } bz_anim_t;")
    lines.append("typedef struct {")
    lines.append("    const char *pvr_file;")
    lines.append("    int frame_w, frame_h, sheet_w, sheet_h, cols;")
    lines.append("} bz_sheet_t;")
    lines.append("typedef struct { const char *pvr_file; int w, h, tile_x; } bz_bg_t;")
    lines.append("")

    for sheet_name, sd in manifest["sheets"].items():
        ident = c_ident(sheet_name)
        pvr_name = os.path.splitext(sd["file"])[0] + ".pvr"
        lines.append(f'static const bz_sheet_t SHEET_{ident} = {{')
        lines.append(f'    "/cd/textures/{pvr_name}", {sd["frame_w"]}, {sd["frame_h"]}, '
                      f'{sd["sheet_w"]}, {sd["sheet_h"]}, {sd["cols"]}')
        lines.append("};")
        if "anims" in sd:
            for anim_name, ad in sd["anims"].items():
                aident = c_ident(f"{sheet_name}_ANIM_{anim_name}")
                lines.append(f'static const bz_anim_t {aident} = '
                              f'{{{ad["start"]}, {ad["count"]}, {ad["fps"]}, {1 if ad["loop"] else 0}}};')
        if "icons" in sd:
            for icon_name, idx in sd["icons"].items():
                lines.append(f'#define UI_ICON_{icon_name} {idx}')
        lines.append("")

    lines.append("/* backgrounds */")
    for bg_name, bd in manifest["backgrounds"].items():
        ident = c_ident(bg_name)
        pvr_name = os.path.splitext(bd["file"])[0] + ".pvr"
        tile_x = 1 if bd.get("tile_x") else 0
        lines.append(f'static const bz_bg_t BG_{ident} = '
                      f'{{"/cd/textures/{pvr_name}", {bd["w"]}, {bd["h"]}, {tile_x}}};')
    lines.append("")
    lines.append("#endif /* BEELZ_SPRITE_DATA_H */")
    lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(lines))


def main():
    build_player()
    build_imp()
    build_thrall()
    build_boss()
    build_fireball()
    build_ui_icons()
    build_backgrounds()
    with open(os.path.join(SPR_DIR, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    include_dir = os.path.join(ROOT, "include")
    os.makedirs(include_dir, exist_ok=True)
    emit_c_header(os.path.join(include_dir, "sprite_data.h"))
    print("Generated sheets:", ", ".join(manifest["sheets"].keys()))
    print("Generated backgrounds:", ", ".join(manifest["backgrounds"].keys()))
    print("Wrote include/sprite_data.h")


if __name__ == "__main__":
    main()
