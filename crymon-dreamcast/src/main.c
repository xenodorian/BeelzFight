/*
 * CryMon - Dreamcast port, using KallistiOS + its simplest 2D path:
 * a single (non-double-buffered) RGB565 framebuffer written directly
 * through KOS's `vram_s` pointer, paced with `vid_waitvbl()`, exactly
 * like KOS's own mrbtris example
 * (examples/dreamcast/mruby/mrbtris/dckos.c). No PVR/TA 3D pipeline,
 * no manual PVR register programming, no hand-rolled Maple driver --
 * KOS owns all of that. This replaces the earlier from-scratch
 * bare-metal version, whose manually-swapped double buffer never
 * landed its PVR_FB_ADDR writes at the vblank boundary and produced a
 * tear on every redraw.
 *
 * Room layout, tile colors, solid/walkable tiles, prop positions and
 * sizes, player movement speed, and the four interactable dialogue
 * lines are taken from the reference implementation
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

#include <kos.h>

#define SCREEN_W 320
#define SCREEN_H 240

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void put_pixel(int x, int y, uint16_t color) {
    if(x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H)
        return;
    vram_s[y * SCREEN_W + x] = color;
}

static void fill_rect(int x, int y, int w, int h, uint16_t color) {
    int px, py;
    for(py = y; py < y + h; py++)
        for(px = x; px < x + w; px++)
            put_pixel(px, py, color);
}

static void vram_clear(void) {
    int i;
    for(i = 0; i < SCREEN_W * SCREEN_H; i++)
        vram_s[i] = 0x0000;
}

#define TEXT_W BFONT_THIN_WIDTH

static int text_width(const char *s) {
    return (int)strlen(s) * TEXT_W;
}

static void draw_text(const char *s, int x, int y) {
    bfont_draw_str(vram_s + y * SCREEN_W + x, SCREEN_W, true, s);
}

static void draw_text_center(const char *s, int cx, int y) {
    draw_text(s, cx - text_width(s) / 2, y);
}

static void draw_press_start(void) {
    vram_clear();
    bfont_set_foreground_color(0xFFFF);
    bfont_set_background_color(0x0000);
    draw_text_center("PRESS START", SCREEN_W / 2, SCREEN_H / 2 - BFONT_HEIGHT / 2);
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

static void draw_prop(char mark, int w, int h, uint16_t color) {
    int cx, cy;
    mark_center(mark, &cx, &cy);
    fill_rect(cx - scale_len(w) / 2, cy - scale_len(h) / 2,
              scale_len(w), scale_len(h), color);
}

static void draw_props(void) {
    /* render.lua sizes: prop-bed-father/prop-bed-empty 64x56,
       prop-shelf 40x44, prop-crate 32x32. Distinct flat colors stand
       in for the sprite art (no asset pipeline yet for this port). */
    draw_prop('B', 64, 56, rgb565(120, 70, 60));   /* father, in bed   */
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
 * father / shelf / crate (love/game/src/data.lua). Split across two
 * lines by hand where needed to fit the BIOS font's 12px-wide glyphs
 * in 320px (26 chars/line) -- the earlier bare-metal port used a
 * smaller hand-rolled 8px font that fit these on one line each.
 * ---------------------------------------------------------------------- */
static void dialogue_lines_for(char mark, const char **line1, const char **line2) {
    switch(mark) {
        case 'U':
            *line1 = "JUST UNTIL THEY BREATHE";
            *line2 = "AGAIN";
            return;
        case 'B':
            *line1 = "THERES A WAR CRYTOWN IS";
            *line2 = "BLEEDING";
            return;
        case 'S':
            *line1 = "THIS IS IT FATHERS";
            *line2 = "CRYSTAL";
            return;
        case 'C':
            *line1 = "A WRAP HE WONT MISS IT";
            *line2 = 0;
            return;
        default:
            *line1 = 0;
            *line2 = 0;
            return;
    }
}

static void draw_dialogue_box(char mark) {
    const char *line1, *line2;
    dialogue_lines_for(mark, &line1, &line2);
    if(!line1)
        return;

    fill_rect(4, SCREEN_H - 60, SCREEN_W - 8, 56, rgb565(18, 17, 14));
    fill_rect(4, SCREEN_H - 60, SCREEN_W - 8, 2, rgb565(197, 206, 198));
    fill_rect(4, SCREEN_H - 6, SCREEN_W - 8, 2, rgb565(197, 206, 198));

    bfont_set_foreground_color(rgb565(232, 228, 216));
    draw_text(line1, 10, SCREEN_H - 52);
    if(line2)
        draw_text(line2, 10, SCREEN_H - 52 + BFONT_HEIGHT);
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

static uint32_t read_buttons(void) {
    maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *state;

    if(!cont)
        return 0;

    state = (cont_state_t *)maple_dev_status(cont);
    if(!state)
        return 0;

    return state->buttons;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int state = 0; /* 0 = title screen, 1 = starting room */
    uint32_t prev_buttons = 0;
    int px, py, pdir = 0; /* dir: 0=down,1=up,2=left,3=right */
    int col, row;
    char dialogue_mark = 0;
    uint32_t buttons;

    vid_set_mode(DM_320x240, PM_RGB565);

    draw_press_start();

    find_mark('P', &col, &row);
    px = ROOM_OX + col * ROOM_TILE + ROOM_TILE / 2;
    py = ROOM_OY + row * ROOM_TILE + ROOM_TILE / 2;

    for(;;) {
        vid_waitvbl();
        buttons = read_buttons();

        if(state == 0) {
            if((buttons & CONT_START) && !(prev_buttons & CONT_START)) {
                state = 1;
                draw_house_background();
                draw_props();
                draw_player(px, py, pdir);
            }
        }
        else {
            int dx = 0, dy = 0;
            int old_px = px, old_py = py, old_dir = pdir;
            char old_dialogue = dialogue_mark;

            if(buttons & CONT_DPAD_LEFT)  { dx = -1; pdir = 2; }
            if(buttons & CONT_DPAD_RIGHT) { dx = 1;  pdir = 3; }
            if(buttons & CONT_DPAD_UP)    { dy = -1; pdir = 1; }
            if(buttons & CONT_DPAD_DOWN)  { dy = 1;  pdir = 0; }

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

            if((buttons & CONT_A) && !(prev_buttons & CONT_A)) {
                dialogue_mark = closest_mark(px, py);
            }

            /* Redraw only when something actually changed -- no point
               repainting a frame identical to the one already on
               screen. This is a single, non-double-buffered
               framebuffer (KOS's default for vid_set_mode without an
               explicit multibuffer flag), the same approach KOS's own
               mrbtris example uses; the vid_waitvbl() at the top of
               this loop paces every iteration to the vblank. */
            if(px != old_px || py != old_py || pdir != old_dir || dialogue_mark != old_dialogue) {
                draw_house_background();
                draw_props();
                draw_player(px, py, pdir);
                if(dialogue_mark)
                    draw_dialogue_box(dialogue_mark);
            }
        }

        prev_buttons = buttons;
    }

    return 0;
}
