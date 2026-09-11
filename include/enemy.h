#ifndef BEELZ_ENEMY_H
#define BEELZ_ENEMY_H

#include "beelz.h"
#include "player.h"

typedef enum { EK_IMP, EK_THRALL } enemy_kind_t;
typedef enum { ES_IDLE, ES_WALK, ES_ATTACK, ES_HIT, ES_DEAD } enemy_state_t;

typedef struct {
    enemy_kind_t kind;
    float x, y;
    int facing;
    float health, health_max;
    enemy_state_t state;
    bz_animator_t anim;
    float state_timer;
    float attack_cooldown;
    float hit_flash;
    int slot_used;      /* still occupying a pool slot                  */
    int active;          /* still updating (false once death anim ends)  */
    int last_hit_swing_id; /* see player_t.attack_id                     */
} enemy_t;

void enemy_spawn(enemy_t *e, enemy_kind_t kind, float x, float y);
void enemy_update(enemy_t *e, player_t *player, float dt);
void enemy_draw(const enemy_t *e, float cam_x);
/* Applies damage from the player's sword; returns 1 if this hit killed it
 * (caller can award the kill / trigger any on-kill effects). */
int  enemy_take_hit(enemy_t *e, float dmg, float from_x);
int  enemy_aabb(const enemy_t *e, float *x, float *y, float *w, float *h);

#endif /* BEELZ_ENEMY_H */
