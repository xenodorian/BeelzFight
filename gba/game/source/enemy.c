#include "game.h"

#define ENEMY_Y (GROUND_Y - 16)
#define ENEMY_OAM_BASE 6
#define ENEMY_SPEED 1
#define MELEE_RANGE 14

void enemiesInit(void) {
	for (int i = 0; i < MAX_ENEMIES; i++) {
		enemies[i].active = 0;
	}
}

void enemySpawn(int worldX) {
	for (int i = 0; i < MAX_ENEMIES; i++) {
		if (!enemies[i].active) {
			enemies[i].active = 1;
			enemies[i].worldX = worldX;
			enemies[i].hp = 2;
			enemies[i].hurt = 0;
			enemies[i].dying = 0;
			enemies[i].walkAnim = 0;
			enemies[i].lastHitId = -1;
			return;
		}
	}
	/* no free slot: dropped, wave spawner will retry later */
}

void enemiesUpdate(void) {
	for (int i = 0; i < MAX_ENEMIES; i++) {
		Enemy *e = &enemies[i];
		if (!e->active) continue;

		if (e->dying > 0) {
			e->dying--;
			if (e->dying == 0) e->active = 0;
			continue;
		}
		if (e->hurt > 0) e->hurt--;

		int dist = e->worldX - player.worldX;
		if (dist > MELEE_RANGE) {
			e->worldX -= ENEMY_SPEED;
			e->walkAnim++;
		} else if (dist < -MELEE_RANGE) {
			/* enemy overshot behind the player: walk back into range */
			e->worldX += ENEMY_SPEED;
			e->walkAnim++;
		}
		if (e->worldX < 0) e->worldX = 0;
		if (e->worldX > LEVEL_WIDTH - 16) e->worldX = LEVEL_WIDTH - 16;
	}
}

void enemiesRender(void) {
	for (int i = 0; i < MAX_ENEMIES; i++) {
		Enemy *e = &enemies[i];
		int oamIdx = ENEMY_OAM_BASE + i;
		if (!e->active) {
			oamHideSprite(oamIdx);
			continue;
		}
		int sx = e->worldX - camX;
		if (sx < -16 || sx > SCREEN_W) {
			oamHideSprite(oamIdx);
			continue;
		}
		int frame;
		if (e->dying > 0) {
			frame = EF_HURT;
		} else if (e->hurt > 0) {
			frame = EF_HURT;
		} else {
			int dist = e->worldX - player.worldX;
			int inRange = (dist <= MELEE_RANGE && dist >= -MELEE_RANGE);
			frame = inRange ? EF_ATTACK : ((e->walkAnim / 8) & 1 ? EF_WALK2 : EF_WALK1);
		}
		oamSetSprite(oamIdx, sx, ENEMY_Y, SQUARE, 1,
		             TILE_ENEMY + frame * ENEMY_FRAME_TILES, PAL_ENEMY, 0, 0);
	}
}
