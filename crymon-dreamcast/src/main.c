/*
 * Story, maps, species, items, and talk beats are NOT authored here.
 * They live in content/*.json (shared with the web build) and are
 * baked into src/content_*.inc by tools/bake_content.py. Edit the
 * JSON, bake, then rebuild. Runtime (video, Maple, battle loop) stays
 * in this file.
 *
 * hello-world-dreamcast/src/hello.c for the video/vblank/Maple-driver
 * derivation notes against KallistiOS's real source, unchanged since
 * step 1). A full, playable port of the reference LÖVE build
 * (xenodorian/CryMon, the Lua sources under love/game/src): title
 * screen, all 4 maps
 * (HOUSE/VELD/FOREST/GROVE) with camera scrolling and every door/warp
 * between them, the starting room's props and Quillpup/bandage
 * grants, every VELD/FOREST/GROVE NPC and pickup, the full turn-based
 * battle system (attack/special-minigame/guard/items/capture/XP),
 * wild encounters, Bram's shop, and the single ending (DEMO_END,
 * reached via the Grove/Shinigami/Anne chain and the resurrection
 * choice -- see draw_choice()). Each major system's own section comment below
 * (search for "----" banners) cites the exact Lua functions/tables it
 * ports and any formula it reproduces verbatim.
 *
 * Known, deliberate departures from the reference -- each is also
 * called out inline at the relevant code, this is just the index:
 *   - Item icons: bag and shop rows show the real items/ PNG icon
 *     next to each item now; the battle item menu stays text-only
 *     (its rows already run long with stat/percent text and BCONTENT_W
 *     is a small fraction of the full-screen bag/shop menus).
 *   - No manual in-battle "switch to bench monster" item-menu row --
 *     the automatic emergency swap-in on a guard-phase faint is
 *     ported (state.lua does this one automatically too), just not
 *     the player-chosen mid-turn version. (Manual party *lead*
 *     switching, engine.ts's cycleParty(to), IS ported -- see the
 *     party-menu section comment.)
 *   - Sprites: the player and the 3 walking world actors (Mason, Anne,
 *     FOREST soldiers) get a real 4-frame walk cycle per direction;
 *     every stationary world NPC and every fightable species' battle
 *     art idle-animates too now (4 frames at 4fps, 3fps for Shinigami,
 *     matching drawWorld()/drawBattle()'s own clock-driven frame
 *     index -- see draw_npc_idle()/draw_battle_sprites()). Map tiles
 *     still draw as flat color blocks (paintTile's own palette, ported
 *     in full -- see draw_tile); the reference itself does this too
 *     for terrain (draw.lua's paintTile is flat rectangles in the
 *     LÖVE build as well, not a tileset image).
 *   - Dialogue/UI text is upper-cased to fit this port's own hand-
 *     authored 8x8 bitmap font (no lowercase glyph set -- a real
 *     mixed-case font is its own separate undertaking), but every
 *     every TALK_* array (and DEMO_END) now carries the reference's own
 *     punctuation (apostrophes, periods, commas, question/exclamation
 *     marks -- see glyph_apostrophe/period/comma/question/exclaim
 *     near draw_glyph). Characters this font still can't render
 *     (quotes, colons/semicolons folded to commas, em dashes folded
 *     to periods) are the only things actually dropped or substituted
 *     from the source text now.
 */

#include <stdint.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#include "sprites.h"

#define PVR_BASE 0xa05f8000u
#define PVR(reg) (*(volatile u32 *)(PVR_BASE + (reg)))

#define PVR_BORDER_COLOR     0x040
#define PVR_FB_CFG_1         0x044
#define PVR_FB_CFG_2         0x048
#define PVR_RENDER_MODULO    0x04c
#define PVR_FB_ADDR          0x050
#define PVR_FB_SIZE          0x05c
#define PVR_VPOS_IRQ         0x0cc
#define PVR_IL_CFG           0x0d0
#define PVR_BORDER_X         0x0d4
#define PVR_SCAN_CLK         0x0d8
#define PVR_BORDER_Y         0x0dc
#define PVR_VIDEO_CFG        0x0e8
#define PVR_BITMAP_X         0x0ec
#define PVR_BITMAP_Y         0x0f0
#define PVR_SYNC_STATUS      0x10c  /* bits 0-8: nonzero while in vblank */

#define SCREEN_W 320
#define SCREEN_H 240

/* Double buffering, pipelined: each iteration flips to show whatever
   was drawn into the back buffer *last* iteration, then draws the
   next frame into the buffer that just became hidden. This is the
   order a KallistiOS/Dreamcast homebrew community reference (a
   DCEmulation forum thread on KOS double buffering) describes as
   correct: wait_vblank() -> flip the already-drawn buffer into view
   -> only then draw the next frame. The single-buffer version tried
   before this made tearing worse, not better, since every redraw
   wrote directly into whatever the display was actively scanning;
   double buffering keeps drawing entirely off-screen, so only a
   correctly-timed flip is needed to avoid tearing.

   This requires drawing every frame unconditionally, not just when
   something changed: alternating buffers while only sometimes
   redrawing would show one buffer's fresh content and then the
   *other* buffer's stale content every other frame once movement
   stops, which is its own visible glitch. Redrawing every frame is
   safe here specifically because it only ever touches the hidden
   buffer -- it was only unsafe in the single-buffer version. */
#define FB_OFFSET0 0x000000u
#define FB_OFFSET1 0x040000u

static u32 fb_back_offset = FB_OFFSET1;
static volatile u16 *draw_fb = (volatile u16 *)(0xa5000000u + FB_OFFSET1);

static void fb_flip(void) {
    PVR(PVR_FB_ADDR) = fb_back_offset;
    fb_back_offset = (fb_back_offset == FB_OFFSET0) ? FB_OFFSET1 : FB_OFFSET0;
    draw_fb = (volatile u16 *)(0xa5000000u + fb_back_offset);
}

/* DM_320x240_NTSC timing parameters, from KallistiOS's vid_builtin table */
#define SCANLINES 262
#define CLOCKS    857
#define BITMAPX   164
#define BITMAPY   24
#define SCANINT1  21
#define SCANINT2  260
#define BORDERX1  141
#define BORDERX2  843
#define BORDERY1  24
#define BORDERY2  263

static void video_init(void) {
    PVR(PVR_VIDEO_CFG) = PVR(PVR_VIDEO_CFG) | 0x8u;
    PVR(PVR_FB_CFG_1)  = PVR(PVR_FB_CFG_1) & ~1u;

    PVR(PVR_BORDER_COLOR) = 0;

    PVR(PVR_FB_CFG_1) = (1u << 2);
    PVR(PVR_FB_CFG_2) = 1u | (1u << 3);

    PVR(PVR_RENDER_MODULO) = (SCREEN_W * 2) / 8;
    PVR(PVR_FB_ADDR) = 0;

    PVR(PVR_FB_SIZE) = (((SCREEN_W * 2) / 4) - 1)
                      | (1u << 20)
                      | ((SCREEN_H - 1u) << 10);

    PVR(PVR_VPOS_IRQ) = (SCANINT1 << 16) | SCANINT2;
    PVR(PVR_IL_CFG) = 0x100;

    PVR(PVR_BORDER_X) = (BORDERX1 << 16) | BORDERX2;
    PVR(PVR_BORDER_Y) = (BORDERY1 << 16) | BORDERY2;
    PVR(PVR_SCAN_CLK) = (SCANLINES << 16) | CLOCKS;

    PVR(PVR_VIDEO_CFG) = PVR(PVR_VIDEO_CFG) | 0x100u;

    PVR(PVR_BITMAP_X) = BITMAPX;
    PVR(PVR_BITMAP_Y) = (BITMAPY << 16) | BITMAPY;

    *(volatile u32 *)0xa0702c00 =
        (*(volatile u32 *)0xa0702c00 & 0xfffffcffu) | (3u << 8);

    PVR(PVR_VIDEO_CFG) = PVR(PVR_VIDEO_CFG) & ~0x8u;
    PVR(PVR_FB_CFG_1)  = PVR(PVR_FB_CFG_1) | 1u;
}

/* Checked against KallistiOS's own vid_waitvbl() (hardware/video.c):
   wait for vblank to start, then wait for it to end, so each call
   corresponds to exactly one fresh frame. */
static void wait_vblank(void) {
    while(!(PVR(PVR_SYNC_STATUS) & 0x01ffu))
        ;
    while(PVR(PVR_SYNC_STATUS) & 0x01ffu)
        ;
}

static void vram_clear(void) {
    u32 i;
    for(i = 0; i < (u32)SCREEN_W * SCREEN_H; i++)
        draw_fb[i] = 0x0000;
}

/* Screen fade to/from black (bed heal, a party wipe teleporting home
   -- see main()'s fade_state machine). There's no alpha channel to
   composite with here, so this darkens whatever was already drawn
   into draw_fb this frame, in place, as the very last step before
   fb_flip -- level is 0 (untouched) to FADE_STEPS (fully black).
   FADE_STEPS is a power of 2 so the per-channel scale is a multiply
   + shift, not a divide, cheap enough to run over all 76800 pixels
   every frame a fade is in progress. */
#define FADE_STEPS 16
static void apply_fade(int level) {
    u32 i, keep;
    if(level <= 0) return;
    if(level >= FADE_STEPS) {
        vram_clear();
        return;
    }
    keep = (u32)(FADE_STEPS - level);
    for(i = 0; i < (u32)SCREEN_W * SCREEN_H; i++) {
        u16 c = draw_fb[i];
        u16 r = (u16)(((u32)((c >> 11) & 0x1Fu) * keep) >> 4);
        u16 g = (u16)(((u32)((c >> 5) & 0x3Fu) * keep) >> 4);
        u16 b = (u16)(((u32)(c & 0x1Fu) * keep) >> 4);
        draw_fb[i] = (u16)((r << 11) | (g << 5) | b);
    }
}

static void put_pixel(int x, int y, u16 color) {
    if(x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H)
        return;
    draw_fb[y * SCREEN_W + x] = color;
}

static void fill_rect(int x, int y, int w, int h, u16 color) {
    int px, py;
    for(py = y; py < y + h; py++)
        for(px = x; px < x + w; px++)
            put_pixel(px, py, color);
}

static u16 rgb565(u8 r, u8 g, u8 b) {
    return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* Blits a w x h RGB565 sprite (see sprites.h) with its top-left
   corner at (x, y), skipping any pixel equal to SPRITE_KEY
   (color-keyed transparency -- the sprite has no separate alpha
   channel once baked into the array). */
static void blit_sprite(const u16 *px, int w, int h, int x, int y) {
    int sx, sy;
    for(sy = 0; sy < h; sy++) {
        for(sx = 0; sx < w; sx++) {
            u16 c = px[sy * w + sx];
            if(c != SPRITE_KEY)
                put_pixel(x + sx, y + sy, c);
        }
    }
}

/* Shiny palette swap: no second set of source art exists for shiny
   CryMon (see mint_shiny()), so this recolors the existing battle
   sprite at blit time instead -- swap the 5-bit R/G565's B channels
   (a cheap, cheerful "basic palette swap" that turns e.g. a green
   Quillpup magenta/teal) and nudge green down a notch so the swap
   reads as a genuinely different color rather than just a channel
   shuffle on near-gray pixels. */
static u16 shiny_tint(u16 c) {
    u16 r = (u16)((c >> 11) & 0x1F);
    u16 g = (u16)((c >> 5) & 0x3F);
    u16 b = (u16)(c & 0x1F);
    u16 g2 = (u16)(g > 8 ? g - 8 : 0);
    return (u16)((b << 11) | (g2 << 5) | r);
}

/* Battle enter/faint animation: one blitter used for both, driven by
   two independent params instead of two separate effects, so a
   battle sprite always renders through the same code whether it's
   idle, entering, or fainting. `revealed` (0..h) only draws that many
   rows counted up from the BOTTOM of the sprite -- at revealed==h
   it's a plain full blit, at revealed==0 nothing draws, and animating
   it 0->h over a few frames reads as the CryMon rising up into place
   (used when a fresh monster -- a new battle, or a bench swap-in --
   first appears). `fade` (0..16) scales every channel down toward
   black like apply_fade's screen-wide version, but per-sprite;
   animating it 16->0 reads as the CryMon fainting. The two are
   independent so idle draws (revealed=h, fade=16) are just the old
   plain blit_sprite/blit_sprite_shiny with extra math that folds
   away. */
static void blit_sprite_anim(const u16 *px, int w, int h, int x, int y,
                              int shiny, int revealed, int fade) {
    int sx, sy, start;
    if(revealed < 0) revealed = 0;
    if(revealed > h) revealed = h;
    if(fade < 0) fade = 0;
    if(fade > 16) fade = 16;
    start = h - revealed;
    for(sy = start; sy < h; sy++) {
        for(sx = 0; sx < w; sx++) {
            u16 c = px[sy * w + sx];
            if(c == SPRITE_KEY) continue;
            if(shiny) c = shiny_tint(c);
            if(fade < 16) {
                u16 r = (u16)((((c >> 11) & 0x1Fu) * (u32)fade) / 16u);
                u16 g = (u16)((((c >> 5) & 0x3Fu) * (u32)fade) / 16u);
                u16 b = (u16)(((c & 0x1Fu) * (u32)fade) / 16u);
                c = (u16)((r << 11) | (g << 5) | b);
            }
            put_pixel(x + sx, y + sy, c);
        }
    }
}

/* ----------------------------------------------------------------------
 * Font: 8x8, 1bpp glyphs, A-Z/0-9/space plus a handful of hand-added
 * symbol glyphs further down (-+/ for stat/damage text, then
 * '.,?! for real dialogue punctuation). Bit 7 = leftmost pixel of
 * each row. No lowercase set, so all on-screen text is upper-cased by
 * construction -- everything else the source text actually uses now
 * renders as written.
 * ---------------------------------------------------------------------- */
static const uint8_t font_AZ[26][8] = {
    /* A */ { 0b00111100, 0b01000010, 0b10000001, 0b10000001,
              0b11111111, 0b10000001, 0b10000001, 0b00000000 },
    /* B */ { 0b11111100, 0b10000010, 0b10000010, 0b11111100,
              0b10000010, 0b10000010, 0b11111100, 0b00000000 },
    /* C */ { 0b01111110, 0b10000000, 0b10000000, 0b10000000,
              0b10000000, 0b10000000, 0b01111110, 0b00000000 },
    /* D */ { 0b11111100, 0b10000010, 0b10000001, 0b10000001,
              0b10000001, 0b10000010, 0b11111100, 0b00000000 },
    /* E */ { 0b11111111, 0b10000000, 0b10000000, 0b11111100,
              0b10000000, 0b10000000, 0b11111111, 0b00000000 },
    /* F */ { 0b11111111, 0b10000000, 0b10000000, 0b11111100,
              0b10000000, 0b10000000, 0b10000000, 0b00000000 },
    /* G */ { 0b01111110, 0b10000000, 0b10000000, 0b10001111,
              0b10000001, 0b10000001, 0b01111110, 0b00000000 },
    /* H */ { 0b10000001, 0b10000001, 0b10000001, 0b11111111,
              0b10000001, 0b10000001, 0b10000001, 0b00000000 },
    /* I */ { 0b11111111, 0b00011000, 0b00011000, 0b00011000,
              0b00011000, 0b00011000, 0b11111111, 0b00000000 },
    /* J */ { 0b00000111, 0b00000010, 0b00000010, 0b00000010,
              0b10000010, 0b10000010, 0b01111100, 0b00000000 },
    /* K */ { 0b10000010, 0b10000100, 0b10001000, 0b11110000,
              0b10001000, 0b10000100, 0b10000010, 0b00000000 },
    /* L */ { 0b10000000, 0b10000000, 0b10000000, 0b10000000,
              0b10000000, 0b10000000, 0b11111111, 0b00000000 },
    /* M */ { 0b10000001, 0b11000011, 0b10100101, 0b10011001,
              0b10000001, 0b10000001, 0b10000001, 0b00000000 },
    /* N */ { 0b10000001, 0b11000001, 0b10100001, 0b10010001,
              0b10001001, 0b10000101, 0b10000011, 0b00000000 },
    /* O */ { 0b01111110, 0b10000001, 0b10000001, 0b10000001,
              0b10000001, 0b10000001, 0b01111110, 0b00000000 },
    /* P */ { 0b11111100, 0b10000010, 0b10000010, 0b11111100,
              0b10000000, 0b10000000, 0b10000000, 0b00000000 },
    /* Q */ { 0b01111110, 0b10000001, 0b10000001, 0b10000001,
              0b10010101, 0b10001001, 0b01110110, 0b00000000 },
    /* R */ { 0b11111110, 0b10000001, 0b10000001, 0b11111110,
              0b10010000, 0b10001000, 0b10000100, 0b00000000 },
    /* S */ { 0b01111110, 0b10000000, 0b10000000, 0b01111100,
              0b00000010, 0b00000010, 0b11111100, 0b00000000 },
    /* T */ { 0b11111111, 0b00011000, 0b00011000, 0b00011000,
              0b00011000, 0b00011000, 0b00011000, 0b00000000 },
    /* U */ { 0b10000001, 0b10000001, 0b10000001, 0b10000001,
              0b10000001, 0b10000001, 0b01111110, 0b00000000 },
    /* V */ { 0b10000001, 0b10000001, 0b10000001, 0b01000010,
              0b01000010, 0b00100100, 0b00011000, 0b00000000 },
    /* W */ { 0b10000001, 0b10000001, 0b10000001, 0b10100101,
              0b10100101, 0b11011011, 0b10000001, 0b00000000 },
    /* X */ { 0b10000001, 0b01000010, 0b00100100, 0b00011000,
              0b00100100, 0b01000010, 0b10000001, 0b00000000 },
    /* Y */ { 0b10000001, 0b01000010, 0b00100100, 0b00011000,
              0b00011000, 0b00011000, 0b00011000, 0b00000000 },
    /* Z */ { 0b11111111, 0b00000010, 0b00000100, 0b00001000,
              0b00010000, 0b00100000, 0b11111111, 0b00000000 },
};

/* Digits 0-9, same style/size as font_AZ. Needed for the room's HUD
   (Quillpup's level, the bandage count) -- nothing in the dialogue
   text itself uses digits. */
static const uint8_t font_09[10][8] = {
    /* 0 */ { 0b00111100, 0b01100110, 0b01101110, 0b01110110,
              0b01100110, 0b01100110, 0b00111100, 0b00000000 },
    /* 1 */ { 0b00011000, 0b00111000, 0b00011000, 0b00011000,
              0b00011000, 0b00011000, 0b01111110, 0b00000000 },
    /* 2 */ { 0b00111100, 0b01100110, 0b00000110, 0b00001100,
              0b00011000, 0b00110000, 0b01111110, 0b00000000 },
    /* 3 */ { 0b00111100, 0b01100110, 0b00000110, 0b00011100,
              0b00000110, 0b01100110, 0b00111100, 0b00000000 },
    /* 4 */ { 0b00001100, 0b00011100, 0b00101100, 0b01001100,
              0b01111110, 0b00001100, 0b00001100, 0b00000000 },
    /* 5 */ { 0b01111110, 0b01100000, 0b01111100, 0b00000110,
              0b00000110, 0b01100110, 0b00111100, 0b00000000 },
    /* 6 */ { 0b00011100, 0b00110000, 0b01100000, 0b01111100,
              0b01100110, 0b01100110, 0b00111100, 0b00000000 },
    /* 7 */ { 0b01111110, 0b00000110, 0b00001100, 0b00011000,
              0b00110000, 0b00110000, 0b00110000, 0b00000000 },
    /* 8 */ { 0b00111100, 0b01100110, 0b01100110, 0b00111100,
              0b01100110, 0b01100110, 0b00111100, 0b00000000 },
    /* 9 */ { 0b00111100, 0b01100110, 0b01100110, 0b00111110,
              0b00000110, 0b00001100, 0b00111000, 0b00000000 },
};

/* '-', '+', '/', needed by the battle system's stat-mod and damage
   messages ("STR+4", "FOE STR-3", "HP N/N") -- font_AZ/font_09 alone
   can't render any of these. */
static const uint8_t glyph_minus[8] = {
    0, 0, 0, 0b00111100, 0, 0, 0, 0,
};
static const uint8_t glyph_plus[8] = {
    0, 0b00011000, 0b00011000, 0b01111110, 0b00011000, 0b00011000, 0, 0,
};
static const uint8_t glyph_slash[8] = {
    0b00000011, 0b00000110, 0b00001100, 0b00011000,
    0b00110000, 0b01100000, 0b01000000, 0,
};

/* Punctuation, added to restore the reference's actual TALK_* text
   (apostrophes/periods/commas/question and exclamation marks) instead
   of the all-depunctuated placeholder text this font's original A-Z/
   0-9/-+/ set forced. Same minimalist style as the digits above (a
   6-wide glyph inside the 8-wide cell). */
static const uint8_t glyph_apostrophe[8] = {
    0b00110000, 0b00110000, 0b00100000, 0, 0, 0, 0, 0,
};
static const uint8_t glyph_period[8] = {
    0, 0, 0, 0, 0, 0, 0b00110000, 0,
};
static const uint8_t glyph_comma[8] = {
    0, 0, 0, 0, 0, 0b00110000, 0b00110000, 0b00100000,
};
static const uint8_t glyph_question[8] = {
    0b00111100, 0b01100110, 0b00001100, 0b00011000,
    0b00011000, 0, 0b00011000, 0,
};
static const uint8_t glyph_exclaim[8] = {
    0b00011000, 0b00011000, 0b00011000, 0b00011000,
    0b00011000, 0, 0b00011000, 0,
};

/* Every text call draws with a 2px black outline instead of sitting
   on an opaque box (see the now-boxless draw_dialogue_box/
   draw_menu_frame/draw_box/draw_hud_toast below).
   The outline is a full 2px ring (every offset from -2..2 except
   (0,0), 24 of them) drawn in black first so the border has no
   gaps at glyph corners even at that thickness, then the glyph
   itself is drawn once on top in the real color (not bold -- see
   git history for the bold faux-thickening pass this replaced).
   Costs ~24x the fill work per glyph over the original single-pass
   version, but glyphs are tiny (8x8) and this only runs for
   on-screen text. */
static void draw_glyph(int ox, int oy, const uint8_t bitmap[8], u16 color, int scale) {
    int row, col, sx, sy, dx, dy;

    for(dy = -2; dy <= 2; dy++) {
        for(dx = -2; dx <= 2; dx++) {
            if(dx == 0 && dy == 0)
                continue;
            for(row = 0; row < 8; row++) {
                uint8_t bits = bitmap[row];
                for(col = 0; col < 8; col++) {
                    if(!(bits & (0x80 >> col)))
                        continue;
                    for(sy = 0; sy < scale; sy++)
                        for(sx = 0; sx < scale; sx++)
                            put_pixel(ox + col * scale + sx + dx,
                                      oy + row * scale + sy + dy, 0x0000);
                }
            }
        }
    }

    for(row = 0; row < 8; row++) {
        uint8_t bits = bitmap[row];

        for(col = 0; col < 8; col++) {
            if(!(bits & (0x80 >> col)))
                continue;

            for(sy = 0; sy < scale; sy++)
                for(sx = 0; sx < scale; sx++)
                    put_pixel(ox + col * scale + sx,
                              oy + row * scale + sy, color);
        }
    }
}

/* Extra gap between characters, beyond the glyph's own 8px cell.
   draw_glyph's black outline is a full 2px ring around each glyph, so
   a gap under 2px still lets adjacent glyphs' outlines collide even
   though the glyphs themselves don't -- the previous 1px gap (scale)
   wasn't enough, still visibly overlapping; +2 flat pixels on top of
   the scaled base gap clears the outline reach with room to spare.
   CHAR_CELL is the resulting total per-character advance; every
   char-count-based word-wrap width (DIALOGUE_MAX_CHARS, the battle
   message box, the ending screen) is computed from it rather than a
   bare /8, so wrapping still matches the font's real on-screen
   width instead of running text past the edge of its box. */
#define LETTER_GAP(scale) ((scale) + 2)
#define CHAR_CELL(scale)  (8 * (scale) + LETTER_GAP(scale))

static void draw_text_s(const char *s, int x, int y, u16 color, int scale) {
    int cx = x;
    int px = CHAR_CELL(scale);
    for(; *s; s++) {
        if(*s >= 'A' && *s <= 'Z')
            draw_glyph(cx, y, font_AZ[*s - 'A'], color, scale);
        else if(*s >= '0' && *s <= '9')
            draw_glyph(cx, y, font_09[*s - '0'], color, scale);
        else if(*s == '-')
            draw_glyph(cx, y, glyph_minus, color, scale);
        else if(*s == '+')
            draw_glyph(cx, y, glyph_plus, color, scale);
        else if(*s == '/')
            draw_glyph(cx, y, glyph_slash, color, scale);
        else if(*s == '\'')
            draw_glyph(cx, y, glyph_apostrophe, color, scale);
        else if(*s == '.')
            draw_glyph(cx, y, glyph_period, color, scale);
        else if(*s == ',')
            draw_glyph(cx, y, glyph_comma, color, scale);
        else if(*s == '?')
            draw_glyph(cx, y, glyph_question, color, scale);
        else if(*s == '!')
            draw_glyph(cx, y, glyph_exclaim, color, scale);
        cx += px;
    }
}

static int text_width_s(const char *s, int scale) {
    int n = 0;
    for(; *s; s++) n++;
    return n * CHAR_CELL(scale);
}

static void draw_text_center_s(const char *s, int cx, int y, u16 color, int scale) {
    draw_text_s(s, cx - text_width_s(s, scale) / 2, y, color, scale);
}

static int word_len(const char *s) {
    int n = 0;
    while(s[n] && s[n] != ' ')
        n++;
    return n;
}

/* Word-wraps s (space-separated, uppercase-and-punctuation-free like
   every TALK string below) into lines of at most max_chars columns,
   greedily packing words, and draws each line left-aligned at x
   starting at y with line_h pixels between line tops. Used for the
   dialogue box, whose longest ported line (data.TALK.shelf's second
   beat) wraps to exactly 3 lines -- the box is sized for that. */
#define WRAP_BUF_MAX 40
static void draw_wrapped(const char *s, int x, int y, u16 color, int scale,
                          int max_chars, int line_h) {
    char buf[WRAP_BUF_MAX + 1];
    int buf_len = 0;
    int line = 0;
    const char *p = s;

    for(;;) {
        int wlen = word_len(p);
        int need = wlen + (buf_len > 0 ? 1 : 0);

        if(buf_len > 0 && buf_len + need > max_chars) {
            buf[buf_len] = 0;
            draw_text_s(buf, x, y + line * line_h, color, scale);
            line++;
            buf_len = 0;
        }

        if(buf_len > 0)
            buf[buf_len++] = ' ';
        {
            int i;
            for(i = 0; i < wlen && buf_len < WRAP_BUF_MAX; i++)
                buf[buf_len++] = p[i];
        }

        p += wlen;
        if(*p != ' ')
            break;
        p++;
    }

    buf[buf_len] = 0;
    draw_text_s(buf, x, y + line * line_h, color, scale);
}

/* ----------------------------------------------------------------------
 * Tiny manual string building for the HUD/bag/party rows below (no
 * libc here -- nostdlib/ffreestanding). s_cat/s_cat_uint append to a
 * caller-owned buffer and return the new length; callers chain them
 * to build one row's text before a single draw_text_s call.
 * ---------------------------------------------------------------------- */
static int s_cat(char *dst, int len, const char *src) {
    while(*src)
        dst[len++] = *src++;
    return len;
}

static int s_cat_uint(char *dst, int len, int v) {
    char tmp[6];
    int n = 0;
    if(v == 0) {
        dst[len++] = '0';
        return len;
    }
    while(v > 0 && n < 6) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while(n > 0)
        dst[len++] = tmp[--n];
    return len;
}

#define TITLE_SCALE 3
#define DIALOGUE_SCALE 1

static void draw_press_start(void) {
    vram_clear();
    draw_text_center_s("PRESS START", SCREEN_W / 2,
                        SCREEN_H / 2 - 4 * TITLE_SCALE, 0xFFFF, TITLE_SCALE);
}

/* ----------------------------------------------------------------------
 * Minimal Maple bus controller driver (port A / unit 0 only). Same
 * derivation as hello-world-dreamcast/src/hello.c -- see that file for
 * the full explanation of the register layout and packet format,
 * checked against KallistiOS's own Maple driver source.
 * ---------------------------------------------------------------------- */
#define MAPLE_BASE      0xa05f6c00u
#define MAPLE_DMA_ADDR  (*(volatile u32 *)(MAPLE_BASE + 0x04))
#define MAPLE_DMA_TSEL  (*(volatile u32 *)(MAPLE_BASE + 0x10))
#define MAPLE_ENABLE    (*(volatile u32 *)(MAPLE_BASE + 0x14))
#define MAPLE_STATE     (*(volatile u32 *)(MAPLE_BASE + 0x18))
#define MAPLE_SPEED     (*(volatile u32 *)(MAPLE_BASE + 0x80))
#define MAPLE_DMA_PROT  (*(volatile u32 *)(MAPLE_BASE + 0x8c))

#define MAPLE_COMMAND_GETCOND   9
#define MAPLE_RESPONSE_DATATRF  8
#define MAPLE_FUNC_CONTROLLER   0x01000000u

#define CONT_C            (1u << 0)
#define CONT_B            (1u << 1)
#define CONT_A            (1u << 2)
#define CONT_START        (1u << 3)
#define CONT_DPAD_UP      (1u << 4)
#define CONT_DPAD_DOWN    (1u << 5)
#define CONT_DPAD_LEFT    (1u << 6)
#define CONT_DPAD_RIGHT   (1u << 7)
#define CONT_Z            (1u << 8)
#define CONT_Y            (1u << 9)
#define CONT_X            (1u << 10)

static u32 maple_cmd_buf[8]  __attribute__((aligned(32)));
static u32 maple_resp_buf[64] __attribute__((aligned(32)));

#define P2(p)   ((volatile u32 *)(((u32)(p)) | 0x20000000u))
#define PHYS(p) (((u32)(p)) & 0x1fffffffu)

static void maple_init(void) {
    MAPLE_DMA_PROT = 0x6155404fu;
    MAPLE_DMA_TSEL = 0;
    MAPLE_SPEED = 0x0000u | (50000u << 16);
    MAPLE_ENABLE = 1;
}

static u16 maple_poll_buttons(void) {
    volatile u32 *cmd  = P2(maple_cmd_buf);
    volatile u32 *resp = P2(maple_resp_buf);
    u32 timeout;

    cmd[0] = 1u | (0u << 16) | 0x80000000u;
    cmd[1] = PHYS(maple_resp_buf);
    cmd[2] = MAPLE_COMMAND_GETCOND | (0x20u << 8) | (0u << 16) | (1u << 24);
    cmd[3] = MAPLE_FUNC_CONTROLLER;

    MAPLE_DMA_ADDR = PHYS(maple_cmd_buf);
    MAPLE_STATE = 1;

    for(timeout = 0; timeout < 2000000u; timeout++) {
        if(MAPLE_STATE == 0)
            break;
    }
    if(MAPLE_STATE != 0)
        return 0xffff;

    if((resp[0] & 0xffu) != MAPLE_RESPONSE_DATATRF)
        return 0xffff;
    if(resp[1] != MAPLE_FUNC_CONTROLLER)
        return 0xffff;

    return (u16)(resp[2] & 0xffffu);
}

static int pressed(u16 raw, u16 mask) {
    return (raw & mask) == 0;
}

/* ----------------------------------------------------------------------
 * The world: all 4 maps, verbatim from CryMon's love/game/src/data.lua
 * (data.HOUSE/VELD/FOREST/GROVE), data.isSolidTile (SOLID_SET), and
 * love/game/src/draw.lua (paintTile). 32px tiles in the original,
 * drawn here at 20px (matching every earlier step); VELD/FOREST/GROVE
 * are much bigger than the 320x240 screen, so this step adds camera
 * scrolling (compute_camera below) -- HOUSE still ends up centered
 * exactly like before, since a map smaller than the screen just gets
 * a centered (possibly negative) camera offset.
 *
 * GROVE's mid-map 'D' row (data.lua splits the Cathleen half from the
 * Shinigami half, opened only after Cathleen is caught) is walkable
 * here rather than gated: no cathCaught state exists yet (that's
 * battle/catching, a later milestone), and the reference project's
 * own data.lua notes that native/crymon.c already ships this same
 * simplification, so it's a documented, precedented deferral rather
 * than a new gap.
 * ---------------------------------------------------------------------- */
#define TILE 20

#define MAP_HOUSE  0
#define MAP_VELD   1
#define MAP_FOREST 2
#define MAP_GROVE  3
#define MAP_CAMP   4
#define MAP_CLIFFS 5
#define MAP_RUINS  6

typedef struct {
    const char *const *rows;
    int cols, rows_n;
} Map;

#include "content_maps.inc"

static int tile_is_solid(char ch) {
    static const char *const solid = "#HWRBC^NKEVAQXUJISMGL89r";
    const char *p;
    for(p = solid; *p; p++)
        if(*p == ch)
            return 1;
    return 0;
}

/* blocked()'s GROVE gate-row addition vs crymon.c (see data.lua's
   GROVE comment): the mid-map door row is walkable per SOLID_SET
   (data.isSolidTile has no 'D'), but state.lua blocks it in the
   collision check itself until Cathleen is caught -- now that
   Cathleen is a real, catchable GROVE fight in this port, this gate
   is ported too instead of staying an open door. The story now has
   her relinquish the door's key on any win against her, capture or
   not (both call sites below OR beat_cathleen into the cath_caught
   param), so the 3rd param here still just means "the key is hers to
   give" regardless of which flag actually earned it. */
static int tile_blocked(int map_id, char ch, int cath_caught, int beat_calder, int beat_shin) {
    if(tile_is_solid(ch)) return 1;
    if(map_id == MAP_GROVE && ch == 'D' && !cath_caught) return 1;
    if(map_id == MAP_VELD && ch == 'F' && !beat_calder) return 1;
    /* 'g': the way south past where Shinigami stood, to MAP_RUINS --
       only open once he's gone (see map_grove_rows' own comment). */
    if(map_id == MAP_GROVE && ch == 'g' && !beat_shin) return 1;
    return 0;
}

/* Out-of-bounds tiles read as '#' (solid), matching data.tileAt. */
static char tile_at(int map_id, int col, int row) {
    const Map *m = &MAPS[map_id];
    if(col < 0 || col >= m->cols || row < 0 || row >= m->rows_n)
        return '#';
    return m->rows[row][col];
}

/* First occurrence of mark, row-major, matching data.spawnOf's
   row:find() scan order. Every call site below only asks for marks
   known to exist on that map (checked against the grids above), so
   the not-found fallback is never actually hit in practice. */
static void find_mark(int map_id, char mark, int *out_col, int *out_row) {
    const Map *m = &MAPS[map_id];
    int row, col;
    for(row = 0; row < m->rows_n; row++) {
        for(col = 0; col < m->cols; col++) {
            if(m->rows[row][col] == mark) {
                *out_col = col;
                *out_row = row;
                return;
            }
        }
    }
    *out_col = 2;
    *out_row = 2;
}

static void mark_center(int map_id, char mark, int *out_x, int *out_y) {
    int col, row;
    find_mark(map_id, mark, &col, &row);
    *out_x = col * TILE + TILE / 2;
    *out_y = row * TILE + TILE / 2;
}

/* draw.lua's paintTile, verbatim palette (including its two-rect
   detail tiles: grass speckle '.', tree 'T', forest-floor '#', flower
   '*'). Tile chars not explicitly listed (every NPC/prop mark, plus
   VELD/FOREST/GROVE decoration letters not ported to real props
   here) fall through to paintTile's own grass-green default. */
static void draw_tile(char ch, int dx, int dy) {
    int t = TILE;

    switch(ch) {
        case 'H':
            fill_rect(dx, dy, t, t, rgb565(42, 30, 22));
            return;
        case 'r':
            /* Roof cap: the top row of every real house block (Max's
               own on VELD, the decorative houses, and any new-map
               houses) is this instead of a second 'H' row now, so
               a house reads as a peaked roof over a wall instead of
               a flat two-tone block. Lowercase since every uppercase
               letter is already spoken for by an existing tile/mark. */
            fill_rect(dx, dy, t, t, rgb565(96, 40, 32));
            return;
        case 'R':
            fill_rect(dx, dy, t, t, rgb565(106, 64, 48));
            return;
        case 'F':
        case 'P':
            fill_rect(dx, dy, t, t, rgb565(106, 82, 56));
            return;
        case 'D':
            fill_rect(dx, dy, t, t, rgb565(26, 18, 12));
            return;
        case 'B':
        case 'U':
        case 'C':
        case 'S':
            fill_rect(dx, dy, t, t, rgb565(106, 82, 56));
            return;
        case '.':
            fill_rect(dx, dy, t, t, rgb565(61, 90, 56));
            fill_rect(dx + 3, dy + 4, 1, 1, rgb565(90, 122, 82));
            return;
        case 'T':
            fill_rect(dx, dy, t, t, rgb565(47, 74, 44));
            fill_rect(dx + 4, dy + 3, 3, 15, rgb565(106, 138, 58));
            fill_rect(dx + 10, dy + 1, 3, 16, rgb565(90, 122, 82));
            return;
        case '=':
        case 'Z':
        case 'Y':
        case '3':
        case 'O':
        case '8':
        case '9':
            fill_rect(dx, dy, t, t, rgb565(107, 90, 58));
            return;
        case ',':
            fill_rect(dx, dy, t, t, rgb565(90, 74, 58));
            return;
        case '#':
            fill_rect(dx, dy, t, t, rgb565(28, 36, 24));
            fill_rect(dx + 3, dy + 1, 15, 11, rgb565(61, 90, 56));
            return;
        case 'W':
            fill_rect(dx, dy, t, t, rgb565(42, 58, 68));
            return;
        case '^':
            fill_rect(dx, dy, t, t, rgb565(74, 64, 48));
            return;
        case 'N':
        case 'E':
            fill_rect(dx, dy, t, t, rgb565(90, 70, 48));
            return;
        case '*':
            fill_rect(dx, dy, t, t, rgb565(61, 90, 56));
            fill_rect(dx + 8, dy + 8, 4, 4, rgb565(180, 60, 80));
            return;
        default:
            fill_rect(dx, dy, t, t, rgb565(61, 90, 56));
            return;
    }
}

/* Keeps the player roughly centered, clamped to the map's edges; a
   map no bigger than the screen (HOUSE) instead gets a fixed,
   centered offset (possibly negative), which is what actually
   produces the letterboxed look the old HOUSE-only code hardcoded --
   a tile drawn at col*TILE - cam_x still lands in the right place
   when cam_x is negative. */
static void compute_camera(int map_id, int px, int py, int *cam_x, int *cam_y) {
    const Map *m = &MAPS[map_id];
    int mw = m->cols * TILE, mh = m->rows_n * TILE;
    int cx, cy;

    if(mw <= SCREEN_W) {
        cx = (mw - SCREEN_W) / 2;
    }
    else {
        cx = px - SCREEN_W / 2;
        if(cx < 0) cx = 0;
        if(cx > mw - SCREEN_W) cx = mw - SCREEN_W;
    }

    if(mh <= SCREEN_H) {
        cy = (mh - SCREEN_H) / 2;
    }
    else {
        cy = py - SCREEN_H / 2;
        if(cy < 0) cy = 0;
        if(cy > mh - SCREEN_H) cy = mh - SCREEN_H;
    }

    *cam_x = cx;
    *cam_y = cy;
}

/* Draws only the tile range that can be visible at this camera
   offset -- VELD alone is 30x22 = 660 tiles, too many to redraw
   in full every frame at 20px/tile through put_pixel. */
static void draw_map(int map_id, int cam_x, int cam_y) {
    const Map *m = &MAPS[map_id];
    int col0 = cam_x / TILE;
    int col1 = (cam_x + SCREEN_W) / TILE + 1;
    int row0 = cam_y / TILE;
    int row1 = (cam_y + SCREEN_H) / TILE + 1;
    int row, col;

    if(col0 < 0) col0 = 0;
    if(row0 < 0) row0 = 0;
    if(col1 > m->cols) col1 = m->cols;
    if(row1 > m->rows_n) row1 = m->rows_n;

    /* Letterboxed maps (HOUSE) leave a border the tile loop below
       never touches, so it needs clearing or it'd show whatever was
       drawn there previously (e.g. the title screen text). */
    vram_clear();

    for(row = row0; row < row1; row++)
        for(col = col0; col < col1; col++)
            draw_tile(m->rows[row][col], col * TILE - cam_x, row * TILE - cam_y);
}

/* Props are center-anchored on their mark's tile center, matching the
   flat-rect placeholders this replaced in an earlier step. Sizes come
   from sprites.h (the real art's own dimensions, downscaled by
   gen_sprites.py -- not the original 32px-tile-space sizes from
   render.lua, which didn't match the actual art's proportions
   anyway). HOUSE-only: B/U/S/C don't appear on any other map. */
static void draw_prop(char mark, const u16 *px, int w, int h, int cam_x, int cam_y) {
    int cx, cy;
    mark_center(MAP_HOUSE, mark, &cx, &cy);
    blit_sprite(px, w, h, cx - cam_x - w / 2, cy - cam_y - h / 2);
}

static void draw_props(int map_id, int cam_x, int cam_y) {
    if(map_id != MAP_HOUSE)
        return;
    draw_prop('B', prop_bed_father, PROP_BED_FATHER_W, PROP_BED_FATHER_H, cam_x, cam_y);
    draw_prop('U', prop_bed_empty, PROP_BED_EMPTY_W, PROP_BED_EMPTY_H, cam_x, cam_y);
    draw_prop('S', prop_shelf, PROP_SHELF_W, PROP_SHELF_H, cam_x, cam_y);
    draw_prop('C', prop_crate, PROP_CRATE_W, PROP_CRATE_H, cam_x, cam_y);
}

/* World NPCs: bottom-anchored on their mark's tile like the player
   (feet at the tile center), matching render.lua's actor placement.
   All stationary single-frame sprites -- see the world-NPC section
   comment further down for why (no walk/idle animation ported for
   any of them, unlike the player). */
/* A patrolling/chasing FOREST soldier. File-scope (not just a local
   inside main()) since draw_npcs below needs the type too. Matches
   engine.ts's soldiers[] entries (id/name/species/level stay in the
   const SoldierDef table further down; this is just the live
   movement state). */
typedef struct {
    float x, y;
    int dir, anim;
    int chase;
    int axis;           /* 0 = patrols x, 1 = patrols y, 2 = stationary */
    float minv, maxv;
    int sign;
} Soldier;

/* Every world actor (the 8 stationary NPCs, Mason/Anne/soldiers, and
   the player) is bottom-center anchored at a "feet" point (cx, cy) in
   map/world pixel space -- draw order used to just be "all the NPCs,
   then the player on top, always", so the player would occlude an
   NPC standing further down the screen than them (in front, by the
   usual 2D convention) instead of the other way around. Now every
   sprite due to be drawn this frame is collected into this list
   first, instead of blitting immediately, so they can all be sorted
   by cy and drawn back-to-front (lower feet == closer to the camera
   == drawn last == on top) regardless of which is the player and
   which is an NPC. */
typedef struct {
    const u16 *px;
    int w, h;
    int cx, cy;
} WorldSprite;

#define MAX_WORLD_SPRITES 12

static void ws_push(WorldSprite *list, int *n, const u16 *px, int w, int h, int cx, int cy) {
    if(*n >= MAX_WORLD_SPRITES) return;
    list[*n].px = px;
    list[*n].w = w;
    list[*n].h = h;
    list[*n].cx = cx;
    list[*n].cy = cy;
    (*n)++;
}

static void ws_push_mark(WorldSprite *list, int *n, int map_id, char mark, const u16 *px, int w, int h) {
    int cx, cy;
    mark_center(map_id, mark, &cx, &cy);
    ws_push(list, n, px, w, h, cx, cy);
}

/* Idle-animated variant of the above, for the 8 stationary NPCs that
   still cycle 4 standing frames in the reference (drawWorld()'s
   `Math.floor(this.clock * 4) % 4 + 1`, `* 3` for Shinigami --
   frames_per_step converts that fps into "how many 60Hz vblank
   frames this idle frame holds", 15 for 4fps, 20 for 3fps). */
static void ws_push_mark_idle(WorldSprite *list, int *n, int map_id, char mark,
                               const u16 *const frames[4], u32 frame_count,
                               int frames_per_step, int w, int h) {
    int f = (int)((frame_count / (u32)frames_per_step) % 4u);
    ws_push_mark(list, n, map_id, mark, frames[f], w, h);
}

/* dir/frame lookup tables for the 3 walking actors (Mason, Anne, the
   FOREST soldiers all share one sprite set), matching
   gen_sprites.py's PLAYER_DIRS order: 0=down,1=up,2=left,3=right. */
static const u16 *const MASON_FRAMES[4][4] = {
    { npc_mason_down_1,  npc_mason_down_2,  npc_mason_down_3,  npc_mason_down_4 },
    { npc_mason_up_1,    npc_mason_up_2,    npc_mason_up_3,    npc_mason_up_4 },
    { npc_mason_left_1,  npc_mason_left_2,  npc_mason_left_3,  npc_mason_left_4 },
    { npc_mason_right_1, npc_mason_right_2, npc_mason_right_3, npc_mason_right_4 },
};
static const u16 *const ANNE_FRAMES[4][4] = {
    { npc_anne_down_1,  npc_anne_down_2,  npc_anne_down_3,  npc_anne_down_4 },
    { npc_anne_up_1,    npc_anne_up_2,    npc_anne_up_3,    npc_anne_up_4 },
    { npc_anne_left_1,  npc_anne_left_2,  npc_anne_left_3,  npc_anne_left_4 },
    { npc_anne_right_1, npc_anne_right_2, npc_anne_right_3, npc_anne_right_4 },
};
static const u16 *const SOLDIER_FRAMES[4][4] = {
    { npc_soldier_down_1,  npc_soldier_down_2,  npc_soldier_down_3,  npc_soldier_down_4 },
    { npc_soldier_up_1,    npc_soldier_up_2,    npc_soldier_up_3,    npc_soldier_up_4 },
    { npc_soldier_left_1,  npc_soldier_left_2,  npc_soldier_left_3,  npc_soldier_left_4 },
    { npc_soldier_right_1, npc_soldier_right_2, npc_soldier_right_3, npc_soldier_right_4 },
};

static void ws_push_walker(WorldSprite *list, int *n, const u16 *const frames[4][4],
                            float x, float y, int dir, int frame) {
    ws_push(list, n, frames[dir & 3][frame & 3], NPC_SPRITE_W, NPC_SPRITE_H, (int)x, (int)y);
}

static const u16 *const WREN_FRAMES[4]   = { npc_wren_1, npc_wren_2, npc_wren_3, npc_wren_4 };
static const u16 *const MAE_FRAMES[4]    = { npc_mae_1, npc_mae_2, npc_mae_3, npc_mae_4 };
static const u16 *const IVO_FRAMES[4]    = { npc_ivo_1, npc_ivo_2, npc_ivo_3, npc_ivo_4 };
static const u16 *const NELL_FRAMES[4]   = { npc_nell_1, npc_nell_2, npc_nell_3, npc_nell_4 };
static const u16 *const PIKE_FRAMES[4]   = { npc_pike_1, npc_pike_2, npc_pike_3, npc_pike_4 };
static const u16 *const BRAM_FRAMES[4]   = { npc_bram_1, npc_bram_2, npc_bram_3, npc_bram_4 };
static const u16 *const CALDER_FRAMES[4] = { npc_calder_1, npc_calder_2, npc_calder_3, npc_calder_4 };
static const u16 *const SHINIGAMI_FRAMES[4] = { npc_shinigami_1, npc_shinigami_2, npc_shinigami_3, npc_shinigami_4 };
static const u16 *const OREN_FRAMES[4]  = { npc_oren_1, npc_oren_2, npc_oren_3, npc_oren_4 };
static const u16 *const TESSA_FRAMES[4] = { npc_tessa_1, npc_tessa_2, npc_tessa_3, npc_tessa_4 };
static const u16 *const BIRCH_FRAMES[4] = { npc_birch_1, npc_birch_2, npc_birch_3, npc_birch_4 };
static const u16 *const SABLE_FRAMES[4] = { npc_sable_1, npc_sable_2, npc_sable_3, npc_sable_4 };
static const u16 *const CROSS_FRAMES[4]      = { npc_cross_1, npc_cross_2, npc_cross_3, npc_cross_4 };
static const u16 *const COMMANDER_FRAMES[4]  = { npc_commander_1, npc_commander_2, npc_commander_3, npc_commander_4 };
static const u16 *const CONSCRIPT_FRAMES[4]  = { npc_conscript_1, npc_conscript_2, npc_conscript_3, npc_conscript_4 };
static const u16 *const ENFORCER_FRAMES[4]   = { npc_enforcer_1, npc_enforcer_2, npc_enforcer_3, npc_enforcer_4 };
static const u16 *const SENTRY_FRAMES[4]     = { npc_sentry_1, npc_sentry_2, npc_sentry_3, npc_sentry_4 };
/* npc_father_1..4 (Father's walk frames) aren't used -- he's bedridden
   and only ever appears via his portrait (SPK_FATHER), never placed as
   a WorldSprite. */

static void collect_npcs(WorldSprite *list, int *n, int map_id, u32 frame_count,
                          int mason_state, float mason_x, float mason_y, int mason_dir, int mason_frame,
                          int anne_state, float anne_x, float anne_y, int anne_dir, int anne_frame,
                          int cath_caught, int beat_shin,
                          const Soldier *soldiers, const int *soldier_beaten) {
    if(map_id == MAP_VELD) {
        ws_push_mark_idle(list, n, map_id, 'K', WREN_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'I', MAE_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'V', IVO_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'A', NELL_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'Q', PIKE_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'J', BRAM_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'E', CALDER_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        if(mason_state)
            ws_push_walker(list, n, MASON_FRAMES, mason_x, mason_y, mason_dir, mason_frame);
    }
    else if(map_id == MAP_FOREST) {
        int i;
        for(i = 0; i < 3; i++) {
            if(soldier_beaten[i]) continue;
            ws_push_walker(list, n, SOLDIER_FRAMES, soldiers[i].x, soldiers[i].y, soldiers[i].dir,
                            (int)soldiers[i].anim);
        }
    }
    else if(map_id == MAP_GROVE) {
        if(!beat_shin)
            ws_push_mark_idle(list, n, map_id, '9', SHINIGAMI_FRAMES, frame_count, 20, NPC_SPRITE_W, NPC_SPRITE_H);
        if(!cath_caught)
            ws_push_mark(list, n, map_id, '8', npc_cathleen, CATHLEEN_WORLD_W, CATHLEEN_WORLD_H);
        ws_push_mark_idle(list, n, map_id, 'K', CROSS_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
    }
    else if(map_id == MAP_CAMP) {
        ws_push_mark_idle(list, n, map_id, 'I', COMMANDER_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'K', CONSCRIPT_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'A', ENFORCER_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
    }
    else if(map_id == MAP_CLIFFS) {
        ws_push_mark_idle(list, n, map_id, 'V', SENTRY_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'Y', TESSA_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        /* Treasure chest: reuses the existing crate prop art rather
           than needing new placeholder art -- close enough visually
           (a wooden storage box) that it doesn't need its own tag. */
        ws_push_mark(list, n, map_id, 'C', prop_crate, PROP_CRATE_W, PROP_CRATE_H);
    }
    else if(map_id == MAP_RUINS) {
        ws_push_mark_idle(list, n, map_id, 'J', OREN_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'K', BIRCH_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
        ws_push_mark_idle(list, n, map_id, 'A', SABLE_FRAMES, frame_count, 15, NPC_SPRITE_W, NPC_SPRITE_H);
    }

    /* Anne isn't tied to one map like the stationary VELD NPCs --
       her second approach now meets Max wherever she is right after
       the Cathleen fight (the GROVE, not necessarily VELD), so this
       is unconditional instead of living inside the MAP_VELD branch
       above. */
    if(anne_state)
        ws_push_walker(list, n, ANNE_FRAMES, anne_x, anne_y, anne_dir, anne_frame);
}

/* Insertion sort by cy (small n, not worth anything fancier) then
   blit back-to-front: lower feet (larger cy) draw last, i.e. on top,
   matching the usual 2D convention that standing further down the
   screen means standing closer to the camera. */
static void ws_sort_and_draw(WorldSprite *list, int n, int cam_x, int cam_y) {
    int i, j;
    for(i = 1; i < n; i++) {
        WorldSprite key = list[i];
        j = i - 1;
        while(j >= 0 && list[j].cy > key.cy) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = key;
    }
    for(i = 0; i < n; i++) {
        const WorldSprite *s = &list[i];
        blit_sprite(s->px, s->w, s->h, s->cx - cam_x - s->w / 2, s->cy - cam_y - s->h);
    }
}

/* Player sprite, bottom-center anchored at (cx, cy) same as every
   other world actor above -- pushed into the same WorldSprite list as
   the NPCs now (see main()'s draw dispatch) instead of always being
   blit last/on top, so the depth sort in ws_sort_and_draw() applies
   to the player too. dir matches the sprite arrays below: 0=down,
   1=up,2=left,3=right. Walk-cycle animation matches state.lua's
   G.panim/G.pframe: 4 frames per direction, stepping to the next one
   every 10 frames while moving (dt*6 per frame at our fixed ~60fps
   vblank rate takes 10 frames to cross 1.0, same as the reference),
   frozen on frame 0 while standing still. main() drives anim_frame
   the same way G.pframe is driven. */
static const u16 *const MAX_FRAMES[4][4] = {
    { max_down_1,  max_down_2,  max_down_3,  max_down_4 },
    { max_up_1,    max_up_2,    max_up_3,    max_up_4 },
    { max_left_1,  max_left_2,  max_left_3,  max_left_4 },
    { max_right_1, max_right_2, max_right_3, max_right_4 },
};

/* ----------------------------------------------------------------------
 * Interact dialogue: full multi-beat sequences from data.TALK.father /
 * fatherAfter / bed / shelf / shelfEmpty / crate / crateEmpty (also
 * cross-checked against the canonical src/game/data.ts, which is
 * where every TALK_* array's actual text below was pulled from --
 * verbatim except upper-cased, our font having no lowercase set).
 * Each sequence is shown one beat per A-press (matching sayn()'s
 * one-beat-per-advance in the reference), word-wrapped into the
 * dialogue box.
 *
 * Each beat also carries a speaker id (data.ts's own beat.speaker,
 * "none" mapped to SPK_NONE), ported here for the first time: the
 * reference shows a real character portrait next to the text whenever
 * speaker isn't "none" (drawWorld()'s `drawSprite('port-${speaker}', ...)`).
 * SPEAKER_PORTRAIT below is the id -> sprite lookup draw_dialogue_box
 * uses; SPK_NONE (soldiers' anonymous lines, plain narration beats)
 * draws no portrait, same as the reference. */
typedef struct {
    const char *text;
    unsigned char speaker;
} TalkBeat;

#define SPK_NONE      0
#define SPK_MAX       1
#define SPK_ANNE      2
#define SPK_MASON     3
#define SPK_WREN      4
#define SPK_MAE       5
#define SPK_IVO       6
#define SPK_NELL      7
#define SPK_PIKE      8
#define SPK_CALDER    9
#define SPK_BRAM      10
#define SPK_CATHLEEN  11
#define SPK_SHINIGAMI 12
#define SPK_OREN      13
#define SPK_TESSA     14
#define SPK_BIRCH     15
#define SPK_SABLE     16
#define SPK_CROSS     17
#define SPK_COMMANDER 18
#define SPK_CONSCRIPT 19
#define SPK_ENFORCER  20
#define SPK_SENTRY    21
#define SPK_FATHER    22
#define SPK_HEAVENFALL 23
#define SPK_COUNT     24

/* Each portrait keeps its source art's own aspect ratio (gen_sprites.py
   scales every one by the same factor on both axes to fill as much of
   PORTRAIT_BOX_W x PORTRAIT_BOX_H as possible -- "contain" scaling,
   no cropping or stretching), so unlike every other sprite table in
   this file they're not all the same size: this carries each one's
   own w/h alongside its pixels for draw_dialogue_box to center. */
typedef struct {
    const u16 *px;
    int w, h;
} Portrait;

static const Portrait SPEAKER_PORTRAIT[SPK_COUNT] = {
    { 0, 0, 0 }, /* SPK_NONE */
    { port_max,       PORT_MAX_W,       PORT_MAX_H },
    { port_anne,      PORT_ANNE_W,      PORT_ANNE_H },
    { port_mason,     PORT_MASON_W,     PORT_MASON_H },
    { port_wren,      PORT_WREN_W,      PORT_WREN_H },
    { port_mae,       PORT_MAE_W,       PORT_MAE_H },
    { port_ivo,       PORT_IVO_W,       PORT_IVO_H },
    { port_nell,      PORT_NELL_W,      PORT_NELL_H },
    { port_pike,      PORT_PIKE_W,      PORT_PIKE_H },
    { port_calder,    PORT_CALDER_W,    PORT_CALDER_H },
    { port_bram,      PORT_BRAM_W,      PORT_BRAM_H },
    { port_cathleen,  PORT_CATHLEEN_W,  PORT_CATHLEEN_H },
    { port_shinigami, PORT_SHINIGAMI_W, PORT_SHINIGAMI_H },
    { port_oren,      PORT_OREN_W,      PORT_OREN_H },
    { port_tessa,     PORT_TESSA_W,     PORT_TESSA_H },
    { port_birch,     PORT_BIRCH_W,     PORT_BIRCH_H },
    { port_sable,     PORT_SABLE_W,     PORT_SABLE_H },
    { port_cross,     PORT_CROSS_W,     PORT_CROSS_H },
    { port_commander, PORT_COMMANDER_W, PORT_COMMANDER_H },
    { port_conscript, PORT_CONSCRIPT_W, PORT_CONSCRIPT_H },
    { port_enforcer,  PORT_ENFORCER_W,  PORT_ENFORCER_H },
    { port_sentry,    PORT_SENTRY_W,    PORT_SENTRY_H },
    { port_father,    PORT_FATHER_W,    PORT_FATHER_H },
    { port_heavenfall,PORT_HEAVENFALL_W,PORT_HEAVENFALL_H },
};

#include "content_talk.inc"

/* The portrait now dominates the screen instead of sitting in a small
   column: nothing else on screen matters while someone's talking
   besides their portrait and what they're saying, so it gets the
   whole width and everything between the top-left HUD (draw_hud,
   0-22ish) and the text strip at the bottom. PORTRAIT_BOX_W/H (from
   sprites.h, gen_sprites.py's own generation box) exactly fill that
   remaining area -- 24 (below the HUD) + PORTRAIT_BOX_H (176) +
   DIALOGUE_TEXT_H (40) == SCREEN_H (240) -- so there's no dead gap
   and no overlap on either side. Each portrait already comes out of
   sprites.h sized to fit inside that box without cropping or
   stretching (gen_sprites.py's own contain-scaling, preserving each
   character's real aspect ratio); this just centers whatever size
   that is. */
#define PORTRAIT_BOX_X     4
#define PORTRAIT_BOX_Y     24
#define DIALOGUE_TEXT_H    40
#define DIALOGUE_TEXT_Y    (SCREEN_H - DIALOGUE_TEXT_H)
#define DIALOGUE_MAX_CHARS ((SCREEN_W - 16) / CHAR_CELL(DIALOGUE_SCALE))
#define DIALOGUE_LINE_H    9

/* No background panel -- outlined text (draw_glyph's own 1px black
   border) reads fine directly over the world/battle scene, so the
   dialogue box is really just a portrait plus wrapped text at a fixed
   screen position now, not an actual drawn box.

   Horizontal placement now tells the two sides apart at a glance:
   Max (the player) sits flush against the box's own left edge --
   already a safe 4px in from the screen edge, PORTRAIT_BOX_X's own
   buffer, so nothing crops -- and every NPC sits flush against the
   right edge instead of the old dead-center placement. Vertical
   centering is unchanged. */
static void draw_dialogue_box(const TalkBeat *beat) {
    if(beat->speaker != SPK_NONE) {
        const Portrait *p = &SPEAKER_PORTRAIT[beat->speaker];
        int px = (beat->speaker == SPK_MAX)
                     ? PORTRAIT_BOX_X
                     : PORTRAIT_BOX_X + PORTRAIT_BOX_W - p->w;
        blit_sprite(p->px, p->w, p->h, px, PORTRAIT_BOX_Y + (PORTRAIT_BOX_H - p->h) / 2);
    }
    draw_wrapped(beat->text, 8, DIALOGUE_TEXT_Y + 8,
                 0xFFFF, DIALOGUE_SCALE,
                 DIALOGUE_MAX_CHARS, DIALOGUE_LINE_H);
}

/* HUD toast (state.lua's G.hud/note()): a small one-line banner near
   the top of the screen, distinct from the dialogue box at the bottom
   so the two are never confused even though they never actually show
   at once (say() always clears hudT, and every note() call site is
   reached from plain world state, not mid-dialogue). No background
   panel, same as the dialogue box above -- just outlined text. */
static void draw_hud_toast(const char *text) {
    draw_text_s(text, 26, 10, 0xFFFF, DIALOGUE_SCALE);
}

/* Map-name banner: replaces the old per-door TALK_*_ENTER/LEAVE
   flavor lines (do_warp() sets this instead of seq_lines now) with a
   plain text fade-in/hold/fade-out showing where the player just
   arrived, independent of the dialogue system entirely -- it needs no
   A press and never blocks movement. The fade is a straight color
   lerp from black to the text's own color and back (there's no true
   alpha layer in this RGB565 framebuffer), driven by a countdown that
   ticks every frame regardless of what else is on screen. */
#define MAP_BANNER_IN    16
#define MAP_BANNER_HOLD  70
#define MAP_BANNER_OUT   16
#define MAP_BANNER_TOTAL (MAP_BANNER_IN + MAP_BANNER_HOLD + MAP_BANNER_OUT)

static void draw_map_banner(int map_id, int timer) {
    int elapsed = MAP_BANNER_TOTAL - timer;
    int level;
    u16 color;

    if(elapsed < MAP_BANNER_IN)
        level = (elapsed * 16) / MAP_BANNER_IN;
    else if(elapsed < MAP_BANNER_IN + MAP_BANNER_HOLD)
        level = 16;
    else
        level = ((MAP_BANNER_TOTAL - elapsed) * 16) / MAP_BANNER_OUT;
    if(level < 0) level = 0;
    if(level > 16) level = 16;

    color = rgb565((u8)((232 * level) / 16), (u8)((228 * level) / 16), (u8)((216 * level) / 16));
    draw_text_center_s(MAP_DISPLAY_NAME[map_id], SCREEN_W / 2, 14, color, 2);
}

/* Small HUD in the screen's top-left corner (fixed there regardless
   of camera position), showing what interacting has granted so far
   -- there's no inventory/party HUD overlay in the reference, but
   there's also no way to see this port's bag/party menus without
   opening them, so this stays as a quick-glance confirmation. */
static void draw_hud(int got_shelf, int looted_crate, int bag_bandage, int has_scroll) {
    int y = 2;

    if(got_shelf) {
        const char *label = "QUILLPUP LV";
        draw_text_s(label, 4, y, 0xFFFF, DIALOGUE_SCALE);
        draw_glyph(4 + text_width_s(label, DIALOGUE_SCALE), y,
                   font_09[3], 0xFFFF, DIALOGUE_SCALE);
        y += DIALOGUE_LINE_H;
    }
    if(looted_crate) {
        const char *label = "BANDAGE X";
        draw_text_s(label, 4, y, 0xFFFF, DIALOGUE_SCALE);
        draw_glyph(4 + text_width_s(label, DIALOGUE_SCALE), y,
                   font_09[bag_bandage % 10], 0xFFFF, DIALOGUE_SCALE);
        y += DIALOGUE_LINE_H;
    }
    if(has_scroll)
        draw_text_s("LEGENDARY REANIMATION", 4, y, 0xFFFF, DIALOGUE_SCALE);
}

/* ----------------------------------------------------------------------
 * Species/monster data and battle math, verbatim from data.lua's
 * SPECIES table, mintMonster(), grantXp(), and captureChance(). Move
 * names are upper-cased/depunctuated for our font; multi-word names
 * keep their space ("FIRE BOLT"). Cathleen alone carries a spells[]
 * list (spells_n > 0 for no other entry below) -- SPELL_FIREBOLT/
 * ICEBEAM/LIGHTNING/MANASURGE (0-3, defined by battle_cast_spell()
 * further down) matching data.ts's own spell id order for her. Every
 * other species leaves spells_n at 0 and never touches battle_cast_spell.
 * ---------------------------------------------------------------------- */
typedef struct {
    const char *name, *basic, *special;
    int maxHp, str, agl, spc, spp;
    int spells_n;      /* 0 for every species but Cathleen */
    int spells[4];     /* SPELL_* ids, spells_n of them valid */
} Species;

#define SP_QUILLPUP   0
#define SP_GLIMMOTH   1
#define SP_TORTCASK   2
#define SP_RAZORBAT   3
#define SP_MOSSBACK   4
#define SP_BRIARFOX   5
#define SP_FENWISP    6
#define SP_DUSKHORN   7
#define SP_NEEDLEROOT 8
#define SP_CATHLEEN   9
#define SP_CRYMARE    10
#define SP_EMBERLING  11
#define SP_FROSTAIL   12
#define SP_BOULDERAM  13
#define SP_STORMWING  14
#define SP_SABLECLAW  15
#define SP_THORNHIDE  16
#define SP_GLASSWISP  17
#define SP_ASHENMAW   18
#define SP_HEAVENFALL 19

#include "content_species.inc"

typedef struct {
    int species;
    int lv, xp;
    int maxHp, hp, str, agl, spc, spp, sppMax;
    int shiny; /* see mint_shiny() below */
} Monster;

/* xorshift32, seeded from the frame counter at title-screen dismissal
   (see main()) -- there's no RTC/libc rand() in this freestanding
   build, so this stands in for math.random()/irand() throughout the
   ported formulas below. Not cryptographic, just enough variance for
   a homebrew game. */
static u32 rng_state = 0x9e3779b9u;
static u32 rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static int irand(int a, int b) {
    return a + (int)(rng_next() % (u32)(b - a + 1));
}

static int jground(float x) {
    return (int)(x + 0.5f);
}

static int clampi(int v, int lo, int hi) {
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

/* No libc/math.h here, so a hand-rolled Newton-Raphson sqrt for the
   actor-movement code below (normalizing a chase/approach direction
   vector needs a real magnitude, not just the squared distance used
   everywhere else's proximity checks). 12 iterations is overkill for
   the small pixel distances involved but costs nothing on the SH4's
   single-precision FPU. */
static float f_sqrt(float x) {
    float guess;
    int i;
    if(x <= 0.0f) return 0.0f;
    guess = x > 1.0f ? x : 1.0f;
    for(i = 0; i < 12; i++)
        guess = 0.5f * (guess + x / guess);
    return guess;
}

static Monster mint_monster(int species, int lv) {
    const Species *s = &SPECIES[species];
    float g = 1.0f + (float)(lv - 3) * 0.12f;
    Monster m;
    m.species = species;
    m.lv = lv < 1 ? 1 : lv;
    m.xp = 0;
    m.maxHp = jground((float)s->maxHp * g);
    m.str = jground((float)s->str * g);
    m.agl = jground((float)s->agl * g);
    m.spc = jground((float)s->spc * g);
    m.spp = s->spp;
    m.sppMax = s->spp;
    m.hp = m.maxHp;
    m.shiny = 0;
    return m;
}

/* Shiny variant: a rare (1/64) wild-encounter-only recolor, minted at
   double the level try_encounter() would otherwise have picked, with
   an extra move (TOXIC BURST, see battle_pick_toxic()) no normal
   CryMon of the species has. Never rolled for a scripted trainer/NPC
   fight (Mason's Glimmoth, Calder's Razorbat, etc. mint_monster()
   directly) or for Cathleen -- only try_encounter()'s wild pool calls
   this. The "palette swap" itself lives in draw_battle_sprites(),
   which recolors the sprite at blit time rather than needing a second
   set of source art (none exists). */
static Monster mint_shiny(int species, int lv) {
    Monster m = mint_monster(species, lv * 2);
    m.shiny = 1;
    return m;
}
static int roll_shiny(void) {
    return irand(0, 63) == 0;
}

/* data.grantXp: +6+4*foeLv xp per win, level up (+3 maxHp, +1 each
   stat) while xp >= lv*10, capped at lv 12. Returns 1 if it leveled
   up at least once. */
static int grant_xp(Monster *m, int foe_lv) {
    int grew = 0;
    m->xp += 6 + foe_lv * 4;
    while(m->xp >= m->lv * 10 && m->lv < 12) {
        m->xp -= m->lv * 10;
        m->lv++;
        m->maxHp += 3;
        m->hp += 3;
        if(m->hp > m->maxHp) m->hp = m->maxHp;
        m->str++;
        m->agl++;
        m->spc++;
        grew = 1;
    }
    return grew;
}

/* fullHeal(), also used by the bed and by a party wipe's fade-and-
   teleport-home (see main()'s FADE_ACTION_BED/FADE_ACTION_LOSS). */
static void heal_party(Monster *party, int party_n) {
    int i;
    for(i = 0; i < party_n; i++) {
        party[i].hp = party[i].maxHp;
        party[i].spp = party[i].sppMax;
    }
}

/* data.captureChance: 5% per foe agility, +1% per % of foe HP
   missing, +25% if any foe stat has been lowered this fight. */
static int capture_chance(int agl, int hp, int max_hp, int vulnerable) {
    int missing = max_hp > 0 ? (max_hp - hp) * 100 / max_hp : 0;
    int chance = 5 * agl + missing;
    if(vulnerable) chance += 25;
    return clampi(chance, 0, 100);
}

/* ----------------------------------------------------------------------
 * Bag and party menus, ported from render.lua's drawBag()/drawParty().
 * Opened from the world with Y (bag) or START (party), closed with B;
 * items are actually used from the battle system's own item menu
 * (below), not from here -- these two screens stay read-only info
 * views outside battle, matching state.lua's BAG/PARTY mode update
 * (open/close only, no cursor/use logic there either). No
 * lead-switching menu here since this port has no multi-monster
 * roster to switch within (see Monster/party_mon). data.lua's
 * START_BAG gives the starting counts (salve 2, bandage 2, bitterroot
 * 1, dust 1, gem 0) and START_MARKS (16); both bag counts and marks
 * are now real, mutable state once the battle system below uses/
 * grants them. Item icons (render.lua's "item-<id>" sprites) aren't
 * ported -- text rows only, like the rest of this port's UI.
 * ---------------------------------------------------------------------- */
typedef struct {
    int salve, bandage, bitterroot, dust, gem;
    int sunbalm, warroot, smokebomb, greatcrystal; /* the 4 new items */
} Bag;

#define MENU_X 20
#define MENU_Y 20
#define MENU_W (SCREEN_W - 2 * MENU_X)
#define MENU_H (SCREEN_H - 2 * MENU_Y)
#define MENU_SCALE 1
#define MENU_ROW_H 16

/* No background panel -- outlined text reads fine directly over
   whatever's behind the menu (the world scene, since draw_bag_menu/
   draw_party_menu/draw_shop are all drawn as an overlay after it). */
static void draw_menu_frame(const char *title, const char *footer) {
    draw_text_s(title, MENU_X + 8, MENU_Y + 8, 0xFFFF, MENU_SCALE);
    draw_text_s(footer, MENU_X + 8, MENU_Y + MENU_H - 16,
                rgb565(180, 220, 170), MENU_SCALE);
}

/* ITEM_COUNT-indexed icon lookup (ITEM_SALVE..ITEM_GEM, defined
   further down with the rest of the item-menu kinds) -- shared by the
   bag, shop, and battle item-menu rows below, all of which previously
   showed items as bare text. icon may be null (the item-menu's "PASS"
   row has no matching icon). */
static const u16 *const ITEM_ICONS[9] = {
    icon_salve, icon_bandage, icon_bitterroot, icon_dust, icon_gem,
    icon_sunbalm, icon_warroot, icon_smokebomb, icon_greatcrystal
};

/* Effect text is stripped out of the row's own title now (matching
   the battle item menu below) and only drawn when the row is the one
   under the cursor, so the list reads as plain item names/counts
   until the player actually navigates onto one. */
static const char *const ITEM_EFFECT_DESC[9] = {
    "+22 HP", "+12 HP", "STR+4", "-3/-2/-2", "CATCH",
    "+40 HP", "AGL+4", "FLEE", "CATCH+"
};

static void draw_bag_row(const u16 *icon, const char *label, int count,
                          int idx, int cur, int y) {
    char buf[48];
    int n;
    u16 color = (idx == cur) ? rgb565(232, 228, 216) : rgb565(138, 134, 120);

    draw_text_s(idx == cur ? ">" : " ", MENU_X + 8, y, color, MENU_SCALE);
    if(icon)
        blit_sprite(icon, ITEM_ICON_W, ITEM_ICON_H, MENU_X + 16, y - 1);

    n = s_cat(buf, 0, label);
    n = s_cat(buf, n, " X");
    n = s_cat_uint(buf, n, count);
    if(idx == cur) {
        n = s_cat(buf, n, "  ");
        n = s_cat(buf, n, ITEM_EFFECT_DESC[idx]);
    }
    buf[n] = 0;
    draw_text_s(buf, MENU_X + 16 + ITEM_ICON_W + 4, y, color, MENU_SCALE);
}

static void draw_bag_menu(const Bag *bag, int marks, int cur) {
    int y = MENU_Y + 24;
    char marks_buf[16];
    int n;

    draw_menu_frame("BAG", "UP/DOWN A USE  B CLOSE");

    n = s_cat(marks_buf, 0, "MARKS ");
    n = s_cat_uint(marks_buf, n, marks);
    marks_buf[n] = 0;
    draw_text_s(marks_buf, MENU_X + MENU_W - 8 - text_width_s(marks_buf, MENU_SCALE),
                MENU_Y + 8, rgb565(143, 74, 64), MENU_SCALE);

    draw_bag_row(icon_salve, "MOSS SALVE", bag->salve, 0, cur, y);            y += MENU_ROW_H;
    draw_bag_row(icon_bandage, "LINEN WRAP", bag->bandage, 1, cur, y);        y += MENU_ROW_H;
    draw_bag_row(icon_bitterroot, "BITTERROOT", bag->bitterroot, 2, cur, y);  y += MENU_ROW_H;
    draw_bag_row(icon_dust, "ASH DUST", bag->dust, 3, cur, y);                y += MENU_ROW_H;
    draw_bag_row(icon_gem, "CAPTURE CRYSTAL", bag->gem, 4, cur, y);          y += MENU_ROW_H;
    draw_bag_row(icon_sunbalm, "SUNBALM", bag->sunbalm, 5, cur, y);          y += MENU_ROW_H;
    draw_bag_row(icon_warroot, "WARROOT", bag->warroot, 6, cur, y);          y += MENU_ROW_H;
    draw_bag_row(icon_smokebomb, "SMOKE BOMB", bag->smokebomb, 7, cur, y);   y += MENU_ROW_H;
    draw_bag_row(icon_greatcrystal, "GREATER CRYSTAL", bag->greatcrystal, 8, cur, y);
}

/* drawParty(): lists every party member (up to data.PARTY_MAX -- see
   Monster party[6] in main()), with a ">" prefix and brighter color on
   the current lead, plus a cursor ("*") on party_cur -- cycleParty(to)
   ported: pressing A on a living, non-lead row makes it the new lead
   (main()'s menu_mode==2 input handling), matching the reference's
   own Digit1-6 hotkeys adapted to a dpad+cursor since there's no
   number row on a Dreamcast pad. */
/* Y opens this from the list for whichever row party_cur is on --
   attacks (basic/special, or the full spell list for a caster like
   Cathleen), stats, and where the CryMon sits in the party order
   (main()'s menu_mode==2 input handling keeps party_cur live under
   up/down while this is open, so browsing the whole party doesn't
   need to back out to the list each time). */
static void draw_party_detail(const Monster *party, int party_n, int idx) {
    const Monster *m = &party[idx];
    const Species *s = &SPECIES[m->species];
    char buf[48];
    int n, y = MENU_Y + 24;

    draw_menu_frame("CRYMON", "UP/DOWN SWITCH  B BACK");

    n = s_cat(buf, 0, m->shiny ? "SHINY " : "");
    n = s_cat(buf, n, s->name);
    buf[n] = 0;
    draw_text_s(buf, MENU_X + 8, y, 0xFFFF, MENU_SCALE); y += MENU_ROW_H;

    n = s_cat(buf, 0, "LV");
    n = s_cat_uint(buf, n, m->lv);
    n = s_cat(buf, n, "  HP ");
    n = s_cat_uint(buf, n, m->hp);
    n = s_cat(buf, n, "/");
    n = s_cat_uint(buf, n, m->maxHp);
    buf[n] = 0;
    draw_text_s(buf, MENU_X + 8, y, rgb565(232, 228, 216), MENU_SCALE); y += MENU_ROW_H;

    n = s_cat(buf, 0, "STR ");
    n = s_cat_uint(buf, n, m->str);
    n = s_cat(buf, n, "  AGL ");
    n = s_cat_uint(buf, n, m->agl);
    n = s_cat(buf, n, "  SPC ");
    n = s_cat_uint(buf, n, m->spc);
    buf[n] = 0;
    draw_text_s(buf, MENU_X + 8, y, rgb565(232, 228, 216), MENU_SCALE); y += MENU_ROW_H * 2;

    draw_text_s("ATTACKS", MENU_X + 8, y, rgb565(180, 220, 170), MENU_SCALE); y += MENU_ROW_H;
    if(s->spells_n > 0) {
        static const char *const SPELL_MENU_NAME[4] = {
            "FIRE BOLT", "ICE BEAM", "LIGHTNING STRIKE", "MANA SURGE"
        };
        int i;
        for(i = 0; i < s->spells_n; i++) {
            draw_text_s(SPELL_MENU_NAME[s->spells[i]], MENU_X + 8, y,
                        rgb565(232, 228, 216), MENU_SCALE);
            y += MENU_ROW_H;
        }
    }
    else {
        draw_text_s(s->basic, MENU_X + 8, y, rgb565(232, 228, 216), MENU_SCALE); y += MENU_ROW_H;
        draw_text_s(s->special, MENU_X + 8, y, rgb565(232, 228, 216), MENU_SCALE); y += MENU_ROW_H;
        if(m->shiny) {
            draw_text_s("TOXIC BURST", MENU_X + 8, y, rgb565(232, 228, 216), MENU_SCALE);
            y += MENU_ROW_H;
        }
    }
    y += MENU_ROW_H;

    n = s_cat(buf, 0, "ORDER ");
    n = s_cat_uint(buf, n, idx + 1);
    n = s_cat(buf, n, " OF ");
    n = s_cat_uint(buf, n, party_n);
    buf[n] = 0;
    draw_text_s(buf, MENU_X + 8, y, rgb565(180, 220, 170), MENU_SCALE);

}

/* heal_item selects who a salve/wrap/sunbalm picked from the bag menu
   goes to (0 salve, 1 wrap, 5 sunbalm, -1 not healing) -- same list as
   the plain party menu, just with a different title/footer and A
   applying the item to party_cur instead of setting the lead (see
   main()'s menu_mode==2 input handling). */
static void draw_party_menu(const Monster *party, int party_n, int lead, int party_cur,
                             int party_detail, int heal_item) {
    int y = MENU_Y + 24;

    if(party_detail && party_n > 0) {
        draw_party_detail(party, party_n, party_cur);
        return;
    }

    if(heal_item >= 0) {
        const char *title = heal_item == 0 ? "USE MOSS SALVE ON WHO?"
                           : heal_item == 1 ? "USE LINEN WRAP ON WHO?"
                                             : "USE SUNBALM ON WHO?";
        draw_menu_frame(title, "A HEAL  B CANCEL");
    }
    else
        draw_menu_frame("CRYMON", "B CLOSE");

    if(party_n > 0) {
        int i;
        for(i = 0; i < party_n; i++) {
            char buf[40];
            u16 color = (i == lead) ? rgb565(232, 228, 216) : rgb565(138, 134, 120);
            int n = s_cat(buf, 0, (i == party_cur) ? "*" : " ");
            n = s_cat(buf, n, (i == lead) ? "> " : "  ");
            if(party[i].shiny) n = s_cat(buf, n, "SHINY ");
            n = s_cat(buf, n, SPECIES[party[i].species].name);
            n = s_cat(buf, n, " LV");
            n = s_cat_uint(buf, n, party[i].lv);
            n = s_cat(buf, n, " HP ");
            n = s_cat_uint(buf, n, party[i].hp);
            n = s_cat(buf, n, "/");
            n = s_cat_uint(buf, n, party[i].maxHp);
            buf[n] = 0;
            draw_text_s(buf, MENU_X + 8, y, color, MENU_SCALE);
            y += MENU_ROW_H;
        }
        if(heal_item < 0)
            draw_text_s("A LEAD  Y VIEW", MENU_X + 8, MENU_Y + MENU_H - 32,
                        rgb565(138, 134, 120), MENU_SCALE);
    }
    else {
        draw_text_s("NO CRYMON YET", MENU_X + 8, y, rgb565(138, 134, 120), MENU_SCALE);
    }
}

/* The father-vs-Heavenfall resurrection choice, armed by
   POST_OPEN_CHOICE once TALK_SHINIGAMI_WIN closes (main()'s
   choice_mode) -- Anne's reveal (TALK_ANNE_RETURN) already happened
   earlier, right after Cathleen, so by the time the scroll actually
   exists this is just the payoff. Same frame/menu-row visual language
   as every other full-screen menu here, just with only 2 rows and no
   way to back out -- this is the one decision in the whole game that
   isn't optional, matching "present the player with a choice" rather
   than a plain dialogue beat with no real branch. */
static void draw_choice_row(const char *label, int idx, int cur, int y) {
    u16 color = (idx == cur) ? rgb565(232, 228, 216) : rgb565(138, 134, 120);
    draw_text_s(idx == cur ? ">" : " ", MENU_X + 8, y, color, MENU_SCALE);
    draw_text_s(label, MENU_X + 16, y, color, MENU_SCALE);
}

static void draw_choice(int cur) {
    int y = MENU_Y + 24;

    draw_menu_frame("THE SCROLL", "A CHOOSE");
    draw_wrapped("THE SCROLL CAN WAKE ONE OF THE DEAD.",
                 MENU_X + 8, y, rgb565(197, 206, 198), MENU_SCALE,
                 (MENU_W - 16) / CHAR_CELL(MENU_SCALE), 9);
    y += 28;

    draw_choice_row("RESURRECT FATHER", 0, cur, y); y += MENU_ROW_H;
    draw_choice_row("RESURRECT HEAVENFALL", 1, cur, y); y += MENU_ROW_H * 2;

    draw_wrapped(cur == 0 ? "HE COMES BACK AS HE WAS. HUMAN, AND HERS."
                          : "AN ANCIENT CRYMON WAKES. VAST AND UNKNOWN.",
                 MENU_X + 8, y, rgb565(138, 134, 120), MENU_SCALE,
                 (MENU_W - 16) / CHAR_CELL(MENU_SCALE), 9);
}

/* ----------------------------------------------------------------------
 * Battle system, ported from state.lua's updateBattle()/pickAtk()/
 * pickGuard()/pickItem()/applyHit()/finishWin() and captureChanceNow().
 * Wild encounters (tryEncounter()/startBattle()) and trainer battles
 * both use this same struct/update loop, matching state.lua (which
 * shares updateBattle() between them too): trainer_kind names who
 * G.bTrainer would be, bench holds Shinigami's 2 backup CryMare
 * (data.lua's only nbench > 0 fight). Cathleen's real castSpell() kit
 * (Fire Bolt/Ice Beam/Lightning Strike/Mana Surge) is ported --
 * battle_cast_spell()/battle_pick_spell(), further down -- for both
 * her wild GROVE fight and as a captured player lead.
 *
 * The post-win "grew to lv N"/"stands over the grass" line is a real
 * timed-fade HUD toast (main()'s hud_flash/hud_t, set at the tail of
 * this switch below), matching state.lua's note()/G.hudT exactly
 * instead of an extra clickable battle message.
 * ---------------------------------------------------------------------- */
typedef struct {
    Monster pl, foe;
    int wild;
    int phase;      /* 0 msg, 1 item menu, 2 attack menu, 3 guard menu,
                        4 special-move timing minigame */
    char msg[3][40];
    int msg_n, msg_i;
    int after;
    int cur;        /* menu cursor for phases 1-3 */
    int mods_self_str, mods_self_agl, mods_self_spc;
    int mods_foe_str, mods_foe_agl, mods_foe_spc;
    int pl_poisoned, foe_poisoned; /* TOXIC BURST, shiny-exclusive move */
    float mg;       /* special-move timing needle, 0-100 */
    int mg_dir;
    int dmg;
    char label[28];
    int grew;       /* set by finish_win() below, read by the WIN_NOTE beat */
    int trainer_kind;      /* TRAINER_* below */
    int soldier_id;        /* valid when trainer_kind == TRAINER_SOLDIER */
    Monster bench[2];
    int bench_n;
} Battle;

#define TRAINER_WILD     0
#define TRAINER_SOLDIER  1
#define TRAINER_MASON    2
#define TRAINER_SHINIGAMI 3
#define TRAINER_CALDER   4
#define TRAINER_MASON2   5
#define TRAINER_WSOLDIER_CLIFFS 6
#define TRAINER_WSOLDIER_CAMP1  7
#define TRAINER_WSOLDIER_CAMP2  8
#define TRAINER_WSOLDIER_GROVE  9

#define BAFTER_ITEM      1
#define BAFTER_ATK       2
#define BAFTER_GUARD     3
#define BAFTER_WIN       5
#define BAFTER_WORLD     6
#define BAFTER_LOSS      7
#define BAFTER_WIN_NOTE  9

static int battle_foe_debuffed(const Battle *b) {
    return b->mods_foe_str < 0 || b->mods_foe_agl < 0 || b->mods_foe_spc < 0;
}
/* selfDebuffed(): mirror of the above, checked from the foe's side of
   castSpell's Mana Surge branch (whether the *player* is debuffed
   decides the foe's 2x multiplier and their 55% chance to prioritize
   casting it -- see battle_pick_guard's Cathleen branch below). */
static int battle_self_debuffed(const Battle *b) {
    return b->mods_self_str < 0 || b->mods_self_agl < 0 || b->mods_self_spc < 0;
}
static int battle_capture_chance(const Battle *b) {
    return capture_chance(b->foe.agl, b->foe.hp, b->foe.maxHp, battle_foe_debuffed(b));
}

/* Cathleen's spell kit (castSpell()): the only species in data.ts with
   a `spells` list, so this only ever fires for her, on either side of
   the fight (as the player's own captured lead, or as the wild foe).
   from_player picks which side's mods get debuffed by the elemental
   spells and, for Mana Surge, which side's debuff state doubles the
   damage. Returns 0 (and leaves *out_dmg/out_label untouched) only for
   a spent-PP Mana Surge cast, matching castSpell()'s early return
   there. */
#define SPELL_FIREBOLT  0
#define SPELL_ICEBEAM   1
#define SPELL_LIGHTNING 2
#define SPELL_MANASURGE 3
static int battle_cast_spell(Battle *b, int spell_id, int from_player, int *out_dmg, char *out_label) {
    Monster *caster = from_player ? &b->pl : &b->foe;
    int dmg = 0, n = 0;
    char label[40];

    if(spell_id == SPELL_FIREBOLT) {
        if(from_player) b->mods_foe_str -= 4; else b->mods_self_str -= 4;
        dmg = jground(5.0f + (float)caster->spc * 0.35f + (float)irand(0, 2));
        if(dmg < 1) dmg = 1;
        n = s_cat(label, 0, "FIRE BOLT  STR-4");
    }
    else if(spell_id == SPELL_ICEBEAM) {
        if(from_player) b->mods_foe_agl -= 4; else b->mods_self_agl -= 4;
        dmg = jground(5.0f + (float)caster->spc * 0.35f + (float)irand(0, 2));
        if(dmg < 1) dmg = 1;
        n = s_cat(label, 0, "ICE BEAM  AGI-4");
    }
    else if(spell_id == SPELL_LIGHTNING) {
        if(from_player) b->mods_foe_spc -= 4; else b->mods_self_spc -= 4;
        dmg = jground(5.0f + (float)caster->spc * 0.35f + (float)irand(0, 2));
        if(dmg < 1) dmg = 1;
        n = s_cat(label, 0, "LIGHTNING STRIKE  SPC-4");
    }
    else {
        int debuffed, atk, def;
        float mul;
        if(caster->spp <= 0) return 0;
        caster->spp--;
        debuffed = from_player ? battle_foe_debuffed(b) : battle_self_debuffed(b);
        mul = debuffed ? 2.0f : 1.0f;
        atk = caster->spc;
        def = from_player ? (b->foe.spc + b->mods_foe_spc) : (b->pl.spc + b->mods_self_spc);
        dmg = jground((11.0f + (float)atk * 0.75f - (float)def * 0.18f) * mul + (float)irand(0, 2));
        if(dmg < 1) dmg = 1;
        n = s_cat(label, 0, debuffed ? "MANA SURGE  2X" : "MANA SURGE");
    }
    label[n] = 0;
    { int i; for(i = 0; label[i]; i++) out_label[i] = label[i]; out_label[i] = 0; }
    *out_dmg = dmg;
    return 1;
}

static void battle_apply_hit(Battle *b) {
    int n, poison_tick = 0;

    /* TOXIC BURST's ongoing chip damage: ticks whatever poison state
       the foe was ALREADY carrying into this turn, before this turn's
       own attack lands -- battle_pick_toxic() only marks foe_poisoned
       afterward, so a freshly-inflicted poison doesn't also tick the
       same turn it's applied. */
    if(b->foe_poisoned) {
        poison_tick = b->foe.maxHp / 16;
        if(poison_tick < 1) poison_tick = 1;
        b->foe.hp -= poison_tick;
        if(b->foe.hp < 0) b->foe.hp = 0;
    }

    b->foe.hp -= b->dmg;
    if(b->foe.hp < 0) b->foe.hp = 0;

    n = s_cat(b->msg[0], 0, b->label);
    n = s_cat(b->msg[0], n, " ");
    n = s_cat_uint(b->msg[0], n, b->dmg);
    n = s_cat(b->msg[0], n, " DMG");
    if(poison_tick > 0) {
        n = s_cat(b->msg[0], n, " PSN-");
        n = s_cat_uint(b->msg[0], n, poison_tick);
    }
    b->msg[0][n] = 0;

    if(b->foe.hp <= 0) {
        if(b->bench_n > 0) {
            /* Shinigami's fight, and now Mason's rematch too -- the
               two nbench > 0 fights. Grants XP for the fallen bench
               member (unlike the final win, which grants XP once the
               whole fight ends), swaps the next bench monster in, and
               clears the foe-side stat mods, matching applyHit's
               bench branch. */
            char fallen[24];
            int fn = s_cat(fallen, 0, SPECIES[b->foe.species].name);
            fallen[fn] = 0;

            grant_xp(&b->pl, b->foe.lv);
            b->foe = b->bench[0];
            if(b->bench_n == 2) b->bench[0] = b->bench[1];
            b->bench_n--;
            b->mods_foe_str = b->mods_foe_agl = b->mods_foe_spc = 0;
            b->foe_poisoned = 0; /* fresh bench monster, not the fallen one */

            n = s_cat(b->msg[1], 0, fallen);
            n = s_cat(b->msg[1], n, " FALLS");
            b->msg[1][n] = 0;

            n = s_cat(b->msg[2], 0,
                      b->trainer_kind == TRAINER_MASON2 ? "MASON SENDS " : "SHINIGAMI SENDS ");
            n = s_cat(b->msg[2], n, SPECIES[b->foe.species].name);
            b->msg[2][n] = 0;

            b->msg_n = 3; b->msg_i = 0; b->phase = 0; b->after = BAFTER_ITEM;
            return;
        }
        n = s_cat(b->msg[1], 0, SPECIES[b->foe.species].name);
        n = s_cat(b->msg[1], n, " FALLS");
        b->msg[1][n] = 0;
        b->msg_n = 2; b->msg_i = 0; b->phase = 0; b->after = BAFTER_WIN;
        return;
    }

    n = s_cat(b->msg[1], 0, "FOE ANSWERS CHOOSE A GUARD");
    b->msg[1][n] = 0;
    b->msg_n = 2; b->msg_i = 0; b->phase = 0; b->after = BAFTER_GUARD;
}

/* pickAtk's basic-move branch. Only reached for a non-spellcaster
   lead -- see battle_pick_spell() further down for Cathleen's own
   branch, dispatched separately in main()'s battle-phase-2 handling. */
static void battle_pick_basic(Battle *b) {
    int atk = b->pl.str + b->mods_self_str;
    int def = b->foe.str + b->mods_foe_str;
    int n = 0;

    b->dmg = jground(6.0f + (float)atk * 0.62f - (float)def * 0.16f + (float)irand(0, 3));
    if(b->dmg < 1) b->dmg = 1;
    n = s_cat(b->label, n, SPECIES[b->pl.species].basic);
    b->label[n] = 0;
    battle_apply_hit(b);
}

/* TOXIC BURST: the shiny-exclusive move (see mint_shiny()). Weaker
   than the plain basic move on its own, but poisons the foe for
   ongoing chip damage every subsequent turn -- see the poison_tick
   handling in battle_apply_hit()/battle_pick_guard(). Only offered to
   a shiny player lead (draw_battle_atk_menu/main()'s phase-2 dispatch
   add the extra row); no PP cost, always available, matching the
   "rare but not fussy" spirit of a shiny encounter. */
static void battle_pick_toxic(Battle *b) {
    int atk = b->pl.str + b->mods_self_str;
    int def = b->foe.str + b->mods_foe_str;
    int n = 0;

    b->dmg = jground(4.0f + (float)atk * 0.5f - (float)def * 0.16f + (float)irand(0, 2));
    if(b->dmg < 1) b->dmg = 1;
    n = s_cat(b->label, n, "TOXIC BURST");
    b->label[n] = 0;
    battle_apply_hit(b);
    b->foe_poisoned = 1;
}

/* pickAtk's special-move branch: engine.ts's timing minigame (mg
   bounces 0-100; landing 46-54 is "perfect" 2x, 38-62 "connected"
   1.45x, else "fizzled" 0.7x). Called once on A-press during phase 4
   (see main()'s battle update). */
static void battle_pick_special(Battle *b) {
    float mul;
    const char *tag;
    int def = b->foe.spc + b->mods_foe_spc;
    int n = 0;

    if(b->mg >= 46.0f && b->mg <= 54.0f)      { mul = 2.0f;  tag = "PERFECT"; }
    else if(b->mg >= 38.0f && b->mg <= 62.0f) { mul = 1.45f; tag = "CONNECTED"; }
    else                                      { mul = 0.7f;  tag = "FIZZLED"; }

    /* atk uses modsSelfStr, not modsSelfSpc -- verbatim from
       state.lua's pickAtk: "local atk = G.bPl.spc + G.modsSelfStr * 0.2". */
    b->dmg = jground((11.0f + ((float)b->pl.spc + (float)b->mods_self_str * 0.2f) * 0.75f
                       - (float)def * 0.18f) * mul + (float)irand(0, 2));
    if(b->dmg < 1) b->dmg = 1;

    n = s_cat(b->label, n, SPECIES[b->pl.species].special);
    n = s_cat(b->label, n, " ");
    n = s_cat(b->label, n, tag);
    b->label[n] = 0;
    battle_apply_hit(b);
}

/* pickAttack()'s spell branch: for a Cathleen lead, every attack-menu
   row is a direct spell cast (no minigame, no separate guard row --
   her attackMenu() is just her 4 spell names, matching data.ts). Does
   nothing if the row was Mana Surge with no PP left, same as
   castSpell()'s early return (the message/phase change that produces
   is handled by the caller, matching pickAttack's own silent-return
   there). */
static int battle_pick_spell(Battle *b, int spell_id) {
    int dmg;
    char label[40];
    if(!battle_cast_spell(b, spell_id, 1, &dmg, label))
        return 0;
    b->dmg = dmg;
    { int i; for(i = 0; label[i]; i++) b->label[i] = label[i]; b->label[i] = 0; }
    battle_apply_hit(b);
    return 1;
}

/* pickGuard(): the foe picks its own move (28% chance of its special
   if it has spp left, otherwise basic), the player's chosen guard is
   checked against a stat-difference success chance, and damage scales
   per guard kind on success. kind: 0 dodge (AGI), 1 block (STR), 2
   barrier (SPC). Needs the live party array to resolve a faint the
   same way state.lua does inline: swap in the next living member if
   one exists (message becomes "<line>" + "<name> JUMPS IN", battle
   continues at the item menu) or end the battle if none do ("<line>"
   + "<name> CANNOT STAND", BAFTER_LOSS). */
static void battle_pick_guard(Battle *b, int kind, Monster *party, int party_n, int *lead) {
    const Species *foe_sp = &SPECIES[b->foe.species];
    int use_special = b->foe.spp > 0 && irand(0, 99) < 28;
    char move_name_buf[40];
    const char *move_name;
    float base;
    int atk_stat, def_stat, chance, success, dmg;
    char line[56];
    int n = 0;
    int poison_tick = 0;
    int inflicts_poison = 0;

    /* TOXIC BURST's chip damage on the player's side, same ordering
       as battle_apply_hit()'s foe-side tick: whatever poison state
       came INTO this turn ticks first, before this turn's own guard
       result is resolved. */
    if(b->pl_poisoned) {
        poison_tick = b->pl.maxHp / 16;
        if(poison_tick < 1) poison_tick = 1;
        b->pl.hp -= poison_tick;
        if(b->pl.hp < 0) b->pl.hp = 0;
    }

    /* A shiny foe has a chance to reach for its own TOXIC BURST
       instead of the usual basic/special ladder, same shape as the
       Cathleen spell branch below (goto guard_chance) -- skipped
       once the player's already poisoned, same one-application-at-a-
       time rule battle_pick_toxic() follows for the player's side. */
    if(b->foe.shiny && !b->pl_poisoned && irand(0, 99) < 30) {
        atk_stat = b->foe.str + b->mods_foe_str;
        base = 4.0f + (float)atk_stat * 0.5f - (float)(b->pl.str + b->mods_self_str) * 0.16f;
        move_name = "TOXIC BURST";
        inflicts_poison = 1;
        goto guard_chance;
    }

    /* resolve_guard()'s spell branch: Cathleen never uses the plain
       basic/special ladder below, she casts one of her 4 spells
       instead (weighted toward Mana Surge when the player is already
       debuffed, same 55% roll as the reference). atkStat/base still
       come from this branch's `base` alone -- the reference's own
       atkStat calculation for the guard-chance formula, further down,
       is untouched by this branch and keeps using the basic-move
       formula even for a spellcaster, a quirk of resolve_guard()
       ported here verbatim rather than "fixed". */
    if(foe_sp->spells_n > 0) {
        static const char *const SPELL_PLAIN_NAME[4] = {
            "FIRE BOLT", "ICE BEAM", "LIGHTNING STRIKE", "MANA SURGE"
        };
        int idx = irand(0, foe_sp->spells_n > 3 ? 2 : foe_sp->spells_n - 1);
        int spell_id = foe_sp->spells[idx];
        if(battle_self_debuffed(b) && b->foe.spp > 0 && irand(0, 99) < 55)
            spell_id = SPELL_MANASURGE;
        if(battle_cast_spell(b, spell_id, 0, &dmg, move_name_buf)) {
            move_name = move_name_buf;
        }
        else {
            /* castSpell()'s spent-PP Mana Surge fallback: "?? { dmg: 1,
               label: spell.name }" -- only reachable here since the
               plain elemental spells never fail this check. */
            dmg = 1;
            move_name = SPELL_PLAIN_NAME[spell_id];
        }
        base = (float)dmg;
        atk_stat = b->foe.str + b->mods_foe_str;
        goto guard_chance;
    }

    if(use_special) b->foe.spp--;
    move_name = use_special ? foe_sp->special : foe_sp->basic;

    if(use_special) {
        atk_stat = b->foe.spc + b->mods_foe_spc;
        base = 10.0f + (float)(b->foe.spc + b->mods_foe_spc) * 0.7f
                     - (float)(b->pl.spc + b->mods_self_spc) * 0.12f;
    }
    else {
        atk_stat = b->foe.str + b->mods_foe_str;
        base = 6.0f + (float)(b->foe.str + b->mods_foe_str) * 0.6f
                    - (float)(b->pl.str + b->mods_self_str) * 0.15f;
    }

guard_chance:
    if(kind == 0)      def_stat = b->pl.agl + b->mods_self_agl;
    else if(kind == 1) def_stat = b->pl.str + b->mods_self_str;
    else               def_stat = b->pl.spc + b->mods_self_spc;

    chance = clampi(50 + (def_stat - atk_stat) * 5 + irand(-10, 10), 12, 88);
    success = irand(1, 100) <= chance;
    dmg = jground(base + (float)irand(0, 3));
    if(dmg < 1) dmg = 1;

    if(kind == 0) {
        if(success) {
            dmg = 0;
            n = s_cat(line, 0, SPECIES[b->pl.species].name);
            n = s_cat(line, n, " SLIPS ASIDE");
        }
        else {
            n = s_cat(line, 0, "THE DODGE FAILS ");
            n = s_cat_uint(line, n, dmg);
            n = s_cat(line, n, " DMG");
        }
    }
    else if(kind == 1) {
        if(success) {
            dmg = jground((float)dmg * 0.5f);
            if(dmg < 1) dmg = 1;
            n = s_cat(line, 0, "BLOCKED ");
            n = s_cat_uint(line, n, dmg);
            n = s_cat(line, n, " DMG LEAKS THROUGH");
        }
        else {
            n = s_cat(line, 0, "THE BLOCK BREAKS ");
            n = s_cat_uint(line, n, dmg);
            n = s_cat(line, n, " DMG");
        }
    }
    else {
        if(success) {
            dmg = jground((float)dmg * 0.4f);
            if(dmg < 1) dmg = 1;
            n = s_cat(line, 0, "A THIN BARRIER HOLDS ");
            n = s_cat_uint(line, n, dmg);
            n = s_cat(line, n, " DMG");
        }
        else {
            n = s_cat(line, 0, "THE BARRIER SHIVERS APART ");
            n = s_cat_uint(line, n, dmg);
            n = s_cat(line, n, " DMG");
        }
    }
    if(poison_tick > 0) {
        n = s_cat(line, n, " PSN-");
        n = s_cat_uint(line, n, poison_tick);
    }
    else if(inflicts_poison) {
        n = s_cat(line, n, " PSN");
    }
    line[n] = 0;

    b->pl.hp -= dmg;
    if(b->pl.hp < 0) b->pl.hp = 0;
    if(inflicts_poison) b->pl_poisoned = 1;
    party[*lead] = b->pl;

    if(b->pl.hp <= 0) {
        int i, nxt = -1;
        for(i = 0; i < party_n; i++)
            if(i != *lead && party[i].hp > 0) { nxt = i; break; }

        n = s_cat(b->msg[0], 0, line);
        b->msg[0][n] = 0;

        if(nxt >= 0) {
            *lead = nxt;
            b->pl = party[nxt];
            b->pl_poisoned = 0; /* fresh monster, not the fallen one */
            n = s_cat(b->msg[1], 0, SPECIES[b->pl.species].name);
            n = s_cat(b->msg[1], n, " JUMPS IN");
            b->msg[1][n] = 0;
            b->msg_n = 2; b->msg_i = 0; b->phase = 0; b->after = BAFTER_ITEM;
        }
        else {
            n = s_cat(b->msg[1], 0, SPECIES[b->pl.species].name);
            n = s_cat(b->msg[1], n, " CANNOT STAND");
            b->msg[1][n] = 0;
            b->msg_n = 2; b->msg_i = 0; b->phase = 0; b->after = BAFTER_LOSS;
        }
        return;
    }

    n = s_cat(b->msg[0], 0, SPECIES[b->foe.species].name);
    n = s_cat(b->msg[0], n, " USES ");
    n = s_cat(b->msg[0], n, move_name);
    b->msg[0][n] = 0;
    n = s_cat(b->msg[1], 0, line);
    b->msg[1][n] = 0;
    b->msg_n = 2; b->msg_i = 0; b->phase = 0; b->after = BAFTER_ITEM;
}

/* Item menu kinds, matching fillItemMenu's row order (state.lua's
   "switch" row is skipped -- see the item-menu drawing/input code in
   main() for why). */
#define ITEM_PASS       0
#define ITEM_SALVE      1
#define ITEM_BANDAGE    2
#define ITEM_BITTERROOT 3
#define ITEM_DUST       4
#define ITEM_GEM        5
#define ITEM_SUNBALM      6
#define ITEM_WARROOT      7
#define ITEM_SMOKEBOMB    8
#define ITEM_GREATCRYSTAL 9

/* pickItem(): items (heal/buff/debuff) are "free" -- they route back
   to the attack menu (BAFTER_ATK), never to the guard phase, matching
   state.lua exactly (only an actual attack or Wait lets the foe act).
   A successful capture ends the battle outright (BAFTER_WORLD); every
   other outcome, including a failed capture, also returns to the
   attack menu. party/party_n are only touched by a successful
   capture. */
static void battle_pick_item(Battle *b, Bag *bag, int kind,
                              Monster *party, int *party_n, int lead) {
    int n = 0;

    if(kind == ITEM_PASS) {
        b->phase = 2;
        b->cur = 0;
        return;
    }

    if(kind == ITEM_SALVE && bag->salve > 0) {
        int heal = b->pl.maxHp - b->pl.hp;
        if(heal > 22) heal = 22;
        bag->salve--;
        b->pl.hp += heal;
        n = s_cat(b->msg[0], 0, "MOSS SALVE ");
        n = s_cat_uint(b->msg[0], n, heal);
        n = s_cat(b->msg[0], n, " HP");
    }
    else if(kind == ITEM_BANDAGE && bag->bandage > 0) {
        int heal = b->pl.maxHp - b->pl.hp;
        if(heal > 12) heal = 12;
        bag->bandage--;
        b->pl.hp += heal;
        n = s_cat(b->msg[0], 0, "LINEN WRAP ");
        n = s_cat_uint(b->msg[0], n, heal);
        n = s_cat(b->msg[0], n, " HP");
    }
    else if(kind == ITEM_BITTERROOT && bag->bitterroot > 0) {
        bag->bitterroot--;
        b->mods_self_str += 4;
        n = s_cat(b->msg[0], 0, "BITTERROOT STR+4 THIS FIGHT");
    }
    else if(kind == ITEM_DUST && bag->dust > 0) {
        bag->dust--;
        b->mods_foe_str -= 3;
        b->mods_foe_agl -= 2;
        b->mods_foe_spc -= 2;
        n = s_cat(b->msg[0], 0, "ASH DUST FOE STR-3 AGI-2 SPC-2");
    }
    else if(kind == ITEM_GEM && bag->gem > 0) {
        bag->gem--;
        if(!b->wild) {
            bag->gem++;
            n = s_cat(b->msg[0], 0, "CRYSTALS WILL NOT TAKE A TAMERS CRYMON");
        }
        else if(*party_n >= 6) {
            bag->gem++;
            n = s_cat(b->msg[0], 0, "SIX IS ALL MAX CAN HOLD");
        }
        else {
            int chance = battle_capture_chance(b);
            if(irand(1, 100) <= chance) {
                Monster c = b->foe;
                c.hp = c.maxHp * 2 / 5;
                if(c.hp < 1) c.hp = 1;
                party[*party_n] = c;
                (*party_n)++;
                n = s_cat(b->msg[0], 0, "THE CRYSTAL TAKES ");
                n = s_cat(b->msg[0], n, SPECIES[c.species].name);
                n = s_cat(b->msg[0], n, " IS YOURS");
                b->msg[0][n] = 0;
                b->msg_n = 1; b->msg_i = 0; b->phase = 0; b->after = BAFTER_WORLD;
                party[lead] = b->pl;
                return;
            }
            n = s_cat(b->msg[0], 0, "THE CRYSTAL CRACKS DARK IT SLIPS FREE");
        }
    }
    else if(kind == ITEM_SUNBALM && bag->sunbalm > 0) {
        int heal = b->pl.maxHp - b->pl.hp;
        if(heal > 40) heal = 40;
        bag->sunbalm--;
        b->pl.hp += heal;
        n = s_cat(b->msg[0], 0, "SUNBALM ");
        n = s_cat_uint(b->msg[0], n, heal);
        n = s_cat(b->msg[0], n, " HP");
    }
    else if(kind == ITEM_WARROOT && bag->warroot > 0) {
        bag->warroot--;
        b->mods_self_agl += 4;
        n = s_cat(b->msg[0], 0, "WARROOT AGL+4 THIS FIGHT");
    }
    else if(kind == ITEM_SMOKEBOMB && bag->smokebomb > 0) {
        if(!b->wild) {
            n = s_cat(b->msg[0], 0, "CANNOT FLEE A TAMER'S FIGHT");
        }
        else {
            bag->smokebomb--;
            n = s_cat(b->msg[0], 0, "SMOKE BOMB MAX SLIPS AWAY");
            b->msg[0][n] = 0;
            b->msg_n = 1; b->msg_i = 0; b->phase = 0; b->after = BAFTER_WORLD;
            party[lead] = b->pl;
            return;
        }
    }
    else if(kind == ITEM_GREATCRYSTAL && bag->greatcrystal > 0) {
        bag->greatcrystal--;
        if(!b->wild) {
            bag->greatcrystal++;
            n = s_cat(b->msg[0], 0, "CRYSTALS WILL NOT TAKE A TAMERS CRYMON");
        }
        else if(*party_n >= 6) {
            bag->greatcrystal++;
            n = s_cat(b->msg[0], 0, "SIX IS ALL MAX CAN HOLD");
        }
        else {
            int chance = battle_capture_chance(b) + 25;
            if(chance > 100) chance = 100;
            if(irand(1, 100) <= chance) {
                Monster c = b->foe;
                c.hp = c.maxHp * 2 / 5;
                if(c.hp < 1) c.hp = 1;
                party[*party_n] = c;
                (*party_n)++;
                n = s_cat(b->msg[0], 0, "THE GREATER CRYSTAL TAKES ");
                n = s_cat(b->msg[0], n, SPECIES[c.species].name);
                n = s_cat(b->msg[0], n, " IS YOURS");
                b->msg[0][n] = 0;
                b->msg_n = 1; b->msg_i = 0; b->phase = 0; b->after = BAFTER_WORLD;
                party[lead] = b->pl;
                return;
            }
            n = s_cat(b->msg[0], 0, "THE GREATER CRYSTAL CRACKS DARK IT SLIPS FREE");
        }
    }
    else {
        n = s_cat(b->msg[0], 0, "NOTHING HAPPENS");
    }

    b->msg[0][n] = 0;
    b->msg_n = 1; b->msg_i = 0; b->phase = 0; b->after = BAFTER_ATK;
    party[lead] = b->pl;
}

/* finishWin()'s generic wild-win tail (the trainer-specific branches
   above it in state.lua all need NPCs this port doesn't have yet --
   see the section comment). Grants XP to the party lead and marks+3;
   the grow/no-grow note is queued by the caller as a WIN_NOTE beat
   rather than shown here (see the section comment on why). */
/* XP is granted on every win regardless of trainer_kind (matches
   finishWin() granting it unconditionally before any trainer branch);
   marks and the mode/flag changes per trainer differ and are handled
   by the caller in main(), which is where all that state (soldiers,
   beat_calder, ending mode...) lives. */
static void battle_finish_win(Battle *b, Monster *party, int lead) {
    b->grew = grant_xp(&party[lead], b->foe.lv);
    b->pl = party[lead];
}

/* tryEncounter(): checked once per tile the player steps onto (not
   every frame -- last_tx/last_ty track the last checked tile, exactly
   like G.lastTx/G.lastTy). Only 'T' tiles trigger, at an 18% chance
   (irand(0,99) < 18, not <= -- data.ts's exact 18-of-100 trigger set),
   gated by a 3-frame cooldown (enc_lock) after each check. Species
   pool/level range matches state.lua exactly; the reference's
   "if MAP_FOREST ... else (implicitly VELD)" is safe to mirror as
   written because HOUSE and GROVE have no 'T' tiles at all (checked
   against the map data above), so the else branch only ever runs for
   VELD in practice. Does nothing if the party is empty (leader(G) ==
   nil guard in startBattle). On a trigger, fills *out (except pl,
   which the caller sets from party[lead]) and returns 1. */
static int try_encounter(int map_id, int px, int py, int party_n,
                          int *enc_lock, int *last_tx, int *last_ty,
                          Battle *out) {
    static const int forest_pool[3] = { SP_FENWISP, SP_DUSKHORN, SP_NEEDLEROOT };
    static const int cliffs_pool[6] = { SP_STORMWING, SP_SABLECLAW, SP_FROSTAIL,
                                         SP_BOULDERAM, SP_THORNHIDE, SP_GLASSWISP };
    int tx = px / TILE, ty = py / TILE;
    int id, lv, n;

    if(tx == *last_tx && ty == *last_ty) return 0;
    *last_tx = tx;
    *last_ty = ty;
    if(tile_at(map_id, tx, ty) != 'T') return 0;
    if(*enc_lock > 0) { (*enc_lock)--; return 0; }
    if(irand(0, 99) >= 18) return 0;
    if(party_n <= 0) return 0;

    *enc_lock = 3;

    if(map_id == MAP_FOREST) {
        id = forest_pool[irand(0, 2)];
        lv = 3 + irand(0, 2);
    }
    else if(map_id == MAP_CLIFFS) {
        id = cliffs_pool[irand(0, 5)];
        lv = 5 + irand(0, 2);
    }
    else {
        /* VELD's tall grass -- EMBERLING/ASHENMAW mixed in alongside
           the original GLIMMOTH/TORTCASK split (1-in-4 each) so both
           are reachable as wild encounters too, not just as the new
           Weeping Army soldiers' fixed rosters. */
        int roll = irand(0, 3);
        if(roll == 0) id = SP_GLIMMOTH;
        else if(roll == 1) id = SP_TORTCASK;
        else if(roll == 2) id = SP_EMBERLING;
        else id = SP_ASHENMAW;
        lv = 2 + (ty > 14 ? 1 : 0) + irand(0, 1);
    }

    out->foe = roll_shiny() ? mint_shiny(id, lv) : mint_monster(id, lv);
    out->wild = 1;
    out->trainer_kind = TRAINER_WILD;
    out->soldier_id = 0;
    out->bench_n = 0;
    out->phase = 0;
    n = s_cat(out->msg[0], 0, out->foe.shiny ? "A SHINY " : "A WILD ");
    n = s_cat(out->msg[0], n, SPECIES[id].name);
    out->msg[0][n] = 0;
    out->msg_n = 1;
    out->msg_i = 0;
    out->after = BAFTER_ITEM;
    out->cur = 0;
    out->mods_self_str = out->mods_self_agl = out->mods_self_spc = 0;
    out->mods_foe_str = out->mods_foe_agl = out->mods_foe_spc = 0;
    out->pl_poisoned = out->foe_poisoned = 0;
    out->mg = 8.0f;
    out->mg_dir = 1;
    out->grew = 0;
    return 1;
}

/* ----------------------------------------------------------------------
 * Battle drawing. Unlike the bag/party/shop menus (one big centered
 * panel -- fine for those, they have no background scene behind
 * them), this uses drawBattle()'s own layout: small boxed status
 * readouts and a content box, both far short of the full screen, so
 * the battle-bg.png background and both sprites stay visible in
 * between them. No background panels (see draw_dialogue_box's own
 * comment) -- status text and the message/menu area both sit
 * directly over the battle background/sprites now, just outlined.
 * ---------------------------------------------------------------------- */

/* BCONTENT (the message/item/attack/guard menu box) is sized tight
   against its own content now that the mid-combat item menu no longer
   carries effect text (see draw_battle_item_menu) -- its widest row
   is the attack menu's "LIGHTNING STRIKE", not an item line, and its
   height is still just the item menu's worst case (PASS + 5 items).
   Shrinking it pushes it further right and further down (it's still
   anchored to the bottom-right corner), which is what actually frees
   the room the two status boxes and MONSTER_SPRITE_W/H below now use.

   The foe's status box takes the top-right edge (as high and as far
   right as it can sit without risking the edge) instead of tucking
   under the foe's sprite; the foe's sprite goes directly below the
   box instead, sized as large as it can get while still clearing
   BCONTENT_Y. Max's status box sits lower, near the screen's vertical
   middle and as far left as it can go -- it doesn't need to clear the
   foe's sprite at all (their X ranges don't overlap: the sprite is
   flush against the right edge, the box flush against the left), only
   the foe's own box (BGAP above it) and BCONTENT_Y (BGAP below it).
   That's what lets the sprite grow far past what stacking both boxes
   in the foe's own column allowed. Max's sprite, still flush in the
   true lower-left corner, isn't part of any of this -- its width
   alone keeps it clear of BCONTENT regardless of height. BGAP is the
   fixed clearance kept between every pair of these elements. */
#define BGAP          4

#define BCONTENT_W    180
#define BCONTENT_H    112
#define BCONTENT_X    (SCREEN_W - 4 - BCONTENT_W)
#define BCONTENT_Y    (SCREEN_H - 4 - BCONTENT_H)

#define BSTATUS_BOX_W 208
#define BSTATUS_BOX_H 18

#define BFOE_BOX_W    BSTATUS_BOX_W
#define BFOE_BOX_H    BSTATUS_BOX_H
#define BFOE_BOX_X    (SCREEN_W - 4 - BSTATUS_BOX_W)
#define BFOE_BOX_Y    4

#define BFOE_SPRITE_X (SCREEN_W - 8 - MONSTER_SPRITE_W)
#define BFOE_SPRITE_Y (BFOE_BOX_Y + BSTATUS_BOX_H + BGAP)

#define BPL_SPRITE_X  8
#define BPL_SPRITE_Y  (SCREEN_H - 4 - MONSTER_SPRITE_H)

#define BPL_BOX_W     BSTATUS_BOX_W
#define BPL_BOX_H     BSTATUS_BOX_H
#define BPL_BOX_X     4
#define BPL_BOX_Y     (BCONTENT_Y - BGAP - BSTATUS_BOX_H)

#define BROW_H        16

/* Index order matches SP_QUILLPUP..SP_CRYMARE and gen_sprites.py's
   MONSTERS list. Used for both the foe's sprite and -- new here --
   "Max's CryMon" (the player's own active monster; Max herself
   already has her own walk sprite on the world map, so this is what
   "the player's battle sprite" actually means in this game). */
static const u16 *const MONSTER_SPRITES[20][4] = {
    { monster_quillpup_1, monster_quillpup_2, monster_quillpup_3, monster_quillpup_4 },
    { monster_glimmoth_1, monster_glimmoth_2, monster_glimmoth_3, monster_glimmoth_4 },
    { monster_tortcask_1, monster_tortcask_2, monster_tortcask_3, monster_tortcask_4 },
    { monster_razorbat_1, monster_razorbat_2, monster_razorbat_3, monster_razorbat_4 },
    { monster_mossback_1, monster_mossback_2, monster_mossback_3, monster_mossback_4 },
    { monster_briarfox_1, monster_briarfox_2, monster_briarfox_3, monster_briarfox_4 },
    { monster_fenwisp_1, monster_fenwisp_2, monster_fenwisp_3, monster_fenwisp_4 },
    { monster_duskhorn_1, monster_duskhorn_2, monster_duskhorn_3, monster_duskhorn_4 },
    { monster_needleroot_1, monster_needleroot_2, monster_needleroot_3, monster_needleroot_4 },
    { monster_cathleen_1, monster_cathleen_2, monster_cathleen_3, monster_cathleen_4 },
    { monster_crymare_1, monster_crymare_2, monster_crymare_3, monster_crymare_4 },
    { monster_emberling_1, monster_emberling_2, monster_emberling_3, monster_emberling_4 },
    { monster_frostail_1, monster_frostail_2, monster_frostail_3, monster_frostail_4 },
    { monster_boulderam_1, monster_boulderam_2, monster_boulderam_3, monster_boulderam_4 },
    { monster_stormwing_1, monster_stormwing_2, monster_stormwing_3, monster_stormwing_4 },
    { monster_sableclaw_1, monster_sableclaw_2, monster_sableclaw_3, monster_sableclaw_4 },
    { monster_thornhide_1, monster_thornhide_2, monster_thornhide_3, monster_thornhide_4 },
    { monster_glasswisp_1, monster_glasswisp_2, monster_glasswisp_3, monster_glasswisp_4 },
    { monster_ashenmaw_1, monster_ashenmaw_2, monster_ashenmaw_3, monster_ashenmaw_4 },
    { monster_heavenfall_1, monster_heavenfall_2, monster_heavenfall_3, monster_heavenfall_4 },
};

/* Idle-animated like the stationary world NPCs (drawBattle()'s own
   `Math.floor(b.t * 4) % 4 + 1`, shared by foe and player sprite) --
   frame_count/15 matches the same 4fps cadence draw_npc_idle() uses.
   enter_t/faint_t are frame_count timestamps (main()'s battle_*_t
   locals): 0 means "no animation in progress" for that side, matching
   frame_count never legitimately being 0 once the title screen has
   run a single frame. enter plays once per fresh monster (a new
   battle, or a bench monster swapping in after a faint); faint plays
   once hp actually hits 0 and holds on its last (fully faded) frame
   for as long as the 0-hp monster stays on screen -- both read from
   the same blit_sprite_anim so a sprite that's both "just entered"
   and immediately guarded (impossible, but if timers ever overlapped)
   would still draw sanely rather than double-applying either effect
   twice. */
#define BATTLE_ANIM_ENTER_FRAMES 18
#define BATTLE_ANIM_FAINT_FRAMES 24
static void draw_battle_sprites(const Battle *b, u32 frame_count,
                                 u32 foe_enter_t, u32 foe_faint_t,
                                 u32 pl_enter_t, u32 pl_faint_t) {
    int f = (int)((frame_count / 15u) % 4u);
    int foe_revealed = MONSTER_SPRITE_H, foe_fade = 16;
    int pl_revealed = MONSTER_SPRITE_H, pl_fade = 16;

    if(foe_faint_t && frame_count >= foe_faint_t) {
        u32 el = frame_count - foe_faint_t;
        foe_fade = (el >= BATTLE_ANIM_FAINT_FRAMES) ? 0
                       : 16 - (int)(el * 16u / BATTLE_ANIM_FAINT_FRAMES);
    }
    else if(foe_enter_t && frame_count >= foe_enter_t) {
        u32 el = frame_count - foe_enter_t;
        foe_revealed = (el >= BATTLE_ANIM_ENTER_FRAMES) ? MONSTER_SPRITE_H
                           : (int)(el * (u32)MONSTER_SPRITE_H / BATTLE_ANIM_ENTER_FRAMES);
    }
    if(pl_faint_t && frame_count >= pl_faint_t) {
        u32 el = frame_count - pl_faint_t;
        pl_fade = (el >= BATTLE_ANIM_FAINT_FRAMES) ? 0
                      : 16 - (int)(el * 16u / BATTLE_ANIM_FAINT_FRAMES);
    }
    else if(pl_enter_t && frame_count >= pl_enter_t) {
        u32 el = frame_count - pl_enter_t;
        pl_revealed = (el >= BATTLE_ANIM_ENTER_FRAMES) ? MONSTER_SPRITE_H
                          : (int)(el * (u32)MONSTER_SPRITE_H / BATTLE_ANIM_ENTER_FRAMES);
    }

    blit_sprite_anim(MONSTER_SPRITES[b->foe.species][f], MONSTER_SPRITE_W, MONSTER_SPRITE_H,
                      BFOE_SPRITE_X, BFOE_SPRITE_Y, b->foe.shiny, foe_revealed, foe_fade);
    blit_sprite_anim(MONSTER_SPRITES[b->pl.species][f], MONSTER_SPRITE_W, MONSTER_SPRITE_H,
                      BPL_SPRITE_X, BPL_SPRITE_Y, b->pl.shiny, pl_revealed, pl_fade);
}

/* One line each -- "*NAME LVxx hp/maxHp" -- sized to BSTATUS_BOX_W's
   worst case (see the layout comment above). */
static void draw_battle_status(const Battle *b) {
    char buf[40];
    int n;

    n = s_cat(buf, 0, b->foe.shiny ? "*" : "");
    n = s_cat(buf, n, SPECIES[b->foe.species].name);
    n = s_cat(buf, n, " LV");
    n = s_cat_uint(buf, n, b->foe.lv);
    n = s_cat(buf, n, " ");
    n = s_cat_uint(buf, n, b->foe.hp);
    n = s_cat(buf, n, "/");
    n = s_cat_uint(buf, n, b->foe.maxHp);
    buf[n] = 0;
    draw_text_s(buf, BFOE_BOX_X + 4, BFOE_BOX_Y + 4, 0xFFFF, MENU_SCALE);

    n = s_cat(buf, 0, b->pl.shiny ? "*" : "");
    n = s_cat(buf, n, SPECIES[b->pl.species].name);
    n = s_cat(buf, n, " LV");
    n = s_cat_uint(buf, n, b->pl.lv);
    n = s_cat(buf, n, " ");
    n = s_cat_uint(buf, n, b->pl.hp);
    n = s_cat(buf, n, "/");
    n = s_cat_uint(buf, n, b->pl.maxHp);
    buf[n] = 0;
    draw_text_s(buf, BPL_BOX_X + 4, BPL_BOX_Y + 4, 0xFFFF, MENU_SCALE);
}

static void draw_battle_menu_row(const char *label, int idx, int cur, int y) {
    u16 color = (idx == cur) ? rgb565(232, 228, 216) : rgb565(138, 134, 120);
    draw_text_s(idx == cur ? ">" : " ", BCONTENT_X + 8, y, color, MENU_SCALE);
    draw_text_s(label, BCONTENT_X + 16, y, color, MENU_SCALE);
}

/* Icon variant of the above, for menus with room for one (the shop --
   the battle item menu's own rows stay text-only, BCONTENT_W is
   already tight against their longer stat strings). */
static void draw_menu_row_icon(const u16 *icon, const char *label, int idx, int cur, int y) {
    u16 color = (idx == cur) ? rgb565(232, 228, 216) : rgb565(138, 134, 120);
    draw_text_s(idx == cur ? ">" : " ", MENU_X + 8, y, color, MENU_SCALE);
    if(icon)
        blit_sprite(icon, ITEM_ICON_W, ITEM_ICON_H, MENU_X + 16, y - 1);
    draw_text_s(label, MENU_X + 16 + ITEM_ICON_W + 4, y, color, MENU_SCALE);
}

/* Row count/kind-at-cursor for the item menu, kept in exact lockstep
   with draw_battle_item_menu's own conditional row order below (both
   walk PASS, salve, bandage, bitterroot, dust, gem in that order,
   skipping any the bag is empty of). Used by main()'s input handling,
   which needs the mapping without actually drawing. */
static int battle_item_menu_count(const Bag *bag) {
    int n = 1; /* PASS always present */
    if(bag->salve > 0) n++;
    if(bag->bandage > 0) n++;
    if(bag->bitterroot > 0) n++;
    if(bag->dust > 0) n++;
    if(bag->gem > 0) n++;
    if(bag->sunbalm > 0) n++;
    if(bag->warroot > 0) n++;
    if(bag->smokebomb > 0) n++;
    if(bag->greatcrystal > 0) n++;
    return n;
}

static int battle_item_menu_kind(const Bag *bag, int idx) {
    int i = 0;
    if(idx == i++) return ITEM_PASS;
    if(bag->salve > 0)        { if(idx == i++) return ITEM_SALVE; }
    if(bag->bandage > 0)      { if(idx == i++) return ITEM_BANDAGE; }
    if(bag->bitterroot > 0)   { if(idx == i++) return ITEM_BITTERROOT; }
    if(bag->dust > 0)         { if(idx == i++) return ITEM_DUST; }
    if(bag->gem > 0)          { if(idx == i++) return ITEM_GEM; }
    if(bag->sunbalm > 0)      { if(idx == i++) return ITEM_SUNBALM; }
    if(bag->warroot > 0)      { if(idx == i++) return ITEM_WARROOT; }
    if(bag->smokebomb > 0)    { if(idx == i++) return ITEM_SMOKEBOMB; }
    if(bag->greatcrystal > 0) { if(idx == i++) return ITEM_GREATCRYSTAL; }
    return ITEM_PASS; /* unreachable: idx is always < battle_item_menu_count() */
}

/* fillItemMenu, minus the "switch" row (this port's party has no
   manual-switch UI -- see the item-menu comment in main()). Capture
   Crystal's label includes the live capture chance, matching
   fillItemMenu's wild-battle branch (the trainer branch, plain
   "Capture Crystal xN", is dead code here -- b->wild is always 1).

   BCONTENT_H only has room for BATTLE_ITEM_VISIBLE_ROWS at once (the
   layout was sized for the original PASS+5 items, not PASS+9) -- with
   9 possible item types now, a player who's collected one of
   everything needs to scroll. Paged in fixed BATTLE_ITEM_VISIBLE_ROWS
   chunks rather than a smooth 1-row scroll so the visible window is a
   pure function of `cur` (which page cur falls on), no extra
   persisted scroll state needed. */
#define BATTLE_ITEM_VISIBLE_ROWS 6
static int draw_battle_item_menu(const Bag *bag, int cur) {
    int y = BCONTENT_Y + 8;
    int i = 0;
    char buf[40];
    int n;
    int total, page, first, last;
    const char *kinds_label[9];
    int kinds_count[9];
    int kn = 0;

    kinds_label[kn] = "PASS"; kinds_count[kn] = -1; kn++;
    if(bag->salve > 0)        { kinds_label[kn] = "SALVE X";     kinds_count[kn] = bag->salve; kn++; }
    if(bag->bandage > 0)      { kinds_label[kn] = "WRAP X";      kinds_count[kn] = bag->bandage; kn++; }
    if(bag->bitterroot > 0)   { kinds_label[kn] = "BITTERROOT X"; kinds_count[kn] = bag->bitterroot; kn++; }
    if(bag->dust > 0)         { kinds_label[kn] = "DUST X";      kinds_count[kn] = bag->dust; kn++; }
    if(bag->gem > 0)          { kinds_label[kn] = "CRYSTAL X";   kinds_count[kn] = bag->gem; kn++; }
    if(bag->sunbalm > 0)      { kinds_label[kn] = "SUNBALM X";   kinds_count[kn] = bag->sunbalm; kn++; }
    if(bag->warroot > 0)      { kinds_label[kn] = "WARROOT X";   kinds_count[kn] = bag->warroot; kn++; }
    if(bag->smokebomb > 0)    { kinds_label[kn] = "SMOKE BOMB X"; kinds_count[kn] = bag->smokebomb; kn++; }
    if(bag->greatcrystal > 0) { kinds_label[kn] = "GR CRYSTAL X"; kinds_count[kn] = bag->greatcrystal; kn++; }

    total = kn;
    page = (cur / BATTLE_ITEM_VISIBLE_ROWS) * BATTLE_ITEM_VISIBLE_ROWS;
    first = page;
    last = first + BATTLE_ITEM_VISIBLE_ROWS;
    if(last > total) last = total;

    for(i = first; i < last; i++) {
        if(kinds_count[i] < 0) {
            draw_battle_menu_row(kinds_label[i], i, cur, y);
        }
        else {
            n = s_cat(buf, 0, kinds_label[i]);
            n = s_cat_uint(buf, n, kinds_count[i]);
            buf[n] = 0;
            draw_battle_menu_row(buf, i, cur, y);
        }
        y += MENU_ROW_H;
    }
    return total; /* row count, for input handling to map kinds <-> cursor */
}

/* attackMenu(): a spellcaster lead's attack menu is just their spell
   names (4 rows for Cathleen, PP shown only next to Mana Surge, the
   one with a pp cost -- matches data.ts's spells[].pp flag), no
   basic/special/wait rows at all. */
static void draw_battle_atk_menu(const Battle *b, int cur) {
    int y = BCONTENT_Y + 8;
    char buf[32];
    int n;
    const Species *s = &SPECIES[b->pl.species];

    if(s->spells_n > 0) {
        static const char *const SPELL_MENU_NAME[4] = {
            "FIRE BOLT", "ICE BEAM", "LIGHTNING STRIKE", "MANA SURGE"
        };
        int i;
        for(i = 0; i < s->spells_n; i++) {
            if(s->spells[i] == SPELL_MANASURGE) {
                n = s_cat(buf, 0, SPELL_MENU_NAME[s->spells[i]]);
                n = s_cat(buf, n, " ");
                n = s_cat_uint(buf, n, b->pl.spp);
                n = s_cat(buf, n, "/");
                n = s_cat_uint(buf, n, b->pl.sppMax);
                buf[n] = 0;
                draw_battle_menu_row(buf, i, cur, y);
            }
            else {
                draw_battle_menu_row(SPELL_MENU_NAME[s->spells[i]], i, cur, y);
            }
            y += MENU_ROW_H;
        }
        return;
    }

    draw_battle_menu_row(s->basic, 0, cur, y); y += MENU_ROW_H;

    n = s_cat(buf, 0, s->special);
    n = s_cat(buf, n, " ");
    n = s_cat_uint(buf, n, b->pl.spp);
    n = s_cat(buf, n, "/");
    n = s_cat_uint(buf, n, b->pl.sppMax);
    buf[n] = 0;
    draw_battle_menu_row(buf, 1, cur, y); y += MENU_ROW_H;

    /* Extra row, shiny leads only -- see battle_pick_toxic(). */
    if(b->pl.shiny) {
        draw_battle_menu_row("TOXIC BURST", 2, cur, y); y += MENU_ROW_H;
        draw_battle_menu_row("WAIT", 3, cur, y);
    }
    else {
        draw_battle_menu_row("WAIT", 2, cur, y);
    }
}

static void draw_battle_guard_menu(int cur) {
    int y = BCONTENT_Y + 8;
    draw_battle_menu_row("DODGE AGI", 0, cur, y); y += MENU_ROW_H;
    draw_battle_menu_row("BLOCK STR", 1, cur, y); y += MENU_ROW_H;
    draw_battle_menu_row("BARRIER SPC", 2, cur, y);
}

static void draw_battle_minigame(const Battle *b) {
    int bar_x = BCONTENT_X + 8, bar_y = BCONTENT_Y + 16, bar_w = BCONTENT_W - 16, bar_h = 10;
    int needle_x = bar_x + (int)(b->mg * (float)bar_w / 100.0f);

    fill_rect(bar_x, bar_y, bar_w, bar_h, rgb565(40, 38, 32));
    fill_rect(bar_x + (int)(0.38f * (float)bar_w), bar_y,
              (int)(0.24f * (float)bar_w), bar_h, rgb565(90, 122, 82));
    fill_rect(bar_x + (int)(0.46f * (float)bar_w), bar_y,
              (int)(0.08f * (float)bar_w), bar_h, rgb565(197, 206, 198));
    fill_rect(needle_x - 1, bar_y - 4, 2, bar_h + 8, 0xFFFF);
    draw_text_s("A TO STRIKE", BCONTENT_X + 8, bar_y + bar_h + 8, rgb565(138, 134, 120), MENU_SCALE);
}

/* drawBattle()'s full-screen background, drawn before the status
   boxes/sprites/content box, all of which are individually small so
   the background (and both battle sprites) stay visible around them
   -- see the section comment above. */
static void draw_battle_bg(void) {
    blit_sprite(battle_bg, BATTLE_BG_W, BATTLE_BG_H, 0, 0);
}

static void draw_battle(const Battle *b, const Bag *bag, u32 frame_count,
                         u32 foe_enter_t, u32 foe_faint_t, u32 pl_enter_t, u32 pl_faint_t) {
    draw_battle_bg();
    draw_battle_status(b);
    draw_battle_sprites(b, frame_count, foe_enter_t, foe_faint_t, pl_enter_t, pl_faint_t);

    switch(b->phase) {
        case 0:
            draw_wrapped(b->msg[b->msg_i], BCONTENT_X + 8, BCONTENT_Y + 8,
                         rgb565(232, 228, 216), MENU_SCALE, BCONTENT_W / CHAR_CELL(MENU_SCALE) - 2, 9);
            break;
        case 1:
            draw_battle_item_menu(bag, b->cur);
            break;
        case 2:
            draw_battle_atk_menu(b, b->cur);
            break;
        case 3:
            draw_battle_guard_menu(b->cur);
            break;
        case 4:
            draw_battle_minigame(b);
            break;
        default:
            break;
    }
}

/* ----------------------------------------------------------------------
 * World NPCs. Wren/Mae/Ivo/Nell/Pike/Bram/Calder/Shinigami/Cathleen
 * are stationary proximity-interact points (this port's own
 * interact()/closest_mark pattern, already used for HOUSE's bed/
 * shelf/crate) -- engine.ts never moves them either, so this is a
 * faithful 1:1. Mason, Anne and the three FOREST soldiers are real
 * engine.ts actors with float positions and per-frame movement
 * (approach/chase/patrol/leave, all ported below with the same speed
 * constants engine.ts uses, scaled by this port's TILE=20 vs the
 * original's TILE=32): Mason force-walks toward the player the moment
 * they leave the house and ambushes them into masonFight on contact
 * (mason_state 0 off/1 approach/2 standing-post-ambush/3 leaving);
 * Anne does the same after the player's first battle, gifts gems on
 * contact, then walks off (anne_state 0/1/2 done-pending/3 leaving);
 * soldiers patrol a fixed axis around their spawn mark and give chase
 * once the player crosses their line of sight (soldier_update()'s
 * raycast below, ported from soldierLos()), ambushing the same way
 * Mason does once they catch up.
 *
 * Anne, corrected: an earlier pass here concluded she was unreachable
 * because state.lua defines maybeAnne() but never calls it from
 * anywhere in state.lua or input.lua (true, checked directly). What
 * that pass missed is that state.lua's own header names
 * src/game/engine.ts as the single source of truth this Lua is
 * ported from, and engine.ts's equivalent, maybeStartAnne(), IS
 * called -- from updateWorld() every frame (gated on
 * !talking() && hudT<=0) and after cycling the party. So the Lua
 * intermediate has a real porting gap (a missing call site), not the
 * TS/canonical game; the faithful port includes Anne.
 * ---------------------------------------------------------------------- */
static int near_mark(int map_id, char mark, int px, int py, int radius_sq) {
    int mx, my, dx, dy;
    mark_center(map_id, mark, &mx, &my);
    dx = px - mx;
    dy = py - my;
    return dx * dx + dy * dy <= radius_sq;
}

/* ensureSoldiers(): id/name/species/level, matching state.lua's
   patrol/scout/sentry entries (their patrol minv/maxv/axis/LOS isn't
   ported -- see the section comment above). */
typedef struct {
    char mark;
    const char *name;
    int species;
    int lv;
} SoldierDef;

static const SoldierDef SOLDIERS[3] = {
    { '1', "PATROL", SP_BRIARFOX, 4 },
    { '2', "SCOUT",  SP_MOSSBACK, 4 },
    { '3', "SENTRY", SP_RAZORBAT, 5 },
};

/* Real-time actor movement constants, ported from engine.ts's own
   dt-based speeds (52/36/112/80 px/sec for approach/patrol/chase/
   leave) scaled by this port's TILE=20 vs the original's TILE=32
   (0.625x), then converted to a fixed per-frame step assuming a
   60Hz vblank-paced loop (dt = 1/60) since this port has no real
   delta-time -- see the "no dt" note in the movement update below. */
#define ACTOR_SPD_APPROACH (32.5f / 60.0f)
#define ACTOR_SPD_PATROL   (22.5f / 60.0f)
#define ACTOR_SPD_CHASE    (70.0f / 60.0f)
#define ACTOR_SPD_LEAVE    (50.0f / 60.0f)
#define ACTOR_REACH_DIST   22.5f   /* 36 * 0.625, engine.ts's dist<36 */
#define ACTOR_CHASE_CATCH  22.5f

/* soldierLos(): raycast one tile at a time along the soldier's facing
   direction until it hits a solid tile (miss) or the player's tile
   (spotted). dir codes match pdir: 0=down,1=up,2=left,3=right. */
static int soldier_los(int map_id, float sx, float sy, int dir, int ppx, int ppy) {
    int stx = (int)sx / TILE, sty = (int)sy / TILE;
    int ptx = ppx / TILE, pty = ppy / TILE;
    int dx = (dir == 2) ? -1 : (dir == 3) ? 1 : 0;
    int dy = (dir == 1) ? -1 : (dir == 0) ? 1 : 0;
    int i, maxs;
    if(dx == 0 && dy == 0) return 0;
    maxs = MAPS[map_id].cols > MAPS[map_id].rows_n ? MAPS[map_id].cols : MAPS[map_id].rows_n;
    for(i = 1; i <= maxs; i++) {
        int tx = stx + dx * i, ty = sty + dy * i;
        char ch = tile_at(map_id, tx, ty);
        if(tile_is_solid(ch)) return 0;
        if(tx == ptx && ty == pty) return 1;
    }
    return 0;
}

/* hitActor(): a live world actor blocks player movement like a solid
   tile, matching state.lua's blocked() actor check. Only actors that
   are actually standing in the world block -- Mason/Anne mid-approach
   or mid-leave don't (the reference doesn't collide against them
   either while they're walking toward/away from the player), nor does
   a beaten soldier (soldier_beaten). Radius is a small fixed circle
   around the actor's feet, not its full sprite box, so the player can
   still walk up close enough to trigger the proximity-interact/ambush
   checks that sit right next to this collision radius. */
static int actor_blocks(int map_id, int cx, int cy,
                         int mason_state, float mason_x, float mason_y,
                         int anne_state, float anne_x, float anne_y,
                         const Soldier *soldiers, const int *soldier_beaten) {
    int dx, dy;
#define HIT_R2 81 /* 9px radius, squared */
    if(map_id == MAP_VELD) {
        if(mason_state == 2) {
            dx = cx - (int)mason_x; dy = cy - (int)mason_y;
            if(dx * dx + dy * dy <= HIT_R2) return 1;
        }
        if(anne_state == 2) {
            dx = cx - (int)anne_x; dy = cy - (int)anne_y;
            if(dx * dx + dy * dy <= HIT_R2) return 1;
        }
    }
    else if(map_id == MAP_FOREST && soldiers) {
        int i;
        for(i = 0; i < 3; i++) {
            if(soldier_beaten[i]) continue;
            dx = cx - (int)soldiers[i].x; dy = cy - (int)soldiers[i].y;
            if(dx * dx + dy * dy <= HIT_R2) return 1;
        }
    }
#undef HIT_R2
    return 0;
}

/* ----------------------------------------------------------------------
 * Bram's shop, ported from state.lua's updateShop() -- data.lua notes
 * this is a deliberate deviation from native/crymon.c (which only
 * draws the shop, no purchase logic at all): the reference itself
 * ports engine.ts's working buy/sell UI instead, and so does this.
 * ---------------------------------------------------------------------- */
typedef struct {
    const char *name;
    int buy, sell;
} ItemDef;

/* Index order matches data.ITEM_ORDER = {salve,bandage,bitterroot,dust,
   gem}, extended with the 4 new items in the same order bag_field()
   uses (5-8). */
#include "content_items.inc"

static int *bag_field(Bag *bag, int idx) {
    switch(idx) {
        case 0: return &bag->salve;
        case 1: return &bag->bandage;
        case 2: return &bag->bitterroot;
        case 3: return &bag->dust;
        case 4: return &bag->gem;
        case 5: return &bag->sunbalm;
        case 6: return &bag->warroot;
        case 7: return &bag->smokebomb;
        default: return &bag->greatcrystal;
    }
}

/* Buy tab always lists all 5 items; sell tab only ones actually owned
   (ownedItems()). Returns the row count and fills *rows with item
   indices (0-4) in display order, for main()'s input handling and
   draw_shop() to stay in lockstep, same pattern as the battle item
   menu above. */
static int shop_rows(const Bag *bag, int sell_tab, int rows[ITEM_COUNT]) {
    int n = 0, i;
    for(i = 0; i < ITEM_COUNT; i++) {
        int owned = *bag_field((Bag *)bag, i);
        if(!sell_tab || owned > 0)
            rows[n++] = i;
    }
    return n;
}

static void draw_shop(const Bag *bag, int marks, int sell_tab, int cur) {
    int rows[ITEM_COUNT];
    int n = shop_rows(bag, sell_tab, rows);
    int y = MENU_Y + 40;
    int i;
    char buf[16];

    draw_menu_frame("BRAMS STALL", "B CLOSE");

    draw_text_s(sell_tab ? "BUY  >SELL" : ">BUY  SELL", MENU_X + 8, MENU_Y + 24,
                rgb565(197, 206, 198), MENU_SCALE);
    {
        int mn = s_cat(buf, 0, "MARKS ");
        mn = s_cat_uint(buf, mn, marks);
        buf[mn] = 0;
        draw_text_s(buf, MENU_X + MENU_W - 8 - text_width_s(buf, MENU_SCALE),
                    MENU_Y + 24, rgb565(143, 74, 64), MENU_SCALE);
    }

    for(i = 0; i < n; i++) {
        int idx = rows[i];
        int price = sell_tab ? ITEMS[idx].sell : ITEMS[idx].buy;
        int owned = *bag_field((Bag *)bag, idx);
        char row[40];
        int rn = s_cat(row, 0, ITEMS[idx].name);
        rn = s_cat(row, rn, " ");
        rn = s_cat_uint(row, rn, price);
        rn = s_cat(row, rn, "M X");
        rn = s_cat_uint(row, rn, owned);
        row[rn] = 0;
        draw_menu_row_icon(ITEM_ICONS[idx], row, i, cur, y);
        y += MENU_ROW_H;
    }
}

/* ----------------------------------------------------------------------
 * Ending screen, ported from render.lua's drawDemoEnd(): data.DEMO_END,
 * reached via the Grove/Shinigami/Anne chain and the resurrection
 * choice (draw_choice()) regardless of which the player picks. Steps
 * through its lines on A and returns to the title screen at the end.
 * ---------------------------------------------------------------------- */
static void draw_ending(const char *const *lines, int n, int i) {
    vram_clear();
    draw_text_center_s("CRYMON", SCREEN_W / 2, 24, 0xFFFF, 2);
    draw_text_center_s("GAME OVER", SCREEN_W / 2, 48, rgb565(143, 74, 64), 1);
    if(i < n)
        draw_wrapped(lines[i], 12, 80, rgb565(197, 206, 198), DIALOGUE_SCALE,
                     (SCREEN_W - 24) / CHAR_CELL(DIALOGUE_SCALE), 9);
    draw_text_center_s("A TO CONTINUE", SCREEN_W / 2, SCREEN_H - 20,
                        rgb565(90, 122, 82), DIALOGUE_SCALE);
}

/* interact() in state.lua: closest of U/B/S/C within a 36px radius
   (36*36=1296) in the original's 32px-tile space; scaled to our 20px
   tiles that's a 22.5px radius (22*22=484). */
/* HOUSE-only, matching interact()'s MAP_HOUSE branch -- callers only
   invoke this when map_id == MAP_HOUSE. */
static char closest_mark(int px, int py) {
    static const char marks[4] = { 'U', 'B', 'S', 'C' };
    int i;
    int best = 484, best_i = -1;

    for(i = 0; i < 4; i++) {
        int mx, my, dx, dy, d;
        mark_center(MAP_HOUSE, marks[i], &mx, &my);
        dx = px - mx;
        dy = py - my;
        d = dx * dx + dy * dy;
        if(d <= best) {
            best = d;
            best_i = i;
        }
    }
    return best_i >= 0 ? marks[best_i] : 0;
}

/* state.lua's warp(): places the player just past the destination
   mark, facing back the way they came (down if arriving from the
   south, up otherwise) -- matches doorLock's 20-frame cooldown below
   against instantly re-triggering the door tile on arrival. */
static void do_warp(int *map_id, int *px, int *py, int *pdir,
                     int to_map, char mark, int from_south,
                     int *banner_timer) {
    int col, row, sx, sy;
    *map_id = to_map;
    find_mark(to_map, mark, &col, &row);
    sx = col * TILE + TILE / 2;
    sy = row * TILE + TILE / 2;
    *px = sx;
    *py = from_south ? (sy + TILE + 8) : (sy - TILE);
    *pdir = from_south ? 0 : 1;
    *banner_timer = MAP_BANNER_TOTAL;
}

void main(void) {
    int state = 0; /* 0 = title screen, 1 = starting room */
    u32 frame_count = 0;
    int prev_start = 0, prev_a = 0, prev_b = 0, prev_y = 0;
    int prev_up = 0, prev_down = 0, prev_left = 0, prev_right = 0;
    int map_id = MAP_HOUSE;
    int px, py, pdir = 0; /* dir: 0=down,1=up,2=left,3=right */
    int anim_counter = 0; /* see draw_player's comment */
    int col, row;
    int cam_x, cam_y;
    u16 raw;
    int start_now, a_now, b_now, y_now, up_now, down_now, left_now, right_now;

    /* Frames left before a door tile can trigger another warp,
       matching state.lua's G.doorLock (set to 20 on spawn/warp,
       ticked down by 1 per world-state frame). */
    int door_lock = 0;

    /* Room state, matching state.lua's G.gotShelf / G.lootedCrate /
       G.bag (data.START_BAG) / G.marks (data.START_MARKS). No HP/SP
       system existed until this step; the bed's "full heal"
       (state.lua's fullHeal()) still has nothing to do here beyond
       showing its dialogue, since it's a full-party heal and this
       port's only source of a party member is the shelf. */
    int got_shelf = 0, looted_crate = 0;
    /* salve, bandage, bitterroot, dust, gem, sunbalm, warroot, smokebomb, greatcrystal */
    Bag bag = { 2, 2, 1, 1, 0, 0, 0, 1, 0 };
    int marks = 16;

    /* G.party, capped at data.PARTY_MAX (6); this port's only ways to
       grow it are the shelf's starter grant and a battle capture --
       no NPC gifts, no other starters. lead mirrors G.lead (0-based
       here). */
    Monster party[6];
    int party_n = 0, lead = 0;

    /* In-battle state (see the Battle section above); in_battle == 0
       means the world is showing normally. enc_lock/last_tx/last_ty
       are tryEncounter()'s G.encLock/G.lastTx/G.lastTy. */
    int in_battle = 0;
    Battle battle;
    int enc_lock = 8, last_tx = -1, last_ty = -1;

    /* draw_battle_sprites' enter/faint animation timestamps -- see its
       own comment. Tracked here by comparing each side's species/hp
       frame to frame rather than threading state through every single
       mint_monster()/battle.pl = party[lead] call site: a species
       change (a fresh battle, or a bench monster swapping in) arms
       enter_t, hp dropping to 0 arms faint_t, and leaving battle
       clears every timer so the next one starts clean. 0 means "no
       timer armed" (frame_count is never 0 by the time a battle can
       start). */
    u32 battle_foe_enter_t = 0, battle_foe_faint_t = 0;
    u32 battle_pl_enter_t = 0, battle_pl_faint_t = 0;
    int battle_prev_foe_species = -1, battle_prev_pl_species = -1;
    int battle_prev_foe_hp = -1, battle_prev_pl_hp = -1;
    int battle_prev_after = -1;
    int battle_was_active = 0;

    /* 0 = no menu, 1 = bag, 2 = party. Opened from the world with Y /
       START (state.lua's selectPressed()/startPressed() -- there's no
       Select button on a Dreamcast pad, so Y stands in for it), closed
       with B (state.lua's cancelPressed(), which also accepts start
       and select; B alone is enough here since neither Y nor START
       need a second meaning while a menu is open). */
    int menu_mode = 0;
    int party_cur = 0; /* cursor row inside the party menu */
    int party_detail = 0; /* party menu: 0 list, 1 viewing party_cur's detail */
    int bag_cur = 0; /* cursor row inside the bag menu */
    int heal_item = -1; /* -1 = not choosing a heal target, else bag_cur (0 salve, 1 wrap) */

    /* HUD toast, matching state.lua's G.hud/G.hudT/note(): a small
       banner (lead-switch confirmation, the post-win "grew to lv N"/
       "stands over the grass" line) that freezes world movement like
       a dialogue box until it either times out or the player presses
       A/B to dismiss it early. 720 frames at 60fps == note()'s own
       12-second hudT budget. */
    char hud_flash[40] = { 0 };
    int hud_t = 0;
#define HUD_NOTE_FRAMES 720

    /* draw_map_banner()'s countdown -- armed by do_warp() on every map
       transition, ticks down every frame regardless of menus/dialogue
       so it always finishes fading out on its own. */
    int map_banner_timer = 0;

    /* Screen fade (bed heal, a party wipe teleporting home): 0 idle,
       1 fading to black, 2 holding one black frame while fade_action
       actually happens (teleport/heal, so it's never visible mid-
       transition), 3 fading back in. Runs as a straight post-process
       over whatever's drawn each frame (apply_fade(), called at the
       very end of the draw dispatch below), so it doesn't care
       whether the world or the battle screen is underneath it, and
       ticks/draws every frame regardless of state/in_battle --
       world movement and battle input are the only things gated on
       fade_state == 0 (see their own gates further down). */
    int fade_state = 0;
    int fade_timer = 0;
    int fade_action = 0;
#define FADE_NONE        0
#define FADE_OUT         1
#define FADE_HOLD        2
#define FADE_IN          3
#define FADE_ACTION_BED  1
#define FADE_ACTION_LOSS 2

    /* Active dialogue sequence: seq_lines/seq_len name the current
       TALK_* array, seq_beat indexes into it. seq_lines == 0 means no
       dialogue is showing. post_action fires once the sequence
       finishes (state.lua's afterTalk/beginTalkEnd): starting a
       trainer battle or opening the shop. */
    const TalkBeat *seq_lines = 0;
    int seq_len = 0, seq_beat = 0;
    int post_action = 0, post_soldier_id = 0;
#define POST_NONE        0
#define POST_CALDER      1
#define POST_MASON       2
#define POST_SHINIGAMI   3
#define POST_SOLDIER     4
#define POST_CATHLEEN    5
#define POST_SHOP        6
#define POST_MASON_LEAVE 7
#define POST_ANNE_LEAVE  8
#define POST_BED_HEAL    10
#define POST_MASON2      11
#define POST_OPEN_CHOICE 12
#define POST_ENDING_FINAL 13
#define POST_WSOLDIER_CLIFFS 14
#define POST_WSOLDIER_CAMP1  15
#define POST_WSOLDIER_CAMP2  16
#define POST_WSOLDIER_GROVE  17
/* Oren's stall reuses POST_SHOP directly -- same draw_shop()/ITEMS
   table Bram's does, no separate post_action needed. */

    /* World NPC/pickup flags, matching state.lua's G.talkedWren etc.
       (see the world-NPC section comment above for what's ported vs
       simplified). */
    int talked_wren = 0, talked_mae = 0, talked_ivo = 0, talked_nell = 0;
    int talked_pike = 0, pike_helped = 0, nell_bonus = 0;
    int got_herb = 0, got_gem = 0, got_stump = 0, read_cart = 0;
    int beat_calder = 0, beat_mason = 0, beat_shin = 0, cath_caught = 0;
    int beat_cathleen = 0; /* set on any win vs her, not just a capture -- see tile_blocked's GROVE gate */
    int beat_wsoldier_cliffs = 0, beat_wsoldier_camp1 = 0;
    int beat_wsoldier_camp2 = 0, beat_wsoldier_grove = 0;
    int got_chest = 0;
    int talked_tessa = 0, talked_birch = 0, talked_sable = 0;
    int has_scroll = 0; /* Legendary Reanimation, granted once Shinigami's win dialogue closes */
    int anne2_told = 0; /* gates Anne's second (father-died/choice) approach to firing once */
    int choice_mode = 0, choice_cur = 0; /* father-vs-Heavenfall resurrection choice screen */
    int soldier_beaten[3] = { 0, 0, 0 };

    /* Mason/Anne real movement, matching state.lua's rival/anne
       phase machines (see the world-actors section comment further
       down for the full state chart and the formulas each number
       comes from). Positions are float since chase/approach movement
       is a normalized direction vector times a per-frame speed, not
       a whole-pixel step like the player's own dpad movement. */
    int mason_state = 0; /* 0 off, 1 approach, 2 standing (post-ambush), 3 leaving */
    float mason_x = 0.0f, mason_y = 0.0f;
    int mason_dir = 0;
    float mason_anim = 0.0f;
    int mason_rematch = 0; /* which loadout/dialogue the next ambush uses */

    /* Mason's rematch: once he's beaten AND Calder's beaten (the
       player has cleared the main VELD gauntlet), he reappears on a
       random open-world map, force-walks up and ambushes the player
       exactly like the first encounter (reusing the same mason_state
       machine, just with mason_rematch=1 picking a 3-CryMon loadout
       and different dialogue) the moment they set foot on that map.
       mason2_map is rolled once, right when beat_calder flips to 1. */
    int mason2_map = -1;
    int mason2_done = 0;

    /* Anne: engine.ts's maybeStartAnne() gate is battlesDone>=1 while
       on VELD (onBattleOver()/battlesDone++ fires on soldier, Mason,
       and generic wild wins -- not Calder or Shinigami, matching the
       Lua port's own audited note on selective battlesDone calls). */
    int battles = 0;
    int anne_state = 0; /* 0 off, 1 approach, 2 done-pending (gift dialogue), 3 leaving */
    float anne_x = 0.0f, anne_y = 0.0f;
    int anne_dir = 0;
    float anne_anim = 0.0f;
    int anne_gifted = 0;

    /* Soldiers: ensureSoldiers()'s 3 fixed NPCs, only meaningful once
       FOREST has been visited (soldiers_init latches that, matching
       ensureSoldiers()'s own "if already built, skip" guard). Type
       is file-scope (see above draw_npcs) since that draw function
       needs it too. */
    Soldier soldiers[3];
    int soldiers_init = 0;

    /* Shop (Bram) and ending screens. */
    int shop_open = 0, shop_sell_tab = 0, shop_cur = 0;
    int ending_mode = 0; /* 0 none, 1 showing DEMO_END (the single ending) */
    int ending_i = 0;

    video_init();
    maple_init();

    find_mark(MAP_HOUSE, 'P', &col, &row);
    px = col * TILE + TILE / 2;
    py = row * TILE + TILE / 2;

    /* Prime both buffers with the title screen before the main loop
       starts flipping, so the first flip doesn't show whatever
       garbage was in VRAM at boot. */
    draw_press_start();
    fb_flip();
    draw_press_start();

    for(;;) {
        wait_vblank();
        fb_flip();
        frame_count++;

        if(map_banner_timer > 0) map_banner_timer--;

        /* Battle enter/faint animation tracking -- see the locals'
           comment. Runs before input so a faint detected by the hit
           that just landed (still this same frame, phase already
           moved to the "X FALLS" message) is caught the very next
           frame's draw, not one frame late. */
        if(in_battle) {
            if(!battle_was_active) {
                battle_prev_foe_species = -1;
                battle_prev_pl_species = -1;
                battle_prev_after = -1;
            }
            if(battle.foe.species != battle_prev_foe_species) {
                battle_foe_enter_t = frame_count;
                battle_foe_faint_t = 0;
                battle_prev_foe_species = battle.foe.species;
                battle_prev_foe_hp = battle.foe.hp;
            }
            else if(battle.foe.hp <= 0 && battle_prev_foe_hp > 0 && !battle_foe_faint_t) {
                battle_foe_faint_t = frame_count;
            }
            /* A successful capture (BAFTER_WORLD, see battle_pick_item)
               withdraws the foe from battle without necessarily
               dropping its hp to 0 -- reuses the same fade-out as
               fainting, which reads fine for "leaving the screen"
               either way instead of needing a third visual language. */
            else if(battle.after == BAFTER_WORLD && battle_prev_after != BAFTER_WORLD &&
                    !battle_foe_faint_t) {
                battle_foe_faint_t = frame_count;
            }
            battle_prev_foe_hp = battle.foe.hp;
            battle_prev_after = battle.after;

            if(battle.pl.species != battle_prev_pl_species) {
                battle_pl_enter_t = frame_count;
                battle_pl_faint_t = 0;
                battle_prev_pl_species = battle.pl.species;
                battle_prev_pl_hp = battle.pl.hp;
            }
            else if(battle.pl.hp <= 0 && battle_prev_pl_hp > 0 && !battle_pl_faint_t) {
                battle_pl_faint_t = frame_count;
            }
            battle_prev_pl_hp = battle.pl.hp;
        }
        else if(battle_was_active) {
            battle_foe_enter_t = battle_foe_faint_t = 0;
            battle_pl_enter_t = battle_pl_faint_t = 0;
            battle_prev_foe_species = battle_prev_pl_species = -1;
            battle_prev_after = -1;
        }
        battle_was_active = in_battle;

        raw = maple_poll_buttons();
        start_now = pressed(raw, CONT_START);
        a_now     = pressed(raw, CONT_A);
        b_now     = pressed(raw, CONT_B);
        y_now     = pressed(raw, CONT_Y);
        up_now    = pressed(raw, CONT_DPAD_UP);
        down_now  = pressed(raw, CONT_DPAD_DOWN);
        left_now  = pressed(raw, CONT_DPAD_LEFT);
        right_now = pressed(raw, CONT_DPAD_RIGHT);

        /* Fade tick: runs every frame regardless of state/menu/battle
           (world movement and battle input are what gate on
           fade_state == 0, not this). FADE_HOLD is exactly one frame
           -- just long enough that apply_fade() below draws one fully
           black frame with the teleport/heal already applied, so
           neither the old nor the new scene is ever visible
           mid-transition. */
        if(fade_state == FADE_OUT) {
            fade_timer++;
            if(fade_timer >= FADE_STEPS) {
                fade_state = FADE_HOLD;
                fade_timer = 0;
            }
        }
        else if(fade_state == FADE_HOLD) {
            if(fade_action == FADE_ACTION_BED) {
                heal_party(party, party_n);
            }
            else if(fade_action == FADE_ACTION_LOSS) {
                heal_party(party, party_n);
                map_id = MAP_HOUSE;
                find_mark(MAP_HOUSE, 'U', &col, &row);
                px = (col + 1) * TILE + TILE / 2;
                py = row * TILE + TILE / 2;
                pdir = 1; /* facing up, toward the bed */
                last_tx = -1;
                last_ty = -1;
                door_lock = 20;
            }
            fade_state = FADE_IN;
            fade_timer = 0;
        }
        else if(fade_state == FADE_IN) {
            fade_timer++;
            if(fade_timer >= FADE_STEPS) {
                fade_state = FADE_NONE;
                fade_timer = 0;
                fade_action = 0;
            }
        }

        if(state == 0) {
            if(start_now && !prev_start) {
                /* resetRun(): every run-scoped variable back to its
                   startup value, matching state.lua's own resetRun()
                   (called on the title screen's next confirm/start
                   after MODE.TITLE). Harmless -- and a no-op -- on the
                   very first boot, since everything below is already
                   sitting at exactly these values; the only path that
                   actually needs it is looping back here from an
                   ending (ending_mode's a_now handler sets state = 0
                   without touching any of this), where without a
                   reset the "new" run would silently resume with the
                   previous one's party/bag/map/flags still live. */
                map_id = MAP_HOUSE;
                find_mark(MAP_HOUSE, 'P', &col, &row);
                px = col * TILE + TILE / 2;
                py = row * TILE + TILE / 2;
                pdir = 0;
                anim_counter = 0;
                door_lock = 0;
                got_shelf = 0; looted_crate = 0;
                bag.salve = 2; bag.bandage = 2; bag.bitterroot = 1; bag.dust = 1; bag.gem = 0;
                bag.sunbalm = 0; bag.warroot = 0; bag.smokebomb = 1; bag.greatcrystal = 0;
                marks = 16;
                party_n = 0; lead = 0;
                in_battle = 0;
                enc_lock = 8; last_tx = -1; last_ty = -1;
                menu_mode = 0; party_cur = 0; party_detail = 0; bag_cur = 0; heal_item = -1;
                hud_flash[0] = 0; hud_t = 0; map_banner_timer = 0;
                seq_lines = 0; seq_len = 0; seq_beat = 0;
                post_action = POST_NONE; post_soldier_id = 0;
                talked_wren = talked_mae = talked_ivo = talked_nell = 0;
                talked_pike = pike_helped = nell_bonus = 0;
                got_herb = got_gem = got_stump = read_cart = 0;
                beat_calder = beat_mason = beat_shin = cath_caught = 0;
                beat_cathleen = 0; has_scroll = 0; anne2_told = 0;
                beat_wsoldier_cliffs = 0; beat_wsoldier_camp1 = 0;
                beat_wsoldier_camp2 = 0; beat_wsoldier_grove = 0;
                got_chest = 0;
                talked_tessa = 0; talked_birch = 0; talked_sable = 0;
                choice_mode = 0; choice_cur = 0;
                soldier_beaten[0] = soldier_beaten[1] = soldier_beaten[2] = 0;
                mason_state = 0; mason_x = mason_y = 0.0f; mason_dir = 0; mason_anim = 0.0f;
                mason_rematch = 0; mason2_map = -1; mason2_done = 0;
                battles = 0;
                anne_state = 0; anne_x = anne_y = 0.0f; anne_dir = 0; anne_anim = 0.0f;
                anne_gifted = 0;
                soldiers_init = 0;
                shop_open = 0; shop_sell_tab = 0; shop_cur = 0;
                ending_mode = 0; ending_i = 0;

                state = 1;
                /* Seeds the battle RNG from however many vblanks
                   passed while the player sat at the title screen --
                   see rng_next()'s comment for why this stands in for
                   a real RTC/rand() source. */
                rng_state ^= frame_count | 1u;
            }
        }
        else if(menu_mode) {
            /* MODE.BAG/MODE.PARTY update. BAG has its own cursor/use
               logic (up/down picks a row, A uses it); bitterroot/
               dust/gem are battle-only mods with nothing to apply
               outside one, so A just says so, but salve/wrap heal
               *some* CryMon, and outside a battle that's a choice --
               picking either one hands off to the party menu
               (heal_item tracks which item this is, so this isn't
               read as an ordinary party-menu visit) where up/down
               now picks who receives it and A applies it, instead of
               always healing the lead. PARTY still has cycleParty(to)
               otherwise (A sets the lead), plus Y opens a detail view
               for party_cur (attacks, stats, party order) that
               up/down keeps browsing live and B backs out of before
               closing the menu itself. */
            if(menu_mode == 1) {
                if(up_now && !prev_up)
                    bag_cur = (bag_cur - 1 + ITEM_COUNT) % ITEM_COUNT;
                if(down_now && !prev_down)
                    bag_cur = (bag_cur + 1) % ITEM_COUNT;
                if(a_now && !prev_a) {
                    int *count = bag_field(&bag, bag_cur);
                    if(*count <= 0) {
                        int n = s_cat(hud_flash, 0, "NONE LEFT");
                        hud_flash[n] = 0;
                        hud_t = HUD_NOTE_FRAMES;
                    }
                    else if(bag_cur == 0 || bag_cur == 1 || bag_cur == 5) {
                        /* salve, bandage, sunbalm: the only healing
                           items that mean anything outside a battle --
                           hand off to the party menu to pick who gets
                           it. */
                        if(party_n <= 0) {
                            int n = s_cat(hud_flash, 0, "NO CRYMON TO HEAL");
                            hud_flash[n] = 0;
                            hud_t = HUD_NOTE_FRAMES;
                        }
                        else {
                            heal_item = bag_cur;
                            party_cur = lead;
                            party_detail = 0;
                            menu_mode = 2;
                        }
                    }
                    else {
                        int n = s_cat(hud_flash, 0, "ONLY USABLE IN BATTLE");
                        hud_flash[n] = 0;
                        hud_t = HUD_NOTE_FRAMES;
                    }
                }
            }
            else if(menu_mode == 2 && party_n > 0) {
                if(up_now && !prev_up)
                    party_cur = (party_cur - 1 + party_n) % party_n;
                if(down_now && !prev_down)
                    party_cur = (party_cur + 1) % party_n;
                if(heal_item < 0 && y_now && !prev_y)
                    party_detail = !party_detail;
                if(a_now && !prev_a) {
                    if(heal_item >= 0) {
                        int *count = bag_field(&bag, heal_item);
                        int heal = party[party_cur].maxHp - party[party_cur].hp;
                        int cap = (heal_item == 0) ? 22 : (heal_item == 5) ? 40 : 12;
                        int n;
                        if(heal <= 0) {
                            n = s_cat(hud_flash, 0, SPECIES[party[party_cur].species].name);
                            n = s_cat(hud_flash, n, " IS AT FULL HP");
                        }
                        else {
                            if(heal > cap) heal = cap;
                            party[party_cur].hp += heal;
                            (*count)--;
                            n = s_cat(hud_flash, 0, SPECIES[party[party_cur].species].name);
                            n = s_cat(hud_flash, n, " HEALED ");
                            n = s_cat_uint(hud_flash, n, heal);
                            n = s_cat(hud_flash, n, " HP");
                        }
                        hud_flash[n] = 0;
                        hud_t = HUD_NOTE_FRAMES;
                        heal_item = -1;
                        menu_mode = 0;
                    }
                    else if(!party_detail) {
                        if(party[party_cur].hp > 0 && party_cur != lead) {
                            int n;
                            lead = party_cur;
                            n = s_cat(hud_flash, 0, SPECIES[party[lead].species].name);
                            n = s_cat(hud_flash, n, " TAKES THE LEAD");
                            hud_flash[n] = 0;
                            hud_t = HUD_NOTE_FRAMES;
                        }
                    }
                }
            }
            if(b_now && !prev_b) {
                if(menu_mode == 2 && heal_item >= 0) {
                    heal_item = -1;
                    menu_mode = 1;
                }
                else if(menu_mode == 2 && party_detail)
                    party_detail = 0;
                else
                    menu_mode = 0;
            }
            else if(start_now && !prev_start) {
                menu_mode = 0;
                heal_item = -1;
                party_detail = 0;
            }
        }
        else if(in_battle) {
            /* updateBattle(), matching state.lua's bPhase dispatch:
               0 = message (A advances; past the last beat, bAfter
               says what's next), 4 = special-move timing minigame
               (A locks it in), else = a menu (dpad nav, A confirms;
               B backs out of the attack menu to the item menu, only
               there). */
            if(battle.phase == 0) {
                if(a_now && !prev_a) {
                    battle.msg_i++;
                    if(battle.msg_i >= battle.msg_n) {
                        switch(battle.after) {
                            case BAFTER_ITEM:  battle.phase = 1; battle.cur = 0; break;
                            case BAFTER_ATK:   battle.phase = 2; battle.cur = 0; break;
                            case BAFTER_GUARD: battle.phase = 3; battle.cur = 0; break;
                            case BAFTER_WIN: {
                                /* finishWin(): XP is granted regardless
                                   of trainer_kind; marks and what
                                   happens next differ per trainer,
                                   matching the reference's branch
                                   order (Calder, soldier, Mason,
                                   Shinigami, then the generic/wild
                                   tail -- Cathleen included, since a
                                   win here means she was defeated, not
                                   captured; capture ends the battle
                                   earlier via BAFTER_WORLD). */
                                battle_finish_win(&battle, party, lead);

                                if(battle.trainer_kind == TRAINER_CALDER) {
                                    /* No ending cut here at all anymore
                                       -- see MAP_CAMP's section comment.
                                       Beating him just opens VELD's 'F'
                                       door; the camp officer further
                                       south is a taunt, not the game's
                                       ending (that's the Grove/
                                       Shinigami/Anne chain now). */
                                    beat_calder = 1;
                                    marks += 18;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_CALDER_WIN;
                                    seq_len = TALK_LEN(TALK_CALDER_WIN);
                                    seq_beat = 0;
                                    /* Mason's rematch unlocks the
                                       moment Calder's beaten -- rolled
                                       once here, not re-rolled if the
                                       player revisits this map again.
                                       Not gated on beat_mason: his
                                       first ambush is practically
                                       unavoidable (he force-walks up
                                       the moment you leave the house),
                                       so this never actually fires
                                       without it, but nothing here
                                       depends on it having happened
                                       either. */
                                    if(!mason2_done && mason2_map < 0) {
                                        static const int MASON2_MAPS[3] =
                                            { MAP_VELD, MAP_FOREST, MAP_GROVE };
                                        mason2_map = MASON2_MAPS[irand(0, 2)];
                                    }
                                }
                                else if(battle.trainer_kind == TRAINER_WSOLDIER_CLIFFS) {
                                    beat_wsoldier_cliffs = 1;
                                    marks += 12;
                                    battles++;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_WSOLDIER_CLIFFS_WIN;
                                    seq_len = TALK_LEN(TALK_WSOLDIER_CLIFFS_WIN);
                                    seq_beat = 0;
                                    post_action = POST_NONE;
                                }
                                else if(battle.trainer_kind == TRAINER_WSOLDIER_CAMP1) {
                                    beat_wsoldier_camp1 = 1;
                                    marks += 14;
                                    battles++;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_WSOLDIER_CAMP1_WIN;
                                    seq_len = TALK_LEN(TALK_WSOLDIER_CAMP1_WIN);
                                    seq_beat = 0;
                                    post_action = POST_NONE;
                                }
                                else if(battle.trainer_kind == TRAINER_WSOLDIER_CAMP2) {
                                    beat_wsoldier_camp2 = 1;
                                    marks += 15;
                                    battles++;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_WSOLDIER_CAMP2_WIN;
                                    seq_len = TALK_LEN(TALK_WSOLDIER_CAMP2_WIN);
                                    seq_beat = 0;
                                    post_action = POST_NONE;
                                }
                                else if(battle.trainer_kind == TRAINER_WSOLDIER_GROVE) {
                                    beat_wsoldier_grove = 1;
                                    marks += 18;
                                    battles++;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_WSOLDIER_GROVE_WIN;
                                    seq_len = TALK_LEN(TALK_WSOLDIER_GROVE_WIN);
                                    seq_beat = 0;
                                    post_action = POST_NONE;
                                }
                                else if(battle.trainer_kind == TRAINER_SOLDIER) {
                                    soldier_beaten[battle.soldier_id] = 1;
                                    marks += 8;
                                    battles++;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_SOLDIER_AFTER;
                                    seq_len = TALK_LEN(TALK_SOLDIER_AFTER);
                                    seq_beat = 0;
                                }
                                else if(battle.trainer_kind == TRAINER_MASON) {
                                    /* He leaves the instant this
                                       closes, same as the rematch --
                                       no more standing-and-wait step
                                       requiring a second walk-up. */
                                    beat_mason = 1;
                                    marks += 10;
                                    battles++;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_MASON_WIN;
                                    seq_len = TALK_LEN(TALK_MASON_WIN);
                                    seq_beat = 0;
                                    post_action = POST_MASON_LEAVE;
                                }
                                else if(battle.trainer_kind == TRAINER_MASON2) {
                                    /* No standing/re-interact step this
                                       time (unlike the first fight) --
                                       he leaves the instant this
                                       closes, via the same
                                       POST_MASON_LEAVE the first
                                       encounter's re-interact uses. */
                                    mason2_done = 1;
                                    marks += 20;
                                    battles++;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_MASON_WIN2;
                                    seq_len = TALK_LEN(TALK_MASON_WIN2);
                                    seq_beat = 0;
                                    post_action = POST_MASON_LEAVE;
                                }
                                else if(battle.trainer_kind == TRAINER_SHINIGAMI) {
                                    /* No instant cut to an ending
                                       screen anymore -- he hands over
                                       the scroll and vanishes from the
                                       map (see TALK_SHINIGAMI_WIN and
                                       collect_npcs' beat_shin guard on
                                       mark '9'). Anne already told Max
                                       about her father right after
                                       Cathleen, so this dialogue closing
                                       goes straight into the
                                       resurrection choice (POST_OPEN_
                                       CHOICE) instead of ending here. */
                                    beat_shin = 1;
                                    has_scroll = 1;
                                    marks += 14;
                                    battles++;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_SHINIGAMI_WIN;
                                    seq_len = TALK_LEN(TALK_SHINIGAMI_WIN);
                                    seq_beat = 0;
                                    post_action = POST_OPEN_CHOICE;
                                }
                                else {
                                    int n;
                                    marks += 3;
                                    battles++;
                                    if(battle.foe.species == SP_CATHLEEN) {
                                        beat_cathleen = 1;
                                        seq_lines = TALK_CATHLEEN_AFTER;
                                        seq_len = TALK_LEN(TALK_CATHLEEN_AFTER);
                                        seq_beat = 0;
                                        in_battle = 0;
                                        enc_lock = 3;
                                        /* Without this, post_action is
                                           still POST_CATHLEEN from
                                           starting the fight -- closing
                                           this dialogue would silently
                                           re-trigger case POST_CATHLEEN
                                           below and restart the battle
                                           (its own !cath_caught guard
                                           doesn't stop a beaten-not-
                                           captured Cathleen). */
                                        post_action = POST_NONE;
                                    }
                                    else {
                                        /* note(): a HUD toast, not a
                                           clickable battle message --
                                           the battle ends immediately,
                                           same as the reference (which
                                           sets mode="world" before
                                           calling note()). */
                                        n = s_cat(hud_flash, 0, SPECIES[party[lead].species].name);
                                        if(battle.grew) {
                                            n = s_cat(hud_flash, n, " GREW TO LV");
                                            n = s_cat_uint(hud_flash, n, party[lead].lv);
                                        }
                                        else {
                                            n = s_cat(hud_flash, n, " STANDS OVER THE GRASS");
                                        }
                                        hud_flash[n] = 0;
                                        hud_t = HUD_NOTE_FRAMES;
                                        in_battle = 0;
                                        enc_lock = 3;
                                    }
                                }
                                break;
                            }
                            case BAFTER_WIN_NOTE:
                                /* Dead as an actual battle.after target
                                   now (the tail above ends the battle
                                   directly), kept only so the #define
                                   and this switch arm still exist for
                                   anything that might still reference
                                   the enum value. */
                                in_battle = 0;
                                enc_lock = 3;
                                break;
                            case BAFTER_WORLD:
                                /* A successful capture (pickItem's gem
                                   branch) always targets battle.foe --
                                   if it was Cathleen, this is the one
                                   and only place G.cathCaught gets set
                                   (matches the source setting it
                                   inline inside pickItem's gem
                                   branch). */
                                if(battle.foe.species == SP_CATHLEEN)
                                    cath_caught = 1;
                                in_battle = 0;
                                enc_lock = 3;
                                break;
                            case BAFTER_LOSS:
                                /* Every party CryMon is at 0 HP (the
                                   only way battle_pick_guard ever
                                   reaches BAFTER_LOSS -- no living
                                   member left to jump in). Fades to
                                   black, teleports home next to the
                                   bed, fully heals the whole party,
                                   fades back in -- see FADE_ACTION_LOSS
                                   in the draw dispatch below. */
                                in_battle = 0;
                                enc_lock = 3;
                                fade_state = FADE_OUT;
                                fade_timer = 0;
                                fade_action = FADE_ACTION_LOSS;
                                break;
                            default:
                                break;
                        }
                    }
                }
            }
            else if(battle.phase == 4) {
                /* mg bounces 0-100 at ~110 units/sec, matching
                   engine.ts's per-frame update at our fixed ~60fps
                   vblank rate (no real dt in this bare-metal loop). */
                battle.mg += (float)battle.mg_dir * (110.0f / 60.0f);
                if(battle.mg > 100.0f) { battle.mg = 100.0f; battle.mg_dir = -1; }
                if(battle.mg < 0.0f)   { battle.mg = 0.0f;    battle.mg_dir = 1; }
                if(a_now && !prev_a) {
                    battle_pick_special(&battle);
                    party[lead] = battle.pl;
                }
            }
            else {
                int n_rows = (battle.phase == 1) ? battle_item_menu_count(&bag)
                             : (battle.phase == 2 && SPECIES[battle.pl.species].spells_n > 0)
                                 ? SPECIES[battle.pl.species].spells_n
                             : (battle.phase == 2 && battle.pl.shiny) ? 4 : 3;

                if(up_now && !prev_up)
                    battle.cur = (battle.cur - 1 + n_rows) % n_rows;
                if(down_now && !prev_down)
                    battle.cur = (battle.cur + 1) % n_rows;

                if(battle.phase == 2 && b_now && !prev_b) {
                    battle.phase = 1;
                    battle.cur = 0;
                }

                if(a_now && !prev_a) {
                    if(battle.phase == 1) {
                        int kind = battle_item_menu_kind(&bag, battle.cur);
                        battle_pick_item(&battle, &bag, kind, party, &party_n, lead);
                    }
                    else if(battle.phase == 2 && SPECIES[battle.pl.species].spells_n > 0) {
                        /* pickAttack()'s spell branch: every row casts
                           directly, no minigame, no guard/wait row at
                           all -- a Cathleen lead's attack menu is only
                           ever her 4 spells (see draw_battle_atk_menu). */
                        int spell_id = SPECIES[battle.pl.species].spells[battle.cur];
                        if(battle_pick_spell(&battle, spell_id))
                            party[lead] = battle.pl;
                        else {
                            int n = s_cat(battle.msg[0], 0, "MANA SURGE IS SPENT");
                            battle.msg[0][n] = 0;
                            battle.msg_n = 1;
                            battle.msg_i = 0;
                            battle.phase = 0;
                            battle.after = BAFTER_ATK;
                        }
                    }
                    else if(battle.phase == 2) {
                        if(battle.cur == 0) {
                            battle_pick_basic(&battle);
                            party[lead] = battle.pl;
                        }
                        else if(battle.cur == 1) {
                            if(battle.pl.spp <= 0) {
                                int n = s_cat(battle.msg[0], 0, SPECIES[battle.pl.species].special);
                                n = s_cat(battle.msg[0], n, " IS SPENT");
                                battle.msg[0][n] = 0;
                                battle.msg_n = 1;
                                battle.msg_i = 0;
                                battle.phase = 0;
                                battle.after = BAFTER_ATK;
                            }
                            else {
                                battle.pl.spp--;
                                party[lead] = battle.pl;
                                battle.mg = 8.0f;
                                battle.mg_dir = 1;
                                battle.phase = 4;
                            }
                        }
                        else if(battle.pl.shiny && battle.cur == 2) {
                            battle_pick_toxic(&battle);
                            party[lead] = battle.pl;
                        }
                        else {
                            int n = s_cat(battle.msg[0], 0, "MAX HOLDS");
                            battle.msg[0][n] = 0;
                            battle.msg_n = 1;
                            battle.msg_i = 0;
                            battle.phase = 0;
                            battle.after = BAFTER_GUARD;
                        }
                    }
                    else {
                        battle_pick_guard(&battle, battle.cur, party, party_n, &lead);
                    }
                }
            }
        }
        else if(shop_open) {
            /* updateShop(): tab toggle (left/right), row cursor
               (up/down), confirm buys/sells, cancel/start/select all
               close (B stands in for select here, same as the bag/
               party menus). */
            int rows[ITEM_COUNT];
            int n_rows = shop_rows(&bag, shop_sell_tab, rows);

            if((b_now && !prev_b) || (start_now && !prev_start)) {
                shop_open = 0;
            }
            else {
                if(up_now && !prev_up && n_rows > 0)
                    shop_cur = (shop_cur - 1 + n_rows) % n_rows;
                if(down_now && !prev_down && n_rows > 0)
                    shop_cur = (shop_cur + 1) % n_rows;
                if((left_now && !prev_left) || (right_now && !prev_right)) {
                    shop_sell_tab = !shop_sell_tab;
                    shop_cur = 0;
                }
                if(a_now && !prev_a && n_rows > 0) {
                    int idx = rows[shop_cur];
                    if(!shop_sell_tab) {
                        int cost = ITEMS[idx].buy;
                        if(marks >= cost) {
                            marks -= cost;
                            (*bag_field(&bag, idx))++;
                        }
                    }
                    else {
                        int *owned = bag_field(&bag, idx);
                        if(*owned > 0) {
                            (*owned)--;
                            marks += ITEMS[idx].sell;
                            if(*owned == 0) shop_cur = 0;
                        }
                    }
                }
            }
        }
        else if(choice_mode) {
            /* draw_choice()'s input: up/down between the 2 rows, A
               locks it in -- no B, this choice doesn't have a "never
               mind" (matches "present the player with a choice", not
               an optional detour). Resolution beat picked by
               choice_cur, then POST_ENDING_FINAL takes it to the
               single ending regardless of which was picked. */
            if(up_now && !prev_up) choice_cur = 1 - choice_cur;
            if(down_now && !prev_down) choice_cur = 1 - choice_cur;
            if(a_now && !prev_a) {
                choice_mode = 0;
                if(choice_cur == 0) {
                    seq_lines = TALK_CHOICE_FATHER;
                    seq_len = TALK_LEN(TALK_CHOICE_FATHER);
                }
                else {
                    seq_lines = TALK_CHOICE_HEAVENFALL;
                    seq_len = TALK_LEN(TALK_CHOICE_HEAVENFALL);
                }
                seq_beat = 0;
                post_action = POST_ENDING_FINAL;
            }
        }
        else if(ending_mode) {
            /* drawDemoEnd(): step through data.DEMO_END on A, return
               to the title screen after the last line (state.lua
               returns to MODE.TITLE, which resetRun()s on the next
               confirm/start -- ported above, in the state == 0
               branch's start_now handler). */
            if(a_now && !prev_a) {
                ending_i++;
                {
                    int n = TALK_LEN(DEMO_END);
                    if(ending_i >= n) {
                        ending_mode = 0;
                        state = 0;
                    }
                }
            }
        }
        else {
            /* Movement is frozen while a dialogue sequence is active,
               matching state.lua's MODE.TALK (movement there is only
               processed in MODE.WALK). */
            if(door_lock > 0)
                door_lock--;

            /* HUD toast countdown/dismiss: ticks down every world
               frame regardless (matches update()'s unconditional
               `if (hudT > 0) hudT -= dt`), and A/B dismiss it early,
               same as the reference's confirm()/cancel() check.
               hud_dismissed_now suppresses the interact-chain further
               down for this same frame -- otherwise the very A press
               that dismisses the toast would immediately fall through
               into a fresh interact/dialogue-advance check, the same
               same-frame-reactivation bug the Mason ambush comment
               elsewhere in this file describes. */
            int hud_dismissed_now = 0;
            if(hud_t > 0) {
                hud_t--;
                if((a_now && !prev_a) || (b_now && !prev_b)) {
                    hud_t = 0;
                    hud_dismissed_now = 1;
                }
            }

            /* maybeStartAnne(): battlesDone>=1 while on VELD, gated
               on not already talking (matches its !talking() check
               closely enough). Spawns her at the player's own spot
               plus a fixed offset, exactly like spawnRival below,
               so she starts walking in from off to one side rather
               than appearing at a fixed VELD landmark.

               Her second approach is no longer tied to VELD at all --
               she meets Max immediately after the Cathleen fight,
               wherever that leaves her (the GROVE), rather than
               waiting for a trip back to town. Gated on
               (beat_cathleen || cath_caught) instead of beat_shin. */
            if(anne_state == 0 && !seq_lines &&
               ((!anne_gifted && battles >= 1 && map_id == MAP_VELD) ||
                (anne_gifted && (beat_cathleen || cath_caught) && !anne2_told))) {
                anne_state = 1;
                anne_x = (float)px;
                anne_y = (float)py + 45.0f; /* 72 * 0.625 */
                anne_dir = 1; /* up */
                anne_anim = 0.0f;
            }

            /* Mason's rematch: fires the instant the player sets foot
               on mason2_map (rolled once, back at the Calder win --
               see there), reusing the exact same mason_state machine
               as his first ambush (approach -> ambush -> standing ->
               leave), just with mason_rematch=1 so the ambush trigger
               further down picks TALK_MASON_FIGHT2/POST_MASON2's
               3-CryMon loadout instead. Guarded on mason_state == 0
               so it can't retrigger while he's already approaching/
               standing/leaving from this same rematch. */
            if(mason2_map >= 0 && !mason2_done && mason_state == 0 &&
               map_id == mason2_map && !seq_lines) {
                mason_state = 1;
                mason_rematch = 1;
                mason_x = (float)px;
                mason_y = (float)py + 100.0f;
                mason_dir = 1; /* up */
                mason_anim = 0.0f;
            }

            /* ensureSoldiers(): lazily place the 3 FOREST soldiers at
               their patrol-origin marks the first time the map is
               entered, matching engine.ts's own lazy build. */
            if(!soldiers_init && map_id == MAP_FOREST) {
                int i;
                for(i = 0; i < 3; i++) {
                    int sx, sy;
                    mark_center(MAP_FOREST, SOLDIERS[i].mark, &sx, &sy);
                    soldiers[i].x = (float)sx;
                    soldiers[i].y = (float)sy;
                    soldiers[i].anim = 0.0f;
                    soldiers[i].chase = 0;
                }
                /* Patrol (soldier 0): x-axis, +-90/-10px around spawn
                   (144/16 * 0.625). Scout (soldier 1): y-axis, +-50px
                   (80 * 0.625). Sentry (soldier 2): stationary,
                   facing up -- matches ensureSoldiers()'s 3 entries. */
                soldiers[0].dir = 3; soldiers[0].axis = 0; soldiers[0].sign = 1;
                soldiers[0].minv = soldiers[0].x - 10.0f; soldiers[0].maxv = soldiers[0].x + 90.0f;
                soldiers[1].dir = 2; soldiers[1].axis = 1; soldiers[1].sign = -1;
                soldiers[1].minv = soldiers[1].y - 50.0f; soldiers[1].maxv = soldiers[1].y + 50.0f;
                soldiers[2].dir = 1; soldiers[2].axis = 2; soldiers[2].sign = 0;
                soldiers[2].minv = soldiers[2].maxv = 0.0f;
                soldiers_init = 1;
            }

            /* Mason/Anne approach: force-walk toward the player,
               freezing all other world movement/interaction until
               they either reach the player (ambush) or the map
               changes out from under them. Matches engine.ts's own
               early-return while rival.phase/anne.phase === "approach". */
            if(mason_state == 1) {
                float dx = (float)px - mason_x, dy = (float)py - mason_y;
                float dist = f_sqrt(dx * dx + dy * dy);
                if(dist < ACTOR_REACH_DIST) {
                    mason_state = 2;
                    if(mason_rematch) {
                        seq_lines = TALK_MASON_FIGHT2;
                        seq_len = TALK_LEN(TALK_MASON_FIGHT2);
                        post_action = POST_MASON2;
                    }
                    else {
                        seq_lines = TALK_MASON_FIGHT;
                        seq_len = TALK_LEN(TALK_MASON_FIGHT);
                        post_action = POST_MASON;
                    }
                    seq_beat = 0;
                }
                else {
                    mason_x += dx / dist * ACTOR_SPD_APPROACH;
                    mason_y += dy / dist * ACTOR_SPD_APPROACH;
                    mason_dir = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                                    ? (dx < 0 ? 2 : 3) : (dy < 0 ? 1 : 0);
                    mason_anim += 8.0f / 60.0f;
                }
            }
            else if(anne_state == 1) {
                float dx = (float)px - anne_x, dy = (float)py - anne_y;
                float dist = f_sqrt(dx * dx + dy * dy);
                if(dist < ACTOR_REACH_DIST) {
                    anne_state = 2;
                    if(!anne_gifted) {
                        anne_gifted = 1;
                        bag.gem += 5;
                        seq_lines = TALK_ANNE_GIFT;
                        seq_len = TALK_LEN(TALK_ANNE_GIFT);
                        seq_beat = 0;
                        post_action = POST_ANNE_LEAVE;
                    }
                    else {
                        /* Second approach, right after the Cathleen
                           fight (not gated on returning to VELD): the
                           father-died reveal. She just walks off after
                           this one, same as her first visit -- the
                           resurrection choice itself doesn't open
                           until Shinigami actually hands over the
                           scroll (POST_OPEN_CHOICE, see
                           TRAINER_SHINIGAMI's win branch). */
                        anne2_told = 1;
                        seq_lines = TALK_ANNE_RETURN;
                        seq_len = TALK_LEN(TALK_ANNE_RETURN);
                        seq_beat = 0;
                        post_action = POST_ANNE_LEAVE;
                    }
                }
                else {
                    anne_x += dx / dist * ACTOR_SPD_APPROACH;
                    anne_y += dy / dist * ACTOR_SPD_APPROACH;
                    anne_dir = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                                   ? (dx < 0 ? 2 : 3) : (dy < 0 ? 1 : 0);
                    anne_anim += 8.0f / 60.0f;
                }
            }

            /* Mason/Anne leaving: walk straight down off VELD, only
               while no dialogue box is up (matches engine.ts's
               talking()/hudT early-returns sitting ahead of these two
               blocks in updateWorld()). */
            if(mason_state == 3 && !seq_lines) {
                mason_y += ACTOR_SPD_LEAVE;
                mason_dir = 0;
                mason_anim += 8.0f / 60.0f;
                if(mason_y > (float)py + 150.0f) mason_state = 0;
            }
            if(anne_state == 3 && !seq_lines) {
                anne_y += ACTOR_SPD_LEAVE;
                anne_dir = 0;
                anne_anim += 8.0f / 60.0f;
                if(anne_y > (float)py + 150.0f) anne_state = 0;
            }

            /* Soldiers: patrol their axis, chase on line-of-sight,
               ambush like Mason once they catch up. Matches
               updateSoldiers()/soldierLos(). */
            if(map_id == MAP_FOREST && !seq_lines) {
                int i;
                for(i = 0; i < 3; i++) {
                    Soldier *s = &soldiers[i];
                    if(soldier_beaten[i]) continue;
                    if(s->chase) {
                        float dx = (float)px - s->x, dy = (float)py - s->y;
                        float dist = f_sqrt(dx * dx + dy * dy);
                        if(dist < ACTOR_CHASE_CATCH) {
                            s->chase = 0;
                            seq_lines = TALK_SOLDIER_SPOT;
                            seq_len = TALK_LEN(TALK_SOLDIER_SPOT);
                            seq_beat = 0;
                            post_action = POST_SOLDIER;
                            post_soldier_id = i;
                            break;
                        }
                        s->x += dx / dist * ACTOR_SPD_CHASE;
                        s->y += dy / dist * ACTOR_SPD_CHASE;
                        s->dir = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                                     ? (dx < 0 ? 2 : 3) : (dy < 0 ? 1 : 0);
                        s->anim += 8.0f / 60.0f;
                        continue;
                    }
                    if(s->axis == 0) {
                        s->x += s->sign * ACTOR_SPD_PATROL;
                        if(s->x > s->maxv) { s->x = s->maxv; s->sign = -1; s->dir = 2; }
                        else if(s->x < s->minv) { s->x = s->minv; s->sign = 1; s->dir = 3; }
                        s->anim += 4.0f / 60.0f;
                    }
                    else if(s->axis == 1) {
                        s->y += s->sign * ACTOR_SPD_PATROL;
                        if(s->y > s->maxv) { s->y = s->maxv; s->sign = -1; s->dir = 1; }
                        else if(s->y < s->minv) { s->y = s->minv; s->sign = 1; s->dir = 0; }
                        s->anim += 4.0f / 60.0f;
                    }
                    if(soldier_los(map_id, s->x, s->y, s->dir, px, py))
                        s->chase = 1;
                }
            }

            if(!seq_lines && hud_t <= 0 && fade_state == FADE_NONE && mason_state != 1 && anne_state != 1) {
                int dx = 0, dy = 0;
                int map_w = MAPS[map_id].cols * TILE;
                int map_h = MAPS[map_id].rows_n * TILE;

                if(pressed(raw, CONT_DPAD_LEFT))  { dx = -1; pdir = 2; }
                if(pressed(raw, CONT_DPAD_RIGHT)) { dx = 1;  pdir = 3; }
                if(pressed(raw, CONT_DPAD_UP))    { dy = -1; pdir = 1; }
                if(pressed(raw, CONT_DPAD_DOWN))  { dy = 1;  pdir = 0; }

                if(dx == 0 && dy == 0) {
                    anim_counter = 0;
                }
                else {
                    anim_counter++;
                }

                if(dx != 0 || dy != 0) {
                    /* Axis-separated movement so the player slides
                       along walls instead of stopping dead on a
                       diagonal. Half the collision box (6px) is
                       checked at the candidate feet position. */
                    int speed = 1; /* px/frame; ~60px/sec at 60fps,
                                       scaled down from the original's
                                       110px/sec at 32px tiles for our
                                       smaller tiles */
                    int nx = px + dx * speed;
                    int ny = py + dy * speed;

                    /* hitActor(): a live NPC blocks movement like a
                       solid tile (see actor_blocks() above). */
                    if(dx != 0 && !tile_blocked(map_id, tile_at(map_id, (nx + (dx > 0 ? 6 : -6)) / TILE,
                                                                 py / TILE), cath_caught || beat_cathleen, beat_calder, beat_shin) &&
                       !actor_blocks(map_id, nx, py, mason_state, mason_x, mason_y,
                                     anne_state, anne_x, anne_y, soldiers, soldier_beaten)) {
                        px = nx;
                    }
                    if(dy != 0 && !tile_blocked(map_id, tile_at(map_id, px / TILE,
                                                                 (ny + (dy > 0 ? 6 : -6)) / TILE), cath_caught || beat_cathleen, beat_calder, beat_shin) &&
                       !actor_blocks(map_id, px, ny, mason_state, mason_x, mason_y,
                                     anne_state, anne_x, anne_y, soldiers, soldier_beaten)) {
                        py = ny;
                    }

                    if(px < 8) px = 8;
                    if(px > map_w - 8) px = map_w - 8;
                    if(py < 8) py = 8;
                    if(py > map_h - 4) py = map_h - 4;

                    if(try_encounter(map_id, px, py, party_n, &enc_lock,
                                      &last_tx, &last_ty, &battle)) {
                        battle.pl = party[lead];
                        in_battle = 1;
                    }
                }

                /* Door/warp tiles, matching state.lua's chain of
                   mapId/tile checks (doorLock gates it, same as the
                   reference). GROVE has no exit warp of its own in
                   this step -- data.lua's GROVE only defines the 'O'
                   entrance shared with FOREST, and the far side (past
                   Shinigami) is battle-gated content not built yet. */
                if(door_lock <= 0) {
                    char here = tile_at(map_id, px / TILE, py / TILE);

                    if(map_id == MAP_HOUSE && here == 'D') {
                        if(!got_shelf) {
                            find_mark(MAP_HOUSE, 'D', &col, &row);
                            py = row * TILE + TILE / 2 - TILE;
                            pdir = 1;
                            door_lock = 20;
                            seq_lines = TALK_DOOR_LOCKED;
                            seq_len = TALK_LEN(TALK_DOOR_LOCKED);
                            seq_beat = 0;
                        }
                        else {
                            do_warp(&map_id, &px, &py, &pdir, MAP_VELD, 'D', 1, &map_banner_timer);
                            door_lock = 20;
                            /* spawnRival()/footsteps: Mason starts
                               south of the player's new VELD position
                               and force-walks up to ambush them
                               (mason_state 1 == "approach") -- their
                               farewell conversation (TALK_MASON_FIGHT)
                               happens once he reaches her, not here.
                               Guarded on !beat_mason so re-using this
                               door after he's already been fought (and
                               left) doesn't respawn him. */
                            if(mason_state == 0 && !beat_mason) {
                                mason_state = 1;
                                mason_x = (float)px;
                                mason_y = (float)py + 100.0f; /* 160 * 0.625 */
                                mason_dir = 1; /* up */
                                mason_anim = 0.0f;
                            }
                        }
                    }
                    else if(map_id == MAP_VELD && here == 'D') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_HOUSE, 'D', 0, &map_banner_timer);
                        door_lock = 20;
                    }
                    else if(map_id == MAP_VELD && here == 'Z') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_FOREST, 'Y', 1, &map_banner_timer);
                        door_lock = 20;
                    }
                    else if(map_id == MAP_FOREST && here == 'Y') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_VELD, 'Z', 0, &map_banner_timer);
                        door_lock = 20;
                    }
                    else if(map_id == MAP_FOREST && here == 'O') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_GROVE, 'O', 1, &map_banner_timer);
                        door_lock = 20;
                    }
                    else if(map_id == MAP_GROVE && here == 'O') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_FOREST, 'O', 0, &map_banner_timer);
                        door_lock = 20;
                    }
                    else if(map_id == MAP_VELD && here == 'F') {
                        /* Gated on beat_calder by tile_blocked() above
                           -- this tile is only walkable, and so only
                           reachable, once he's out of the way. */
                        do_warp(&map_id, &px, &py, &pdir, MAP_CAMP, 'D', 1, &map_banner_timer);
                        door_lock = 20;
                    }
                    else if(map_id == MAP_CAMP && here == 'D') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_VELD, 'F', 0, &map_banner_timer);
                        door_lock = 20;
                    }
                    else if(map_id == MAP_VELD && here == 'c') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_CLIFFS, 'D', 1, &map_banner_timer);
                        door_lock = 20;
                    }
                    else if(map_id == MAP_CLIFFS && here == 'D') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_VELD, 'c', 0, &map_banner_timer);
                        door_lock = 20;
                    }
                    else if(map_id == MAP_GROVE && here == 'g') {
                        /* Gated on beat_shin by tile_blocked() above. */
                        do_warp(&map_id, &px, &py, &pdir, MAP_RUINS, 'D', 1, &map_banner_timer);
                        door_lock = 20;
                    }
                    else if(map_id == MAP_RUINS && here == 'D') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_GROVE, 'g', 0, &map_banner_timer);
                        door_lock = 20;
                    }
                }
            }

            if(a_now && !prev_a && !hud_dismissed_now && fade_state == FADE_NONE) {
                if(seq_lines) {
                    /* Advance to the next beat; close the box (and
                       fire any queued post_action -- beginTalkEnd())
                       after the last one. */
                    seq_beat++;
                    if(seq_beat >= seq_len) {
                        seq_lines = 0;
                        seq_len = 0;
                        seq_beat = 0;

                        /* startBattle()'s leader-nil guard applies to
                           every battle-starting post_action, but not
                           to opening the shop (beginTalkEnd's a==9
                           branch has no such check). */
                        if(party_n > 0 || post_action == POST_SHOP ||
                           post_action == POST_MASON_LEAVE || post_action == POST_ANNE_LEAVE ||
                           post_action == POST_OPEN_CHOICE || post_action == POST_ENDING_FINAL ||
                           post_action == POST_BED_HEAL) {
                            switch(post_action) {
                                case POST_CALDER:
                                    battle.foe = mint_monster(SP_RAZORBAT, 4);
                                    battle.wild = 0;
                                    battle.trainer_kind = TRAINER_CALDER;
                                    battle.phase = 0;
                                    { int n = s_cat(battle.msg[0], 0, "CALDER SENDS RAZORBAT");
                                      battle.msg[0][n] = 0; }
                                    battle.msg_n = 1; battle.msg_i = 0; battle.after = BAFTER_ITEM;
                                    battle.cur = 0;
                                    battle.mods_self_str = battle.mods_self_agl = battle.mods_self_spc = 0;
                                    battle.mods_foe_str = battle.mods_foe_agl = battle.mods_foe_spc = 0;
                                    battle.pl_poisoned = battle.foe_poisoned = 0;
                                    battle.bench_n = 0;
                                    battle.grew = 0;
                                    battle.pl = party[lead];
                                    in_battle = 1;
                                    break;
                                case POST_MASON:
                                    battle.foe = mint_monster(SP_GLIMMOTH, 3);
                                    battle.wild = 0;
                                    battle.trainer_kind = TRAINER_MASON;
                                    battle.phase = 0;
                                    { int n = s_cat(battle.msg[0], 0, "MASON SENDS GLIMMOTH");
                                      battle.msg[0][n] = 0; }
                                    battle.msg_n = 1; battle.msg_i = 0; battle.after = BAFTER_ITEM;
                                    battle.cur = 0;
                                    battle.mods_self_str = battle.mods_self_agl = battle.mods_self_spc = 0;
                                    battle.mods_foe_str = battle.mods_foe_agl = battle.mods_foe_spc = 0;
                                    battle.pl_poisoned = battle.foe_poisoned = 0;
                                    battle.bench_n = 0;
                                    battle.grew = 0;
                                    battle.pl = party[lead];
                                    in_battle = 1;
                                    break;
                                case POST_MASON2:
                                    /* Rematch: 3 CryMon back to back
                                       (Shinigami's bench mechanic,
                                       reused), all higher level than
                                       the lv3 Glimmoth he opened with
                                       the first time. */
                                    battle.foe = mint_monster(SP_GLIMMOTH, 6);
                                    battle.wild = 0;
                                    battle.trainer_kind = TRAINER_MASON2;
                                    battle.phase = 0;
                                    { int n = s_cat(battle.msg[0], 0, "MASON SENDS GLIMMOTH");
                                      battle.msg[0][n] = 0; }
                                    battle.msg_n = 1; battle.msg_i = 0; battle.after = BAFTER_ITEM;
                                    battle.cur = 0;
                                    battle.mods_self_str = battle.mods_self_agl = battle.mods_self_spc = 0;
                                    battle.mods_foe_str = battle.mods_foe_agl = battle.mods_foe_spc = 0;
                                    battle.pl_poisoned = battle.foe_poisoned = 0;
                                    battle.bench[0] = mint_monster(SP_BRIARFOX, 7);
                                    battle.bench[1] = mint_monster(SP_DUSKHORN, 8);
                                    battle.bench_n = 2;
                                    battle.grew = 0;
                                    battle.pl = party[lead];
                                    in_battle = 1;
                                    break;
                                case POST_WSOLDIER_CLIFFS:
                                    battle.foe = mint_monster(SP_EMBERLING, 5);
                                    battle.wild = 0;
                                    battle.trainer_kind = TRAINER_WSOLDIER_CLIFFS;
                                    battle.phase = 0;
                                    { int n = s_cat(battle.msg[0], 0, "SENTRY SENDS EMBERLING");
                                      battle.msg[0][n] = 0; }
                                    battle.msg_n = 1; battle.msg_i = 0; battle.after = BAFTER_ITEM;
                                    battle.cur = 0;
                                    battle.mods_self_str = battle.mods_self_agl = battle.mods_self_spc = 0;
                                    battle.mods_foe_str = battle.mods_foe_agl = battle.mods_foe_spc = 0;
                                    battle.pl_poisoned = battle.foe_poisoned = 0;
                                    battle.bench[0] = mint_monster(SP_FROSTAIL, 6);
                                    battle.bench[1] = mint_monster(SP_STORMWING, 7);
                                    battle.bench_n = 2;
                                    battle.grew = 0;
                                    battle.pl = party[lead];
                                    in_battle = 1;
                                    break;
                                case POST_WSOLDIER_CAMP1:
                                    battle.foe = mint_monster(SP_SABLECLAW, 7);
                                    battle.wild = 0;
                                    battle.trainer_kind = TRAINER_WSOLDIER_CAMP1;
                                    battle.phase = 0;
                                    { int n = s_cat(battle.msg[0], 0, "CONSCRIPT SENDS SABLECLAW");
                                      battle.msg[0][n] = 0; }
                                    battle.msg_n = 1; battle.msg_i = 0; battle.after = BAFTER_ITEM;
                                    battle.cur = 0;
                                    battle.mods_self_str = battle.mods_self_agl = battle.mods_self_spc = 0;
                                    battle.mods_foe_str = battle.mods_foe_agl = battle.mods_foe_spc = 0;
                                    battle.pl_poisoned = battle.foe_poisoned = 0;
                                    battle.bench[0] = mint_monster(SP_BOULDERAM, 8);
                                    battle.bench[1] = mint_monster(SP_ASHENMAW, 9);
                                    battle.bench_n = 2;
                                    battle.grew = 0;
                                    battle.pl = party[lead];
                                    in_battle = 1;
                                    break;
                                case POST_WSOLDIER_CAMP2:
                                    battle.foe = mint_monster(SP_THORNHIDE, 8);
                                    battle.wild = 0;
                                    battle.trainer_kind = TRAINER_WSOLDIER_CAMP2;
                                    battle.phase = 0;
                                    { int n = s_cat(battle.msg[0], 0, "ENFORCER SENDS THORNHIDE");
                                      battle.msg[0][n] = 0; }
                                    battle.msg_n = 1; battle.msg_i = 0; battle.after = BAFTER_ITEM;
                                    battle.cur = 0;
                                    battle.mods_self_str = battle.mods_self_agl = battle.mods_self_spc = 0;
                                    battle.mods_foe_str = battle.mods_foe_agl = battle.mods_foe_spc = 0;
                                    battle.pl_poisoned = battle.foe_poisoned = 0;
                                    battle.bench[0] = mint_monster(SP_GLASSWISP, 9);
                                    battle.bench[1] = mint_monster(SP_DUSKHORN, 10);
                                    battle.bench_n = 2;
                                    battle.grew = 0;
                                    battle.pl = party[lead];
                                    in_battle = 1;
                                    break;
                                case POST_WSOLDIER_GROVE:
                                    battle.foe = mint_monster(SP_CRYMARE, 9);
                                    battle.wild = 0;
                                    battle.trainer_kind = TRAINER_WSOLDIER_GROVE;
                                    battle.phase = 0;
                                    { int n = s_cat(battle.msg[0], 0, "WARDEN SENDS CRYMARE");
                                      battle.msg[0][n] = 0; }
                                    battle.msg_n = 1; battle.msg_i = 0; battle.after = BAFTER_ITEM;
                                    battle.cur = 0;
                                    battle.mods_self_str = battle.mods_self_agl = battle.mods_self_spc = 0;
                                    battle.mods_foe_str = battle.mods_foe_agl = battle.mods_foe_spc = 0;
                                    battle.pl_poisoned = battle.foe_poisoned = 0;
                                    battle.bench[0] = mint_monster(SP_ASHENMAW, 10);
                                    battle.bench[1] = mint_monster(SP_BOULDERAM, 11);
                                    battle.bench_n = 2;
                                    battle.grew = 0;
                                    battle.pl = party[lead];
                                    in_battle = 1;
                                    break;
                                case POST_SHINIGAMI:
                                    battle.foe = mint_monster(SP_CRYMARE, 5);
                                    battle.wild = 0;
                                    battle.trainer_kind = TRAINER_SHINIGAMI;
                                    battle.phase = 0;
                                    { int n = s_cat(battle.msg[0], 0, "SHINIGAMI SENDS CRYMARE");
                                      battle.msg[0][n] = 0; }
                                    battle.msg_n = 1; battle.msg_i = 0; battle.after = BAFTER_ITEM;
                                    battle.cur = 0;
                                    battle.mods_self_str = battle.mods_self_agl = battle.mods_self_spc = 0;
                                    battle.mods_foe_str = battle.mods_foe_agl = battle.mods_foe_spc = 0;
                                    battle.pl_poisoned = battle.foe_poisoned = 0;
                                    battle.bench[0] = mint_monster(SP_CRYMARE, 6);
                                    battle.bench[1] = mint_monster(SP_CRYMARE, 7);
                                    battle.bench_n = 2;
                                    battle.grew = 0;
                                    battle.pl = party[lead];
                                    in_battle = 1;
                                    break;
                                case POST_SOLDIER: {
                                    const SoldierDef *sd = &SOLDIERS[post_soldier_id];
                                    int n;
                                    battle.foe = mint_monster(sd->species, sd->lv);
                                    battle.wild = 0;
                                    battle.trainer_kind = TRAINER_SOLDIER;
                                    battle.soldier_id = post_soldier_id;
                                    battle.phase = 0;
                                    n = s_cat(battle.msg[0], 0, sd->name);
                                    n = s_cat(battle.msg[0], n, " SENDS ");
                                    n = s_cat(battle.msg[0], n, SPECIES[sd->species].name);
                                    battle.msg[0][n] = 0;
                                    battle.msg_n = 1; battle.msg_i = 0; battle.after = BAFTER_ITEM;
                                    battle.cur = 0;
                                    battle.mods_self_str = battle.mods_self_agl = battle.mods_self_spc = 0;
                                    battle.mods_foe_str = battle.mods_foe_agl = battle.mods_foe_spc = 0;
                                    battle.pl_poisoned = battle.foe_poisoned = 0;
                                    battle.bench_n = 0;
                                    battle.grew = 0;
                                    battle.pl = party[lead];
                                    in_battle = 1;
                                    break;
                                }
                                case POST_CATHLEEN:
                                    if(!cath_caught) {
                                        int n;
                                        battle.foe = mint_monster(SP_CATHLEEN, 6);
                                        battle.wild = 1;
                                        battle.trainer_kind = TRAINER_WILD;
                                        battle.phase = 0;
                                        n = s_cat(battle.msg[0], 0, "CATHLEEN STANDS AGAINST YOU");
                                        battle.msg[0][n] = 0;
                                        battle.msg_n = 1; battle.msg_i = 0; battle.after = BAFTER_ITEM;
                                        battle.cur = 0;
                                        battle.mods_self_str = battle.mods_self_agl = battle.mods_self_spc = 0;
                                        battle.mods_foe_str = battle.mods_foe_agl = battle.mods_foe_spc = 0;
                                    battle.pl_poisoned = battle.foe_poisoned = 0;
                                        battle.bench_n = 0;
                                        battle.grew = 0;
                                        battle.pl = party[lead];
                                        in_battle = 1;
                                    }
                                    break;
                                case POST_SHOP:
                                    shop_open = 1;
                                    shop_sell_tab = 0;
                                    shop_cur = 0;
                                    break;
                                case POST_MASON_LEAVE:
                                    /* startRivalLeave(). */
                                    mason_state = 3;
                                    mason_dir = 0;
                                    mason_anim = 0.0f;
                                    break;
                                case POST_ANNE_LEAVE:
                                    /* startAnneLeave(). anne_state is
                                       already 2 from the gift-reach
                                       trigger above; this just kicks
                                       off the actual walk-away once
                                       her gift dialogue has closed. */
                                    anne_state = 3;
                                    anne_dir = 0;
                                    anne_anim = 0.0f;
                                    break;
                                case POST_OPEN_CHOICE:
                                    /* Opens draw_choice() once
                                       TALK_SHINIGAMI_WIN closes -- Anne
                                       already delivered the reveal
                                       right after Cathleen, so this is
                                       just the scroll/choice payoff,
                                       no walk-up needed. */
                                    choice_mode = 1;
                                    choice_cur = 0;
                                    break;
                                case POST_ENDING_FINAL:
                                    ending_mode = 1;
                                    ending_i = 0;
                                    break;
                                case POST_BED_HEAL:
                                    fade_state = FADE_OUT;
                                    fade_timer = 0;
                                    fade_action = FADE_ACTION_BED;
                                    break;
                                default:
                                    break;
                            }
                        }
                        post_action = POST_NONE;
                    }
                }
                else if(map_id == MAP_HOUSE) {
                    /* interact(): state.lua's MAP_HOUSE branch,
                       verbatim -- U always shows TALK.bed; B shows
                       TALK.father the first time and TALK.fatherAfter
                       once the shelf is looted; S grants Quillpup
                       (tracked as got_shelf, no party system exists
                       yet to hold it) the first time and shows
                       TALK.shelfEmpty after; C grants a bandage the
                       first time and shows TALK.crateEmpty after. */
                    char mark = closest_mark(px, py);

                    switch(mark) {
                        case 'U':
                            seq_lines = TALK_BED;
                            seq_len = TALK_LEN(TALK_BED);
                            post_action = POST_BED_HEAL;
                            break;
                        case 'B':
                            if(got_shelf) {
                                seq_lines = TALK_FATHER_AFTER;
                                seq_len = TALK_LEN(TALK_FATHER_AFTER);
                            }
                            else {
                                seq_lines = TALK_FATHER;
                                seq_len = TALK_LEN(TALK_FATHER);
                            }
                            break;
                        case 'S':
                            if(!got_shelf) {
                                got_shelf = 1;
                                party[0] = mint_monster(SP_QUILLPUP, 3);
                                party_n = 1;
                                lead = 0;
                                seq_lines = TALK_SHELF;
                                seq_len = TALK_LEN(TALK_SHELF);
                            }
                            else {
                                seq_lines = TALK_SHELF_EMPTY;
                                seq_len = TALK_LEN(TALK_SHELF_EMPTY);
                            }
                            break;
                        case 'C':
                            if(!looted_crate) {
                                looted_crate = 1;
                                bag.bandage++;
                                seq_lines = TALK_CRATE;
                                seq_len = TALK_LEN(TALK_CRATE);
                            }
                            else {
                                seq_lines = TALK_CRATE_EMPTY;
                                seq_len = TALK_LEN(TALK_CRATE_EMPTY);
                            }
                            break;
                        default:
                            break;
                    }
                    seq_beat = 0;
                }
                else if(map_id == MAP_VELD) {
                    /* interact()'s MAP_VELD branch, in the reference's
                       exact check order (Wren, Mae, Ivo, Nell, Pike,
                       Bram, herb, gem, stump, cart, Calder). Anne/
                       Mason proximity checks (annePh==2/masonPh>=2 in
                       the source) aren't here: Anne doesn't exist in
                       this port (see the section comment above) and
                       Mason's own mark is handled below, once spawned. */
                    if(near_mark(map_id, 'K', px, py, 676)) {
                        if(!talked_wren) {
                            talked_wren = 1;
                            bag.salve++;
                            seq_lines = TALK_WREN_FIRST;
                            seq_len = TALK_LEN(TALK_WREN_FIRST);
                        }
                        else if(beat_calder) {
                            seq_lines = TALK_WREN_BEAT;
                            seq_len = TALK_LEN(TALK_WREN_BEAT);
                        }
                        else if(read_cart) {
                            seq_lines = TALK_WREN_CART;
                            seq_len = TALK_LEN(TALK_WREN_CART);
                        }
                        else {
                            /* fullHeal(): heals every party member --
                               meaningful now that captures can bring
                               in more than the lead. */
                            int i;
                            for(i = 0; i < party_n; i++) party[i].hp = party[i].maxHp;
                            seq_lines = TALK_WREN_HEAL;
                            seq_len = TALK_LEN(TALK_WREN_HEAL);
                        }
                    }
                    else if(near_mark(map_id, 'I', px, py, 676)) {
                        if(!talked_mae) {
                            talked_mae = 1;
                            bag.bandage++;
                            seq_lines = TALK_MAE_FIRST;
                            seq_len = TALK_LEN(TALK_MAE_FIRST);
                        }
                        else {
                            seq_lines = TALK_MAE_AGAIN;
                            seq_len = TALK_LEN(TALK_MAE_AGAIN);
                        }
                    }
                    else if(near_mark(map_id, 'V', px, py, 676)) {
                        if(!talked_ivo) {
                            talked_ivo = 1;
                            bag.bitterroot++;
                            seq_lines = TALK_IVO_FIRST;
                            seq_len = TALK_LEN(TALK_IVO_FIRST);
                        }
                        else {
                            seq_lines = TALK_IVO_AGAIN;
                            seq_len = TALK_LEN(TALK_IVO_AGAIN);
                        }
                    }
                    else if(near_mark(map_id, 'A', px, py, 676)) {
                        if(!talked_nell) {
                            talked_nell = 1;
                            bag.salve++;
                            seq_lines = TALK_NELL_FIRST;
                            seq_len = TALK_LEN(TALK_NELL_FIRST);
                        }
                        else if(party_n > 1 && !nell_bonus) {
                            nell_bonus = 1;
                            bag.salve++;
                            seq_lines = TALK_NELL_BONUS;
                            seq_len = TALK_LEN(TALK_NELL_BONUS);
                        }
                        else {
                            seq_lines = TALK_NELL_AGAIN;
                            seq_len = TALK_LEN(TALK_NELL_AGAIN);
                        }
                    }
                    else if(near_mark(map_id, 'Q', px, py, 676)) {
                        if(got_gem && !pike_helped) {
                            pike_helped = 1;
                            bag.bandage++;
                            seq_lines = TALK_PIKE_HELP;
                            seq_len = TALK_LEN(TALK_PIKE_HELP);
                        }
                        else if(!talked_pike) {
                            talked_pike = 1;
                            seq_lines = TALK_PIKE_FIRST;
                            seq_len = TALK_LEN(TALK_PIKE_FIRST);
                        }
                        else if(pike_helped) {
                            seq_lines = TALK_PIKE_DONE;
                            seq_len = TALK_LEN(TALK_PIKE_DONE);
                        }
                        else {
                            seq_lines = TALK_PIKE_HINT;
                            seq_len = TALK_LEN(TALK_PIKE_HINT);
                        }
                    }
                    else if(near_mark(map_id, 'J', px, py, 676)) {
                        seq_lines = TALK_BRAM_OPEN;
                        seq_len = TALK_LEN(TALK_BRAM_OPEN);
                        post_action = POST_SHOP;
                    }
                    else if(near_mark(map_id, 'M', px, py, 676)) {
                        if(!got_herb) {
                            got_herb = 1;
                            bag.bitterroot++;
                            seq_lines = TALK_HERB;
                            seq_len = TALK_LEN(TALK_HERB);
                        }
                        else {
                            seq_lines = TALK_HERB_GONE;
                            seq_len = TALK_LEN(TALK_HERB_GONE);
                        }
                    }
                    else if(near_mark(map_id, 'G', px, py, 676)) {
                        if(!got_gem) {
                            got_gem = 1;
                            bag.gem++;
                            if(talked_pike) {
                                seq_lines = TALK_GEM_PIKE;
                                seq_len = TALK_LEN(TALK_GEM_PIKE);
                            }
                            else {
                                seq_lines = TALK_GEM_WILD;
                                seq_len = TALK_LEN(TALK_GEM_WILD);
                            }
                        }
                        else {
                            seq_lines = TALK_GEM_GONE;
                            seq_len = TALK_LEN(TALK_GEM_GONE);
                        }
                    }
                    else if(near_mark(map_id, 'L', px, py, 676)) {
                        if(!got_stump) {
                            got_stump = 1;
                            bag.bandage++;
                            seq_lines = TALK_STUMP;
                            seq_len = TALK_LEN(TALK_STUMP);
                        }
                        else {
                            seq_lines = TALK_STUMP_GONE;
                            seq_len = TALK_LEN(TALK_STUMP_GONE);
                        }
                    }
                    else if(near_mark(map_id, 'X', px, py, 676)) {
                        read_cart = 1;
                        seq_lines = TALK_CART;
                        seq_len = TALK_LEN(TALK_CART);
                    }
                    else if(near_mark(map_id, 'E', px, py, 676) || near_mark(map_id, 'N', px, py, 676)) {
                        if(beat_calder) {
                            seq_lines = TALK_CALDER_AFTER;
                            seq_len = TALK_LEN(TALK_CALDER_AFTER);
                        }
                        else {
                            seq_lines = TALK_CALDER_FIGHT;
                            seq_len = TALK_LEN(TALK_CALDER_FIGHT);
                            post_action = POST_CALDER;
                        }
                    }
                    /* Mason has no standing/repeat-visit interact of
                       his own either (mason_state == 2 is just the
                       moment he's ambushing/fighting, not a wait-for-
                       a-second-visit step): he leaves automatically
                       the instant the win dialogue closes (see
                       POST_MASON_LEAVE at the TRAINER_MASON win
                       handler), same as Anne below. */
                    /* Anne has no standing/repeat-visit interact of her
                       own: she force-walks up, gifts, and leaves in
                       one uninterrupted sequence (see the actor-
                       movement update above), matching engine.ts --
                       there's no anne.phase "done, stand and wait"
                       state to interact with, unlike Mason's. */
                    seq_beat = 0;
                }
                else if(map_id == MAP_FOREST) {
                    /* Soldiers now patrol/chase for real (see the
                       actor-movement update above) -- the un-beaten
                       ambush trigger lives there (soldierLos catching
                       the player), same as Mason's. This interact
                       block only covers walking up to one already
                       beaten, for the repeat-visit flavor line;
                       engine.ts has no such line, this is a harmless
                       extra. */
                    int i;
                    for(i = 0; i < 3; i++) {
                        int ddx = px - (int)soldiers[i].x, ddy = py - (int)soldiers[i].y;
                        if(soldier_beaten[i] && ddx * ddx + ddy * ddy <= 676) {
                            seq_lines = TALK_SOLDIER_DONE;
                            seq_len = TALK_LEN(TALK_SOLDIER_DONE);
                            seq_beat = 0;
                            break;
                        }
                    }
                }
                else if(map_id == MAP_GROVE) {
                    /* Cathleen (mark '8') and Shinigami (mark '9').
                       676 (26px, roughly one tile) matches every other
                       mark's radius -- interacting should require
                       actually touching the NPC, from any angle
                       (near_mark is a plain radius check, no facing
                       requirement), not being loosely nearby. Both are
                       solid now too (tile_is_solid's extended set), so
                       676 is also about as close as collision alone
                       would ever let the player get. */
                    if(near_mark(map_id, '9', px, py, 676)) {
                        if(beat_shin) {
                            seq_lines = TALK_SHINIGAMI_DONE;
                            seq_len = TALK_LEN(TALK_SHINIGAMI_DONE);
                        }
                        else {
                            seq_lines = TALK_SHINIGAMI_SPOT;
                            seq_len = TALK_LEN(TALK_SHINIGAMI_SPOT);
                            post_action = POST_SHINIGAMI;
                        }
                        seq_beat = 0;
                    }
                    else if(!cath_caught && near_mark(map_id, '8', px, py, 676)) {
                        seq_lines = TALK_CATHLEEN_SPOT;
                        seq_len = TALK_LEN(TALK_CATHLEEN_SPOT);
                        post_action = POST_CATHLEEN;
                        seq_beat = 0;
                    }
                    else if(cath_caught && near_mark(map_id, '8', px, py, 676)) {
                        seq_lines = TALK_CATHLEEN_GONE;
                        seq_len = TALK_LEN(TALK_CATHLEEN_GONE);
                        seq_beat = 0;
                    }
                    else if(near_mark(map_id, 'K', px, py, 676)) {
                        /* Warden Cross, a Weeping Army soldier. */
                        if(beat_wsoldier_grove) {
                            seq_lines = TALK_WSOLDIER_GROVE_WIN;
                            seq_len = TALK_LEN(TALK_WSOLDIER_GROVE_WIN);
                        }
                        else {
                            seq_lines = TALK_WSOLDIER_GROVE_SPOT;
                            seq_len = TALK_LEN(TALK_WSOLDIER_GROVE_SPOT);
                            post_action = POST_WSOLDIER_GROVE;
                        }
                        seq_beat = 0;
                    }
                }
                else if(map_id == MAP_CAMP) {
                    /* The Weeping Army officer (mark 'I'): a taunt,
                       not an ending -- the story's real ending is the
                       Grove/Shinigami/Anne chain, so this no longer
                       cuts to a title-screen "win" the moment Calder
                       falls (see MAP_CAMP's section comment). */
                    if(near_mark(map_id, 'I', px, py, 676)) {
                        seq_lines = TALK_CAMP_COMMANDER;
                        seq_len = TALK_LEN(TALK_CAMP_COMMANDER);
                        post_action = POST_NONE;
                        seq_beat = 0;
                    }
                    else if(near_mark(map_id, 'K', px, py, 676)) {
                        if(beat_wsoldier_camp1) {
                            seq_lines = TALK_WSOLDIER_CAMP1_WIN;
                            seq_len = TALK_LEN(TALK_WSOLDIER_CAMP1_WIN);
                        }
                        else {
                            seq_lines = TALK_WSOLDIER_CAMP1_SPOT;
                            seq_len = TALK_LEN(TALK_WSOLDIER_CAMP1_SPOT);
                            post_action = POST_WSOLDIER_CAMP1;
                        }
                        seq_beat = 0;
                    }
                    else if(near_mark(map_id, 'A', px, py, 676)) {
                        if(beat_wsoldier_camp2) {
                            seq_lines = TALK_WSOLDIER_CAMP2_WIN;
                            seq_len = TALK_LEN(TALK_WSOLDIER_CAMP2_WIN);
                        }
                        else {
                            seq_lines = TALK_WSOLDIER_CAMP2_SPOT;
                            seq_len = TALK_LEN(TALK_WSOLDIER_CAMP2_SPOT);
                            post_action = POST_WSOLDIER_CAMP2;
                        }
                        seq_beat = 0;
                    }
                }
                else if(map_id == MAP_CLIFFS) {
                    if(near_mark(map_id, 'V', px, py, 676)) {
                        if(beat_wsoldier_cliffs) {
                            seq_lines = TALK_WSOLDIER_CLIFFS_WIN;
                            seq_len = TALK_LEN(TALK_WSOLDIER_CLIFFS_WIN);
                        }
                        else {
                            seq_lines = TALK_WSOLDIER_CLIFFS_SPOT;
                            seq_len = TALK_LEN(TALK_WSOLDIER_CLIFFS_SPOT);
                            post_action = POST_WSOLDIER_CLIFFS;
                        }
                        seq_beat = 0;
                    }
                    else if(near_mark(map_id, 'Y', px, py, 676)) {
                        if(talked_tessa) {
                            seq_lines = TALK_TESSA_AGAIN;
                            seq_len = TALK_LEN(TALK_TESSA_AGAIN);
                        }
                        else {
                            talked_tessa = 1;
                            bag.greatcrystal++;
                            seq_lines = TALK_TESSA_FIRST;
                            seq_len = TALK_LEN(TALK_TESSA_FIRST);
                        }
                        post_action = POST_NONE;
                        seq_beat = 0;
                    }
                    else if(near_mark(map_id, 'C', px, py, 676)) {
                        if(got_chest) {
                            seq_lines = TALK_CHEST_EMPTY;
                            seq_len = TALK_LEN(TALK_CHEST_EMPTY);
                        }
                        else {
                            got_chest = 1;
                            marks += 25;
                            bag.sunbalm++;
                            bag.greatcrystal++;
                            seq_lines = TALK_CHEST;
                            seq_len = TALK_LEN(TALK_CHEST);
                        }
                        post_action = POST_NONE;
                        seq_beat = 0;
                    }
                }
                else if(map_id == MAP_RUINS) {
                    if(near_mark(map_id, 'J', px, py, 676)) {
                        seq_lines = TALK_OREN_OPEN;
                        seq_len = TALK_LEN(TALK_OREN_OPEN);
                        post_action = POST_SHOP;
                        seq_beat = 0;
                    }
                    else if(near_mark(map_id, 'K', px, py, 676)) {
                        if(talked_birch) {
                            seq_lines = TALK_BIRCH_AGAIN;
                            seq_len = TALK_LEN(TALK_BIRCH_AGAIN);
                        }
                        else {
                            talked_birch = 1;
                            bag.sunbalm++;
                            seq_lines = TALK_BIRCH_FIRST;
                            seq_len = TALK_LEN(TALK_BIRCH_FIRST);
                        }
                        post_action = POST_NONE;
                        seq_beat = 0;
                    }
                    else if(near_mark(map_id, 'A', px, py, 676)) {
                        if(talked_sable) {
                            seq_lines = TALK_SABLE_AGAIN;
                            seq_len = TALK_LEN(TALK_SABLE_AGAIN);
                        }
                        else {
                            talked_sable = 1;
                            bag.warroot++;
                            seq_lines = TALK_SABLE_FIRST;
                            seq_len = TALK_LEN(TALK_SABLE_FIRST);
                        }
                        post_action = POST_NONE;
                        seq_beat = 0;
                    }
                }
            }

            /* Menu open, only from plain world state (state.lua only
               reaches selectPressed()/startPressed() outside TALK/
               BATTLE/etc, which here just means no dialogue active). */
            if(!seq_lines && fade_state == FADE_NONE) {
                if(y_now && !prev_y) {
                    menu_mode = 1;
                    bag_cur = 0;
                    heal_item = -1;
                }
                else if(start_now && !prev_start) {
                    menu_mode = 2;
                    party_detail = 0;
                    heal_item = -1;
                }
            }
        }

        /* Draw every frame, unconditionally, into the buffer that was
           just hidden by the flip above. See the fb_flip comment for
           why this has to be unconditional now, unlike the earlier
           dirty-check versions. */
        if(state == 0) {
            draw_press_start();
        }
        else if(ending_mode) {
            draw_ending(DEMO_END, TALK_LEN(DEMO_END), ending_i);
        }
        else {
            compute_camera(map_id, px, py, &cam_x, &cam_y);
            draw_map(map_id, cam_x, cam_y);
            draw_props(map_id, cam_x, cam_y);
            {
                WorldSprite ws_list[MAX_WORLD_SPRITES];
                int ws_n = 0;
                int mason_frame = (mason_state == 1 || mason_state == 3) ? (int)mason_anim % 4 : 0;
                int anne_frame = (anne_state == 1 || anne_state == 3) ? (int)anne_anim % 4 : 0;
                collect_npcs(ws_list, &ws_n, map_id, frame_count,
                             mason_state, mason_x, mason_y, mason_dir, mason_frame,
                             anne_state, anne_x, anne_y, anne_dir, anne_frame,
                             cath_caught, beat_shin, soldiers, soldier_beaten);
                ws_push(ws_list, &ws_n, MAX_FRAMES[pdir][(anim_counter / 10) & 3],
                        MAX_SPRITE_W, MAX_SPRITE_H, px, py);
                ws_sort_and_draw(ws_list, ws_n, cam_x, cam_y);
            }
            draw_hud(got_shelf, looted_crate, bag.bandage, has_scroll);
            if(seq_lines)
                draw_dialogue_box(&seq_lines[seq_beat]);
            else if(hud_t > 0)
                draw_hud_toast(hud_flash);
            if(map_banner_timer > 0)
                draw_map_banner(map_id, map_banner_timer);
            if(menu_mode == 1)
                draw_bag_menu(&bag, marks, bag_cur);
            else if(menu_mode == 2)
                draw_party_menu(party, party_n, lead, party_cur, party_detail, heal_item);
            if(in_battle)
                draw_battle(&battle, &bag, frame_count,
                            battle_foe_enter_t, battle_foe_faint_t,
                            battle_pl_enter_t, battle_pl_faint_t);
            if(shop_open)
                draw_shop(&bag, marks, shop_sell_tab, shop_cur);
            if(choice_mode)
                draw_choice(choice_cur);
        }

        /* Post-process over whatever was just drawn, whatever it was
           -- see the fade_state comment up at its declaration. */
        if(fade_state == FADE_OUT)
            apply_fade(fade_timer);
        else if(fade_state == FADE_HOLD)
            apply_fade(FADE_STEPS);
        else if(fade_state == FADE_IN)
            apply_fade(FADE_STEPS - fade_timer);

        prev_start = start_now;
        prev_b = b_now;
        prev_y = y_now;
        prev_a = a_now;
        prev_up = up_now;
        prev_down = down_now;
        prev_left = left_now;
        prev_right = right_now;
    }
}
