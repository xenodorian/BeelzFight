#include "game.h"

static OBJATTR shadow[128];

void oamClear(void) {
	for (int i = 0; i < 128; i++) {
		shadow[i].attr0 = ATTR0_DISABLED;
		shadow[i].attr1 = 0;
		shadow[i].attr2 = 0;
	}
}

void oamHideSprite(int idx) {
	shadow[idx].attr0 = ATTR0_DISABLED;
}

void oamSetSprite(int idx, int x, int y, int shape, int size, int tileIndex,
                   int paletteBank, int hflip, int priority) {
	u16 a0 = (y & 0xff) | ATTR0_NORMAL | ATTR0_COLOR_16 | OBJ_SHAPE(shape);
	u16 a1 = (x & 0x1ff) | OBJ_SIZE(size) | (hflip ? ATTR1_FLIP_X : 0);
	u16 a2 = OBJ_CHAR(tileIndex) | ATTR2_PRIORITY(priority) | ATTR2_PALETTE(paletteBank);
	shadow[idx].attr0 = a0;
	shadow[idx].attr1 = a1;
	shadow[idx].attr2 = a2;
}

void oamUpdate(void) {
	memcpy((void *)OAM, shadow, sizeof(shadow));
}

void loadSpriteSheet(const unsigned int *tiles, unsigned int tilesLenBytes,
                      int tileOffset, const unsigned short *pal, int bank) {
	memcpy((void *)SPR_VRAM(tileOffset), tiles, tilesLenBytes);
	memcpy((void *)(SPRITE_PALETTE + bank * 16), pal, 16 * sizeof(unsigned short));
}
