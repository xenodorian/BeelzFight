/*
 * Hello World - minimal bare-metal Nintendo 64 program.
 *
 * No libdragon SDK, no Nintendo IPL3/bootcode: this ROM is booted by
 * libdragon's own from-scratch, public-domain "IPL3 compat" bootcode
 * (see boot/ in https://github.com/DragonMinded/libdragon), which loads
 * this flat binary into RDRAM and jumps to it -- nothing here links
 * against or calls into libdragon's C library.
 *
 * This talks to the Video Interface (VI) hardware directly to bring up
 * a 320x240 16bpp (RGBA5551) framebuffer, then plots "HELLO WORLD" into
 * it with the same hand-drawn 8x8 font used in the companion SNES and
 * Dreamcast hello-world projects. The VI register values are the
 * standard NTSC 320x240 non-interlaced preset (see vi_ntsc_p in
 * libdragon's src/vi.h, which this was checked against).
 */

#include <stdint.h>

typedef unsigned short u16;
typedef unsigned int   u32;

#define VI_BASE 0xa4400000u
#define VI_REG(i) (*(volatile u32 *)(VI_BASE + (i) * 4))

#define SCREEN_W 320
#define SCREEN_H 240

/* KSEG0 (cached) -> KSEG1 (uncached) direct-mapped alias, so framebuffer
 * writes go straight to RDRAM instead of sitting in D-cache while the VI's
 * independent DMA reads stale data. */
#define UNCACHED(ptr) ((volatile u16 *)(0xa0000000u | ((u32)(ptr) & 0x1fffffffu)))

static uint16_t framebuffer[SCREEN_H][SCREEN_W] __attribute__((aligned(64)));

static void video_init(void) {
    u32 origin = (u32)framebuffer & 0x1fffffffu;

    /* VI_CTRL: default pixel advance, AA/resample off, 16bpp framebuffer */
    VI_REG(0) = (0b0011u << 12) | (0b11u << 8) | 0b10u;
    /* VI_ORIGIN: framebuffer physical address */
    VI_REG(1) = origin & 0xffffffu;
    /* VI_WIDTH */
    VI_REG(2) = SCREEN_W;
    /* VI_V_INTR (NTSC preset) */
    VI_REG(3) = 0x00000002u;
    /* VI_V_CURRENT is read-only status, left untouched */
    /* VI_BURST (NTSC preset) */
    VI_REG(5) = 0x03e52239u;
    /* VI_V_SYNC: total lines, NTSC non-interlaced */
    VI_REG(6) = 0x0000020du;
    /* VI_H_SYNC */
    VI_REG(7) = 0x00000c15u;
    /* VI_H_SYNC_LEAP */
    VI_REG(8) = 0x0c150c15u;
    /* VI_H_VIDEO: active horizontal window (NTSC preset) */
    VI_REG(9) = 0x006c02ecu;
    /* VI_V_VIDEO: active vertical window (NTSC preset) */
    VI_REG(10) = 0x002501ffu;
    /* VI_V_BURST */
    VI_REG(11) = 0x000e0204u;
    /* VI_X_SCALE: (1024*320+320)/640, for a 320-wide framebuffer */
    VI_REG(12) = 0x200u;
    /* VI_Y_SCALE: (1024*240+120)/240, for a 240-line framebuffer */
    VI_REG(13) = 0x400u;
}

static void fb_clear(u16 color) {
    volatile u16 *p = UNCACHED(&framebuffer[0][0]);
    u32 i;
    for(i = 0; i < (u32)SCREEN_W * SCREEN_H; i++)
        p[i] = color;
}

static void put_pixel(int x, int y, u16 color) {
    if(x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H)
        return;
    UNCACHED(&framebuffer[0][0])[y * SCREEN_W + x] = color;
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

    fb_clear(0x0001); /* black, RGBA5551 (alpha bit set) */

    for(i = 0; i < MESSAGE_LEN; i++)
        draw_glyph(ox + i * GLYPH_PX, oy, message[i]);

    video_init();

    for(;;)
        ;
}
