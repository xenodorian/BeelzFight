#ifndef BEELZ_ASSETS_H
#define BEELZ_ASSETS_H

#include "beelz.h"

/* Shared, load-once textures. Enemies spawn in "large quantities" per the
 * design brief, so per-kind sheets are loaded exactly once here and every
 * instance just holds a pointer -- not its own VRAM copy. */
typedef struct {
    bz_texture_t player, imp, thrall, boss, fireball, ui_icons, shadow;
    bz_texture_t bg_sky, bg_sky_boss, bg_layer_far, bg_layer_mid, bg_ground, bg_gate;
} bz_assets_t;

extern bz_assets_t g_assets;
void assets_load(void);

#endif /* BEELZ_ASSETS_H */
