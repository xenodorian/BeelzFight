#ifndef BEELZFIGHT_GAME_H
#define BEELZFIGHT_GAME_H

#include <gba_base.h>
#include <gba_video.h>
#include <gba_sprites.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_console.h>
#include <stdio.h>
#include <string.h>

/* ---- Screen / world layout ---------------------------------------- */
#define SCREEN_W 240
#define SCREEN_H 160
#define GROUND_Y 140          /* top of the ground strip in bg.png    */
#define LEVEL_WIDTH 1400      /* total scrollable level width, px     */
#define CAMERA_LEAD 100       /* player offset from left of screen    */

/* ---- Tile offsets into OBJ VRAM (4bpp tile units, 32 bytes each) --- */
#define PLAYER_FRAME_TILES 16   /* 32x32 = 4x4 tiles */
#define ENEMY_FRAME_TILES  4    /* 16x16 = 2x2 tiles */
#define BOSS_FRAME_TILES   64   /* 64x64 = 8x8 tiles */
#define HEART_FRAME_TILES  4    /* 16x16 = 2x2 tiles */
#define PLAYER_FRAME_COUNT 18

#define TILE_PLAYER      0
#define TILE_ENEMY       (TILE_PLAYER + PLAYER_FRAME_COUNT * PLAYER_FRAME_TILES)
#define TILE_BOSS        (TILE_ENEMY  + 4  * ENEMY_FRAME_TILES)
#define TILE_PROJECTILE  (TILE_BOSS   + 3  * BOSS_FRAME_TILES)
#define TILE_HEART       (TILE_PROJECTILE + 1)
#define TILE_HPSEG       (TILE_HEART  + 2  * HEART_FRAME_TILES)
#define TILE_PSHOT       (TILE_HPSEG  + 1)
#define TILE_SPARK       (TILE_PSHOT  + 1)

/* ---- OBJ palette banks (4bpp, 16 banks of 16 colors) --------------- */
#define PAL_PLAYER     0
#define PAL_ENEMY      1
#define PAL_BOSS       2
#define PAL_PROJECTILE 3
#define PAL_HEART      4
#define PAL_HPSEG      5
#define PAL_PSHOT      6
#define PAL_SPARK      7

/* ---- OAM sprite slot layout ----------------------------------------- */
#define PLAYER_OAM            0
#define BOSS_OAM              1
#define PROJECTILE_OAM_BASE   2   /* boss projectiles, 4 slots */
#define PLAYER_SHOT_OAM_BASE  6   /* player-fired shots, 4 slots */
#define ENEMY_OAM_BASE        10  /* 8 slots */
#define HEART_OAM_BASE        18  /* 5 slots */
#define HPSEG_OAM_BASE        23  /* 10 slots */
#define PARTICLE_OAM_BASE     33  /* 8 slots */

/* ---- Player animation frame indices (see gen_assets/gen_sprites.py) */
enum {
	PF_IDLE, PF_WALK1, PF_WALK2, PF_LIGHT1, PF_LIGHT2,
	PF_HEAVY1, PF_HEAVY2, PF_BLOCK, PF_PARRY, PF_HURT,
	PF_UPSLASH1, PF_UPSLASH2, PF_DASH1, PF_DASH2,
	PF_BURST1, PF_BURST2, PF_SHOOT1, PF_SHOOT2
};
enum { EF_WALK1, EF_WALK2, EF_ATTACK, EF_HURT };
enum { BF_IDLE, BF_SHOOT, BF_HURT };
enum { HF_FULL, HF_EMPTY };

/* ---- Entity state machines ----------------------------------------- */
typedef enum {
	P_IDLE, P_WALK, P_LIGHT, P_HEAVY, P_BLOCK, P_PARRY, P_HURT,
	P_UPSLASH, P_DASH, P_BURST, P_SHOOT
} PlayerState;

typedef struct {
	int worldX;
	int hp, maxHp;
	PlayerState state;
	int stateTimer;   /* frames spent in current state */
	int iframes;      /* invulnerability frames remaining */
	int attackId;     /* bumped each new swing; gates one-hit-per-swing */
	int parryCooldown;
	/* combo input tracking */
	int fwdTapCount;  /* how many forward taps chained so far */
	int fwdTapTimer;  /* frames left to chain another forward tap */
	int burstCooldown;
	int aBufferTimer; /* recently-pressed-A grace window, for A+B */
	int bBufferTimer; /* recently-pressed-B grace window, for A+B */
} Player;

#define MAX_ENEMIES 8
typedef struct {
	int active;
	int worldX;
	int hp;
	int hurt;         /* hurt-flash timer */
	int dying;        /* death-anim timer, 0 = not dying */
	int walkAnim;
	int lastHitId;    /* last player attackId that damaged this enemy */
	int launchTimer;  /* >0: airborne from an Upward Slash, counts down to 0 */
} Enemy;

typedef struct {
	int active;
	int worldX;
	int hp, maxHp;
	int shootTimer;
	int hurt;
	int lastHitId;
} Boss;

#define MAX_PROJECTILES 4
typedef struct {
	int active;
	int worldX;
	int vx;
} Projectile;

/* Player-fired shots (A+B combo), separate from the boss's projectiles
 * since they travel the other way and damage enemies/boss instead of
 * the player. */
#define MAX_PLAYER_SHOTS 4
typedef struct {
	int active;
	int worldX;
	int vx;
} PlayerShot;

/* Small fire-and-forget visual particles for combo effects. */
#define MAX_PARTICLES 8
typedef struct {
	int active;
	int worldX;
	int y;      /* screen-space Y, not world -- these never scroll vertically */
	int vx, vy; /* 1/4-px fixed point, per frame */
	int timer;
} Particle;

typedef enum { GS_TITLE, GS_PLAY, GS_WIN, GS_LOSE } GameState;

/* ---- Globals (defined in main.c) ----------------------------------- */
extern Player player;
extern Enemy enemies[MAX_ENEMIES];
extern Boss boss;
extern Projectile projectiles[MAX_PROJECTILES];
extern PlayerShot playerShots[MAX_PLAYER_SHOTS];
extern Particle particles[MAX_PARTICLES];
extern int camX;
extern GameState gameState;

/* ---- oam.c ----------------------------------------------------------*/
void oamClear(void);
void oamSetSprite(int idx, int x, int y, int shape, int size, int tileIndex,
                   int paletteBank, int hflip, int priority);
void oamHideSprite(int idx);
void oamUpdate(void);
void loadSpriteSheet(const unsigned int *tiles, unsigned int tilesLenBytes,
                      int tileOffset, const unsigned short *pal, int bank);

/* ---- player.c ---------------------------------------------------------*/
void playerInit(void);
void playerUpdate(u16 held, u16 down);
void playerRender(void);
int  playerHurtboxLo(void);
int  playerHurtboxHi(void);
/* applies dmg to any enemy/boss overlapping [lo,hi) not already hit by the
 * current attackId, with knockback; shared by every move (light/heavy and
 * the combos) so they all hit consistently. launch != 0 also tosses any
 * hit enemy airborne (the Up Slash combo). */
void combatHit(int lo, int hi, int dmg, int knockback, int launch);

/* ---- enemy.c ---------------------------------------------------------*/
void enemiesInit(void);
void enemiesUpdate(void);
void enemiesRender(void);
void enemySpawn(int worldX);
void enemyLaunch(int idx); /* used by combatHit to toss a hit enemy airborne */

/* ---- boss.c ------------------------------------------------------------*/
void bossInit(void);
void bossUpdate(void);
void bossRender(void);
void bossHudRender(void);

/* ---- fx.c: player shots + particles -----------------------------------*/
void fxInit(void);
void fxUpdate(void);
void fxRender(void);
void playerShotSpawn(int worldX, int vx);
void particleSpawn(int worldX, int y, int vx, int vy, int timer);

/* ---- text.c ------------------------------------------------------------*/
void textInit(void);
void textClear(void);
void textDrawString(int row, int col, const char *s);
void textShow(void);

#endif
