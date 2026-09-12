#include "game.h"

#include "gfx_player.h"
#include "gfx_enemy.h"
#include "gfx_boss.h"
#include "gfx_projectile.h"
#include "gfx_heart.h"
#include "gfx_hpseg.h"
#include "gfx_bg.h"

Player player;
Enemy enemies[MAX_ENEMIES];
Boss boss;
Projectile projectiles[MAX_PROJECTILES];
int camX;
GameState gameState;

#define HEART_OAM_BASE 14
#define BG_SCREEN_BLOCK 8

typedef struct { int triggerX; int count; } Wave;
static const Wave waves[] = {
	{150, 4},
	{450, 5},
	{800, 5},
};
#define NUM_WAVES (sizeof(waves) / sizeof(waves[0]))
static int waveDone[NUM_WAVES];
static int pendingSpawns;
static int spawnTimer;

static void loadAssets(void) {
	loadSpriteSheet(gfx_playerTiles, gfx_playerTilesLen, TILE_PLAYER, gfx_playerPal, PAL_PLAYER);
	loadSpriteSheet(gfx_enemyTiles, gfx_enemyTilesLen, TILE_ENEMY, gfx_enemyPal, PAL_ENEMY);
	loadSpriteSheet(gfx_bossTiles, gfx_bossTilesLen, TILE_BOSS, gfx_bossPal, PAL_BOSS);
	loadSpriteSheet(gfx_projectileTiles, gfx_projectileTilesLen, TILE_PROJECTILE, gfx_projectilePal, PAL_PROJECTILE);
	loadSpriteSheet(gfx_heartTiles, gfx_heartTilesLen, TILE_HEART, gfx_heartPal, PAL_HEART);
	loadSpriteSheet(gfx_hpsegTiles, gfx_hpsegTilesLen, TILE_HPSEG, gfx_hpsegPal, PAL_HPSEG);
}

static void loadBackground(void) {
	memcpy(CHAR_BASE_ADR(0), gfx_bgTiles, gfx_bgTilesLen);
	memcpy(SCREEN_BASE_BLOCK(BG_SCREEN_BLOCK), gfx_bgMap, gfx_bgMapLen);
	memcpy((void *)BG_PALETTE, gfx_bgPal, gfx_bgPalLen);
	BGCTRL[0] = CHAR_BASE(0) | SCREEN_BASE(BG_SCREEN_BLOCK) | TEXTBG_SIZE_256x256;
}

static void showTextScreen(const char *line1, const char *line2) {
	oamClear();
	oamUpdate();
	BG_OFFSET[0].x = 0;
	BG_OFFSET[0].y = 0;
	textInit();
	textClear();
	int col1 = (32 - (int)strlen(line1)) / 2;
	int col2 = (32 - (int)strlen(line2)) / 2;
	textDrawString(9, col1, line1);
	textDrawString(11, col2, line2);
	textShow();
}

static void startGame(void) {
	playerInit();
	enemiesInit();
	bossInit();
	camX = 0;
	for (unsigned i = 0; i < NUM_WAVES; i++) waveDone[i] = 0;
	pendingSpawns = 0;
	spawnTimer = 0;

	loadBackground();
	SetMode(MODE_0 | BG0_ON | OBJ_ON | OBJ_1D_MAP);
	gameState = GS_PLAY;
}

static void updateWaves(void) {
	for (unsigned i = 0; i < NUM_WAVES; i++) {
		if (!waveDone[i] && player.worldX >= waves[i].triggerX) {
			waveDone[i] = 1;
			pendingSpawns += waves[i].count;
		}
	}
	if (pendingSpawns > 0) {
		spawnTimer--;
		if (spawnTimer <= 0) {
			enemySpawn(player.worldX + 150);
			pendingSpawns--;
			spawnTimer = 24;
		}
	}
}

static void updateCamera(void) {
	camX = player.worldX - CAMERA_LEAD;
	if (camX < 0) camX = 0;
	if (camX > LEVEL_WIDTH - SCREEN_W) camX = LEVEL_WIDTH - SCREEN_W;
}

static void renderHud(void) {
	for (int i = 0; i < player.maxHp; i++) {
		int frame = (i < player.hp) ? HF_FULL : HF_EMPTY;
		oamSetSprite(HEART_OAM_BASE + i, 8 + i * 18, 8, SQUARE, 1,
		             TILE_HEART + frame * HEART_FRAME_TILES, PAL_HEART, 0, 0);
	}
	bossHudRender();
}

int main(void) {
	irqInit();
	irqEnable(IRQ_VBLANK);
	REG_IME = 1;

	loadAssets();
	oamClear();
	oamUpdate();

	gameState = GS_TITLE;
	showTextScreen("BEELZFIGHT", "Press START");

	while (1) {
		VBlankIntrWait();
		scanKeys();
		u16 held = keysHeld();
		u16 down = keysDown();

		switch (gameState) {
		case GS_TITLE:
			if (down & KEY_START) {
				startGame();
			}
			continue; /* console text screen, nothing else to draw */

		case GS_PLAY:
			playerUpdate(held, down);
			enemiesUpdate();
			bossUpdate();
			updateWaves();
			updateCamera();

			BG_OFFSET[0].x = camX;
			BG_OFFSET[0].y = 0;

			playerRender();
			enemiesRender();
			bossRender();
			renderHud();
			oamUpdate();

			if (player.hp <= 0) {
				gameState = GS_LOSE;
				showTextScreen("YOU DIED", "Press START to retry");
			} else if (boss.active && boss.hp <= 0) {
				gameState = GS_WIN;
				showTextScreen("STAGE CLEAR", "Press START to retry");
			}
			break;

		case GS_WIN:
		case GS_LOSE:
			if (down & KEY_START) {
				gameState = GS_TITLE;
				showTextScreen("BEELZFIGHT", "Press START");
			}
			continue;
		}
	}
}
