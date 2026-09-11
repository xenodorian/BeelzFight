#ifndef BEELZ_H
#define BEELZ_H

#include <kos.h>
#include "sprite_data.h"

#define SCREEN_W 640
#define SCREEN_H 480
/* Emulators (and real TVs) don't necessarily show all 480 scanlines --
 * Flycast drops roughly the bottom 22 of them. Keep every HUD element
 * inside this and nothing gets clipped on the way out. */
#define SAFE_BOTTOM 452.0f

/* ---- texture / animation ---------------------------------------------- */

typedef struct {
    pvr_ptr_t ptr;
    pvr_poly_hdr_t hdr;
    int w, h;           /* sheet pixel dims (power of two)                */
    int frame_w, frame_h, cols;
    int fmt;             /* 0 = ARGB4444 (translucent), 1 = RGB565 (opaque) */
    /* Copied from the sheet metadata the art pipeline emits. anchor_* is
     * the point inside a frame that sits on the actor's world position
     * (feet, centre); draw_* is the on-screen quad size that puts the art
     * back at its intended 480p scale after the generator's auto-fit
     * shrink. draw_actor() is the only thing that needs to know. */
    float anchor_x, anchor_y;
    float draw_w, draw_h;
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
 *   render_bg_list_begin();      the sky only -- it is the one genuinely
 *                                 opaque (RGB565) layer, and it covers the
 *                                 whole 640x480 so nothing can show black
 *   render_bg_list_end();
 *   render_sprite_list_begin();  parallax cut-outs (far, mid, ground, gate),
 *                                 THEN shadows and world entities,
 *                                 THEN draw_quad/draw_bar/draw_text*(...)
 *                                 for HUD. All of these are ARGB4444 or
 *                                 vertex-coloured translucent quads, so
 *                                 they all live in TR_POLY and submission
 *                                 order alone decides layering (the depth
 *                                 test is disabled).
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
/* Places a frame by its sheet anchor: (x, y) is the actor's world-to-screen
 * position (feet on the ground line), not a corner, so sheets with different
 * canvas sizes and off-centre poses all line up. Mirroring reflects about
 * the anchor, so facing left doesn't teleport the sprite sideways. */
void draw_actor(const bz_texture_t *tex, int frame, float x, float y,
                 int flip_x, float alpha_mul);
void draw_actor_tint(const bz_texture_t *tex, int frame, float x, float y,
                      int flip_x, float alpha_mul, float r, float g, float b);
/* Ground shadow centred on (x, y), w wide (height follows the texture). */
void draw_shadow(const bz_texture_t *tex, float x, float y, float w, float alpha_mul);
void draw_bg_scroll(const bz_texture_t *tex, float scroll_x, float y, float draw_w, float draw_h);
void draw_quad(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void draw_bar(float x, float y, float w, float h, float pct, uint8_t r, uint8_t g, uint8_t b);
void draw_text(const char *str, float x, float y, float scale);
void draw_text_slot(int slot, const char *str, float x, float y, float scale);
/* Same, but x is the centre of the string. bfont is fixed-pitch, so the
 * width is exactly len * 12 * scale -- hand-computed offsets drifted and
 * overran their backing plates. */
void draw_text_centered(int slot, const char *str, float cx, float y, float scale);
float text_width(const char *str, float scale);

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

#define GROUND_Y        400.0f   /* screen-space y of everyone's feet     */
#define LEVEL_LENGTH    3600.0f  /* total scroll distance to the arena    */
/* The camera stops panning at LEVEL_LENGTH - SCREEN_W, so world x beyond
 * these bounds is off the right of the screen no matter what. The boss is
 * ~266px wide, so it needs a wider margin than she does or half of it hangs
 * off the edge during the fight. */
#define ARENA_GATE_X        3150.0f  /* the portcullis; arena left wall */
#define ARENA_MAX_PLAYER_X  (LEVEL_LENGTH - 150.0f)
#define ARENA_MAX_BOSS_X    (LEVEL_LENGTH - 200.0f)

/* Parallax band geometry. Every layer is drawn full screen width; the sky
 * alone covers all 480 rows, so no combination of scroll positions can ever
 * expose the cleared framebuffer as a black band (an earlier layout ended
 * the ground at y=460 and did exactly that). The layers below it are
 * alpha cut-outs stacked back-to-front on top of the sky. */
#define SKY_H           480.0f
#define FAR_Y           100.0f
#define FAR_H           280.0f
#define MID_Y           144.0f
#define MID_H           270.0f
#define GROUND_TOP      (GROUND_Y - 14.0f)
#define GROUND_H        (SCREEN_H - GROUND_TOP)

/* Logical sizes used for hit boxes and spacing. Drawing no longer uses
 * these -- each sheet carries its own on-screen size (bz_texture_t.draw_w)
 * -- but the combat code still needs a body size per actor kind. */
#define PLAYER_DISPLAY  160.0f
#define IMP_DISPLAY     80.0f
#define THRALL_DISPLAY  96.0f
#define BOSS_DISPLAY    250.0f

#endif /* BEELZ_H */
