#include "game.h"

#define BOSS_Y (GROUND_Y - 64)
#define HPSEG_COUNT 10
#define PROJECTILE_Y (GROUND_Y - 24)

#define BOSS_TRIGGER_X (LEVEL_WIDTH - 280)
#define BOSS_SPAWN_X (LEVEL_WIDTH - 80)
#define SHOOT_PERIOD 90
#define SHOOT_ANIM 14
#define PROJECTILE_SPEED 2

void bossInit(void) {
	boss.active = 0;
	boss.hp = boss.maxHp = 20;
	boss.shootTimer = SHOOT_PERIOD;
	boss.hurt = 0;
	boss.lastHitId = -1;
	for (int i = 0; i < MAX_PROJECTILES; i++) projectiles[i].active = 0;
}

static void spawnProjectile(void) {
	for (int i = 0; i < MAX_PROJECTILES; i++) {
		if (!projectiles[i].active) {
			projectiles[i].active = 1;
			projectiles[i].worldX = boss.worldX;
			projectiles[i].vx = -PROJECTILE_SPEED;
			return;
		}
	}
}

void bossUpdate(void) {
	if (!boss.active) {
		if (player.worldX >= BOSS_TRIGGER_X && boss.hp > 0) {
			boss.active = 1;
			boss.worldX = BOSS_SPAWN_X;
		}
		return;
	}
	if (boss.hp <= 0) {
		for (int i = 0; i < MAX_PROJECTILES; i++) projectiles[i].active = 0;
		return;
	}
	if (boss.hurt > 0) boss.hurt--;

	boss.shootTimer--;
	if (boss.shootTimer == SHOOT_ANIM) {
		spawnProjectile();
	}
	if (boss.shootTimer <= 0) {
		boss.shootTimer = SHOOT_PERIOD;
	}

	for (int i = 0; i < MAX_PROJECTILES; i++) {
		Projectile *pr = &projectiles[i];
		if (!pr->active) continue;
		pr->worldX += pr->vx;
		if (pr->worldX < -8) pr->active = 0;
	}
}

void bossRender(void) {
	if (!boss.active || boss.hp <= 0) {
		oamHideSprite(BOSS_OAM);
	} else {
		int frame = boss.hurt > 0 ? BF_HURT
		            : (boss.shootTimer > SHOOT_PERIOD - SHOOT_ANIM ? BF_SHOOT : BF_IDLE);
		int sx = boss.worldX - camX;
		oamSetSprite(BOSS_OAM, sx, BOSS_Y, SQUARE, 3,
		             TILE_BOSS + frame * BOSS_FRAME_TILES, PAL_BOSS, 1, 0);
	}

	for (int i = 0; i < MAX_PROJECTILES; i++) {
		Projectile *pr = &projectiles[i];
		int oamIdx = PROJECTILE_OAM_BASE + i;
		if (!pr->active) {
			oamHideSprite(oamIdx);
			continue;
		}
		int sx = pr->worldX - camX;
		oamSetSprite(oamIdx, sx, PROJECTILE_Y, SQUARE, 0,
		             TILE_PROJECTILE, PAL_PROJECTILE, 0, 0);
	}
}

void bossHudRender(void) {
	if (!boss.active) {
		for (int i = 0; i < HPSEG_COUNT; i++) oamHideSprite(HPSEG_OAM_BASE + i);
		return;
	}
	int hpPerSeg = boss.maxHp / HPSEG_COUNT;
	int filled = (boss.hp + hpPerSeg - 1) / hpPerSeg;
	if (filled < 0) filled = 0;
	/* top-right corner, well clear of the player's hearts top-left */
	int startX = SCREEN_W - HPSEG_COUNT * 9 - 8;
	for (int i = 0; i < HPSEG_COUNT; i++) {
		int oamIdx = HPSEG_OAM_BASE + i;
		if (i < filled) {
			oamSetSprite(oamIdx, startX + i * 9, 6, SQUARE, 0,
			             TILE_HPSEG, PAL_HPSEG, 0, 0);
		} else {
			oamHideSprite(oamIdx);
		}
	}
}
