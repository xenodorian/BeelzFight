#ifndef BEELZ_H
#define BEELZ_H

#include <kos.h>
#include "sprite_data.h"

#define SCREEN_W 640
#define SCREEN_H 480

/* ---- texture / animation ---------------------------------------------- */

typedef struct {
    pvr_ptr_t ptr;
    pvr_poly_hdr_t hdr;
    int w, h;           /* sheet pixel dims (power of two)                */
    int frame_w, frame_h, cols;
    int fmt;             /* 0 = ARGB4444 (translucent), 1 = RGB565 (opaque) */
} bz_texture_t;

int texture_load(bz_texture_t *out, const char *path, int frame_w, int frame_h, int cols);
int texture_load_sheet(bz_texture_t *out, const bz_sheet_t *sheet);
int texture_load_bg(bz_texture_t *out, const bz_bg_t *bg);

typedef struct {
    const bz_texture_t *tex;
    int start, count, cur, fps;
    int loop, finished;
    float timer;
} bz_animator_t;

void anim_play(bz_animator_t *a, const bz_texture_t *tex, const bz_anim_t *def);
void anim_update(bz_animator_t *a, float dt); /* sets a->finished when a one-shot ends */
int  anim_frame(const bz_animator_t *a);

/* ---- video / rendering -------------------------------------------------*/

/* Per-frame render order (PVR always composites OP_POLY before TR_POLY,
 * so this ordering is not optional):
 *   render_frame_begin();
 *   render_bg_list_begin();      draw_bg_scroll(...) for parallax layers
 *   render_bg_list_end();
 *   render_sprite_list_begin();  draw_sprite(...) for world entities,
 *                                 THEN draw_quad/draw_bar/draw_text*(...)
 *                                 for HUD (both live in TR_POLY; HUD must
 *                                 be submitted after world sprites to land
 *                                 on top, since depth test is disabled and
 *                                 same-list order is the only tiebreak)
 *   render_sprite_list_end();
 *   render_frame_end();
 */
void video_init(void);
void render_frame_begin(void);
void render_bg_list_begin(void);
void render_bg_list_end(void);
void render_sprite_list_begin(void);
void render_sprite_list_end(void);
void render_frame_end(void);

void draw_sprite(const bz_texture_t *tex, int frame, float x, float y, float w, float h,
                  int flip_x, float alpha_mul);
void draw_tint_sprite(const bz_texture_t *tex, int frame, float x, float y, float w, float h,
                       int flip_x, float alpha_mul, float r, float g, float b);
void draw_bg_scroll(const bz_texture_t *tex, float scroll_x, float y, float draw_w, float draw_h);
void draw_quad(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void draw_bar(float x, float y, float w, float h, float pct, uint8_t r, uint8_t g, uint8_t b);
void draw_text(const char *str, float x, float y, float scale);
void draw_text_slot(int slot, const char *str, float x, float y, float scale);

/* ---- input ---------------------------------------------------------- */

typedef struct {
    uint32_t buttons, buttons_prev, pressed, released;
    int ltrig, rtrig;
    int joyx, joyy;
} bz_input_t;

void input_init(void);
void input_update(bz_input_t *in);

/* ---- misc ------------------------------------------------------------ */

static inline float bz_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float bz_lerp(float a, float b, float t) { return a + (b - a) * t; }

/* ---- shared world constants -------------------------------------------*/

#define GROUND_Y        380.0f   /* screen-space y of everyone's feet     */
#define LEVEL_LENGTH    3600.0f  /* total scroll distance to the arena    */
/* 144 = 48 (logical canvas) * 3, preserving the original logical-pixel-to
 * screen-pixel zoom factor (96/32) from before the canvas was enlarged to
 * stop clipping the hair/sword -- otherwise she'd render visibly smaller
 * than intended. */
#define PLAYER_DISPLAY  144.0f
#define IMP_DISPLAY     72.0f
#define THRALL_DISPLAY  84.0f
#define BOSS_DISPLAY    220.0f

#endif /* BEELZ_H */
