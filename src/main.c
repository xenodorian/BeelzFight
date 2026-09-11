#include <kos.h>
#include <stdlib.h>
#include "beelz.h"
#include "assets.h"
#include "level.h"
#include "hud.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

typedef enum { GS_TITLE, GS_PLAY } game_state_t;

/* The title screen reuses the in-game parallax stack rather than being a
 * flat colour: same layers, same order, just parked at a fixed scroll. */
static void draw_title_screen(float t) {
    render_frame_begin();

    render_bg_list_begin();
    draw_bg_scroll(&g_assets.bg_sky_boss, 0.0f, 0.0f, SCREEN_W, SKY_H);
    render_bg_list_end();

    render_sprite_list_begin();
    float drift = t * 6.0f;
    draw_bg_scroll(&g_assets.bg_layer_far, 120.0f + drift * 0.35f, FAR_Y, SCREEN_W, FAR_H);
    draw_bg_scroll(&g_assets.bg_layer_mid, 400.0f + drift, MID_Y, SCREEN_W, MID_H);
    draw_bg_scroll(&g_assets.bg_ground, drift * 1.8f, GROUND_TOP, SCREEN_W, GROUND_H);

    /* she stands on the title screen, idling, facing the camera-left menu */
    /* left of centre, so the prompt panel on the right never covers her */
    const float hero_x = 180.0f;
    draw_shadow(&g_assets.shadow, hero_x, GROUND_Y - 2.0f, 58.0f, 0.85f);
    int idle_frame = PLAYER_ANIM_IDLE.start +
                     ((int)(t * PLAYER_ANIM_IDLE.fps) % PLAYER_ANIM_IDLE.count);
    draw_actor(&g_assets.player, idle_frame, hero_x, GROUND_Y, 0, 1.0f);

    const char *title = "BEELZFIGHT";
    /* bfont draws at most TXT_MAX_CHARS (32); the old legend string was 34
     * and lost its tail, and the title's hand-picked x overran its plate. */
    const char *legend = "ABXY ATTACK  L PARRY  R BLOCK";
    float tw = text_width(title, 2.4f);
    draw_quad(SCREEN_W * 0.5f - tw * 0.5f - 26.0f, 58.0f, tw + 52.0f, 76.0f, 12, 6, 12, 200);
    draw_quad(SCREEN_W * 0.5f - tw * 0.5f - 26.0f, 58.0f, tw + 52.0f, 3.0f, 214, 62, 72, 235);
    draw_quad(SCREEN_W * 0.5f - tw * 0.5f - 26.0f, 131.0f, tw + 52.0f, 3.0f, 214, 62, 72, 235);
    draw_text_centered(4, title, SCREEN_W * 0.5f, 72.0f, 2.4f);

    /* right-hand panel, well inside SAFE_BOTTOM: at the foot of the screen
     * the bottom line was being eaten by the emulator's scanline crop */
    const float px = 412.0f;
    draw_quad(px - 190.0f, 288.0f, 380.0f, 96.0f, 10, 5, 10, 195);
    draw_quad(px - 190.0f, 288.0f, 380.0f, 2.0f, 214, 62, 72, 210);
    draw_quad(px - 190.0f, 382.0f, 380.0f, 2.0f, 214, 62, 72, 210);
    draw_text_centered(5, "PRESS START", px, 300.0f, 1.4f);
    draw_text_centered(6, legend, px, 344.0f, 1.0f);
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
    float title_t = 0.0f;

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
            title_t += dt;
            draw_title_screen(title_t);
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
