/*
 * CryMon - Dreamcast port, step 2: populate the starting room with its
 * entities (player + furniture) and hook up movement/interact
 * controls. Still no CryMon-catching mechanics and no leaving the
 * room via the door -- both explicitly deferred to a later step.
 *
 * Bare-metal, no KallistiOS/BIOS calls. Video setup, vblank sync, and
 * the Maple controller driver are unchanged from step 1 (see that
 * commit / hello-world-dreamcast/src/hello.c for the full derivation
 * notes against KallistiOS's real source).
 *
 * Room layout, tile colors, solid/walkable tiles, prop positions and
 * sizes, player movement speed, and the four interactable dialogue
 * lines are all taken from the reference implementation
 * (xenodorian/CryMon):
 *   - love/game/src/data.lua: data.HOUSE (tile grid), data.isSolidTile
 *     (SOLID_SET), data.spawnOf (tile-center world coords), data.TALK
 *     (dialogue text).
 *   - love/game/src/draw.lua: paintTile (tile colors), draw.actor
 *     (player silhouette: colored body + facing wedge, used here
 *     as-is since no sprite art pipeline exists yet for this port).
 *   - love/game/src/render.lua: drawWorld (prop positions/sizes: e.g.
 *     "prop-bed-father" 64x56, "prop-shelf" 40x44) and the movement
 *     code (speed = 110px/sec at the original's 32px tile size).
 * No monster-granting, item-granting, or door-warp logic is ported --
 * interacting always shows the same first-time line, and stepping on
 * the door tile does nothing.
 */

#include <stdint.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

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

/* Double buffering: draw into whichever buffer isn't currently being
   scanned out, then flip PVR_FB_ADDR to it right after a vblank wait.
   Without this, redrawing on every frame that the player moves writes
   into the same buffer the display hardware is actively reading from,
   which is what caused the movement-time flicker/tearing -- the
   earlier "only redraw when something changed" fix only addressed the
   at-rest case, since there's nothing to race when nothing redraws.
   Two 320x240x16bpp buffers (150 KB each) easily fit in the PVR's 8MB
   VRAM; 0x040000 (256KB) keeps the second buffer clear of the first
   with room to spare. */
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
 * The starting room ("HOUSE"), verbatim from CryMon's
 * love/game/src/data.lua (data.HOUSE), data.isSolidTile (SOLID_SET),
 * and love/game/src/draw.lua (paintTile). 14 columns x 11 rows, 32px
 * tiles in the original; drawn here at 20px tiles (280x220) so the
 * whole room fits centered on the Dreamcast's 320x240 screen -- this
 * step doesn't need camera scrolling since nothing leaves the room.
 * ---------------------------------------------------------------------- */
#define ROOM_TILE   20
#define ROOM_COLS   14
#define ROOM_ROWS   11
#define ROOM_OX     ((SCREEN_W - ROOM_COLS * ROOM_TILE) / 2)
#define ROOM_OY     ((SCREEN_H - ROOM_ROWS * ROOM_TILE) / 2)

static const char *const house_room[ROOM_ROWS] = {
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

/* data.lua's SOLID_SET restricted to the characters that actually
   appear in HOUSE: H (wall), B (father's bed), C (crate), U (empty
   bed) are solid; F, P, D, S are walkable (yes, the shelf tile itself
   is walkable in the reference game -- SOLID_SET has no 'S' in it). */
static int tile_is_solid(char ch) {
    return ch == 'H' || ch == 'B' || ch == 'C' || ch == 'U';
}

static char tile_at(int col, int row) {
    if(col < 0 || col >= ROOM_COLS || row < 0 || row >= ROOM_ROWS)
        return 'H';
    return house_room[row][col];
}

static void find_mark(char mark, int *out_col, int *out_row) {
    int row, col;
    for(row = 0; row < ROOM_ROWS; row++) {
        for(col = 0; col < ROOM_COLS; col++) {
            if(house_room[row][col] == mark) {
                *out_col = col;
                *out_row = row;
                return;
            }
        }
    }
    *out_col = 2;
    *out_row = 2;
}

static void draw_room_tile(char ch, int dx, int dy) {
    int t = ROOM_TILE;

    switch(ch) {
        case 'H':
            fill_rect(dx, dy, t, t, rgb565(42, 30, 22));
            return;
        case 'D':
            fill_rect(dx, dy, t, t, rgb565(26, 18, 12));
            return;
        case 'F':
        case 'P':
        case 'B':
        case 'U':
        case 'C':
        case 'S':
        default:
            fill_rect(dx, dy, t, t, rgb565(106, 82, 56));
            return;
    }
}

static void draw_house_background(void) {
    int row, col;

    /* The room (280x220) doesn't cover the full 320x240 screen, and
       with two alternating framebuffers the "other" one was never
       cleared -- without this, its border margin would show stale
       content from two frames ago the first time it's drawn into. */
    vram_clear();

    for(row = 0; row < ROOM_ROWS; row++)
        for(col = 0; col < ROOM_COLS; col++)
            draw_room_tile(house_room[row][col],
                            ROOM_OX + col * ROOM_TILE, ROOM_OY + row * ROOM_TILE);
}

/* Prop sizes below are the original 32px-tile-space sprite sizes
   (render.lua's drawPropImg calls) scaled by 20/32 to match our tile
   size; positions are each mark's tile center in our own room space. */
#define SCALE_NUM 20
#define SCALE_DEN 32
static int scale_len(int px) { return (px * SCALE_NUM) / SCALE_DEN; }

static void mark_center(char mark, int *out_x, int *out_y) {
    int col, row;
    find_mark(mark, &col, &row);
    *out_x = ROOM_OX + col * ROOM_TILE + ROOM_TILE / 2;
    *out_y = ROOM_OY + row * ROOM_TILE + ROOM_TILE / 2;
}

static void draw_prop(char mark, int w, int h, u16 color) {
    int cx, cy;
    mark_center(mark, &cx, &cy);
    fill_rect(cx - scale_len(w) / 2, cy - scale_len(h) / 2,
              scale_len(w), scale_len(h), color);
}

static void draw_props(void) {
    /* render.lua sizes: prop-bed-father/prop-bed-empty 64x56,
       prop-shelf 40x44, prop-crate 32x32. Distinct flat colors stand
       in for the sprite art (no asset pipeline yet for this port). */
    draw_prop('B', 64, 56, rgb565(120, 70, 60));  /* father, in bed   */
    draw_prop('U', 64, 56, rgb565(150, 140, 120)); /* empty bed        */
    draw_prop('S', 40, 44, rgb565(90, 60, 40));    /* shelf            */
    draw_prop('C', 32, 32, rgb565(110, 90, 50));   /* crate            */
}

/* Player silhouette, matching draw.lua's draw.actor(): a colored body
   block plus a facing wedge, since no sprite art exists yet. */
#define PLAYER_W scale_len(48)
#define PLAYER_H scale_len(52)

static void draw_player(int cx, int cy, int dir) {
    int x = cx - PLAYER_W / 2, y = cy - PLAYER_H;
    fill_rect(x, y, PLAYER_W, PLAYER_H, rgb565(90, 140, 200));
    fill_rect(x + 2, y + 2, PLAYER_W - 4, PLAYER_H / 3, rgb565(120, 170, 230));

    /* facing wedge, drawn as a small filled diamond-half at the front */
    {
        int fx = cx, fy = y + PLAYER_H / 3;
        int i;
        for(i = 0; i < 4; i++) {
            switch(dir) {
                case 0: fill_rect(fx - (3 - i), fy + i, (3 - i) * 2 + 1, 1, 0xFFFF); break; /* down */
                case 1: fill_rect(fx - (3 - i), fy + 3 - i, (3 - i) * 2 + 1, 1, 0xFFFF); break; /* up */
                case 2: fill_rect(fx - 3 + i, fy - (3 - i), 1, (3 - i) * 2 + 1, 0xFFFF); break; /* left */
                default: fill_rect(fx + 3 - i, fy - (3 - i), 1, (3 - i) * 2 + 1, 0xFFFF); break; /* right */
            }
        }
    }
}

/* ----------------------------------------------------------------------
 * Interact dialogue: first line only from each of data.TALK.bed /
 * father / shelf / crate (love/game/src/data.lua), upper-cased for our
 * A-Z-only font. No state is changed by interacting -- no monster or
 * item is granted, so the same line shows every time.
 * ---------------------------------------------------------------------- */
static const char *dialogue_for(char mark) {
    switch(mark) {
        case 'U': return "JUST UNTIL THEY BREATHE AGAIN";
        case 'B': return "THERES A WAR CRYTOWN IS BLEEDING";
        case 'S': return "THIS IS IT FATHERS CRYSTAL";
        case 'C': return "A WRAP HE WONT MISS IT";
        default:  return 0;
    }
}

static void draw_dialogue_box(const char *line) {
    fill_rect(4, SCREEN_H - 44, SCREEN_W - 8, 40, rgb565(18, 17, 14));
    fill_rect(4, SCREEN_H - 44, SCREEN_W - 8, 2, rgb565(197, 206, 198));
    fill_rect(4, SCREEN_H - 6, SCREEN_W - 8, 2, rgb565(197, 206, 198));
    draw_text_s(line, 12, SCREEN_H - 30, rgb565(232, 228, 216), DIALOGUE_SCALE);
}

/* interact() in state.lua: closest of U/B/S/C within a 36px radius
   (36*36=1296) in the original's 32px-tile space; scaled to our 20px
   tiles that's a 22.5px radius (22*22=484). */
static char closest_mark(int px, int py) {
    static const char marks[4] = { 'U', 'B', 'S', 'C' };
    int i;
    int best = 484, best_i = -1;

    for(i = 0; i < 4; i++) {
        int mx, my, dx, dy, d;
        mark_center(marks[i], &mx, &my);
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

void main(void) {
    int state = 0; /* 0 = title screen, 1 = starting room */
    int prev_start = 0, prev_a = 0;
    int px, py, pdir = 0; /* dir: 0=down,1=up,2=left,3=right */
    int col, row;
    const char *dialogue = 0;
    u16 raw;
    int start_now, a_now;

    video_init();
    maple_init();
    draw_press_start();
    fb_flip();

    find_mark('P', &col, &row);
    px = ROOM_OX + col * ROOM_TILE + ROOM_TILE / 2;
    py = ROOM_OY + row * ROOM_TILE + ROOM_TILE / 2;

    for(;;) {
        wait_vblank();
        raw = maple_poll_buttons();
        start_now = pressed(raw, CONT_START);
        a_now     = pressed(raw, CONT_A);

        if(state == 0) {
            if(start_now && !prev_start) {
                state = 1;
                draw_house_background();
                draw_props();
                draw_player(px, py, pdir);
                fb_flip();
            }
        }
        else {
            int dx = 0, dy = 0;
            int old_px = px, old_py = py, old_dir = pdir;
            const char *old_dialogue = dialogue;

            if(pressed(raw, CONT_DPAD_LEFT))  { dx = -1; pdir = 2; }
            if(pressed(raw, CONT_DPAD_RIGHT)) { dx = 1;  pdir = 3; }
            if(pressed(raw, CONT_DPAD_UP))    { dy = -1; pdir = 1; }
            if(pressed(raw, CONT_DPAD_DOWN))  { dy = 1;  pdir = 0; }

            if(dx != 0 || dy != 0) {
                /* Axis-separated movement so the player slides along
                   walls instead of stopping dead on a diagonal. Half
                   the collision box (6px) is checked at the
                   candidate feet position. */
                int speed = 1; /* px/frame; ~60px/sec at 60fps, scaled
                                  down from the original's 110px/sec
                                  at 32px tiles for our smaller room */
                int nx = px + dx * speed;
                int ny = py + dy * speed;

                if(dx != 0 && !tile_is_solid(tile_at((nx + (dx > 0 ? 6 : -6) - ROOM_OX) / ROOM_TILE,
                                                      (py - ROOM_OY) / ROOM_TILE))) {
                    px = nx;
                }
                if(dy != 0 && !tile_is_solid(tile_at((px - ROOM_OX) / ROOM_TILE,
                                                      (ny + (dy > 0 ? 6 : -6) - ROOM_OY) / ROOM_TILE))) {
                    py = ny;
                }

                if(px < ROOM_OX + 8) px = ROOM_OX + 8;
                if(px > ROOM_OX + ROOM_COLS * ROOM_TILE - 8) px = ROOM_OX + ROOM_COLS * ROOM_TILE - 8;
                if(py < ROOM_OY + 8) py = ROOM_OY + 8;
                if(py > ROOM_OY + ROOM_ROWS * ROOM_TILE - 4) py = ROOM_OY + ROOM_ROWS * ROOM_TILE - 4;
            }

            if(a_now && !prev_a) {
                char mark = closest_mark(px, py);
                dialogue = mark ? dialogue_for(mark) : 0;
            }

            /* Redraw (into the back buffer) only when something
               actually changed -- no point flipping to a frame
               identical to the one already on screen. Combined with
               fb_flip() below, this is what actually fixes tearing
               while moving: each new frame is fully drawn into the
               buffer NOT currently being scanned out, and only shown
               once it's complete, instead of being painted piece by
               piece into the buffer the display is actively reading
               (which is what the earlier "skip redraw when idle" fix
               didn't address -- it stopped the idle flicker because
               there was nothing left to race when nothing redrew, but
               every frame that *did* redraw during movement was still
               racing the single on-screen buffer). */
            if(px != old_px || py != old_py || pdir != old_dir || dialogue != old_dialogue) {
                draw_house_background();
                draw_props();
                draw_player(px, py, pdir);
                if(dialogue)
                    draw_dialogue_box(dialogue);
                fb_flip();
            }
        }

        prev_start = start_now;
        prev_a = a_now;
    }
}
