#include "assets.h"

bz_assets_t g_assets;

void assets_load(void) {
    texture_load_sheet(&g_assets.player, &SHEET_PLAYER);
    texture_load_sheet(&g_assets.imp, &SHEET_IMP);
    texture_load_sheet(&g_assets.thrall, &SHEET_THRALL);
    texture_load_sheet(&g_assets.boss, &SHEET_BOSS);
    texture_load_sheet(&g_assets.fireball, &SHEET_FIREBALL);
    texture_load_sheet(&g_assets.ui_icons, &SHEET_UI_ICONS);
    texture_load_sheet(&g_assets.shadow, &SHEET_SHADOW);

    texture_load_bg(&g_assets.bg_sky, &BG_SKY);
    texture_load_bg(&g_assets.bg_sky_boss, &BG_SKY_BOSS);
    texture_load_bg(&g_assets.bg_layer_far, &BG_LAYER_FAR);
    texture_load_bg(&g_assets.bg_layer_mid, &BG_LAYER_MID);
    texture_load_bg(&g_assets.bg_ground, &BG_GROUND);
    texture_load_bg(&g_assets.bg_gate, &BG_GATE);
}
