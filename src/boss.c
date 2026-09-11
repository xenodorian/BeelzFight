#include <stdlib.h>
#include <string.h>
#include "boss.h"
#include "assets.h"

#define BOSS_SPEED          50.0f
#define MELEE_RANGE         100.0f
#define SLAM_RANGE          120.0f
#define CAST_MIN_RANGE      150.0f
#define FIREBALL_SPEED      220.0f
#define FIREBALL_DMG        12.0f
#define FIREBALL_DISPLAY    36.0f
#define SWEEP_DMG           14.0f
#define SLAM_DMG            20.0f
#define PHASE2_THRESHOLD    0.5f

static float frand(void) { return (float)(rand() % 1000) / 1000.0f; }

void boss_init(boss_t *b, float x, float y) {
    memset(b, 0, sizeof(*b));
    b->x = x;
    b->y = y;
    b->facing = -1;
    b->health = b->health_max = 300.0f;
    b->state = BS_IDLE;
    b->active = 1;
    anim_play(&b->anim, &g_assets.boss, &BOSS_ANIM_IDLE);
}

static void enter_state(boss_t *b, boss_state_t s, const bz_anim_t *def) {
    b->state = s;
    b->state_timer = 0.0f;
    b->hit_landed = 0;
    anim_play(&b->anim, &g_assets.boss, def);
}

static void spawn_fireball(boss_t *b, float dir) {
    for (int i = 0; i < MAX_FIREBALLS; i++) {
        if (!b->fireballs[i].active) {
            b->fireballs[i].active = 1;
            b->fireballs[i].x = b->x;
            b->fireballs[i].y = b->y - BOSS_DISPLAY * 0.55f;
            b->fireballs[i].vx = dir * FIREBALL_SPEED;
            anim_play(&b->fireballs[i].anim, &g_assets.fireball, &FIREBALL_ANIM_SPIN);
            return;
        }
    }
}

static void update_fireballs(boss_t *b, player_t *player, float dt) {
    for (int i = 0; i < MAX_FIREBALLS; i++) {
        fireball_t *f = &b->fireballs[i];
        if (!f->active) continue;
        f->x += f->vx * dt;
        anim_update(&f->anim, dt);
        if (f->x < 0.0f || f->x > LEVEL_LENGTH) { f->active = 0; continue; }
        if (player->alive) {
            float dx = player->x - f->x;
            float dy = (player->y - PLAYER_DISPLAY * 0.5f) - f->y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx < FIREBALL_DISPLAY * 0.8f && dy < FIREBALL_DISPLAY * 1.2f) {
                player_resolve_incoming_attack(player, FIREBALL_DMG, f->x);
                f->active = 0;
            }
        }
    }
}

void boss_update(boss_t *b, player_t *player, float dt) {
    if (!b->active) return;
    b->state_timer += dt;
    if (b->attack_cooldown > 0.0f) b->attack_cooldown -= dt;
    if (b->health <= b->health_max * PHASE2_THRESHOLD) b->phase2 = 1;

    update_fireballs(b, player, dt);

    float dx = player->x - b->x;
    float dist = dx < 0 ? -dx : dx;
    if (b->state != BS_DEAD) b->facing = (dx < 0) ? -1 : 1;

    switch (b->state) {
    case BS_IDLE:
    case BS_WALK: {
        if (!player->alive) {
            if (b->state != BS_IDLE) enter_state(b, BS_IDLE, &BOSS_ANIM_IDLE);
            break;
        }
        if (b->attack_cooldown <= 0.0f) {
            float roll = frand();
            if (dist <= MELEE_RANGE) {
                if (roll < 0.55f) { enter_state(b, BS_SWEEP, &BOSS_ANIM_ATTACK_SWEEP); break; }
                else if (roll < 0.85f) { enter_state(b, BS_SLAM, &BOSS_ANIM_ATTACK_SLAM); break; }
                else { enter_state(b, BS_CAST, &BOSS_ANIM_ATTACK_CAST); break; }
            } else if (dist <= SLAM_RANGE) {
                if (roll < 0.5f) { enter_state(b, BS_SLAM, &BOSS_ANIM_ATTACK_SLAM); break; }
            } else if (dist >= CAST_MIN_RANGE) {
                if (roll < (b->phase2 ? 0.7f : 0.45f)) {
                    enter_state(b, BS_CAST, &BOSS_ANIM_ATTACK_CAST);
                    break;
                }
            }
        }
        if (dist > MELEE_RANGE * 0.7f) {
            b->x += (dx < 0 ? -1.0f : 1.0f) * BOSS_SPEED * (b->phase2 ? 1.3f : 1.0f) * dt;
            if (b->state != BS_WALK) enter_state(b, BS_WALK, &BOSS_ANIM_WALK);
        } else if (b->state != BS_IDLE) {
            enter_state(b, BS_IDLE, &BOSS_ANIM_IDLE);
        }
        break;
    }

    case BS_SWEEP:
    case BS_SLAM: {
        const bz_anim_t *def = (b->state == BS_SWEEP) ? &BOSS_ANIM_ATTACK_SWEEP : &BOSS_ANIM_ATTACK_SLAM;
        float range = (b->state == BS_SWEEP) ? MELEE_RANGE : SLAM_RANGE;
        float dmg = (b->state == BS_SWEEP) ? SWEEP_DMG : SLAM_DMG;
        float duration = (float)def->count / (float)def->fps;
        float frac = duration > 0.0f ? b->state_timer / duration : 1.0f;
        if (!b->hit_landed && frac >= 0.55f) {
            if (dist <= range * 1.2f && player->alive)
                player_resolve_incoming_attack(player, dmg, b->x);
            b->hit_landed = 1;
        }
        if (b->anim.finished) {
            b->attack_cooldown = b->phase2 ? 0.6f : 1.0f;
            enter_state(b, BS_IDLE, &BOSS_ANIM_IDLE);
        }
        break;
    }

    case BS_CAST: {
        float duration = (float)BOSS_ANIM_ATTACK_CAST.count / (float)BOSS_ANIM_ATTACK_CAST.fps;
        float frac = duration > 0.0f ? b->state_timer / duration : 1.0f;
        if (!b->hit_landed && frac >= 0.6f) {
            spawn_fireball(b, (float)b->facing);
            b->hit_landed = 1;
        }
        if (b->anim.finished) {
            b->attack_cooldown = b->phase2 ? 0.5f : 0.9f;
            enter_state(b, BS_IDLE, &BOSS_ANIM_IDLE);
        }
        break;
    }

    case BS_HURT:
        if (b->anim.finished) enter_state(b, BS_IDLE, &BOSS_ANIM_IDLE);
        break;

    case BS_DEAD:
        break;
    }

    anim_update(&b->anim, dt);
}

void boss_draw(const boss_t *b, float cam_x) {
    if (!b->active) return;
    float sx = b->x - cam_x - BOSS_DISPLAY * 0.5f;
    float sy = b->y - BOSS_DISPLAY;
    int flip = (b->facing < 0);
    draw_sprite(&g_assets.boss, anim_frame(&b->anim), sx, sy, BOSS_DISPLAY, BOSS_DISPLAY, flip, 1.0f);

    for (int i = 0; i < MAX_FIREBALLS; i++) {
        const fireball_t *f = &b->fireballs[i];
        if (!f->active) continue;
        draw_sprite(&g_assets.fireball, anim_frame(&f->anim),
                    f->x - cam_x - FIREBALL_DISPLAY * 0.5f, f->y - FIREBALL_DISPLAY * 0.5f,
                    FIREBALL_DISPLAY, FIREBALL_DISPLAY, 0, 1.0f);
    }
}

int boss_aabb(const boss_t *b, float *x, float *y, float *w, float *h) {
    if (!b->active) return 0;
    *w = BOSS_DISPLAY * 0.55f;
    *h = BOSS_DISPLAY * 0.85f;
    *x = b->x - *w * 0.5f;
    *y = b->y - *h;
    return 1;
}

int boss_take_hit(boss_t *b, float dmg, float from_x) {
    if (!b->active || b->state == BS_DEAD) return 0;
    b->health -= dmg;
    b->facing = (from_x < b->x) ? -1 : 1;
    if (b->health <= 0.0f) {
        b->health = 0.0f;
        enter_state(b, BS_DEAD, &BOSS_ANIM_DEATH);
        return 1;
    }
    /* Only flinch out of a passive state -- mid-swing hits don't cancel an
     * attack already committed, same convention as the weaker enemies. */
    if (b->state == BS_IDLE || b->state == BS_WALK)
        enter_state(b, BS_HURT, &BOSS_ANIM_HURT);
    return 0;
}
