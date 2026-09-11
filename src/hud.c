#include <stdio.h>
#include "hud.h"
#include "assets.h"

#define TXT_SLOT_HP    0
#define TXT_SLOT_BOSS  1
#define TXT_SLOT_PHASE 2

static void draw_button_legend(void) {
    float bx = SCREEN_W - 172.0f, by = SAFE_BOTTOM - 76.0f;
    const float sz = 30.0f, gap = 34.0f;
    draw_quad(bx - 10.0f, by - 8.0f, gap * 4.0f + 12.0f, gap + sz + 16.0f,
              10, 5, 12, 120);
    draw_sprite(&g_assets.ui_icons, UI_ICON_A, bx,          by,          sz, sz, 0, 1.0f);
    draw_sprite(&g_assets.ui_icons, UI_ICON_B, bx + gap,    by,          sz, sz, 0, 1.0f);
    draw_sprite(&g_assets.ui_icons, UI_ICON_X, bx + gap * 2, by,         sz, sz, 0, 1.0f);
    draw_sprite(&g_assets.ui_icons, UI_ICON_Y, bx + gap * 3, by,         sz, sz, 0, 1.0f);
    draw_sprite(&g_assets.ui_icons, UI_ICON_L, bx,          by + gap,    sz, sz, 0, 1.0f);
    draw_sprite(&g_assets.ui_icons, UI_ICON_R, bx + gap,    by + gap,    sz, sz, 0, 1.0f);
}

void hud_draw(const level_t *lv) {
    const player_t *p = &lv->player;

    /* backing plate so the bars stay readable over a bright sky */
    draw_quad(12.0f, 12.0f, 320.0f, 58.0f, 10, 5, 12, 140);
    draw_bar(20.0f, 20.0f, 220.0f, 22.0f, p->health / p->health_max, 205, 35, 45);
    draw_bar(20.0f, 46.0f, 180.0f, 14.0f, p->stamina / p->stamina_max, 60, 170, 210);

    char buf[24];
    snprintf(buf, sizeof(buf), "HP %d", (int)p->health);
    draw_text_slot(TXT_SLOT_HP, buf, 250.0f, 18.0f, 1.0f);

    if (lv->boss.active) {
        /* Below the player's own plate, not across it: at y=18 the boss bar
         * and its name plate sat straight on top of the HP readout and the
         * stamina bar. */
        float bw = 460.0f, bx = SCREEN_W * 0.5f - bw * 0.5f;
        draw_quad(bx - 10.0f, 74.0f, bw + 20.0f, 54.0f, 10, 5, 12, 165);
        draw_text_centered(TXT_SLOT_BOSS, "B E E L Z", SCREEN_W * 0.5f, 78.0f, 1.0f);
        draw_bar(bx, 104.0f, bw, 18.0f, lv->boss.health / lv->boss.health_max,
                 168, 26, 158);
    }

    const char *phase_label = NULL;
    switch (lv->phase) {
    case LV_WAVE1:      phase_label = "WAVE 1"; break;
    case LV_WAVE2:       phase_label = "WAVE 2"; break;
    case LV_GATE_CLOSE:  phase_label = "THE GATE SEALS"; break;
    case LV_WIN:         phase_label = "VICTORY - PRESS START"; break;
    case LV_LOSE:        phase_label = "YOU DIED - PRESS START"; break;
    default: break;
    }
    if (phase_label) {
        float w = text_width(phase_label, 1.3f);
        draw_quad(SCREEN_W * 0.5f - w * 0.5f - 12.0f, 78.0f, w + 24.0f, 36.0f, 10, 5, 12, 175);
        draw_text_centered(TXT_SLOT_PHASE, phase_label, SCREEN_W * 0.5f, 82.0f, 1.3f);
    }

    draw_button_legend();
}
