#include <kos.h>
#include <stdlib.h>
#include "beelz.h"
#include "assets.h"
#include "level.h"
#include "hud.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

typedef enum { GS_TITLE, GS_PLAY } game_state_t;

static void draw_title_screen(void) {
    render_frame_begin();

    render_bg_list_begin();
    draw_bg_scroll(&g_assets.bg_sky_boss, 0.0f, 0.0f, SCREEN_W, SCREEN_H);
    render_bg_list_end();

    render_sprite_list_begin();
    draw_quad(SCREEN_W * 0.5f - 180.0f, 120.0f, 360.0f, 70.0f, 15, 8, 14, 210);
    draw_text_slot(4, "B E E L Z F I G H T", SCREEN_W * 0.5f - 156.0f, 140.0f, 1.6f);
    draw_text_slot(5, "PRESS START", SCREEN_W * 0.5f - 66.0f, 320.0f, 1.2f);
    draw_text_slot(6, "A/B/X/Y ATTACK  L PARRY  R BLOCK", SCREEN_W * 0.5f - 190.0f, 360.0f, 1.0f);
    render_sprite_list_end();

    render_frame_end();
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    video_init();
    /* KOS's default dbgio target is the serial port ("scif"), which is
     * invisible in a plain emulator screenshot; route dbglog/printf to the
     * framebuffer console too so load failures and asserts are actually
     * visible during QA instead of silently vanishing. */
    dbgio_dev_select("fb");
    input_init();
    assets_load();
    srand((unsigned)timer_ms_gettime64());

    game_state_t gs = GS_TITLE;
    level_t level;

    bz_input_t input;
    input.buttons = input.buttons_prev = input.pressed = input.released = 0;
    input.ltrig = input.rtrig = input.joyx = input.joyy = 0;

    uint64_t last_ms = timer_ms_gettime64();

    while (1) {
        input_update(&input);

        uint64_t now_ms = timer_ms_gettime64();
        float dt = (float)(now_ms - last_ms) / 1000.0f;
        last_ms = now_ms;
        if (dt > 0.05f) dt = 0.05f; /* clamp huge stalls (e.g. first frame) */

        if (gs == GS_TITLE) {
            if (input.pressed & CONT_START) {
                level_init(&level);
                gs = GS_PLAY;
            }
            draw_title_screen();
        } else {
            level_update(&level, &input, dt);
            if ((level.phase == LV_WIN || level.phase == LV_LOSE) &&
                (input.pressed & CONT_START)) {
                gs = GS_TITLE;
            }
            level_draw(&level);
        }
    }

    return 0;
}
