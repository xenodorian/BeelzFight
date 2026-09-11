#include <string.h>
#include "enemy.h"
#include "assets.h"

typedef struct {
    const bz_texture_t *tex;
    const bz_anim_t *idle, *walk, *attack, *death;
    float health, speed, attack_range, attack_dmg, attack_cooldown, display;
    float hit_frac; /* fraction through the attack anim when the hit lands */
} enemy_def_t;

static enemy_def_t g_defs[2];
static int g_defs_ready = 0;

static void ensure_defs(void) {
    if (g_defs_ready) return;
    g_defs[EK_IMP] = (enemy_def_t){
        &g_assets.imp, &IMP_ANIM_IDLE, &IMP_ANIM_WALK, &IMP_ANIM_ATTACK, &IMP_ANIM_DEATH,
        20.0f, 70.0f, 44.0f, 6.0f, 1.0f, IMP_DISPLAY, 0.6f
    };
    g_defs[EK_THRALL] = (enemy_def_t){
        &g_assets.thrall, &THRALL_ANIM_IDLE, &THRALL_ANIM_WALK, &THRALL_ANIM_ATTACK, &THRALL_ANIM_DEATH,
        32.0f, 55.0f, 50.0f, 10.0f, 1.3f, THRALL_DISPLAY, 0.55f
    };
#ifdef BEELZ_QA
    /* Visual-QA builds: one-hit kills so scripted input actually clears the
     * waves and reaches the boss arena, which is the part that still has to
     * be looked at in the emulator. Never defined for the shipped disc. */
    g_defs[EK_IMP].health = 1.0f;
    g_defs[EK_THRALL].health = 1.0f;
#endif
    g_defs_ready = 1;
}

void enemy_spawn(enemy_t *e, enemy_kind_t kind, float x, float y) {
    ensure_defs();
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->x = x;
    e->y = y;
    e->facing = -1;
    e->health = e->health_max = g_defs[kind].health;
    e->state = ES_IDLE;
    e->slot_used = 1;
    e->active = 1;
    anim_play(&e->anim, g_defs[kind].tex, g_defs[kind].idle);
}

static void enter_state(enemy_t *e, enemy_state_t s, const bz_anim_t *def) {
    e->state = s;
    e->state_timer = 0.0f;
    anim_play(&e->anim, g_defs[e->kind].tex, def);
}

void enemy_update(enemy_t *e, player_t *player, float dt) {
    if (!e->slot_used) return;
    const enemy_def_t *d = &g_defs[e->kind];
    e->state_timer += dt;
    if (e->hit_flash > 0.0f) e->hit_flash -= dt;
    if (e->attack_cooldown > 0.0f) e->attack_cooldown -= dt;

    float dx = player->x - e->x;
    e->facing = (dx < 0) ? -1 : 1;

    switch (e->state) {
    case ES_IDLE:
    case ES_WALK: {
        if (!player->alive) {
            if (e->state != ES_IDLE) enter_state(e, ES_IDLE, d->idle);
            break;
        }
        float dist = dx < 0 ? -dx : dx;
        if (dist <= d->attack_range) {
            if (e->attack_cooldown <= 0.0f) {
                enter_state(e, ES_ATTACK, d->attack);
                break;
            }
            if (e->state != ES_IDLE) enter_state(e, ES_IDLE, d->idle);
        } else {
            e->x += (dx < 0 ? -1.0f : 1.0f) * d->speed * dt;
            if (e->state != ES_WALK) enter_state(e, ES_WALK, d->walk);
        }
        break;
    }

    case ES_ATTACK: {
        const bz_anim_t *def = d->attack;
        float duration = (float)def->count / (float)def->fps;
        float frac = duration > 0.0f ? e->state_timer / duration : 1.0f;
        if (!e->anim.finished && frac >= d->hit_frac && e->attack_cooldown <= 0.0f) {
            float dist = dx < 0 ? -dx : dx;
            if (dist <= d->attack_range * 1.3f && player->alive)
                player_resolve_incoming_attack(player, d->attack_dmg, e->x);
            e->attack_cooldown = d->attack_cooldown;
        }
        if (e->anim.finished)
            enter_state(e, ES_IDLE, d->idle);
        break;
    }

    case ES_HIT:
        if (e->anim.finished) enter_state(e, ES_IDLE, d->idle);
        break;

    case ES_DEAD:
        if (e->anim.finished) {
            e->active = 0;
            e->slot_used = 0;
        }
        break;
    }

    anim_update(&e->anim, dt);
}

void enemy_draw(const enemy_t *e, float cam_x) {
    if (!e->slot_used) return;
    const enemy_def_t *d = &g_defs[e->kind];
    int flip = (e->facing < 0);
    /* Hit feedback is a white flash rather than a fade: at 50% alpha over a
     * busy background the sprite mostly just disappeared. */
    if (e->hit_flash > 0.0f)
        draw_actor_tint(d->tex, anim_frame(&e->anim), e->x - cam_x, e->y, flip,
                         1.0f, 1.0f, 0.55f, 0.55f);
    else
        draw_actor(d->tex, anim_frame(&e->anim), e->x - cam_x, e->y, flip, 1.0f);
}

void enemy_draw_shadow(const enemy_t *e, float cam_x) {
    if (!e->slot_used || e->state == ES_DEAD) return;
    const enemy_def_t *d = &g_defs[e->kind];
    draw_shadow(&g_assets.shadow, e->x - cam_x, e->y - 2.0f, d->display * 0.62f, 0.8f);
}

int enemy_aabb(const enemy_t *e, float *x, float *y, float *w, float *h) {
    if (!e->slot_used || e->state == ES_DEAD) return 0;
    const enemy_def_t *d = &g_defs[e->kind];
    *w = d->display * 0.6f;
    *h = d->display * 0.85f;
    *x = e->x - *w * 0.5f;
    *y = e->y - *h;
    return 1;
}

int enemy_take_hit(enemy_t *e, float dmg, float from_x) {
    if (!e->slot_used || e->state == ES_DEAD) return 0;
    const enemy_def_t *d = &g_defs[e->kind];
    e->health -= dmg;
    e->hit_flash = 0.12f;
    e->facing = (from_x < e->x) ? -1 : 1;
    if (e->health <= 0.0f) {
        e->health = 0.0f;
        enter_state(e, ES_DEAD, d->death);
        return 1;
    }
    if (e->state != ES_ATTACK) /* don't interrupt a swing already in flight */
        enter_state(e, ES_HIT, d->idle); /* no dedicated hit pose; hit_flash alpha-flicker sells it */
    return 0;
}
