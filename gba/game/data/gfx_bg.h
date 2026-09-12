
//{{BLOCK(gfx_bg)

//======================================================================
//
//	gfx_bg, 256x256@4, 
//	+ palette 16 entries, not compressed
//	+ 51 tiles (t|f|p reduced) not compressed
//	+ regular map (flat), not compressed, 32x32 
//	Total size: 32 + 1632 + 2048 = 3712
//
//	Time-stamp: 2026-09-12, 17:46:50
//	Exported by Cearn's GBA Image Transmogrifier, v0.9.2
//	( http://www.coranac.com/projects/#grit )
//
//======================================================================

#ifndef GRIT_GFX_BG_H
#define GRIT_GFX_BG_H

#define gfx_bgTilesLen 1632
extern const unsigned int gfx_bgTiles[408];

#define gfx_bgMapLen 2048
extern const unsigned short gfx_bgMap[1024];

#define gfx_bgPalLen 32
extern const unsigned short gfx_bgPal[16];

#endif // GRIT_GFX_BG_H

//}}BLOCK(gfx_bg)
