#!/usr/bin/env python3
"""Convert the generated PNG sprite sheets / backgrounds into raw texture
blobs the game's own tiny loader (src/texture.c) understands, targeting the
PowerVR2 GPU's non-twiddled 16-bit texture formats directly. This avoids any
dependency on KOS's bundled kmgenc/pvrtex tool -- both formats below are
exactly what PVR hardware consumes, just written out by hand:

  ARGB4444 (has alpha): sprite sheets, UI, and the parallax/ground layers,
                        which are cut-out silhouettes with transparent sky
                        showing through them.
  RGB565   (opaque):    fully-opaque layers only (the sky gradients), which
                        halves nothing in size but does let them render in
                        the cheap opaque list.

File layout ("BPVR" custom container, parsed by src/texture.c):
  char[4]  magic "BPVR"
  u16      width
  u16      height
  u8       format   (0 = ARGB4444, 1 = RGB565)
  u8       reserved
  ...      width*height*2 bytes, row-major, little-endian u16 texels

Width/height are already powers of two coming out of gen_sprites.py, which
PVR textures require.
"""
import os
import struct

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPR_DIR = os.path.join(ROOT, "assets", "sprites")
BG_DIR = os.path.join(ROOT, "assets", "backgrounds")
OUT_DIR = os.path.join(ROOT, "romdisc", "textures")
os.makedirs(OUT_DIR, exist_ok=True)

FMT_ARGB4444 = 0
FMT_RGB565 = 1


def to_argb4444(im):
    a = np.asarray(im.convert("RGBA"), dtype=np.uint16)
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    v = ((al >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)
    return v.astype("<u2").tobytes()


def to_rgb565(im):
    a = np.asarray(im.convert("RGB"), dtype=np.uint16)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return v.astype("<u2").tobytes()


def pick_format(im):
    """ARGB4444 unless the image is genuinely opaque everywhere."""
    if im.mode not in ("RGBA", "LA", "PA") and "transparency" not in im.info:
        return FMT_RGB565
    a = np.asarray(im.convert("RGBA"))[..., 3]
    return FMT_RGB565 if a.min() == 255 else FMT_ARGB4444


def convert(src_png, dst_name, fmt=None):
    im = Image.open(src_png)
    if fmt is None:
        fmt = pick_format(im)
    w, h = im.size
    assert (w & (w - 1)) == 0 and (h & (h - 1)) == 0, f"{src_png} not power-of-two ({w}x{h})"
    data = to_argb4444(im) if fmt == FMT_ARGB4444 else to_rgb565(im)
    out_path = os.path.join(OUT_DIR, dst_name)
    with open(out_path, "wb") as f:
        f.write(b"BPVR")
        f.write(struct.pack("<HHBB", w, h, fmt, 0))
        f.write(data)
    print(f"{os.path.basename(src_png):20s} -> {dst_name:20s} {w}x{h} "
          f"{'ARGB4444' if fmt == FMT_ARGB4444 else 'RGB565'} ({len(data)} bytes)")


def main():
    # files starting with "_" are QA contact sheets, not game assets
    sprite_files = sorted(f for f in os.listdir(SPR_DIR)
                          if f.endswith(".png") and not f.startswith("_"))
    bg_files = sorted(f for f in os.listdir(BG_DIR) if f.endswith(".png"))
    for d, files in ((SPR_DIR, sprite_files), (BG_DIR, bg_files)):
        for f in files:
            convert(os.path.join(d, f), os.path.splitext(f)[0] + ".pvr")

    total = sum(os.path.getsize(os.path.join(OUT_DIR, f)) for f in os.listdir(OUT_DIR))
    print(f"\n{len(sprite_files) + len(bg_files)} textures written to romdisc/textures/ "
          f"({total / 1024:.0f} KiB total)")


if __name__ == "__main__":
    main()
