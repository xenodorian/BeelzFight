#include <math.h>
#include <string.h>
#include "player.h"
#include "assets.h"

#define PARRY_WINDOW            0.15f   /* seconds of true parry frames at anim start */
#define BLOCK_DRAIN_PER_SEC     40.0f
#define STAMINA_REGEN_PER_SEC   20.0f
#define I_FRAME_TIME            0.4f
#define MOVE_SPEED              160.0f
#define COMBO_WINDOW_FRAC       0.35f   /* last 65%->100% of an attack accepts a chain input */
#define HITBOX_ACTIVE_START     0.2f
#define HITBOX_ACTIVE_END       0.9f

typedef struct { const bz_anim_t *def; float dmg; float reach; float width; } attack_info_t;

static const attack_info_t ATTACKS[4] = {
    { &PLAYER_ANIM_ATTACK1, 10.0f, 46.0f, 40.0f }, /* A - horizontal slash */
    { &PLAYER_ANIM_ATTACK2, 14.0f, 44.0f, 44.0f }, /* B - overhead chop    */
    { &PLAYER_ANIM_ATTACK3,  8.0f, 60.0f, 30.0f }, /* X - thrust, fast/long */
    { &PLAYER_ANIM_ATTACK4, 22.0f, 50.0f, 60.0f }, /* Y - spin finisher    */
};

void player_init(player_t *p, const bz_texture_t *tex, float x, float y) {
    memset(p, 0, sizeof(*p));
    p->x = x;
    p->y = y;
    p->facing = 1;
#ifdef BEELZ_QA
    /* Visual-QA builds only (scripts/build_qa.sh): synthetic input can't
     * actually play the game well enough to reach the boss arena, and the
     * boss fight has to be looked at in the emulator like everything else.
     * Never defined for the shipped disc. */
    p->health_max = p->health = 100000.0f;
#else
    p->health_max = p->health = 100.0f;
#endif
    p->stamina_max = p->stamina = 100.0f;
    p->alive = 1;
    p->state = P_IDLE;
    p->tex = tex;
    anim_play(&p->anim, p->tex, &PLAYER_ANIM_IDLE);
}

static void enter_state(player_t *p, player_state_t s, const bz_anim_t *def) {
    p->state = s;
    p->state_timer = 0.0f;
    anim_play(&p->anim, p->tex, def);
}

static int attack_index_from_buttons(uint32_t pressed) {
    if (pressed & CONT_A) return 1;
    if (pressed & CONT_B) return 2;
    if (pressed & CONT_X) return 3;
    if (pressed & CONT_Y) return 4;
    return 0;
}

static void start_attack(player_t *p, int idx) {
    enter_state(p, (player_state_t)(P_ATTACK1 + (idx - 1)), ATTACKS[idx - 1].def);
    p->combo_queued = 0;
    p->attack_id++;
}

void player_update(player_t *p, const bz_input_t *in, float dt) {
    if (!p->alive) {
        anim_update(&p->anim, dt);
        return;
    }
    p->just_parried = 0;
    p->took_hit_this_frame = 0;
    p->state_timer += dt;
    if (p->invuln_timer > 0.0f) p->invuln_timer -= dt;

    int attack_press = attack_index_from_buttons(in->pressed);

    switch (p->state) {
    case P_IDLE:
    case P_RUN: {
        if (in->ltrig > 60) { enter_state(p, P_PARRY, &PLAYER_ANIM_PARRY); break; }
        if (in->rtrig > 60) { enter_state(p, P_BLOCK, &PLAYER_ANIM_BLOCK); break; }
        if (attack_press) { start_attack(p, attack_press); break; }

        float move = 0.0f;
        if (in->joyx < -20 || (in->buttons & CONT_DPAD_LEFT)) { move = -1.0f; p->facing = -1; }
        else if (in->joyx > 20 || (in->buttons & CONT_DPAD_RIGHT)) { move = 1.0f; p->facing = 1; }
        p->vx = move * MOVE_SPEED;
        p->x = bz_clampf(p->x + p->vx * dt, 40.0f, LEVEL_LENGTH - 40.0f);

        player_state_t want = (move != 0.0f) ? P_RUN : P_IDLE;
        if (want != p->state)
            enter_state(p, want, want == P_RUN ? &PLAYER_ANIM_RUN : &PLAYER_ANIM_IDLE);

        p->stamina = bz_clampf(p->stamina + STAMINA_REGEN_PER_SEC * dt, 0.0f, p->stamina_max);
        break;
    }

    case P_ATTACK1:
    case P_ATTACK2:
    case P_ATTACK3:
    case P_ATTACK4: {
        const bz_anim_t *def = ATTACKS[p->state - P_ATTACK1].def;
        float duration = (float)def->count / (float)def->fps;
        float frac = duration > 0.0f ? p->state_timer / duration : 1.0f;
        if (frac >= COMBO_WINDOW_FRAC && attack_press)
            p->combo_queued = attack_press;
        if (p->anim.finished) {
            if (p->combo_queued) start_attack(p, p->combo_queued);
            else enter_state(p, P_IDLE, &PLAYER_ANIM_IDLE);
        }
        break;
    }

    case P_PARRY:
        if (p->anim.finished) enter_state(p, P_IDLE, &PLAYER_ANIM_IDLE);
        break;

    case P_BLOCK:
        if (in->rtrig <= 40 || p->stamina <= 0.0f) {
            enter_state(p, P_IDLE, &PLAYER_ANIM_IDLE);
        } else {
            p->stamina = bz_clampf(p->stamina - BLOCK_DRAIN_PER_SEC * dt, 0.0f, p->stamina_max);
        }
        break;

    case P_HIT:
        if (p->anim.finished) enter_state(p, P_IDLE, &PLAYER_ANIM_IDLE);
        break;

    case P_DEATH:
        break;
    }

    anim_update(&p->anim, dt);
}

void player_draw(const player_t *p, float cam_x) {
    int flip = (p->facing < 0);
    float alpha = 1.0f;
    if (p->invuln_timer > 0.0f && p->state != P_DEATH)
        alpha = (fmodf(p->invuln_timer, 0.1f) > 0.05f) ? 1.0f : 0.4f;
    /* (x, y) is her feet on the ground line; the sheet's own anchor and
     * draw size do the rest (see draw_actor). */
    draw_actor(p->tex, anim_frame(&p->anim), p->x - cam_x, p->y, flip, alpha);
}

void player_draw_shadow(const player_t *p, float cam_x) {
    if (p->state == P_DEATH) return;
    draw_shadow(&g_assets.shadow, p->x - cam_x, p->y - 2.0f, 58.0f, 0.85f);
}

int player_get_hitbox(const player_t *p, float *x, float *y, float *w, float *h, float *dmg) {
    if (p->state < P_ATTACK1 || p->state > P_ATTACK4) return 0;
    const attack_info_t *a = &ATTACKS[p->state - P_ATTACK1];
    float duration = (float)a->def->count / (float)a->def->fps;
    float frac = duration > 0.0f ? p->state_timer / duration : 0.0f;
    if (frac < HITBOX_ACTIVE_START || frac > HITBOX_ACTIVE_END) return 0;

    *w = a->width;
    *h = 50.0f;
    *x = p->x + p->facing * (a->reach - a->width * 0.5f) - a->width * 0.5f;
    *y = p->y - 70.0f;
    *dmg = a->dmg;
    return 1;
}

int player_attack_id(const player_t *p) {
    if (p->state < P_ATTACK1 || p->state > P_ATTACK4) return 0;
    return p->attack_id;
}

int player_resolve_incoming_attack(player_t *p, float dmg, float from_x) {
    if (!p->alive || p->invuln_timer > 0.0f) return 0;

    if (p->state == P_PARRY && p->state_timer <= PARRY_WINDOW) {
        p->just_parried = 1;
        p->invuln_timer = 0.3f;
        return 1;
    }
    if (p->state == P_BLOCK && p->stamina > 0.0f) {
        p->stamina = bz_clampf(p->stamina - dmg * 1.5f, 0.0f, p->stamina_max);
        p->health -= dmg * 0.1f; /* small chip damage still gets through a block */
        p->took_hit_this_frame = 1;
        return 0;
    }

    p->health -= dmg;
    p->took_hit_this_frame = 1;
    p->invuln_timer = I_FRAME_TIME;
    p->facing = (from_x < p->x) ? 1 : -1;

    if (p->health <= 0.0f) {
        p->health = 0.0f;
        p->alive = 0;
        enter_state(p, P_DEATH, &PLAYER_ANIM_DEATH);
    } else {
        enter_state(p, P_HIT, &PLAYER_ANIM_HIT);
    }
    return 0;
}
