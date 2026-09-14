/*
 * Hello World (slideshow edition) - minimal bare-metal Dreamcast program.
 *
 * No KallistiOS, no BIOS calls. Boots to a "PRESS START" screen (drawn
 * with the same hand-rolled 8x8 font/put_pixel technique as the
 * original text-only version of this project). Pressing Start on
 * controller port A begins a slideshow of three photos; A/B cycle
 * forward/backward through them.
 *
 * Controller reading talks directly to the Maple bus controller
 * hardware (registers at 0xa05f6c00+) -- there's no simpler "read a
 * button register" shortcut on this platform; Maple is a serial bus
 * and every peripheral, controller included, is addressed by sending
 * it a DMA-driven request packet and parsing its response packet. The
 * register layout, request/response packet format, and the
 * command/function codes below were checked against KallistiOS's own
 * Maple driver (MIT licensed, https://github.com/KallistiOS/KallistiOS,
 * kernel/arch/dreamcast/hardware/maple/ (all .c files) and
 * kernel/arch/dreamcast/include/dc/maple.h /
 * kernel/arch/dreamcast/include/dc/maple/controller.h) rather than
 * hand-derived from memory, the same way PCSX-Redux's source was used
 * for the PS1 project's GPU/CD-ROM code.
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
        VRAM16[i] = 0x0000;
}

static void put_pixel(int x, int y, u16 color) {
    if(x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H)
        return;
    VRAM16[y * SCREEN_W + x] = color;
}

/* 8x8, 1bpp glyphs: bit 7 = leftmost pixel of each row. Covers the
   letters needed for "PRESS START". */
enum { GLYPH_SPACE, GLYPH_P, GLYPH_R, GLYPH_E, GLYPH_S, GLYPH_T, GLYPH_A };

static const uint8_t glyphs[7][8] = {
    /* space */
    { 0b00000000, 0b00000000, 0b00000000, 0b00000000,
      0b00000000, 0b00000000, 0b00000000, 0b00000000 },
    /* P */
    { 0b11111100, 0b10000010, 0b10000010, 0b11111100,
      0b10000000, 0b10000000, 0b10000000, 0b00000000 },
    /* R */
    { 0b11111110, 0b10000001, 0b10000001, 0b11111110,
      0b10010000, 0b10001000, 0b10000100, 0b00000000 },
    /* E */
    { 0b11111111, 0b10000000, 0b10000000, 0b11111100,
      0b10000000, 0b10000000, 0b11111111, 0b00000000 },
    /* S */
    { 0b01111110, 0b10000000, 0b10000000, 0b01111100,
      0b00000010, 0b00000010, 0b11111100, 0b00000000 },
    /* T */
    { 0b11111111, 0b00011000, 0b00011000, 0b00011000,
      0b00011000, 0b00011000, 0b00011000, 0b00000000 },
    /* A */
    { 0b00111100, 0b01000010, 0b10000001, 0b10000001,
      0b11111111, 0b10000001, 0b10000001, 0b00000000 },
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
    GLYPH_P, GLYPH_R, GLYPH_E, GLYPH_S, GLYPH_S, GLYPH_SPACE,
    GLYPH_S, GLYPH_T, GLYPH_A, GLYPH_R, GLYPH_T
};
#define MESSAGE_LEN (int)(sizeof(message) / sizeof(message[0]))

static void draw_press_start(void) {
    int i;
    int total_w = MESSAGE_LEN * GLYPH_PX;
    int ox = (SCREEN_W - total_w) / 2;
    int oy = (SCREEN_H - GLYPH_PX) / 2;

    vram_clear();
    for(i = 0; i < MESSAGE_LEN; i++)
        draw_glyph(ox + i * GLYPH_PX, oy, message[i]);
}

/* ----------------------------------------------------------------------
 * Minimal Maple bus controller driver (port A / unit 0 only).
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

#define CONT_C      (1u << 0)
#define CONT_B      (1u << 1)
#define CONT_A      (1u << 2)
#define CONT_START  (1u << 3)

/* DMA command/response buffers, 32-byte aligned as the hardware
   requires; accessed through the uncached P2 memory area (bit 29 set)
   so the CPU never sees stale cached copies of DMA-written data, and
   through the masked, region-free "physical" form (top 3 bits
   stripped) when handing addresses to the Maple registers themselves --
   both exactly as KallistiOS's own driver does it. */
static u32 maple_cmd_buf[8]  __attribute__((aligned(32)));
static u32 maple_resp_buf[64] __attribute__((aligned(32)));

#define P2(p)   ((volatile u32 *)(((u32)(p)) | 0x20000000u))
#define PHYS(p) (((u32)(p)) & 0x1fffffffu)

static void maple_init(void) {
    /* DMA address-range protection: allow the whole 16MB RAM window
       (0x0c000000-0x0cffffff), same fixed value KallistiOS computes
       for a standard 16MB Dreamcast. */
    MAPLE_DMA_PROT = 0x6155404fu;
    MAPLE_DMA_TSEL = 0;               /* software-triggered DMA */
    MAPLE_SPEED = 0x0000u | (50000u << 16); /* 2 Mbps, standard timeout */
    MAPLE_ENABLE = 1;
}

/* Polls controller port A, unit 0. Returns the raw (active-low)
   16-bit button field on success, or 0xffff (nothing pressed) if the
   transfer didn't complete in time -- e.g. no controller plugged in. */
static u16 maple_poll_buttons(void) {
    volatile u32 *cmd  = P2(maple_cmd_buf);
    volatile u32 *resp = P2(maple_resp_buf);
    u32 timeout;

    cmd[0] = 1u | (0u << 16) | 0x80000000u;   /* 1 param word, port 0, last */
    cmd[1] = PHYS(maple_resp_buf);
    cmd[2] = MAPLE_COMMAND_GETCOND | (0x20u << 8) | (0u << 16) | (1u << 24);
    cmd[3] = MAPLE_FUNC_CONTROLLER;

    MAPLE_DMA_ADDR = PHYS(maple_cmd_buf);
    MAPLE_STATE = 1;                          /* start DMA */

    for(timeout = 0; timeout < 2000000u; timeout++) {
        if(MAPLE_STATE == 0)
            break;
    }
    if(MAPLE_STATE != 0)
        return 0xffff;                        /* timed out, no response */

    /* Word 0 is {response(byte0), dst_addr, src_addr, data_len}
       packed little-endian, so the response code is its low byte.
       Word 1 is the echoed function code; word 2 has the raw
       (active-low) button bitfield in its low 16 bits. */
    if((resp[0] & 0xffu) != MAPLE_RESPONSE_DATATRF)
        return 0xffff;
    if(resp[1] != MAPLE_FUNC_CONTROLLER)
        return 0xffff;

    return (u16)(resp[2] & 0xffffu);
}

/* Raw Maple button bits are active-low (0 = pressed). */
static int pressed(u16 raw, u16 mask) {
    return (raw & mask) == 0;
}

/* ----------------------------------------------------------------------
 * Slideshow images: raw 320x240 RGB565, letterboxed, baked in by
 * src/image_data.S.
 * ---------------------------------------------------------------------- */
extern const uint16_t image0_rgb565[];
extern const uint16_t image1_rgb565[];
extern const uint16_t image2_rgb565[];

static void show_image(int idx) {
    const uint16_t *src;
    u32 i;

    if(idx == 0)      src = image0_rgb565;
    else if(idx == 1) src = image1_rgb565;
    else              src = image2_rgb565;

    for(i = 0; i < (u32)SCREEN_W * SCREEN_H; i++)
        VRAM16[i] = src[i];
}

void main(void) {
    int state = 0; /* 0 = start screen, 1 = slideshow */
    int cur_image = 0;
    int prev_start = 0, prev_a = 0, prev_b = 0;
    u16 raw;
    int start_now, a_now, b_now;

    video_init();
    maple_init();
    draw_press_start();

    for(;;) {
        wait_vblank();
        raw = maple_poll_buttons();

        start_now = pressed(raw, CONT_START);
        a_now     = pressed(raw, CONT_A);
        b_now     = pressed(raw, CONT_B);

        if(state == 0) {
            if(start_now && !prev_start) {
                state = 1;
                cur_image = 0;
                show_image(cur_image);
            }
        }
        else {
            if(a_now && !prev_a) {
                cur_image = (cur_image + 1) % 3;
                show_image(cur_image);
            }
            else if(b_now && !prev_b) {
                cur_image = (cur_image + 2) % 3;
                show_image(cur_image);
            }
        }

        prev_start = start_now;
        prev_a = a_now;
        prev_b = b_now;
    }
}
