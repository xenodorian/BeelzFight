#ifndef BEELZ_HUD_H
#define BEELZ_HUD_H

#include "level.h"

/* Must be called between render_sprite_list_begin/end, after the world
 * sprites (player/enemies/boss) so it draws on top -- see the ordering
 * contract documented in beelz.h. */
void hud_draw(const level_t *lv);

#endif /* BEELZ_HUD_H */
