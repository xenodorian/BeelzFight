/*
 * Hello World (image edition) - minimal bare-metal PS1 program.
 *
 * No PsyQ/PSn00bSDK library calls. Sets up the same 320x240 15bpp GPU
 * mode as the original text version, then instead of drawing glyphs,
 * reads a raw image straight off the CD (via direct, from-scratch
 * CD-ROM controller register access -- see cdrom_read_sectors() below)
 * and uploads it into VRAM.
 *
 * Why the image isn't just baked into this executable as a data blob
 * (which is how the SNES/Dreamcast versions of this project do it):
 * doing that here made the resulting ~150KB executable fail to load in
 * every PS1 emulator available (Mednafen, PCSX-Redux), both via direct
 * sideload and via a normal CD boot -- while the same emulators load a
 * tiny (~3KB) executable, like the original text version, instantly.
 * Padding that same original tiny executable with harmless zero bytes
 * up to a similar size reproduces the identical failure, which rules
 * out a bug in the image-handling code itself and points at some
 * loader-side issue specific to a PS-EXE of that size in this
 * environment. Keeping the boot executable small and pulling the image
 * data from a second file on the disc at runtime -- something real
 * commercial PS1 games do as a matter of course, since games are
 * always much bigger than what fits in one loaded executable -- avoids
 * the problem entirely.
 *
 * CD-ROM register addresses, command opcodes, and the exact
 * command/response/data-transfer handshake below were checked against
 * PCSX-Redux's own CD-ROM controller implementation (MIT licensed,
 * https://github.com/grumpycoders/pcsx-redux, src/core/cdrom.cc) --
 * the same file that emulates this hardware for every PS1 program that
 * runs on that emulator -- rather than hand-derived from memory.
 */

#include <stdint.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define GPU_DATA   (*(volatile u32 *)0x1f801810u)
#define GPU_STATUS (*(volatile u32 *)0x1f801814u)

#define SCREEN_W 320
#define SCREEN_H 240

static void wait_gpu(void) {
    while((GPU_STATUS & 0x04000000u) == 0)
        ;
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

/* ----------------------------------------------------------------------
 * Minimal CD-ROM controller driver.
 *
 * Registers (all byte-wide), selected by the low 2 bits of 0x1f801800:
 *   0x1f801800  Index/Status (R: status flags; W: select index 0-3)
 *   0x1f801801  index0 W: Command register   | index1 R: Response FIFO
 *   0x1f801802  index0 W: Parameter FIFO     | any index R: Data FIFO
 *   0x1f801803  index0 W: Request register   | index1 W: Int Flag reg (ack)
 *
 * Status register bits (0x1f801800 read):
 *   0x20  response FIFO not empty
 *   0x40  data FIFO not empty
 *   0x80  command/parameter busy
 *
 * Interrupt Flag register (index1 read of 0x1f801803) low 3 bits give
 * the pending interrupt number: 1 = DataReady, 2 = Complete,
 * 3 = Acknowledge, 5 = DiskError. Acknowledge by writing the same bits
 * (plus 0x40 to also reset the parameter FIFO) back to that register.
 * ---------------------------------------------------------------------- */
#define CD_REG0 (*(volatile u8 *)0x1f801800u)
#define CD_REG1 (*(volatile u8 *)0x1f801801u)
#define CD_REG2 (*(volatile u8 *)0x1f801802u)
#define CD_REG3 (*(volatile u8 *)0x1f801803u)

#define CD_CMD_SETLOC 0x02
#define CD_CMD_READN  0x06
#define CD_CMD_PAUSE  0x09
#define CD_CMD_SETMODE 0x0e

#define CD_INT_MASK    0x07
#define CD_INT_DATAREADY 1
#define CD_INT_COMPLETE  2
#define CD_INT_ACK       3

static void cd_set_index(int idx) {
    CD_REG0 = (u8)idx;
}

static int cd_get_int(void) {
    cd_set_index(1);
    return CD_REG3 & CD_INT_MASK;
}

static void cd_ack_int(void) {
    cd_set_index(1);
    CD_REG3 = 0x5fu; /* clear int flags + reset parameter FIFO */
}

static int cd_wait_int(void) {
    int v;
    while((v = cd_get_int()) == 0)
        ;
    return v;
}

/* Sends a command with its parameters, waits for the Acknowledge
   interrupt that confirms the controller accepted it, drains one
   response byte, and acks the interrupt. */
static void cd_command(u8 cmd, const u8 *params, int nparams) {
    int i;

    cd_set_index(0);
    for(i = 0; i < nparams; i++)
        CD_REG2 = params[i];
    CD_REG1 = cmd;

    cd_wait_int();
    cd_set_index(1);
    (void)CD_REG1; /* drain one response byte */
    cd_ack_int();
}

/* Reads `count` consecutive 2048-byte sectors starting at logical
   block `lba` (0 = first sector of the data track) into `dest`. */
static void cdrom_read_sectors(u32 lba, u32 count, u8 *dest) {
    u32 msf_frames = lba + 150; /* LBA -> absolute MSF, 2-second lead-in */
    u8 mm = msf_frames / 4500;
    u8 ss = (msf_frames / 75) % 60;
    u8 ff = msf_frames % 75;
    u8 loc[3];
    u32 sector, i;

    /* BCD-encode the MSF fields the controller expects. */
    loc[0] = (u8)(((mm / 10) << 4) | (mm % 10));
    loc[1] = (u8)(((ss / 10) << 4) | (ss % 10));
    loc[2] = (u8)(((ff / 10) << 4) | (ff % 10));

    {
        u8 mode = 0x00; /* 2048 bytes/sector, normal (1x) speed */
        cd_command(CD_CMD_SETMODE, &mode, 1);
    }
    cd_command(CD_CMD_SETLOC, loc, 3);
    cd_command(CD_CMD_READN, 0, 0);

    for(sector = 0; sector < count; sector++) {
        while(cd_wait_int() != CD_INT_DATAREADY)
            ;

        cd_set_index(0);
        CD_REG3 = 0x80u; /* Request register: BFRD, start data readout */
        while((CD_REG0 & 0x40u) == 0)
            ; /* wait for data FIFO not empty */

        for(i = 0; i < 2048; i++)
            dest[i] = CD_REG2;
        dest += 2048;

        CD_REG3 = 0x00u;
        cd_ack_int();
    }

    cd_command(CD_CMD_PAUSE, 0, 0);
}

/* The image is stored as a second, plain file on the disc (see
   tools/gen_image.py / hello.xml), at a fixed logical block address
   determined when the disc image is built (mkpsxiso -lba). */
#define IMAGE_LBA   24
#define IMAGE_SECTORS 75 /* 75 * 2048 = 153600 = 320*240*2 bytes */

static u8 image_buf[IMAGE_SECTORS * 2048];

/* GP0(A0h): CPU-to-VRAM transfer. */
static void upload_image(void) {
    uint32_t i;
    uint32_t count = ((uint32_t)SCREEN_W * SCREEN_H) / 2;
    const u16 *px = (const u16 *)image_buf;

    wait_gpu();
    GPU_DATA = 0xa0000000u;
    GPU_DATA = 0u | (0u << 16);                 /* dest x,y = 0,0 */
    GPU_DATA = (uint32_t)SCREEN_W | ((uint32_t)SCREEN_H << 16);

    for(i = 0; i < count; i++) {
        GPU_DATA = (uint32_t)px[0] | ((uint32_t)px[1] << 16);
        px += 2;
    }
}

void main(void) {
    video_init();
    cdrom_read_sectors(IMAGE_LBA, IMAGE_SECTORS, image_buf);
    upload_image();

    for(;;)
        ;
}
