/*
 * Hello World - minimal bare-metal Dreamcast program.
 *
 * No KallistiOS, no BIOS calls: this talks to the PowerVR2 (PVR) display
 * hardware's scan-out registers directly to bring up a 320x240 RGB565
 * framebuffer, then plots "HELLO WORLD" into it with a hand-drawn 8x8
 * font (the same glyph bitmaps used in the companion SNES hello-world
 * project). The register set/values mirror what KallistiOS's vid_init()
 * does for its DM_320x240_NTSC mode -- see video.c in KallistiOS for the
 * reference implementation this was checked against.
 */

#include <stdint.h>

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

#define VRAM16 ((volatile u16 *)0xa5000000u)

#define SCREEN_W 320
#define SCREEN_H 240

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
    /* Blank the display while we reconfigure it */
    PVR(PVR_VIDEO_CFG) = PVR(PVR_VIDEO_CFG) | 0x8u;
    PVR(PVR_FB_CFG_1)  = PVR(PVR_FB_CFG_1) & ~1u;

    PVR(PVR_BORDER_COLOR) = 0;

    /* Pixel format: RGB565 (mode 1), display still disabled (bit 0 = 0) */
    PVR(PVR_FB_CFG_1) = (1u << 2);
    /* RGB565 + dithering, matching KallistiOS's vid_bpp_to_pvr_cfg2[] */
    PVR(PVR_FB_CFG_2) = 1u | (1u << 3);

    /* Bytes per scanline, in the hardware's 8-byte units */
    PVR(PVR_RENDER_MODULO) = (SCREEN_W * 2) / 8;

    /* Framebuffer lives at the very start of VRAM */
    PVR(PVR_FB_ADDR) = 0;

    /* Non-interlaced display size */
    PVR(PVR_FB_SIZE) = (((SCREEN_W * 2) / 4) - 1)
                      | (1u << 20)
                      | ((SCREEN_H - 1u) << 10);

    PVR(PVR_VPOS_IRQ) = (SCANINT1 << 16) | SCANINT2;

    /* Progressive (non-interlaced) */
    PVR(PVR_IL_CFG) = 0x100;

    PVR(PVR_BORDER_X) = (BORDERX1 << 16) | BORDERX2;
    PVR(PVR_BORDER_Y) = (BORDERY1 << 16) | BORDERY2;
    PVR(PVR_SCAN_CLK) = (SCANLINES << 16) | CLOCKS;

    /* Horizontal pixel doubling, required for this mode at this resolution */
    PVR(PVR_VIDEO_CFG) = PVR(PVR_VIDEO_CFG) | 0x100u;

    PVR(PVR_BITMAP_X) = BITMAPX;
    PVR(PVR_BITMAP_Y) = (BITMAPY << 16) | BITMAPY;

    /* Cable type selector (0=VGA,1=none,2=RGB,3=composite); composite is
       the safest default when nothing is auto-detected. Cosmetic only for
       emulation, but matches what real hardware/KOS also configures. */
    *(volatile u32 *)0xa0702c00 =
        (*(volatile u32 *)0xa0702c00 & 0xfffffcffu) | (3u << 8);

    /* Un-blank and enable the display */
    PVR(PVR_VIDEO_CFG) = PVR(PVR_VIDEO_CFG) & ~0x8u;
    PVR(PVR_FB_CFG_1)  = PVR(PVR_FB_CFG_1) | 1u;
}

static void vram_clear(void) {
    u32 i;
    for(i = 0; i < (u32)SCREEN_W * SCREEN_H; i++)
        VRAM16[i] = 0x0000;
}

static void put_pixel(int x, int y, u16 color) {
    if(x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H)
        return;
    VRAM16[y * SCREEN_W + x] = color;
}

/* 8x8, 1bpp glyphs: bit 7 = leftmost pixel of each row. Index 0 is a
   blank space; the rest are the letters needed to spell "HELLO WORLD". */
enum { GLYPH_SPACE, GLYPH_H, GLYPH_E, GLYPH_L, GLYPH_O, GLYPH_W, GLYPH_R, GLYPH_D };

static const uint8_t glyphs[8][8] = {
    /* space */
    { 0b00000000, 0b00000000, 0b00000000, 0b00000000,
      0b00000000, 0b00000000, 0b00000000, 0b00000000 },
    /* H */
    { 0b10000001, 0b10000001, 0b10000001, 0b11111111,
      0b10000001, 0b10000001, 0b10000001, 0b00000000 },
    /* E */
    { 0b11111111, 0b10000000, 0b10000000, 0b11111100,
      0b10000000, 0b10000000, 0b11111111, 0b00000000 },
    /* L */
    { 0b10000000, 0b10000000, 0b10000000, 0b10000000,
      0b10000000, 0b10000000, 0b11111111, 0b00000000 },
    /* O */
    { 0b01111110, 0b10000001, 0b10000001, 0b10000001,
      0b10000001, 0b10000001, 0b01111110, 0b00000000 },
    /* W */
    { 0b10000001, 0b10000001, 0b10000001, 0b10100101,
      0b10100101, 0b11011011, 0b10000001, 0b00000000 },
    /* R */
    { 0b11111110, 0b10000001, 0b10000001, 0b11111110,
      0b10010000, 0b10001000, 0b10000100, 0b00000000 },
    /* D */
    { 0b11111100, 0b10000010, 0b10000001, 0b10000001,
      0b10000001, 0b10000010, 0b11111100, 0b00000000 },
};

#define GLYPH_SCALE 3
#define GLYPH_PX    (8 * GLYPH_SCALE)

static void draw_glyph(int ox, int oy, int glyph) {
    int row, col, sx, sy;

    for(row = 0; row < 8; row++) {
        uint8_t bits = glyphs[glyph][row];

        for(col = 0; col < 8; col++) {
            if(!(bits & (0x80 >> col)))
                continue;

            for(sy = 0; sy < GLYPH_SCALE; sy++)
                for(sx = 0; sx < GLYPH_SCALE; sx++)
                    put_pixel(ox + col * GLYPH_SCALE + sx,
                              oy + row * GLYPH_SCALE + sy,
                              0xFFFF);
        }
    }
}

static const int message[] = {
    GLYPH_H, GLYPH_E, GLYPH_L, GLYPH_L, GLYPH_O, GLYPH_SPACE,
    GLYPH_W, GLYPH_O, GLYPH_R, GLYPH_L, GLYPH_D
};
#define MESSAGE_LEN (int)(sizeof(message) / sizeof(message[0]))

void main(void) {
    int i;
    int total_w = MESSAGE_LEN * GLYPH_PX;
    int ox = (SCREEN_W - total_w) / 2;
    int oy = (SCREEN_H - GLYPH_PX) / 2;

    video_init();
    vram_clear();

    for(i = 0; i < MESSAGE_LEN; i++)
        draw_glyph(ox + i * GLYPH_PX, oy, message[i]);

    for(;;)
        ;
}
