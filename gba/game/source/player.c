#include "game.h"

#define PLAYER_Y (GROUND_Y - 32)
#define PLAYER_SPEED 2
#define PLAYER_OAM 0

#define LIGHT_STARTUP 4
#define LIGHT_ACTIVE_END 8
#define LIGHT_TOTAL 14
#define LIGHT_DMG 1
#define LIGHT_LO 16
#define LIGHT_HI 34

#define HEAVY_STARTUP 10
#define HEAVY_ACTIVE_END 16
#define HEAVY_TOTAL 26
#define HEAVY_DMG 3
#define HEAVY_LO 10
#define HEAVY_HI 44

#define PARRY_TOTAL 14
#define PARRY_COOLDOWN 24
#define HURT_TOTAL 20
#define HURT_IFRAMES 60

/* hurtbox used for enemy contact + projectile collision */
int playerHurtboxLo(void) { return player.worldX + 6; }
int playerHurtboxHi(void) { return player.worldX + 26; }

void playerInit(void) {
	player.worldX = 16;
	player.hp = player.maxHp = 5;
	player.state = P_IDLE;
	player.stateTimer = 0;
	player.iframes = 0;
	player.attackId = 0;
	player.parryCooldown = 0;
}

static int overlap(int lo1, int hi1, int lo2, int hi2) {
	return lo1 < hi2 && lo2 < hi1;
}

/* Applies the currently-active attack hitbox against enemies and the boss.
 * Called only while an attack's active frames are ticking. */
static void applyAttackHitbox(int lo, int hi, int dmg) {
	for (int i = 0; i < MAX_ENEMIES; i++) {
		Enemy *e = &enemies[i];
		if (!e->active || e->dying) continue;
		if (e->lastHitId == player.attackId) continue;
		int elo = e->worldX + 2, ehi = e->worldX + 14;
		if (overlap(lo, hi, elo, ehi)) {
			e->lastHitId = player.attackId;
			e->hp -= dmg;
			e->hurt = 10;
			e->worldX += 6; /* knockback away from player */
			if (e->hp <= 0) e->dying = 16;
		}
	}
	if (boss.active && boss.lastHitId != player.attackId) {
		int blo = boss.worldX + 8, bhi = boss.worldX + 56;
		if (overlap(lo, hi, blo, bhi)) {
			boss.lastHitId = player.attackId;
			boss.hp -= dmg;
			boss.hurt = 10;
			if (boss.hp < 0) boss.hp = 0;
		}
	}
}

void playerUpdate(u16 held, u16 down) {
	if (player.iframes > 0) player.iframes--;
	if (player.parryCooldown > 0) player.parryCooldown--;

	/* Interrupt-driven states run their course before accepting new input */
	switch (player.state) {
	case P_LIGHT:
	case P_HEAVY: {
		int total = (player.state == P_LIGHT) ? LIGHT_TOTAL : HEAVY_TOTAL;
		int activeEnd = (player.state == P_LIGHT) ? LIGHT_ACTIVE_END : HEAVY_ACTIVE_END;
		int startup = (player.state == P_LIGHT) ? LIGHT_STARTUP : HEAVY_STARTUP;
		if (player.stateTimer == startup) {
			int lo = player.worldX + (player.state == P_LIGHT ? LIGHT_LO : HEAVY_LO);
			int hi = player.worldX + (player.state == P_LIGHT ? LIGHT_HI : HEAVY_HI);
			applyAttackHitbox(lo, hi, player.state == P_LIGHT ? LIGHT_DMG : HEAVY_DMG);
		}
		(void)activeEnd;
		player.stateTimer++;
		if (player.stateTimer >= total) {
			player.state = P_IDLE;
			player.stateTimer = 0;
		}
		return;
	}
	case P_HURT:
		player.stateTimer++;
		if (player.stateTimer >= HURT_TOTAL) {
			player.state = P_IDLE;
			player.stateTimer = 0;
		}
		return;
	case P_PARRY:
		player.stateTimer++;
		if (player.stateTimer >= PARRY_TOTAL) {
			player.state = P_IDLE;
			player.stateTimer = 0;
		}
		return;
	default:
		break;
	}

	/* Taking a hit always wins over whatever idle/walk/block state we're in */
	if (player.iframes == 0) {
		int hlo = playerHurtboxLo(), hhi = playerHurtboxHi();
		for (int i = 0; i < MAX_ENEMIES; i++) {
			Enemy *e = &enemies[i];
			if (!e->active || e->dying) continue;
			int elo = e->worldX + 2, ehi = e->worldX + 14;
			if (overlap(hlo, hhi, elo, ehi)) {
				if (player.state == P_BLOCK) {
					/* blocked: no damage, hold ground */
				} else {
					player.hp--;
					player.iframes = HURT_IFRAMES;
					player.worldX -= 6;
					if (player.worldX < 0) player.worldX = 0;
					player.state = P_HURT;
					player.stateTimer = 0;
					return;
				}
			}
		}
		for (int i = 0; i < MAX_PROJECTILES; i++) {
			Projectile *pr = &projectiles[i];
			if (!pr->active) continue;
			if (overlap(hlo, hhi, pr->worldX, pr->worldX + 8)) {
				if (player.state == P_PARRY) {
					pr->active = 0; /* parried: destroyed, no damage */
				} else if (player.state == P_BLOCK) {
					pr->active = 0; /* blocked: absorbed, no damage */
				} else {
					pr->active = 0;
					player.hp--;
					player.iframes = HURT_IFRAMES;
					player.state = P_HURT;
					player.stateTimer = 0;
					return;
				}
			}
		}
	}

	/* Block / parry are held/pressed states that lock out movement */
	if (held & KEY_R) {
		player.state = P_BLOCK;
		player.stateTimer = 0;
		return;
	}
	if ((down & KEY_L) && player.parryCooldown == 0) {
		player.state = P_PARRY;
		player.stateTimer = 0;
		player.parryCooldown = PARRY_COOLDOWN;
		return;
	}
	if (down & KEY_A) {
		player.state = P_LIGHT;
		player.stateTimer = 0;
		player.attackId++;
		return;
	}
	if (down & KEY_B) {
		player.state = P_HEAVY;
		player.stateTimer = 0;
		player.attackId++;
		return;
	}

	/* Movement */
	int moved = 0;
	if (held & KEY_RIGHT) {
		player.worldX += PLAYER_SPEED;
		moved = 1;
	}
	if (held & KEY_LEFT) {
		player.worldX -= PLAYER_SPEED;
		moved = 1;
	}
	if (player.worldX < 0) player.worldX = 0;
	if (player.worldX > LEVEL_WIDTH - 32) player.worldX = LEVEL_WIDTH - 32;
	/* keep the fight framed: can't walk past the boss once it's engaged,
	 * or its (always-leftward) projectiles would fly away from the player */
	if (boss.active && boss.hp > 0 && player.worldX > boss.worldX - 24) {
		player.worldX = boss.worldX - 24;
	}

	player.state = moved ? P_WALK : P_IDLE;
	player.stateTimer++;
}

void playerRender(void) {
	int frame;
	switch (player.state) {
	case P_WALK:
		frame = ((player.stateTimer / 8) & 1) ? PF_WALK2 : PF_WALK1;
		break;
	case P_LIGHT:
		frame = (player.stateTimer < LIGHT_ACTIVE_END) ? PF_LIGHT1 : PF_LIGHT2;
		break;
	case P_HEAVY:
		frame = (player.stateTimer < HEAVY_ACTIVE_END) ? PF_HEAVY1 : PF_HEAVY2;
		break;
	case P_BLOCK:
		frame = PF_BLOCK;
		break;
	case P_PARRY:
		frame = PF_PARRY;
		break;
	case P_HURT:
		frame = PF_HURT;
		break;
	default:
		frame = PF_IDLE;
		break;
	}
	/* flicker while invulnerable, but not during the hurt pose itself */
	if (player.iframes > 0 && player.state != P_HURT && (player.iframes & 4)) {
		oamHideSprite(PLAYER_OAM);
		return;
	}
	int sx = player.worldX - camX;
	oamSetSprite(PLAYER_OAM, sx, PLAYER_Y, SQUARE, 2,
	             TILE_PLAYER + frame * PLAYER_FRAME_TILES, PAL_PLAYER, 0, 0);
}
