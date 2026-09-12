#include "game.h"
#include "gfx_pshot.h"
#include "gfx_spark.h"

#define PSHOT_SPEED 4
#define PSHOT_Y (GROUND_Y - 24)
#define SPARK_FRAME_TILES 1

void fxInit(void) {
	loadSpriteSheet(gfx_pshotTiles, gfx_pshotTilesLen, TILE_PSHOT, gfx_pshotPal, PAL_PSHOT);
	loadSpriteSheet(gfx_sparkTiles, gfx_sparkTilesLen, TILE_SPARK, gfx_sparkPal, PAL_SPARK);
	for (int i = 0; i < MAX_PLAYER_SHOTS; i++) playerShots[i].active = 0;
	for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = 0;
}

void playerShotSpawn(int worldX, int vx) {
	for (int i = 0; i < MAX_PLAYER_SHOTS; i++) {
		if (!playerShots[i].active) {
			playerShots[i].active = 1;
			playerShots[i].worldX = worldX;
			playerShots[i].vx = vx;
			return;
		}
	}
}

void particleSpawn(int worldX, int y, int vx, int vy, int timer) {
	for (int i = 0; i < MAX_PARTICLES; i++) {
		if (!particles[i].active) {
			particles[i].active = 1;
			particles[i].worldX = worldX;
			particles[i].y = y;
			particles[i].vx = vx;
			particles[i].vy = vy;
			particles[i].timer = timer;
			return;
		}
	}
}

void fxUpdate(void) {
	for (int i = 0; i < MAX_PLAYER_SHOTS; i++) {
		PlayerShot *s = &playerShots[i];
		if (!s->active) continue;
		s->worldX += s->vx;
		if (s->worldX > LEVEL_WIDTH || s->worldX < -8) s->active = 0;
	}
	for (int i = 0; i < MAX_PARTICLES; i++) {
		Particle *p = &particles[i];
		if (!p->active) continue;
		p->worldX += p->vx >> 2;
		p->y += p->vy >> 2;
		p->timer--;
		if (p->timer <= 0) p->active = 0;
	}
}

void fxRender(void) {
	for (int i = 0; i < MAX_PLAYER_SHOTS; i++) {
		PlayerShot *s = &playerShots[i];
		int oamIdx = PLAYER_SHOT_OAM_BASE + i;
		if (!s->active) {
			oamHideSprite(oamIdx);
			continue;
		}
		int sx = s->worldX - camX;
		if (sx < -8 || sx > SCREEN_W) {
			oamHideSprite(oamIdx);
			continue;
		}
		oamSetSprite(oamIdx, sx, PSHOT_Y, SQUARE, 0, TILE_PSHOT, PAL_PSHOT, 0, 0);
	}
	for (int i = 0; i < MAX_PARTICLES; i++) {
		Particle *p = &particles[i];
		int oamIdx = PARTICLE_OAM_BASE + i;
		if (!p->active) {
			oamHideSprite(oamIdx);
			continue;
		}
		int sx = p->worldX - camX;
		/* the last few frames of life shrink to the small "dot" spark frame */
		int frame = (p->timer > 4) ? 0 : 1;
		oamSetSprite(oamIdx, sx, p->y, SQUARE, 0, TILE_SPARK + frame * SPARK_FRAME_TILES,
		             PAL_SPARK, 0, 0);
	}
}
