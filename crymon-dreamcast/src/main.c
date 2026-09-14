/*
 * CryMon - Dreamcast port. Bare-metal, no KallistiOS/BIOS calls (see
 * hello-world-dreamcast/src/hello.c for the video/vblank/Maple-driver
 * derivation notes against KallistiOS's real source, unchanged since
 * step 1). A full, playable port of the reference LÖVE build
 * (xenodorian/CryMon, the Lua sources under love/game/src): title
 * screen, all 4 maps
 * (HOUSE/VELD/FOREST/GROVE) with camera scrolling and every door/warp
 * between them, the starting room's props and Quillpup/bandage
 * grants, every VELD/FOREST/GROVE NPC and pickup, the full turn-based
 * battle system (attack/special-minigame/guard/items/capture/XP),
 * wild encounters, Bram's shop, and both endings (Calder/ENDING_WIN,
 * Shinigami/DEMO_END). Each major system's own section comment below
 * (search for "----" banners) cites the exact Lua functions/tables it
 * ports and any formula it reproduces verbatim.
 *
 * Known, deliberate departures from the reference -- each is also
 * called out inline at the relevant code, this is just the index:
 *   - No real-time chase AI: state.lua's soldiers (patrol + line-of-
 *     sight + chase) and Mason (force-walks toward the player after
 *     the house door) are ported as stationary proximity-interact
 *     NPCs instead -- see the world-NPC section comment. The
 *     mechanical outcome (you cannot reach Calder without fighting
 *     them) is unchanged; only the chase presentation is cut.
 *   - Anne is not ported at all: state.lua's maybeAnne(), the only
 *     code that would ever spawn her, is never called from anywhere
 *     in the source -- checked directly, not inferred. She is
 *     unreachable in the reference as shipped, so omitting her is the
 *     faithful port.
 *   - Cathleen fights using the generic wild-monster basic/special AI
 *     (her SPECIES entry's real move names still show correctly),
 *     not her unique castSpell() kit (Fire Bolt/Ice Beam/Lightning
 *     Strike/Mana Surge) -- see the battle-system section comment.
 *   - No item icons, no manual party lead-switch menu, no manual
 *     in-battle item "switch to bench monster" row -- text-only UI
 *     throughout, and the automatic emergency swap-in on a guard-
 *     phase faint is ported (state.lua does this one automatically
 *     too), just not the player-chosen version.
 *   - The post-win "grew to lv N"/"stands over the grass" toast has
 *     no timed-fade HUD to live in here, so it's one extra clickable
 *     battle message instead (BAFTER_WIN_NOTE) -- see the battle
 *     section comment.
 *   - Returning to the title screen after an ending does not reset
 *     game state the way state.lua's resetRun() does: this port's
 *     world/party/flags are plain locals in main(), initialized once
 *     before the main loop rather than re-initializable mid-function
 *     without a larger restructure. A single playthrough (title all
 *     the way to an ending) works correctly; starting a second run in
 *     the same boot session will resume with whatever party/flags/
 *     marks the first run left behind rather than a fresh save. Power
 *     -cycling (or a fresh emulator boot) is the workaround.
 *   - Sprites: only the player (max) and the 4 HOUSE props use real
 *     pixel art (tools/gen_sprites.py, from public/sprites/); every
 *     NPC and every map tile still draws as flat color blocks
 *     (paintTile's own palette, ported in full -- see draw_tile).
 *   - No animation frames anywhere (player, or the props) -- every
 *     sprite is its single static down/up/left/right pose.
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

/* ----------------------------------------------------------------------
 * Font: 8x8, 1bpp glyphs, A-Z + space. Bit 7 = leftmost pixel of each
 * row. Enough to render the interact-dialogue lines and the title
 * screen; text is upper-cased and punctuation-free by construction.
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

static void draw_glyph(int ox, int oy, const uint8_t bitmap[8], u16 color, int scale) {
    int row, col, sx, sy;

    for(row = 0; row < 8; row++) {
        uint8_t bits = bitmap[row];

        for(col = 0; col < 8; col++) {
            if(!(bits & (0x80 >> col)))
                continue;

            for(sy = 0; sy < scale; sy++)
                for(sx = 0; sx < scale; sx++)
                    put_pixel(ox + col * scale + sx, oy + row * scale + sy, color);
        }
    }
}

static void draw_text_s(const char *s, int x, int y, u16 color, int scale) {
    int cx = x;
    int px = 8 * scale;
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
        cx += px;
    }
}

static int text_width_s(const char *s, int scale) {
    int n = 0;
    for(; *s; s++) n++;
    return n * 8 * scale;
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

typedef struct {
    const char *const *rows;
    int cols, rows_n;
} Map;

static const char *const map_house_rows[] = {
    "HHHHHHHHHHHHHH",
    "HFFFFFFFFFFFFH",
    "HFFFFFFFFFFFFH",
    "HFFFFFFFFFFFFH",
    "HFBFFFFSFFFCFH",
    "HFFFFFFFFFFFFH",
    "HUFFFFFFFFFFFH",
    "HFFFFPFFFFFFFH",
    "HFFFFFFFFFFFFH",
    "HFFFFFFFFFFFFH",
    "HHHHHHDHHHHHHH",
};

static const char *const map_veld_rows[] = {
    "##############################",
    "####..........RRRR..........##",
    "##.Q.^^.......HHHH......WWW.##",
    "##............HDH......WWA..##",
    "##..K.........===...I...W....#",
    "##...TTT.....=====.....TTT..G#",
    "##...TTT....===,===....TTT...#",
    "##....M......=====....**.....#",
    "##.V.TTT......===......TTT...#",
    "##............===............#",
    "###....X...J.=====...........#",
    "##...TTT......===............#",
    "##............===.......L....#",
    "##...TTT.....=====......TTT..#",
    "##............===......^^....#",
    "##....TTT....=====......TTT..#",
    "##....TTT.....===......TTT...#",
    "##............===............#",
    "##...........=====.....NNNN..#",
    "##............===.......NE...#",
    "###...........===...........##",
    "#############=Z=##############",
};

static const char *const map_forest_rows[] = {
    "##########################",
    "####.........Y.........###",
    "###.........===.........##",
    "##...TTT....===....TTT..##",
    "##...TTT...=====...TTT..##",
    "##.1........===.......2.##",
    "##...TTT....===....TTT..##",
    "##..........=====.......##",
    "##...TTT....===....TTT..##",
    "##...........===........##",
    "##...TTT....=====..TTT..##",
    "##...........===........##",
    "##...TTT.....===...TTT..##",
    "##...........=====......##",
    "##....TTT....===...TTT..##",
    "##............===.......##",
    "##...TTT......===..TTT..##",
    "##............===....3..##",
    "###...........===......###",
    "#############=O=##########",
};

static const char *const map_grove_rows[] = {
    "##########################",
    "####.........O.........###",
    "###.........===.........##",
    "##..........===.........##",
    "##.........=====........##",
    "##..........===.........##",
    "##.........=====........##",
    "##..........===.........##",
    "##.........=====........##",
    "##..........===.........##",
    "##........=======.......##",
    "##........===8===.......##",
    "##........=======.......##",
    "#############D############",
    "##.........=====........##",
    "##..........===.........##",
    "##.........=====........##",
    "##..........===.........##",
    "##...........9..........##",
    "###....................###",
    "##########################",
};

static const Map MAPS[4] = {
    { map_house_rows,  14, 11 },
    { map_veld_rows,   30, 22 },
    { map_forest_rows, 26, 20 },
    { map_grove_rows,  26, 21 },
};

/* data.lua's SOLID_SET, verbatim: "#HWRBC^NKEVAQXUJI". D (door), S
   (shelf) and the digit/letter NPC marks are deliberately absent --
   doors must be walkable to trigger a warp, and marks are interacted
   with by proximity, not blocked by collision. */
static int tile_is_solid(char ch) {
    static const char *const solid = "#HWRBC^NKEVAQXUJI";
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
   is ported too instead of staying an open door. */
static int tile_blocked(int map_id, char ch, int cath_caught) {
    if(tile_is_solid(ch)) return 1;
    if(map_id == MAP_GROVE && ch == 'D' && !cath_caught) return 1;
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

/* Player sprite, bottom-center anchored at (cx, cy) same as the
   silhouette this replaces (draw.lua's draw.actor() used the same
   anchor). dir matches the sprite arrays below: 0=down,1=up,2=left,
   3=right. */
static void draw_player(int cx, int cy, int dir) {
    static const u16 *const frames[4] = {
        max_down, max_up, max_left, max_right
    };
    int x = cx - MAX_SPRITE_W / 2, y = cy - MAX_SPRITE_H;
    blit_sprite(frames[dir], MAX_SPRITE_W, MAX_SPRITE_H, x, y);
}

/* ----------------------------------------------------------------------
 * Interact dialogue: full multi-beat sequences from data.TALK.father /
 * fatherAfter / bed / shelf / shelfEmpty / crate / crateEmpty
 * (love/game/src/data.lua), verbatim except upper-cased and stripped
 * of punctuation our A-Z/0-9-only font can't render. Each sequence is
 * shown one beat per A-press (matching sayn()'s one-beat-per-advance
 * in the reference), word-wrapped into the dialogue box.
 * ---------------------------------------------------------------------- */
static const char *const TALK_FATHER[] = {
    "THERES A WAR CRYTOWN IS ALREADY BLEEDING",
    "YOURE TOO SICK TO DEFEND IT FROM THE SOLDIERS I KNOW THAT",
    "SO IM STEALING YOUR CRYMON",
    "FATHER DOES NOT WAKE THE CAPTURE CRYSTAL IS STILL ON THE SHELF",
};
static const char *const TALK_FATHER_AFTER[] = {
    "I ALREADY TOOK QUILLPUP SLEEP ILL DO THE FIGHTING",
    "HIS BREATH IS THIN HE DOES NOT ANSWER",
};
static const char *const TALK_BED[] = {
    "JUST UNTIL THEY BREATHE AGAIN",
    "MAXS EMPTY BED THE CRYMON SLEEP CUTS CLOSE SPECIALS RETURN",
};
static const char *const TALK_SHELF[] = {
    "THIS IS IT FATHERS CRYSTAL QUILLPUP IS INSIDE",
    "THE CRYSTAL BREAKS WARM IN HER HANDS QUILLPUP SHAKES OUT ONTO THE FLOORBOARDS",
    "YOURE COMING CRYTOWN DOESNT GET TO FALL",
};
static const char *const TALK_SHELF_EMPTY[] = {
    "DUST THE CRYSTAL IS ALREADY OPEN",
};
static const char *const TALK_CRATE[] = {
    "A WRAP HE WONT MISS IT",
    "A LINEN WRAP UNDER THE LID YOU TAKE IT",
};
static const char *const TALK_CRATE_EMPTY[] = {
    "SPLINTERS AND A MOTH EMPTY",
};

/* Door/warp flavor lines, data.TALK.doorLocked/doorOut/cottage/
   forestEnter/forestLeave/groveEnter/groveLeave. doorOut's original
   also kicks off the Mason NPC encounter (masonPh) the first time you
   leave the house; that's NPC/battle content not built yet, so this
   step always shows doorOut's plain flavor text instead. */
static const char *const TALK_DOOR_LOCKED[] = {
    "NOT YET FATHERS CRYMON IS STILL ON THE SHELF",
};
static const char *const TALK_DOOR_OUT[] = {
    "NIGHT AIR I CAN DO THIS",
    "TALL GRASS HIDES CRYMON WREN WEST POND EAST BRAM ON THE PATH CALDER SOUTH",
};
static const char *const TALK_COTTAGE[] = {
    "THE COTTAGE FATHER IN THE BED MY BED SOUTH DOOR LEAVES",
};
static const char *const TALK_FOREST_ENTER[] = {
    "THE TREES CLOSE OVER THE PATH",
    "TALL GRASS PATROLS IF THEY SEE YOU THEY WILL COME THE PATH KEEPS SOUTH",
};
static const char *const TALK_FOREST_LEAVE[] = {
    "BACK TOWARD THE COTTAGE PATH",
};
static const char *const TALK_GROVE_ENTER[] = {
    "THE GRASS DIES OUT STONE AND HUSH",
    "NO TALL GRASS SOMETHING WAITS ON THE PATH",
};
static const char *const TALK_GROVE_LEAVE[] = {
    "BACK UNDER THE TREES",
};

/* state.lua's bAfter==7 loss handler: "Max" / "We still breathe. Crawl
   back." shown as a plain world message once the battle itself has
   already ended (matches say1(G, "Max", ...) running after G.mode is
   set back to MODE.WORLD). */
static const char *const TALK_LOSS[] = {
    "WE STILL BREATHE CRAWL BACK",
};

/* Remaining data.TALK entries: the VELD/FOREST/GROVE NPCs, Mason,
   Anne, and Bram's shop-open line. Ported verbatim except upper-cased
   and stripped of punctuation, same as every TALK_* array above. */
static const char *const TALK_MASON_FIGHT[] = {
    "YOU WALKED OUT WITH THAT HOUND",
    "HES MINE",
    "I ALREADY CAUGHT A CRYMON FIGHT ME",
};
static const char *const TALK_MASON_AFTER[] = {
    "FINE CALDER IS STILL SOUTH",
    "I WONT DIE FIRST",
};
static const char *const TALK_MASON_WIN[] = {
    "MASON SPITS IN THE DIRT THE PATH IS YOURS CALDER STILL WAITS SOUTH",
};
static const char *const TALK_WREN_FIRST[] = {
    "TOO YOUNG TAKE THE SALVE CALDER CAMPS SOUTH",
    "IM NOT TOO YOUNG",
    "WEST IS IVO EAST IS NELL KEEP THAT HOUND FED",
};
static const char *const TALK_WREN_BEAT[] = {
    "YOU BEAT HIM THE WAR STILL WANTS MORE OF US",
    "THEN IT CAN WAIT",
};
static const char *const TALK_WREN_CART[] = {
    "YOU FOUND THEIR LETTER THEY ALREADY KNEW YOUR NAME",
    "I READ IT ANYWAY",
};
static const char *const TALK_WREN_HEAL[] = {
    "CUTS BOUND SPECIALS RETURN KEEP THEM FED",
    "THANK YOU",
};
static const char *const TALK_MAE_FIRST[] = {
    "YOURE MAX I WATCHED YOU LEAVE THE HOUSE",
    "DONT FOLLOW ME",
    "I WONT TAKE THE WRAP WREN HEALS I JUST DIDNT WANT THE PATH EMPTY",
};
static const char *const TALK_MAE_AGAIN[] = {
    "ILL BE HERE SOUTH STILL DRUMS",
    "I HEAR THEM",
};
static const char *const TALK_IVO_FIRST[] = {
    "CAMP TOOK MY CRYMON CHEW THIS CALDER SITS SOUTH",
    "IM GOING SOUTH ANYWAY",
    "DONT GIVE HIM A CLEAN FIGHT",
};
static const char *const TALK_IVO_AGAIN[] = {
    "HIT FIRST RUN IF THE BAT FOLDS YOU",
    "I DONT RUN YET",
};
static const char *const TALK_NELL_FIRST[] = {
    "TOO YOUNG DRINK THIS ANYWAY REEDS HIDE A STONE",
    "I CAN HOLD A CRYSTAL",
};
static const char *const TALK_NELL_BONUS[] = {
    "THAT MOTH WASNT YOURS YESTERDAY ANOTHER SALVE",
    "I CAUGHT IT FAIR",
};
static const char *const TALK_NELL_AGAIN[] = {
    "THE POND KEEPS SECRETS SOUTH STILL DRUMS",
    "I HEAR THEM",
};
static const char *const TALK_PIKE_FIRST[] = {
    "I DROPPED A MOONSTONE IN THE EAST REEDS DONT TELL WREN",
    "I WONT TELL WREN",
};
static const char *const TALK_PIKE_HELP[] = {
    "YOU FOUND IT KEEP THE STONE TAKE THIS WRAP",
    "I WAS ONLY LOOKING",
};
static const char *const TALK_PIKE_DONE[] = {
    "THE CLIFFS ARE JUST ROCKS THE WAR IS THE SCARY PART",
    "I KNOW",
};
static const char *const TALK_PIKE_HINT[] = {
    "EAST OF THE PATH IN THE TALL GRASS BY THE WATER",
};
static const char *const TALK_HERB[] = {
    "BITTERROOT STR+4 IF I LAST",
};
static const char *const TALK_HERB_GONE[] = {
    "A HOLE WHERE THE HERB WAS ONLY GRIT",
};
static const char *const TALK_GEM_PIKE[] = {
    "PIKES MOONSTONE HE SAID KEEP IT",
};
static const char *const TALK_GEM_WILD[] = {
    "A CAPTURE CRYSTAL SOMEONE SMALL LOST THIS",
};
static const char *const TALK_GEM_GONE[] = {
    "MUD AND A FROG THE STONE IS ALREADY MINE",
};
static const char *const TALK_STUMP[] = {
    "A WRAP JAMMED IN THE STUMP I TAKE IT",
};
static const char *const TALK_STUMP_GONE[] = {
    "JUST A STUMP ANTS NO MORE CLOTH",
};
static const char *const TALK_CART[] = {
    "THEY ALREADY KNEW MY NAME",
    "A CAMP LETTER ON THE WRECK SEND THE COTTAGE GIRL SOUTH WE NEED BODIES",
};
static const char *const TALK_CALDER_AFTER[] = {
    "SOUTH IS THE CAMP DONT DIE STUPID",
    "I DONT PLAN TO",
};
static const char *const TALK_CALDER_FIGHT[] = {
    "THE CAMP TAKES STRAYS",
    "IM NOT STRAY",
};
static const char *const TALK_CATHLEEN_SPOT[] = {
    "YOU WALKED THE PATH I AM THE PATHS ANSWER",
    "YOURE A CRYMON",
    "I AM CATHLEEN I FIGHT AS MYSELF",
};
static const char *const TALK_CATHLEEN_AFTER[] = {
    "YOU STAND COME AGAIN IF YOU MEAN TO KEEP ME",
    "I MIGHT",
};
static const char *const TALK_CATHLEEN_GONE[] = {
    "ONLY THE HOODS SHADOW SHES WITH ME NOW",
};
static const char *const TALK_SHINIGAMI_SPOT[] = {
    "THREE NAMES THREE GRAVES I KEEP THEM",
    "YOURE IN THE WAY",
    "CRYMARE COME",
};
static const char *const TALK_SHINIGAMI_DONE[] = {
    "THE GRAVES ARE QUIET GO",
};
static const char *const TALK_SOLDIER_SPOT[] = {
    "A SOLDIER SEES YOU YOU THERE THIS WOOD IS CAMP GROUND",
    "IM PASSING THROUGH",
};
static const char *const TALK_SOLDIER_DONE[] = {
    "THEY ALREADY LOST THEY WILL NOT RISE",
};
/* finishWin()'s soldier branch uses a raw say1() string identical to
   TALK.soldierAfter's text rather than the table entry itself -- the
   source has both; we just use one array for it. */
static const char *const TALK_SOLDIER_AFTER[] = {
    "THE SOLDIER SITS GO BEFORE I CHANGE MY MIND",
};
static const char *const TALK_BRAM_OPEN[] = {
    "MARKS FOR MOSS WRAPS STONES BUY OR SELL",
    "I HAVE CUTS I NEED STONES",
};
/* data.TALK.anneGift/anneAgain are NOT ported: Anne is unreachable in
   the reference as shipped (see the world-NPC section comment further
   down for why), so there is nothing that would ever show this text. */

/* data.ENDING_WIN / data.DEMO_END, shown by the new ending screen
   (draw_ending() in main()) after beating Calder / Shinigami. */
static const char *const ENDING_WIN[] = {
    "CALDER SITS IN THE MUD AND LAUGHS ONCE WITHOUT HUMOUR",
    "FINE THE CAMP TAKES STRAYS KEEP THAT HOUND CLOSE THE WAR DOES NOT CARE THAT YOU ARE EIGHT",
    "SOUTH DRUMS MAX CHECKS THE CRYSTALS THEY ARE FEWER THAN SHE THOUGHT",
    "CRYMON THE ROAD CONTINUES WALK CATCH SURVIVE",
};
static const char *const DEMO_END[] = {
    "SHINIGAMI KNEELS THE MARES FADE BACK INTO FOG",
    "THE GROVE GOES QUIET THE GRAVES KEEP THEIR NAMES",
    "THANK YOU FOR PLAYING THE DEMO OF CRYMON",
};

#define TALK_LEN(arr) (int)(sizeof(arr) / sizeof((arr)[0]))

#define DIALOGUE_MAX_CHARS 37
#define DIALOGUE_LINE_H    9

static void draw_dialogue_box(const char *line) {
    fill_rect(4, SCREEN_H - 44, SCREEN_W - 8, 40, rgb565(18, 17, 14));
    fill_rect(4, SCREEN_H - 44, SCREEN_W - 8, 2, rgb565(197, 206, 198));
    fill_rect(4, SCREEN_H - 6, SCREEN_W - 8, 2, rgb565(197, 206, 198));
    draw_wrapped(line, 12, SCREEN_H - 38, rgb565(232, 228, 216), DIALOGUE_SCALE,
                 DIALOGUE_MAX_CHARS, DIALOGUE_LINE_H);
}

/* Small HUD in the screen's top-left corner (fixed there regardless
   of camera position), showing what interacting has granted so far
   -- there's no inventory/party HUD overlay in the reference, but
   there's also no way to see this port's bag/party menus without
   opening them, so this stays as a quick-glance confirmation. */
static void draw_hud(int got_shelf, int looted_crate, int bag_bandage) {
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
    }
}

/* ----------------------------------------------------------------------
 * Species/monster data and battle math, verbatim from data.lua's
 * SPECIES table, mintMonster(), grantXp(), and captureChance(). Move
 * names are upper-cased/depunctuated for our font; multi-word names
 * keep their space ("FIRE BOLT"). Cathleen's spellcaster kit
 * (castSpell -- Fire Bolt/Ice Beam/Lightning Strike/Mana Surge) isn't
 * ported: she's only reachable as a Grove NPC battle, which isn't
 * built yet (no NPCs exist in this port), so nothing can mint or
 * fight a Cathleen yet. Her species entry is still here for table
 * symmetry with data.lua and so mint_monster/grant_xp already work
 * for her once that NPC exists.
 * ---------------------------------------------------------------------- */
typedef struct {
    const char *name, *basic, *special;
    int maxHp, str, agl, spc, spp;
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

static const Species SPECIES[11] = {
    /* name          basic       special         maxHp str agl spc spp */
    { "QUILLPUP",   "NIP",       "QUILLBURST",   34, 15, 10, 7,  3 },
    { "GLIMMOTH",   "DUSTWING",  "LAMPFLARE",    26, 7,  13, 16, 3 },
    { "TORTCASK",   "SHOVE",     "SHELLSLAM",    42, 13, 5,  8,  3 },
    { "RAZORBAT",   "RAKE",      "SWOOPCUT",     30, 14, 16, 9,  3 },
    { "MOSSBACK",   "SQUELCH",   "MOSSGUARD",    38, 12, 6,  11, 3 },
    { "BRIARFOX",   "BRAMBLE",   "THORNRUSH",    28, 13, 17, 10, 3 },
    { "FENWISP",    "GLIM",      "FENFLARE",     24, 8,  16, 17, 3 },
    { "DUSKHORN",   "GORE",      "DUSKRAM",      36, 16, 8,  7,  3 },
    { "NEEDLEROOT", "PRICK",     "SAPDRAIN",     32, 12, 7,  14, 3 },
    { "CATHLEEN",   "FIRE BOLT", "MANA SURGE",   38, 11, 13, 19, 4 },
    { "CRYMARE",    "WAIL",      "NIGHTBRIDLE",  30, 9,  14, 18, 3 },
};

typedef struct {
    int species;
    int lv, xp;
    int maxHp, hp, str, agl, spc, spp, sppMax;
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
    return m;
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
} Bag;

#define MENU_X 20
#define MENU_Y 20
#define MENU_W (SCREEN_W - 2 * MENU_X)
#define MENU_H (SCREEN_H - 2 * MENU_Y)
#define MENU_SCALE 1
#define MENU_ROW_H 16

static void draw_menu_frame(const char *title) {
    fill_rect(MENU_X, MENU_Y, MENU_W, MENU_H, rgb565(18, 17, 14));
    fill_rect(MENU_X, MENU_Y, MENU_W, 2, rgb565(197, 206, 198));
    fill_rect(MENU_X, MENU_Y + MENU_H - 2, MENU_W, 2, rgb565(197, 206, 198));
    draw_text_s(title, MENU_X + 8, MENU_Y + 8, rgb565(197, 206, 198), MENU_SCALE);
    draw_text_s("B CLOSE", MENU_X + 8, MENU_Y + MENU_H - 16,
                rgb565(90, 122, 82), MENU_SCALE);
}

static void draw_bag_row(const char *label, int count, int y) {
    char buf[32];
    int n = s_cat(buf, 0, label);
    n = s_cat(buf, n, " X");
    n = s_cat_uint(buf, n, count);
    buf[n] = 0;
    draw_text_s(buf, MENU_X + 8, y, rgb565(232, 228, 216), MENU_SCALE);
}

static void draw_bag_menu(const Bag *bag, int marks) {
    int y = MENU_Y + 24;
    char marks_buf[16];
    int n;

    draw_menu_frame("BAG");

    n = s_cat(marks_buf, 0, "MARKS ");
    n = s_cat_uint(marks_buf, n, marks);
    marks_buf[n] = 0;
    draw_text_s(marks_buf, MENU_X + MENU_W - 8 - text_width_s(marks_buf, MENU_SCALE),
                MENU_Y + 8, rgb565(143, 74, 64), MENU_SCALE);

    draw_bag_row("MOSS SALVE", bag->salve, y);       y += MENU_ROW_H;
    draw_bag_row("LINEN WRAP", bag->bandage, y);     y += MENU_ROW_H;
    draw_bag_row("BITTERROOT", bag->bitterroot, y);  y += MENU_ROW_H;
    draw_bag_row("ASH DUST", bag->dust, y);          y += MENU_ROW_H;
    draw_bag_row("CAPTURE CRYSTAL", bag->gem, y);
}

/* has_party mirrors state.lua's #G.party > 0; party_mon is the one
   party slot this port supports (no PARTY_MAX-6 roster, no switching
   -- Quillpup is the only species obtainable so far). Shows real,
   current HP now that the battle system tracks it. */
/* drawParty(): lists every party member (up to data.PARTY_MAX -- see
   Monster party[6] in main()), not just the lead, with a ">" prefix
   and brighter color on the lead, matching the reference exactly now
   that captures can grow the roster past one. No manual lead-switch
   UI (this port's own "1-6 lead" equivalent) -- see the bag/party
   menu section comment on why. */
static void draw_party_menu(const Monster *party, int party_n, int lead) {
    int y = MENU_Y + 24;

    draw_menu_frame("CRYMON");

    if(party_n > 0) {
        int i;
        for(i = 0; i < party_n; i++) {
            char buf[40];
            u16 color = (i == lead) ? rgb565(232, 228, 216) : rgb565(138, 134, 120);
            int n = s_cat(buf, 0, (i == lead) ? "> " : "  ");
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
    }
    else {
        draw_text_s("NO CRYMON YET", MENU_X + 8, y, rgb565(138, 134, 120), MENU_SCALE);
    }
}

/* ----------------------------------------------------------------------
 * Battle system, ported from state.lua's updateBattle()/pickAtk()/
 * pickGuard()/pickItem()/applyHit()/finishWin() and captureChanceNow().
 * Wild encounters (tryEncounter()/startBattle()) and trainer battles
 * both use this same struct/update loop, matching state.lua (which
 * shares updateBattle() between them too): trainer_kind names who
 * G.bTrainer would be, bench holds Shinigami's 2 backup CryMare
 * (data.lua's only nbench > 0 fight). Cathleen fights as a wild
 * (capturable) foe using the generic basic/special AI below rather
 * than her real castSpell() kit (Fire Bolt/Ice Beam/Lightning Strike/
 * Mana Surge) -- a deliberate simplification: her SPECIES entry's
 * basic/special names ("FIRE BOLT"/"MANA SURGE") still show correctly
 * in battle text, only the underlying formula differs from the
 * reference (generic str/spc-based instead of her unique spell
 * formulas). Soldiers/Mason/Anne are also simplified: stationary
 * proximity-interact NPCs rather than the reference's patrolling/
 * chasing real-time actors -- see the world NPC section further down
 * for why.
 *
 * One deliberate adaptation: state.lua's post-win "grew to lv N" /
 * "stands over the grass" line is a timed-fade G.hud toast
 * (note()/G.hudT), not a clickable message. This port has no
 * timed-fade HUD, so it's shown as one extra battle-message beat
 * (BAFTER_WIN_NOTE below) before returning to the world instead.
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

#define BAFTER_ITEM      1
#define BAFTER_ATK       2
#define BAFTER_GUARD     3
#define BAFTER_WIN       5
#define BAFTER_WORLD     6
#define BAFTER_LOSS      7
#define BAFTER_WIN_NOTE  9

/* selfDebuffed(G) (the mirror of this, used by Cathleen's foe-AI mana
   surge override chance) isn't ported -- see the section comment on
   why Cathleen's kit is out of scope here. */
static int battle_foe_debuffed(const Battle *b) {
    return b->mods_foe_str < 0 || b->mods_foe_agl < 0 || b->mods_foe_spc < 0;
}
static int battle_capture_chance(const Battle *b) {
    return capture_chance(b->foe.agl, b->foe.hp, b->foe.maxHp, battle_foe_debuffed(b));
}

static void battle_apply_hit(Battle *b) {
    int n;
    b->foe.hp -= b->dmg;
    if(b->foe.hp < 0) b->foe.hp = 0;

    n = s_cat(b->msg[0], 0, b->label);
    n = s_cat(b->msg[0], n, " ");
    n = s_cat_uint(b->msg[0], n, b->dmg);
    n = s_cat(b->msg[0], n, " DMG");
    b->msg[0][n] = 0;

    if(b->foe.hp <= 0) {
        if(b->bench_n > 0) {
            /* Shinigami's fight only -- the sole nbench > 0 case in
               data.lua. Grants XP for the fallen bench member (unlike
               the final win, which grants XP once the whole fight
               ends), swaps the next bench monster in, and clears the
               foe-side stat mods, matching applyHit's bench branch. */
            char fallen[24];
            int fn = s_cat(fallen, 0, SPECIES[b->foe.species].name);
            fallen[fn] = 0;

            grant_xp(&b->pl, b->foe.lv);
            b->foe = b->bench[0];
            if(b->bench_n == 2) b->bench[0] = b->bench[1];
            b->bench_n--;
            b->mods_foe_str = b->mods_foe_agl = b->mods_foe_spc = 0;

            n = s_cat(b->msg[1], 0, fallen);
            n = s_cat(b->msg[1], n, " FALLS");
            b->msg[1][n] = 0;

            n = s_cat(b->msg[2], 0, "SHINIGAMI SENDS ");
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

/* pickAtk's basic-move branch (Cathleen's spell branch isn't ported --
   see the section comment above). */
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
    const char *move_name;
    float base;
    int atk_stat, def_stat, chance, success, dmg;
    char line[40];
    int n = 0;

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
    line[n] = 0;

    b->pl.hp -= dmg;
    if(b->pl.hp < 0) b->pl.hp = 0;
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
    else {
        if(tx < 12) id = SP_GLIMMOTH;
        else if(tx > 18) id = SP_TORTCASK;
        else id = irand(0, 1) == 0 ? SP_GLIMMOTH : SP_TORTCASK;
        lv = 2 + (ty > 14 ? 1 : 0) + irand(0, 1);
    }

    out->foe = mint_monster(id, lv);
    out->wild = 1;
    out->phase = 0;
    n = s_cat(out->msg[0], 0, "A WILD ");
    n = s_cat(out->msg[0], n, SPECIES[id].name);
    out->msg[0][n] = 0;
    out->msg_n = 1;
    out->msg_i = 0;
    out->after = BAFTER_ITEM;
    out->cur = 0;
    out->mods_self_str = out->mods_self_agl = out->mods_self_spc = 0;
    out->mods_foe_str = out->mods_foe_agl = out->mods_foe_spc = 0;
    out->mg = 8.0f;
    out->mg_dir = 1;
    out->grew = 0;
    return 1;
}

/* ----------------------------------------------------------------------
 * Battle drawing: one full-screen panel (draw_menu_frame's style)
 * whose content depends on b->phase -- a status header (both HP bars)
 * stays up throughout, with either the current message, or the
 * item/attack/guard menu with a ">" cursor, or the special-move timing
 * bar underneath.
 * ---------------------------------------------------------------------- */
static void draw_battle_status(const Battle *b) {
    char buf[40];
    int n;

    n = s_cat(buf, 0, SPECIES[b->foe.species].name);
    n = s_cat(buf, n, " LV");
    n = s_cat_uint(buf, n, b->foe.lv);
    n = s_cat(buf, n, " HP ");
    n = s_cat_uint(buf, n, b->foe.hp);
    n = s_cat(buf, n, "/");
    n = s_cat_uint(buf, n, b->foe.maxHp);
    buf[n] = 0;
    draw_text_s(buf, MENU_X + 8, MENU_Y + 8, rgb565(197, 206, 198), MENU_SCALE);

    n = s_cat(buf, 0, SPECIES[b->pl.species].name);
    n = s_cat(buf, n, " LV");
    n = s_cat_uint(buf, n, b->pl.lv);
    n = s_cat(buf, n, " HP ");
    n = s_cat_uint(buf, n, b->pl.hp);
    n = s_cat(buf, n, "/");
    n = s_cat_uint(buf, n, b->pl.maxHp);
    buf[n] = 0;
    draw_text_s(buf, MENU_X + 8, MENU_Y + 8 + MENU_ROW_H, rgb565(232, 228, 216), MENU_SCALE);
}

static void draw_battle_menu_row(const char *label, int idx, int cur, int y) {
    u16 color = (idx == cur) ? rgb565(232, 228, 216) : rgb565(138, 134, 120);
    draw_text_s(idx == cur ? ">" : " ", MENU_X + 8, y, color, MENU_SCALE);
    draw_text_s(label, MENU_X + 16, y, color, MENU_SCALE);
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
    return n;
}

static int battle_item_menu_kind(const Bag *bag, int idx) {
    int i = 0;
    if(idx == i++) return ITEM_PASS;
    if(bag->salve > 0)      { if(idx == i++) return ITEM_SALVE; }
    if(bag->bandage > 0)    { if(idx == i++) return ITEM_BANDAGE; }
    if(bag->bitterroot > 0) { if(idx == i++) return ITEM_BITTERROOT; }
    if(bag->dust > 0)       { if(idx == i++) return ITEM_DUST; }
    if(bag->gem > 0)        { if(idx == i++) return ITEM_GEM; }
    return ITEM_PASS; /* unreachable: idx is always < battle_item_menu_count() */
}

/* fillItemMenu, minus the "switch" row (this port's party has no
   manual-switch UI -- see the item-menu comment in main()). Capture
   Crystal's label includes the live capture chance, matching
   fillItemMenu's wild-battle branch (the trainer branch, plain
   "Capture Crystal xN", is dead code here -- b->wild is always 1). */
static int draw_battle_item_menu(const Battle *b, const Bag *bag, int cur) {
    int y = MENU_Y + 32;
    int i = 0;
    char buf[40];
    int n;

    draw_battle_menu_row("PASS", i++, cur, y); y += MENU_ROW_H;

    if(bag->salve > 0) {
        n = s_cat(buf, 0, "MOSS SALVE UP TO 22 HP X");
        n = s_cat_uint(buf, n, bag->salve);
        buf[n] = 0;
        draw_battle_menu_row(buf, i++, cur, y); y += MENU_ROW_H;
    }
    if(bag->bandage > 0) {
        n = s_cat(buf, 0, "LINEN WRAP UP TO 12 HP X");
        n = s_cat_uint(buf, n, bag->bandage);
        buf[n] = 0;
        draw_battle_menu_row(buf, i++, cur, y); y += MENU_ROW_H;
    }
    if(bag->bitterroot > 0) {
        n = s_cat(buf, 0, "BITTERROOT STR+4 X");
        n = s_cat_uint(buf, n, bag->bitterroot);
        buf[n] = 0;
        draw_battle_menu_row(buf, i++, cur, y); y += MENU_ROW_H;
    }
    if(bag->dust > 0) {
        n = s_cat(buf, 0, "ASH DUST STR-3 AGI-2 SPC-2 X");
        n = s_cat_uint(buf, n, bag->dust);
        buf[n] = 0;
        draw_battle_menu_row(buf, i++, cur, y); y += MENU_ROW_H;
    }
    if(bag->gem > 0) {
        n = s_cat(buf, 0, "CAPTURE CRYSTAL ");
        n = s_cat_uint(buf, n, battle_capture_chance(b));
        n = s_cat(buf, n, " PCT X");
        n = s_cat_uint(buf, n, bag->gem);
        buf[n] = 0;
        draw_battle_menu_row(buf, i++, cur, y); y += MENU_ROW_H;
    }
    return i; /* row count, for input handling to map kinds <-> cursor */
}

static void draw_battle_atk_menu(const Battle *b, int cur) {
    int y = MENU_Y + 32;
    char buf[32];
    int n;

    draw_battle_menu_row(SPECIES[b->pl.species].basic, 0, cur, y); y += MENU_ROW_H;

    n = s_cat(buf, 0, SPECIES[b->pl.species].special);
    n = s_cat(buf, n, " ");
    n = s_cat_uint(buf, n, b->pl.spp);
    n = s_cat(buf, n, "/");
    n = s_cat_uint(buf, n, b->pl.sppMax);
    buf[n] = 0;
    draw_battle_menu_row(buf, 1, cur, y); y += MENU_ROW_H;

    draw_battle_menu_row("WAIT", 2, cur, y);
}

static void draw_battle_guard_menu(int cur) {
    int y = MENU_Y + 32;
    draw_battle_menu_row("DODGE AGI", 0, cur, y); y += MENU_ROW_H;
    draw_battle_menu_row("BLOCK STR", 1, cur, y); y += MENU_ROW_H;
    draw_battle_menu_row("BARRIER SPC", 2, cur, y);
}

static void draw_battle_minigame(const Battle *b) {
    int bar_x = MENU_X + 8, bar_y = MENU_Y + 40, bar_w = MENU_W - 16, bar_h = 10;
    int needle_x = bar_x + (int)(b->mg * (float)bar_w / 100.0f);

    fill_rect(bar_x, bar_y, bar_w, bar_h, rgb565(40, 38, 32));
    fill_rect(bar_x + (int)(0.38f * (float)bar_w), bar_y,
              (int)(0.24f * (float)bar_w), bar_h, rgb565(90, 122, 82));
    fill_rect(bar_x + (int)(0.46f * (float)bar_w), bar_y,
              (int)(0.08f * (float)bar_w), bar_h, rgb565(197, 206, 198));
    fill_rect(needle_x - 1, bar_y - 4, 2, bar_h + 8, 0xFFFF);
    draw_text_s("A TO STRIKE", MENU_X + 8, bar_y + bar_h + 8, rgb565(138, 134, 120), MENU_SCALE);
}

static void draw_battle(const Battle *b, const Bag *bag) {
    draw_menu_frame(b->phase == 0 ? "BATTLE" :
                     b->phase == 1 ? "ITEM" :
                     b->phase == 2 ? "ATTACK" :
                     b->phase == 3 ? "GUARD" : "QUILLBURST");
    draw_battle_status(b);

    switch(b->phase) {
        case 0:
            draw_wrapped(b->msg[b->msg_i], MENU_X + 8, MENU_Y + 32,
                         rgb565(232, 228, 216), MENU_SCALE, MENU_W / 8 - 2, 9);
            break;
        case 1:
            draw_battle_item_menu(b, bag, b->cur);
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
 * World NPCs. Every VELD/FOREST/GROVE character below is a stationary
 * proximity-interact point (this port's own interact()/closest_mark
 * pattern, already used for HOUSE's bed/shelf/crate), not the
 * reference's real-time actor: state.lua's soldiers patrol and give
 * chase on line-of-sight, and Mason force-walks toward the player the
 * moment they leave the house (masonPh 1, freezing player movement
 * until he catches up and starts masonFight). Porting that AI
 * faithfully is a real-time movement/collision system on the scale of
 * the battle system already built, for three soldiers plus Mason;
 * given everything else still to port (this comment's neighbors), the
 * pragmatic choice here is the same mechanical outcome (you can't
 * reach Calder without going through the soldiers/Mason encounters
 * that gate the path) reached by walking up and interacting, not by
 * being chased down. Mason's fixed spot is near the VELD door (the
 * player's exit point, close to where the reference's chase would
 * have caught them anyway); soldiers stand at their own patrol
 * origins (data.spawnOf(FOREST, "1"/"2"/"3")).
 *
 * Anne is not ported at all: state.lua defines maybeAnne() (the only
 * code that would ever set G.annePh away from 0, spawning her) but
 * never calls it from anywhere in state.lua or input.lua -- checked
 * directly against the source, not inferred. G.annePh only otherwise
 * changes once it's already non-zero. She is unreachable in the
 * reference as shipped, so leaving her out (her TALK text included --
 * see the note near TALK_BRAM_OPEN above) is the faithful port, not a
 * cut corner.
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

/* Index order matches data.ITEM_ORDER = {salve,bandage,bitterroot,dust,gem}. */
#define ITEM_COUNT 5
static const ItemDef ITEMS[ITEM_COUNT] = {
    { "MOSS SALVE",      10, 5 },
    { "LINEN WRAP",       6, 3 },
    { "BITTERROOT",       8, 4 },
    { "ASH DUST",         8, 4 },
    { "CAPTURE CRYSTAL", 20, 10 },
};

static int *bag_field(Bag *bag, int idx) {
    switch(idx) {
        case 0: return &bag->salve;
        case 1: return &bag->bandage;
        case 2: return &bag->bitterroot;
        case 3: return &bag->dust;
        default: return &bag->gem;
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

    draw_menu_frame("BRAMS STALL");

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
        draw_battle_menu_row(row, i, cur, y);
        y += MENU_ROW_H;
    }
}

/* ----------------------------------------------------------------------
 * Ending screens, ported from render.lua's drawEnding()/drawDemoEnd():
 * data.ENDING_WIN after beating Calder, data.DEMO_END after beating
 * Shinigami. Both just step through their lines on A and return to
 * the title screen at the end.
 * ---------------------------------------------------------------------- */
static void draw_ending(const char *const *lines, int n, int i) {
    vram_clear();
    draw_text_center_s("CRYMON", SCREEN_W / 2, 24, 0xFFFF, 2);
    if(i < n)
        draw_wrapped(lines[i], 12, 80, rgb565(197, 206, 198), DIALOGUE_SCALE, 37, 9);
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
                     int to_map, char mark, int from_south) {
    int col, row, sx, sy;
    *map_id = to_map;
    find_mark(to_map, mark, &col, &row);
    sx = col * TILE + TILE / 2;
    sy = row * TILE + TILE / 2;
    *px = sx;
    *py = from_south ? (sy + TILE + 8) : (sy - TILE);
    *pdir = from_south ? 0 : 1;
}

void main(void) {
    int state = 0; /* 0 = title screen, 1 = starting room */
    u32 frame_count = 0;
    int prev_start = 0, prev_a = 0, prev_b = 0, prev_y = 0;
    int prev_up = 0, prev_down = 0, prev_left = 0, prev_right = 0;
    int map_id = MAP_HOUSE;
    int px, py, pdir = 0; /* dir: 0=down,1=up,2=left,3=right */
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
    Bag bag = { 2, 2, 1, 1, 0 }; /* salve, bandage, bitterroot, dust, gem */
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

    /* 0 = no menu, 1 = bag, 2 = party. Opened from the world with Y /
       START (state.lua's selectPressed()/startPressed() -- there's no
       Select button on a Dreamcast pad, so Y stands in for it), closed
       with B (state.lua's cancelPressed(), which also accepts start
       and select; B alone is enough here since neither Y nor START
       need a second meaning while a menu is open). */
    int menu_mode = 0;

    /* Active dialogue sequence: seq_lines/seq_len name the current
       TALK_* array, seq_beat indexes into it. seq_lines == 0 means no
       dialogue is showing. post_action fires once the sequence
       finishes (state.lua's afterTalk/beginTalkEnd): starting a
       trainer battle or opening the shop. */
    const char *const *seq_lines = 0;
    int seq_len = 0, seq_beat = 0;
    int post_action = 0, post_soldier_id = 0;
#define POST_NONE      0
#define POST_CALDER    1
#define POST_MASON     2
#define POST_SHINIGAMI 3
#define POST_SOLDIER   4
#define POST_CATHLEEN  5
#define POST_SHOP      6

    /* World NPC/pickup flags, matching state.lua's G.talkedWren etc.
       (see the world-NPC section comment above for what's ported vs
       simplified). mason_spawned gates whether Mason's mark is
       interactive at all -- he doesn't exist in the world until the
       player first leaves the house. */
    int talked_wren = 0, talked_mae = 0, talked_ivo = 0, talked_nell = 0;
    int talked_pike = 0, pike_helped = 0, nell_bonus = 0;
    int got_herb = 0, got_gem = 0, got_stump = 0, read_cart = 0;
    int beat_calder = 0, beat_mason = 0, beat_shin = 0, cath_caught = 0;
    int mason_spawned = 0;
    int soldier_beaten[3] = { 0, 0, 0 };

    /* Shop (Bram) and ending screens. */
    int shop_open = 0, shop_sell_tab = 0, shop_cur = 0;
    int ending_mode = 0; /* 0 none, 1 ENDING_WIN, 2 DEMO_END */
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

        raw = maple_poll_buttons();
        start_now = pressed(raw, CONT_START);
        a_now     = pressed(raw, CONT_A);
        b_now     = pressed(raw, CONT_B);
        y_now     = pressed(raw, CONT_Y);
        up_now    = pressed(raw, CONT_DPAD_UP);
        down_now  = pressed(raw, CONT_DPAD_DOWN);
        left_now  = pressed(raw, CONT_DPAD_LEFT);
        right_now = pressed(raw, CONT_DPAD_RIGHT);

        if(state == 0) {
            if(start_now && !prev_start) {
                state = 1;
                /* Seeds the battle RNG from however many vblanks
                   passed while the player sat at the title screen --
                   see rng_next()'s comment for why this stands in for
                   a real RTC/rand() source. */
                rng_state ^= frame_count | 1u;
            }
        }
        else if(menu_mode) {
            /* state.lua's MODE.BAG/MODE.PARTY update: only closing is
               handled, matching the reference (no cursor/use/swap
               logic in this port's scope). */
            if((b_now && !prev_b) || (start_now && !prev_start))
                menu_mode = 0;
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
                                    beat_calder = 1;
                                    marks += 18;
                                    ending_mode = 1;
                                    ending_i = 0;
                                    in_battle = 0;
                                }
                                else if(battle.trainer_kind == TRAINER_SOLDIER) {
                                    soldier_beaten[battle.soldier_id] = 1;
                                    marks += 8;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_SOLDIER_AFTER;
                                    seq_len = TALK_LEN(TALK_SOLDIER_AFTER);
                                    seq_beat = 0;
                                }
                                else if(battle.trainer_kind == TRAINER_MASON) {
                                    beat_mason = 1;
                                    marks += 10;
                                    in_battle = 0;
                                    enc_lock = 3;
                                    seq_lines = TALK_MASON_WIN;
                                    seq_len = TALK_LEN(TALK_MASON_WIN);
                                    seq_beat = 0;
                                }
                                else if(battle.trainer_kind == TRAINER_SHINIGAMI) {
                                    beat_shin = 1;
                                    marks += 14;
                                    ending_mode = 2;
                                    ending_i = 0;
                                    in_battle = 0;
                                }
                                else {
                                    int n;
                                    marks += 3;
                                    if(battle.foe.species == SP_CATHLEEN) {
                                        seq_lines = TALK_CATHLEEN_AFTER;
                                        seq_len = TALK_LEN(TALK_CATHLEEN_AFTER);
                                        seq_beat = 0;
                                        in_battle = 0;
                                        enc_lock = 3;
                                    }
                                    else {
                                        n = s_cat(battle.msg[0], 0, SPECIES[party[lead].species].name);
                                        if(battle.grew) {
                                            n = s_cat(battle.msg[0], n, " GREW TO LV");
                                            n = s_cat_uint(battle.msg[0], n, party[lead].lv);
                                        }
                                        else {
                                            n = s_cat(battle.msg[0], n, " STANDS OVER THE GRASS");
                                        }
                                        battle.msg[0][n] = 0;
                                        battle.msg_n = 1;
                                        battle.msg_i = 0;
                                        battle.phase = 0;
                                        battle.after = BAFTER_WIN_NOTE;
                                    }
                                }
                                break;
                            }
                            case BAFTER_WIN_NOTE:
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
                            case BAFTER_LOSS: {
                                int revived = party[lead].maxHp * 2 / 5;
                                if(revived < 1) revived = 1;
                                party[lead].hp = revived;
                                in_battle = 0;
                                enc_lock = 3;
                                seq_lines = TALK_LOSS;
                                seq_len = TALK_LEN(TALK_LOSS);
                                seq_beat = 0;
                                break;
                            }
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
                int n_rows = (battle.phase == 1) ? battle_item_menu_count(&bag) : 3;

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
        else if(ending_mode) {
            /* drawEnding()/drawDemoEnd(): step through data.ENDING_WIN
               or data.DEMO_END on A, return to the title screen after
               the last line (state.lua returns to MODE.TITLE, which
               resetRun()s on the next confirm/start -- this port's
               title screen already restarts a fresh run by construction,
               since state==0 only ever leads into a freshly-initialized
               state==1 the first time; see the note below on why we
               don't attempt a true mid-session reset). */
            if(a_now && !prev_a) {
                ending_i++;
                {
                    int n = (ending_mode == 1) ? TALK_LEN(ENDING_WIN) : TALK_LEN(DEMO_END);
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

            if(!seq_lines) {
                int dx = 0, dy = 0;
                int map_w = MAPS[map_id].cols * TILE;
                int map_h = MAPS[map_id].rows_n * TILE;

                if(pressed(raw, CONT_DPAD_LEFT))  { dx = -1; pdir = 2; }
                if(pressed(raw, CONT_DPAD_RIGHT)) { dx = 1;  pdir = 3; }
                if(pressed(raw, CONT_DPAD_UP))    { dy = -1; pdir = 1; }
                if(pressed(raw, CONT_DPAD_DOWN))  { dy = 1;  pdir = 0; }

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

                    if(dx != 0 && !tile_blocked(map_id, tile_at(map_id, (nx + (dx > 0 ? 6 : -6)) / TILE,
                                                                 py / TILE), cath_caught)) {
                        px = nx;
                    }
                    if(dy != 0 && !tile_blocked(map_id, tile_at(map_id, px / TILE,
                                                                 (ny + (dy > 0 ? 6 : -6)) / TILE), cath_caught)) {
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
                            do_warp(&map_id, &px, &py, &pdir, MAP_VELD, 'D', 1);
                            door_lock = 20;
                            seq_lines = TALK_DOOR_OUT;
                            seq_len = TALK_LEN(TALK_DOOR_OUT);
                            seq_beat = 0;
                            /* footsteps/masonPh=1 in the reference (an
                               immediate chase-and-ambush) -- this port's
                               Mason waits at a fixed spot instead, see
                               the world-NPC section comment. */
                            mason_spawned = 1;
                        }
                    }
                    else if(map_id == MAP_VELD && here == 'D') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_HOUSE, 'D', 0);
                        door_lock = 20;
                        seq_lines = TALK_COTTAGE;
                        seq_len = TALK_LEN(TALK_COTTAGE);
                        seq_beat = 0;
                    }
                    else if(map_id == MAP_VELD && here == 'Z') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_FOREST, 'Y', 1);
                        door_lock = 20;
                        seq_lines = TALK_FOREST_ENTER;
                        seq_len = TALK_LEN(TALK_FOREST_ENTER);
                        seq_beat = 0;
                    }
                    else if(map_id == MAP_FOREST && here == 'Y') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_VELD, 'Z', 0);
                        door_lock = 20;
                        seq_lines = TALK_FOREST_LEAVE;
                        seq_len = TALK_LEN(TALK_FOREST_LEAVE);
                        seq_beat = 0;
                    }
                    else if(map_id == MAP_FOREST && here == 'O') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_GROVE, 'O', 1);
                        door_lock = 20;
                        seq_lines = TALK_GROVE_ENTER;
                        seq_len = TALK_LEN(TALK_GROVE_ENTER);
                        seq_beat = 0;
                    }
                    else if(map_id == MAP_GROVE && here == 'O') {
                        do_warp(&map_id, &px, &py, &pdir, MAP_FOREST, 'O', 0);
                        door_lock = 20;
                        seq_lines = TALK_GROVE_LEAVE;
                        seq_len = TALK_LEN(TALK_GROVE_LEAVE);
                        seq_beat = 0;
                    }
                }
            }

            if(a_now && !prev_a) {
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
                        if(party_n > 0 || post_action == POST_SHOP) {
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
                                    battle.bench_n = 0;
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
                    seq_beat = 0;
                }
                else if(map_id == MAP_FOREST) {
                    /* Soldiers, stationary at their patrol origin marks
                       (see the section comment above). */
                    int i;
                    for(i = 0; i < 3; i++) {
                        if(near_mark(map_id, SOLDIERS[i].mark, px, py, 676)) {
                            if(soldier_beaten[i]) {
                                seq_lines = TALK_SOLDIER_DONE;
                                seq_len = TALK_LEN(TALK_SOLDIER_DONE);
                            }
                            else {
                                seq_lines = TALK_SOLDIER_SPOT;
                                seq_len = TALK_LEN(TALK_SOLDIER_SPOT);
                                post_action = POST_SOLDIER;
                                post_soldier_id = i;
                            }
                            seq_beat = 0;
                            break;
                        }
                    }
                }
                else if(map_id == MAP_GROVE) {
                    /* Cathleen (mark '8') and Shinigami (mark '9'). */
                    if(near_mark(map_id, '9', px, py, 2704)) {
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
                    else if(!cath_caught && near_mark(map_id, '8', px, py, 2704)) {
                        seq_lines = TALK_CATHLEEN_SPOT;
                        seq_len = TALK_LEN(TALK_CATHLEEN_SPOT);
                        post_action = POST_CATHLEEN;
                        seq_beat = 0;
                    }
                    else if(cath_caught && near_mark(map_id, '8', px, py, 1600)) {
                        seq_lines = TALK_CATHLEEN_GONE;
                        seq_len = TALK_LEN(TALK_CATHLEEN_GONE);
                        seq_beat = 0;
                    }
                }
            }

            /* Mason: spawns (mason_spawned) the first time the player
               leaves the house, standing at a fixed VELD spot near the
               door (see the section comment above). Interact triggers
               his fight the first time, masonAfter afterward. */
            if(a_now && !prev_a && !seq_lines && mason_spawned && map_id == MAP_VELD) {
                int mx = 15 * TILE + TILE / 2, my = 5 * TILE + TILE / 2;
                int ddx = px - mx, ddy = py - my;
                if(ddx * ddx + ddy * ddy <= 676) {
                    if(beat_mason) {
                        seq_lines = TALK_MASON_AFTER;
                        seq_len = TALK_LEN(TALK_MASON_AFTER);
                    }
                    else {
                        seq_lines = TALK_MASON_FIGHT;
                        seq_len = TALK_LEN(TALK_MASON_FIGHT);
                        post_action = POST_MASON;
                    }
                    seq_beat = 0;
                }
            }

            /* Menu open, only from plain world state (state.lua only
               reaches selectPressed()/startPressed() outside TALK/
               BATTLE/etc, which here just means no dialogue active). */
            if(!seq_lines) {
                if(y_now && !prev_y)
                    menu_mode = 1;
                else if(start_now && !prev_start)
                    menu_mode = 2;
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
            draw_ending(ending_mode == 1 ? ENDING_WIN : DEMO_END,
                        ending_mode == 1 ? TALK_LEN(ENDING_WIN) : TALK_LEN(DEMO_END),
                        ending_i);
        }
        else {
            compute_camera(map_id, px, py, &cam_x, &cam_y);
            draw_map(map_id, cam_x, cam_y);
            draw_props(map_id, cam_x, cam_y);
            draw_player(px - cam_x, py - cam_y, pdir);
            draw_hud(got_shelf, looted_crate, bag.bandage);
            if(seq_lines)
                draw_dialogue_box(seq_lines[seq_beat]);
            if(menu_mode == 1)
                draw_bag_menu(&bag, marks);
            else if(menu_mode == 2)
                draw_party_menu(party, party_n, lead);
            if(in_battle)
                draw_battle(&battle, &bag);
            if(shop_open)
                draw_shop(&bag, marks, shop_sell_tab, shop_cur);
        }

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
