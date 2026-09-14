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

/* Raw 320x240 RGB565 image, letterboxed to preserve aspect ratio, baked
   in by src/image_data.S (see tools/gen_image.py for the conversion). */
extern const uint16_t image_rgb565[];

void main(void) {
    u32 i;

    video_init();

    for(i = 0; i < (u32)SCREEN_W * SCREEN_H; i++)
        VRAM16[i] = image_rgb565[i];

    for(;;)
        ;
}
