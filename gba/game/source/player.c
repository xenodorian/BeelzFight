#include "game.h"

#define PLAYER_Y (GROUND_Y - 32)
#define PLAYER_SPEED 2

#define LIGHT_STARTUP 4
#define LIGHT_ACTIVE_END 8
#define LIGHT_TOTAL 14
#define LIGHT_DMG 2
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

/* ---- Combo tuning ---------------------------------------------------- */
#define FWD_TAP_WINDOW 20   /* frames a forward tap stays "chained" */
#define AB_BUFFER_WINDOW 6  /* frames A or B stays buffered for the other */

#define UPSLASH_STARTUP 6
#define UPSLASH_ACTIVE_END 12
#define UPSLASH_TOTAL 24
#define UPSLASH_DMG (LIGHT_DMG * 2)
#define UPSLASH_LO -4
#define UPSLASH_HI 36

#define DASH_TOTAL 18
#define DASH_SPEED 6
#define DASH_DMG 3
#define DASH_LO 2
#define DASH_HI 32
#define DASH_KNOCKBACK 14

#define BURST_STARTUP 8
#define BURST_ACTIVE_END 12
#define BURST_TOTAL 22
#define BURST_DMG 3
#define BURST_LO -40
#define BURST_HI 56
#define BURST_KNOCKBACK 16

#define SHOOT_SPAWN_FRAME 6
#define SHOOT_TOTAL 16
#define SHOOT_DMG (LIGHT_DMG / 2)
#define SHOOT_SPEED 4

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
	player.fwdTapCount = 0;
	player.fwdTapTimer = 0;
	player.burstCooldown = 0;
	player.aBufferTimer = 0;
	player.bBufferTimer = 0;
}

static int overlap(int lo1, int hi1, int lo2, int hi2) {
	return lo1 < hi2 && lo2 < hi1;
}

/* Applies dmg to any enemy/boss overlapping [lo,hi) not already hit by the
 * current attackId, with knockback -- shared by every move so light,
 * heavy and every combo all hit the same way. launch != 0 additionally
 * tosses a hit enemy airborne (the Up Slash combo). */
void combatHit(int lo, int hi, int dmg, int knockback, int launch) {
	for (int i = 0; i < MAX_ENEMIES; i++) {
		Enemy *e = &enemies[i];
		if (!e->active || e->dying) continue;
		if (e->lastHitId == player.attackId) continue;
		int elo = e->worldX, ehi = e->worldX + 16; /* full sprite width: a generous, forgiving hitbox */
		if (overlap(lo, hi, elo, ehi)) {
			e->lastHitId = player.attackId;
			e->hp -= dmg;
			e->hurt = 10;
			e->worldX += (e->worldX >= player.worldX) ? knockback : -knockback;
			if (e->hp <= 0) e->dying = 16;
			else if (launch) enemyLaunch(i);
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

/* Player shots (A+B) hit enemies/boss the same way a melee swing does,
 * just from a travelling projectile instead of a fixed hitbox. */
static void playerShotsHit(void) {
	for (int i = 0; i < MAX_PLAYER_SHOTS; i++) {
		PlayerShot *s = &playerShots[i];
		if (!s->active) continue;
		int lo = s->worldX, hi = s->worldX + 8;
		int hitSomething = 0;
		for (int j = 0; j < MAX_ENEMIES; j++) {
			Enemy *e = &enemies[j];
			if (!e->active || e->dying) continue;
			if (overlap(lo, hi, e->worldX, e->worldX + 16)) {
				e->hp -= SHOOT_DMG;
				e->hurt = 10;
				e->worldX += 4;
				if (e->hp <= 0) e->dying = 16;
				hitSomething = 1;
			}
		}
		if (boss.active && overlap(lo, hi, boss.worldX + 8, boss.worldX + 56)) {
			boss.hp -= SHOOT_DMG;
			if (boss.hp < 0) boss.hp = 0;
			boss.hurt = 10;
			hitSomething = 1;
		}
		if (hitSomething) {
			s->active = 0;
			particleSpawn(lo, PLAYER_Y + 12, 0, 0, 8);
		}
	}
}

static void spawnBurstRing(void) {
	static const int dirs[6][2] = {
		{4, -4}, {4, 0}, {4, 4}, {-4, -4}, {-4, 0}, {-4, 4}
	};
	for (int i = 0; i < 6; i++) {
		particleSpawn(player.worldX + 16, PLAYER_Y + 16, dirs[i][0] * 3, dirs[i][1] * 3, 16);
	}
}

void playerUpdate(u16 held, u16 down) {
	if (player.iframes > 0) player.iframes--;
	if (player.parryCooldown > 0) player.parryCooldown--;
	/* Shots keep flying and can keep landing hits no matter what the
	 * player does after firing one, so this isn't gated to any state. */
	playerShotsHit();

	/* Combo input bookkeeping: runs every frame regardless of state so a
	 * forward tap or an A/B press is never lost while e.g. a hit reaction
	 * is being processed below. */
	if (player.fwdTapTimer > 0) {
		player.fwdTapTimer--;
		if (player.fwdTapTimer == 0) player.fwdTapCount = 0;
	}
	if (down & KEY_RIGHT) {
		player.fwdTapCount++;
		player.fwdTapTimer = FWD_TAP_WINDOW;
	}
	if (player.aBufferTimer > 0) player.aBufferTimer--;
	if (player.bBufferTimer > 0) player.bBufferTimer--;
	if (down & KEY_A) player.aBufferTimer = AB_BUFFER_WINDOW;
	if (down & KEY_B) player.bBufferTimer = AB_BUFFER_WINDOW;
	/* burstCooldown doubles as an "armed" latch: it's held at 1 while L+R
	 * are both down so the burst can't refire until they're both released. */
	if (!((held & KEY_L) && (held & KEY_R))) player.burstCooldown = 0;

	/* Interrupt-driven states run their course before accepting new input */
	switch (player.state) {
	case P_LIGHT:
	case P_HEAVY: {
		int total = (player.state == P_LIGHT) ? LIGHT_TOTAL : HEAVY_TOTAL;
		int activeEnd = (player.state == P_LIGHT) ? LIGHT_ACTIVE_END : HEAVY_ACTIVE_END;
		int startup = (player.state == P_LIGHT) ? LIGHT_STARTUP : HEAVY_STARTUP;
		/* Check every active frame, not just the instant startup ends: a
		 * single-frame (1/60s) hit window was nearly impossible to land
		 * even when correctly positioned, which read as "the enemies are
		 * too small to hit" -- lastHitId already guards against a swing
		 * hitting the same target twice, so widening this window is safe. */
		if (player.stateTimer >= startup && player.stateTimer < activeEnd) {
			int lo = player.worldX + (player.state == P_LIGHT ? LIGHT_LO : HEAVY_LO);
			int hi = player.worldX + (player.state == P_LIGHT ? LIGHT_HI : HEAVY_HI);
			combatHit(lo, hi, player.state == P_LIGHT ? LIGHT_DMG : HEAVY_DMG, 6, 0);
		}
		player.stateTimer++;
		if (player.stateTimer >= total) {
			player.state = P_IDLE;
			player.stateTimer = 0;
		}
		return;
	}
	case P_UPSLASH:
		if (player.stateTimer == UPSLASH_STARTUP) {
			particleSpawn(player.worldX + 16, PLAYER_Y, -2, -12, 14);
			particleSpawn(player.worldX + 20, PLAYER_Y + 4, 2, -14, 14);
		}
		if (player.stateTimer >= UPSLASH_STARTUP && player.stateTimer < UPSLASH_ACTIVE_END) {
			combatHit(player.worldX + UPSLASH_LO, player.worldX + UPSLASH_HI, UPSLASH_DMG, 8, 1);
		}
		player.stateTimer++;
		if (player.stateTimer >= UPSLASH_TOTAL) {
			player.state = P_IDLE;
			player.stateTimer = 0;
		}
		return;
	case P_DASH:
		player.worldX += DASH_SPEED;
		if (player.worldX > LEVEL_WIDTH - 32) player.worldX = LEVEL_WIDTH - 32;
		if (boss.active && boss.hp > 0 && player.worldX > boss.worldX - 24) {
			player.worldX = boss.worldX - 24;
		}
		combatHit(player.worldX + DASH_LO, player.worldX + DASH_HI, DASH_DMG, DASH_KNOCKBACK, 0);
		if ((player.stateTimer & 2) == 0) {
			particleSpawn(player.worldX, PLAYER_Y + 16, -6, 0, 10);
		}
		player.stateTimer++;
		if (player.stateTimer >= DASH_TOTAL) {
			player.state = P_IDLE;
			player.stateTimer = 0;
		}
		return;
	case P_BURST:
		if (player.stateTimer == BURST_STARTUP) {
			spawnBurstRing();
		}
		if (player.stateTimer >= BURST_STARTUP && player.stateTimer < BURST_ACTIVE_END) {
			combatHit(player.worldX + BURST_LO, player.worldX + BURST_HI, BURST_DMG, BURST_KNOCKBACK, 0);
		}
		player.stateTimer++;
		if (player.stateTimer >= BURST_TOTAL) {
			player.state = P_IDLE;
			player.stateTimer = 0;
		}
		return;
	case P_SHOOT:
		if (player.stateTimer == SHOOT_SPAWN_FRAME) {
			playerShotSpawn(player.worldX + 24, SHOOT_SPEED);
			particleSpawn(player.worldX + 22, PLAYER_Y + 12, 3, 0, 8);
		}
		player.stateTimer++;
		if (player.stateTimer >= SHOOT_TOTAL) {
			player.state = P_IDLE;
			player.stateTimer = 0;
		}
		return;
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
			if (!e->active || e->dying || e->launchTimer > 0) continue;
			int elo = e->worldX, ehi = e->worldX + 16; /* full sprite width: a generous, forgiving hitbox */
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

	/* ---- Combos: checked before the plain single-button moves they build
	 * on, so e.g. A+B fires a shot instead of a light attack. ---- */
	if ((held & KEY_DOWN) && (down & KEY_B)) {
		player.state = P_UPSLASH;
		player.stateTimer = 0;
		player.attackId++;
		return;
	}
	if (player.aBufferTimer > 0 && player.bBufferTimer > 0) {
		player.aBufferTimer = 0;
		player.bBufferTimer = 0;
		player.state = P_SHOOT;
		player.stateTimer = 0;
		return;
	}
	if ((down & KEY_A) && player.fwdTapCount >= 2) {
		player.fwdTapCount = 0;
		player.fwdTapTimer = 0;
		player.state = P_DASH;
		player.stateTimer = 0;
		player.attackId++;
		return;
	}
	if ((held & KEY_L) && (held & KEY_R) && player.burstCooldown == 0) {
		player.burstCooldown = 1; /* armed-latch: see the top of this function */
		player.state = P_BURST;
		player.stateTimer = 0;
		player.attackId++;
		return;
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
	case P_UPSLASH:
		frame = (player.stateTimer < UPSLASH_STARTUP) ? PF_UPSLASH1 : PF_UPSLASH2;
		break;
	case P_DASH:
		frame = (player.stateTimer & 4) ? PF_DASH2 : PF_DASH1;
		break;
	case P_BURST:
		frame = (player.stateTimer < BURST_STARTUP) ? PF_BURST1 : PF_BURST2;
		break;
	case P_SHOOT:
		frame = (player.stateTimer < SHOOT_SPAWN_FRAME) ? PF_SHOOT1 : PF_SHOOT2;
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
