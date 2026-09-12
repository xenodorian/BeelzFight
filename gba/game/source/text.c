#include "game.h"
#include "gfx_font.h"

/* Our own tiny BG text renderer, used instead of libgba's consoleDemoInit()
 * + iprintf(). That combo rendered fine in mGBA but showed only the BG
 * backdrop color with no glyphs at all on at least one real-world GBA
 * emulator -- plausibly a BIOS/font-decompression compatibility gap in
 * whatever consoleDemoInit() does internally. This instead loads a plain
 * tile sheet and writes screen-entries directly, the exact same
 * plain-memcpy approach already proven (in the same emulators) for the
 * background and every sprite in this game. */

#define FONT_CHARBASE 1
#define FONT_SCREENBASE 16
#define TEXT_MAP_TILES 32

static u16 *const textMap = (u16 *)SCREEN_BASE_BLOCK(FONT_SCREENBASE);

void textInit(void) {
	memcpy(CHAR_BASE_ADR(FONT_CHARBASE), gfx_fontTiles, gfx_fontTilesLen);
	memcpy((void *)BG_PALETTE, gfx_fontPal, gfx_fontPalLen);
	/* Unlike an OBJ palette, BG palette index 0 isn't "transparent" -- it's
	 * the opaque backdrop color for the whole screen (nothing draws behind
	 * BG0). The font sheet's index 0 is its magenta transparency key, so
	 * without this it would paint the entire backdrop magenta. */
	BG_PALETTE[0] = RGB5(8, 11, 21);
}

void textClear(void) {
	for (int i = 0; i < TEXT_MAP_TILES * TEXT_MAP_TILES; i++) {
		textMap[i] = 0; /* tile 0 == the space glyph: blank */
	}
}

void textDrawString(int row, int col, const char *s) {
	int x = col;
	while (*s && x < TEXT_MAP_TILES) {
		char c = *s++;
		if (c < 32 || c > 126) c = 32;
		textMap[row * TEXT_MAP_TILES + x] = (u16)(c - 32);
		x++;
	}
}

void textShow(void) {
	BGCTRL[0] = CHAR_BASE(FONT_CHARBASE) | SCREEN_BASE(FONT_SCREENBASE) | TEXTBG_SIZE_256x256;
	SetMode(MODE_0 | BG0_ON);
}
