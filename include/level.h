#ifndef BEELZ_LEVEL_H
#define BEELZ_LEVEL_H

#include "beelz.h"
#include "player.h"
#include "enemy.h"
#include "boss.h"

#define MAX_ENEMIES 16
#define MAX_EMBERS  40

/* Drifting ash/ember motes. Pure vertex-coloured quads (no texture, no
 * VRAM), purely decorative -- they give the empty upper half of the screen
 * some motion without costing anything the PVR cares about. */
typedef struct {
    float x, y, vx, vy, life, life_max, size;
    uint8_t r, g, b;
} ember_t;

typedef enum {
    LV_INTRO,       /* walking toward wave 1                        */
    LV_WAVE1,       /* wave 1 active, advance locked until cleared  */
    LV_TRAVEL2,     /* wave 1 cleared, walking toward wave 2        */
    LV_WAVE2,       /* wave 2 active, advance locked until cleared  */
    LV_TRAVEL3,     /* wave 2 cleared, walking toward the arena     */
    LV_GATE_CLOSE,  /* brief beat as the gate seals behind the player */
    LV_BOSS,        /* boss fight                                    */
    LV_WIN,
    LV_LOSE
} level_phase_t;

typedef struct {
    player_t player;
    boss_t boss;
    enemy_t enemies[MAX_ENEMIES];

    level_phase_t phase;
    float cam_x;
    float phase_timer;
    ember_t embers[MAX_EMBERS];
    float ember_spawn_timer;
} level_t;

void level_init(level_t *lv);
void level_update(level_t *lv, const bz_input_t *in, float dt);
void level_draw(const level_t *lv);

#endif /* BEELZ_LEVEL_H */
