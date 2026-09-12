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

#define TILE_PLAYER      0
#define TILE_ENEMY       (TILE_PLAYER + 10 * PLAYER_FRAME_TILES)
#define TILE_BOSS        (TILE_ENEMY  + 4  * ENEMY_FRAME_TILES)
#define TILE_PROJECTILE  (TILE_BOSS   + 3  * BOSS_FRAME_TILES)
#define TILE_HEART       (TILE_PROJECTILE + 1)
#define TILE_HPSEG       (TILE_HEART  + 2  * HEART_FRAME_TILES)

/* ---- OBJ palette banks (4bpp, 16 banks of 16 colors) --------------- */
#define PAL_PLAYER     0
#define PAL_ENEMY      1
#define PAL_BOSS       2
#define PAL_PROJECTILE 3
#define PAL_HEART      4
#define PAL_HPSEG      5

/* ---- Player animation frame indices (see gen_assets/gen_sprites.py) */
enum {
	PF_IDLE, PF_WALK1, PF_WALK2, PF_LIGHT1, PF_LIGHT2,
	PF_HEAVY1, PF_HEAVY2, PF_BLOCK, PF_PARRY, PF_HURT
};
enum { EF_WALK1, EF_WALK2, EF_ATTACK, EF_HURT };
enum { BF_IDLE, BF_SHOOT, BF_HURT };
enum { HF_FULL, HF_EMPTY };

/* ---- Entity state machines ----------------------------------------- */
typedef enum {
	P_IDLE, P_WALK, P_LIGHT, P_HEAVY, P_BLOCK, P_PARRY, P_HURT
} PlayerState;

typedef struct {
	int worldX;
	int hp, maxHp;
	PlayerState state;
	int stateTimer;   /* frames spent in current state */
	int iframes;      /* invulnerability frames remaining */
	int attackId;     /* bumped each new swing; gates one-hit-per-swing */
	int parryCooldown;
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

typedef enum { GS_TITLE, GS_PLAY, GS_WIN, GS_LOSE } GameState;

/* ---- Globals (defined in main.c) ----------------------------------- */
extern Player player;
extern Enemy enemies[MAX_ENEMIES];
extern Boss boss;
extern Projectile projectiles[MAX_PROJECTILES];
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

/* ---- enemy.c ---------------------------------------------------------*/
void enemiesInit(void);
void enemiesUpdate(void);
void enemiesRender(void);
void enemySpawn(int worldX);

/* ---- boss.c ------------------------------------------------------------*/
void bossInit(void);
void bossUpdate(void);
void bossRender(void);
void bossHudRender(void);

/* ---- text.c ------------------------------------------------------------*/
void textInit(void);
void textClear(void);
void textDrawString(int row, int col, const char *s);
void textShow(void);

#endif
