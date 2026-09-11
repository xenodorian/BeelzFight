#ifndef BEELZ_PLAYER_H
#define BEELZ_PLAYER_H

#include "beelz.h"

typedef enum {
    P_IDLE, P_RUN, P_ATTACK1, P_ATTACK2, P_ATTACK3, P_ATTACK4,
    P_PARRY, P_BLOCK, P_HIT, P_DEATH
} player_state_t;

typedef struct {
    float x, y;
    float vx;
    int facing;               /* 1 = right, -1 = left */

    float health, health_max;
    float stamina, stamina_max;

    player_state_t state;
    bz_animator_t anim;
    float state_timer;        /* seconds since entering current state */
    float invuln_timer;       /* i-frames after a hit / successful parry */
    int combo_queued;         /* 1-4 = queued next attack, 0 = none */
    int attack_id;            /* bumped every new swing; lets callers hit each
                                * enemy once per swing even though the hitbox
                                * stays active across several frames */
    int block_broken_timer_active;
    float block_break_timer;

    int alive;
    const bz_texture_t *tex;  /* shared, load-once (see assets.h) -- never owned/freed here */

    /* transient, filled in by player_update, read by level.c this frame */
    int just_parried;         /* set for one frame when a parry lands */
    int took_hit_this_frame;
} player_t;

void player_init(player_t *p, const bz_texture_t *tex, float x, float y);
void player_update(player_t *p, const bz_input_t *in, float dt);
void player_draw(const player_t *p, float cam_x);
void player_draw_shadow(const player_t *p, float cam_x);

/* Active melee hitbox for this frame, world space. Returns 0 if no attack
 * is currently active. */
int player_get_hitbox(const player_t *p, float *x, float *y, float *w, float *h, float *dmg);
/* Identifies the current swing (see attack_id above); 0 while not attacking. */
int player_attack_id(const player_t *p);

/* Called by an attacking enemy/boss when it lands a hit against the player
 * at world-x from_x. Internally resolves block/parry/i-frames and applies
 * damage + knockback as appropriate. Returns 1 if the attack was parried
 * (attacker should be told to stagger). */
int player_resolve_incoming_attack(player_t *p, float dmg, float from_x);

#endif /* BEELZ_PLAYER_H */
