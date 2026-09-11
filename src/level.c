#include <string.h>
#include "level.h"
#include "assets.h"
#include "hud.h"

#define WAVE1_TRIGGER_X   700.0f
#define WAVE1_LOCK_X      1000.0f
#define WAVE2_TRIGGER_X   1800.0f
#define WAVE2_LOCK_X      2100.0f
#define ARENA_GATE_X      3150.0f
#define BOSS_TRIGGER_X    3250.0f
#define BOSS_SPAWN_X      3500.0f
#define GATE_CLOSE_TIME   1.2f

/* Parallax scroll factors: farther layers move slower than the camera. */
#define SCROLL_FAR   0.25f
#define SCROLL_MID   0.55f
#define SCROLL_GROUND 1.0f

static int find_free_enemy_slot(level_t *lv) {
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (!lv->enemies[i].slot_used) return i;
    return -1;
}

static int count_active_enemies(level_t *lv) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (lv->enemies[i].slot_used) n++;
    return n;
}

static void spawn_enemy_at(level_t *lv, enemy_kind_t kind, float x) {
    int slot = find_free_enemy_slot(lv);
    if (slot < 0) return;
    enemy_spawn(&lv->enemies[slot], kind, x, GROUND_Y);
}

static void spawn_wave1(level_t *lv) {
    float base = lv->player.x + 260.0f;
    spawn_enemy_at(lv, EK_IMP, base);
    spawn_enemy_at(lv, EK_IMP, base + 70.0f);
    spawn_enemy_at(lv, EK_IMP, base + 300.0f);
    spawn_enemy_at(lv, EK_IMP, base + 380.0f);
}

static void spawn_wave2(level_t *lv) {
    float base = lv->player.x + 260.0f;
    spawn_enemy_at(lv, EK_IMP, base);
    spawn_enemy_at(lv, EK_IMP, base + 90.0f);
    spawn_enemy_at(lv, EK_THRALL, base + 260.0f);
    spawn_enemy_at(lv, EK_THRALL, base + 420.0f);
    spawn_enemy_at(lv, EK_IMP, base + 520.0f);
}

void level_init(level_t *lv) {
    memset(lv, 0, sizeof(*lv));
    player_init(&lv->player, &g_assets.player, 80.0f, GROUND_Y);
    lv->boss.active = 0;
    lv->phase = LV_INTRO;
}

static void resolve_player_attacks(level_t *lv) {
    float hx, hy, hw, hh, dmg;
    if (!player_get_hitbox(&lv->player, &hx, &hy, &hw, &hh, &dmg)) return;
    int swing_id = player_attack_id(&lv->player);
    if (swing_id == 0) return;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &lv->enemies[i];
        if (!e->slot_used || e->last_hit_swing_id == swing_id) continue;
        float ex, ey, ew, eh;
        if (!enemy_aabb(e, &ex, &ey, &ew, &eh)) continue;
        if (hx < ex + ew && hx + hw > ex && hy < ey + eh && hy + hh > ey) {
            enemy_take_hit(e, dmg, lv->player.x);
            e->last_hit_swing_id = swing_id;
        }
    }

    if (lv->boss.active && lv->boss.last_hit_swing_id != swing_id) {
        float bx, by, bw, bh;
        if (boss_aabb(&lv->boss, &bx, &by, &bw, &bh)) {
            if (hx < bx + bw && hx + hw > bx && hy < by + bh && hy + hh > by) {
                boss_take_hit(&lv->boss, dmg, lv->player.x);
                lv->boss.last_hit_swing_id = swing_id;
            }
        }
    }
}

static void update_camera(level_t *lv) {
    float target = lv->player.x - SCREEN_W * 0.5f;
    lv->cam_x = bz_clampf(target, 0.0f, LEVEL_LENGTH - SCREEN_W);
}

void level_update(level_t *lv, const bz_input_t *in, float dt) {
    lv->phase_timer += dt;

    /* Advance-lock while a wave is active, mirroring classic brawler
     * "invisible wall until you clear the room" pacing. Wave locks cap how
     * far forward you can get; the arena gate instead floors how far back
     * you can retreat once it's shut behind you -- these are opposite
     * clamp directions, not the same lock reused. */
    float forward_cap = -1.0f;
    if (lv->phase == LV_WAVE1) forward_cap = WAVE1_LOCK_X;
    if (lv->phase == LV_WAVE2) forward_cap = WAVE2_LOCK_X;

    player_update(&lv->player, in, dt);
    if (forward_cap > 0.0f && lv->player.x > forward_cap)
        lv->player.x = forward_cap;
    if ((lv->phase == LV_GATE_CLOSE || lv->phase == LV_BOSS) && lv->player.x < ARENA_GATE_X)
        lv->player.x = ARENA_GATE_X;

    for (int i = 0; i < MAX_ENEMIES; i++)
        enemy_update(&lv->enemies[i], &lv->player, dt);

    if (lv->boss.active)
        boss_update(&lv->boss, &lv->player, dt);

    resolve_player_attacks(lv);
    update_camera(lv);

    switch (lv->phase) {
    case LV_INTRO:
        if (lv->player.x >= WAVE1_TRIGGER_X) {
            spawn_wave1(lv);
            lv->phase = LV_WAVE1;
        }
        break;

    case LV_WAVE1:
        if (count_active_enemies(lv) == 0)
            lv->phase = LV_TRAVEL2;
        break;

    case LV_TRAVEL2:
        if (lv->player.x >= WAVE2_TRIGGER_X) {
            spawn_wave2(lv);
            lv->phase = LV_WAVE2;
        }
        break;

    case LV_WAVE2:
        if (count_active_enemies(lv) == 0)
            lv->phase = LV_TRAVEL3;
        break;

    case LV_TRAVEL3:
        if (lv->player.x >= BOSS_TRIGGER_X) {
            lv->phase = LV_GATE_CLOSE;
            lv->phase_timer = 0.0f;
        }
        break;

    case LV_GATE_CLOSE:
        if (lv->phase_timer >= GATE_CLOSE_TIME) {
            boss_init(&lv->boss, BOSS_SPAWN_X, GROUND_Y);
            lv->phase = LV_BOSS;
        }
        break;

    case LV_BOSS:
        if (!lv->player.alive) {
            lv->phase = LV_LOSE;
        } else if (lv->boss.active && lv->boss.state == BS_DEAD && lv->boss.anim.finished) {
            lv->boss.active = 0;
            lv->phase = LV_WIN;
        }
        break;

    case LV_WIN:
    case LV_LOSE:
        break;
    }

    if (lv->phase != LV_BOSS && lv->phase != LV_WIN && lv->phase != LV_LOSE && !lv->player.alive)
        lv->phase = LV_LOSE;
}

void level_draw(const level_t *lv) {
    render_frame_begin();

    render_bg_list_begin();
    int in_arena = (lv->phase == LV_GATE_CLOSE || lv->phase == LV_BOSS ||
                     lv->phase == LV_WIN || lv->phase == LV_LOSE);
    const bz_texture_t *sky = in_arena ? &g_assets.bg_sky_boss : &g_assets.bg_sky;
    draw_bg_scroll(sky, lv->cam_x * 0.05f, 0.0f, SCREEN_W, 300.0f);
    draw_bg_scroll(&g_assets.bg_layer_far, lv->cam_x * SCROLL_FAR, 140.0f, SCREEN_W, 220.0f);
    draw_bg_scroll(&g_assets.bg_layer_mid, lv->cam_x * SCROLL_MID, 220.0f, SCREEN_W, 200.0f);
    draw_bg_scroll(&g_assets.bg_ground, lv->cam_x * SCROLL_GROUND, GROUND_Y - 10.0f, SCREEN_W, 90.0f);
    if (lv->phase == LV_GATE_CLOSE || lv->phase == LV_BOSS || lv->phase == LV_WIN) {
        /* Gate is opaque (RGB565), so its precompiled header targets
         * OP_POLY -- it must be submitted while that list is open, same as
         * every other background piece, not from the TR_POLY sprite pass. */
        float gx = ARENA_GATE_X - lv->cam_x - 64.0f;
        draw_sprite(&g_assets.bg_gate, 0, gx, GROUND_Y - 256.0f, 128.0f, 256.0f, 0, 1.0f);
    }
    render_bg_list_end();

    render_sprite_list_begin();

    /* world-space entities, back to front */
    for (int i = 0; i < MAX_ENEMIES; i++)
        enemy_draw(&lv->enemies[i], lv->cam_x);
    if (lv->boss.active)
        boss_draw(&lv->boss, lv->cam_x);
    player_draw(&lv->player, lv->cam_x);

    /* HUD on top, same list, submitted last (see beelz.h ordering contract) */
    hud_draw(lv);

    render_sprite_list_end();
    render_frame_end();
}
