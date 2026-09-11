#ifndef BEELZ_BOSS_H
#define BEELZ_BOSS_H

#include "beelz.h"
#include "player.h"

typedef enum { BS_IDLE, BS_WALK, BS_SWEEP, BS_SLAM, BS_CAST, BS_HURT, BS_DEAD } boss_state_t;

#define MAX_FIREBALLS 4

typedef struct {
    float x, y, vx;
    int active;
    bz_animator_t anim;
} fireball_t;

typedef struct {
    float x, y;
    int facing;
    float health, health_max;
    boss_state_t state;
    bz_animator_t anim;
    float state_timer;
    float attack_cooldown;
    int phase2;
    int active;
    int hit_landed;               /* guards a melee swing to one hit */
    int last_hit_swing_id;        /* see player_t.attack_id           */
    fireball_t fireballs[MAX_FIREBALLS];
} boss_t;

void boss_init(boss_t *b, float x, float y);
void boss_update(boss_t *b, player_t *player, float dt);
void boss_draw(const boss_t *b, float cam_x);
int  boss_take_hit(boss_t *b, float dmg, float from_x); /* returns 1 if this hit killed it */
int  boss_aabb(const boss_t *b, float *x, float *y, float *w, float *h);

#endif /* BEELZ_BOSS_H */
