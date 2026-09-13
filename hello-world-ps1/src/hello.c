/*
 * Hello World - minimal bare-metal PS1 (PlayStation 1) program.
 *
 * No PsyQ/PSn00bSDK library calls: this talks directly to the GPU's two
 * I/O ports (GP0 "data"/GP1 "control", at 0x1f801810/0x1f801814) to set
 * a 320x240 NTSC 15bpp display mode, then draws "HELLO WORLD" using the
 * GPU's monochrome-rectangle draw command (one filled rectangle per run
 * of lit pixels in each font row) -- the same hand-drawn 8x8 font used
 * in the companion SNES/Dreamcast/N64 hello-world projects.
 *
 * The GPU register addresses and command encodings were checked against
 * PCSX-Redux's OpenBIOS/psyqo (MIT licensed, https://github.com/
 * grumpycoders/pcsx-redux) -- common/hardware/gpu.h and hwregs.h for the
 * display-mode/fill commands, psyqo/primitives/rectangles.hh for the
 * monochrome rectangle opcode (0x60, confirmed not the 16px-aligned
 * "fast fill" command, which would have rounded these small glyph
 * blocks to much larger ones), and main/splash.c for the GPU
 * reset/mode-set/enable sequence -- rather than hand-derived from
 * memory, the same way KOS/libdragon source was used for the
 * Dreamcast/N64 projects.
 */

#include <stdint.h>

typedef unsigned int u32;

#define GPU_DATA   (*(volatile u32 *)0x1f801810u)
#define GPU_STATUS (*(volatile u32 *)0x1f801814u)

#define SCREEN_W 320
#define SCREEN_H 240

static void wait_gpu(void) {
    while((GPU_STATUS & 0x04000000u) == 0)
        ;
}

/* GP0(60h): monochrome, opaque, variable-size rectangle. Goes through the
   normal rasterizer (respects drawing area/offset, no 16px alignment
   rounding), unlike GP0(02h) fast-fill. */
static void draw_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    wait_gpu();
    GPU_DATA = 0x60000000u | r | ((u32)g << 8) | ((u32)b << 16);
    GPU_DATA = (u32)(x & 0xffff) | ((u32)y << 16);
    GPU_DATA = (u32)(w & 0xffff) | ((u32)h << 16);
}

static void video_init(void) {
    GPU_STATUS = 0x00000000u; /* GP1(00h): reset GPU */

    /* GP1(05h): display area start in VRAM (0,0) */
    GPU_STATUS = 0x05000000u | 0 | (0 << 10);

    /* GP1(06h): horizontal display range, standard NTSC 320-wide offset */
    GPU_STATUS = 0x06000000u | (0x260) | ((320 * 10 + 0x260) << 12);

    /* GP1(07h): vertical display range, standard 240-line NTSC window */
    GPU_STATUS = 0x07000000u | 16 | (255 << 10);

    /* GP1(08h): display mode -- 320-wide, 240 lines, NTSC, 15bpp, no
       interlace, normal (non-368) horizontal resolution */
    GPU_STATUS = 0x08000000u | 1;

    /* GP0(E3h)/(E4h): drawing area covers the whole framebuffer */
    GPU_DATA = 0xe3000000u | 0 | (0 << 10);
    GPU_DATA = 0xe4000000u | (SCREEN_W - 1) | ((SCREEN_H - 1) << 10);

    /* GP0(E5h): no drawing offset */
    GPU_DATA = 0xe5000000u | 0 | (0 << 11);

    /* GP1(03h): display enable (bit0 = 0 means ENABLED) */
    GPU_STATUS = 0x03000000u;
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
    int row, col_start;

    /* Coalesce each row's run(s) of lit pixels into one rectangle
       instead of one per pixel -- fewer, larger GPU commands for the
       same picture. */
    for(row = 0; row < 8; row++) {
        uint8_t bits = glyphs[glyph][row];
        int col = 0;

        while(col < 8) {
            if(!(bits & (0x80 >> col))) {
                col++;
                continue;
            }
            col_start = col;
            while(col < 8 && (bits & (0x80 >> col)))
                col++;

            draw_rect(ox + col_start * GLYPH_SCALE, oy + row * GLYPH_SCALE,
                      (col - col_start) * GLYPH_SCALE, GLYPH_SCALE,
                      255, 255, 255);
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

    draw_rect(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0); /* black background */

    for(i = 0; i < MESSAGE_LEN; i++)
        draw_glyph(ox + i * GLYPH_PX, oy, message[i]);

    for(;;)
        ;
}
