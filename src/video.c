#include <string.h>
#include "beelz.h"

void video_init(void) {
    /* 640x480 progressive VGA -- the Dreamcast's native "480p" mode.
     * Flycast/redream and a real VGA box both accept this without
     * additional negotiation. */
    vid_set_mode(DM_640x480_VGA, PM_RGB565);

    /* opb_sizes is {OP_POLY, OP_MOD, TR_POLY, TR_MOD, PT_POLY}. Nearly
     * everything this game draws (every sprite, all HUD) is ARGB4444 and
     * goes through the translucent list (see beelz.h's render-order
     * contract) -- giving it a BINSIZE_0 tile-binning buffer, as an
     * earlier version of this did, means the GPU has nowhere to bin those
     * primitives and the very first translucent draw call corrupts
     * execution instead of rendering. Only OP_MOD/TR_MOD/PT_POLY are
     * genuinely unused here (no modifier volumes or punch-through). */
    pvr_init_params_t params = {
        { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 },
        512 * 1024,
        0, 0, 0
    };
    pvr_init(&params);
}

void render_frame_begin(void) {
    pvr_wait_ready();
    pvr_scene_begin();
}

void render_bg_list_begin(void) { pvr_list_begin(PVR_LIST_OP_POLY); }
void render_bg_list_end(void)   { pvr_list_finish(); }
void render_sprite_list_begin(void) { pvr_list_begin(PVR_LIST_TR_POLY); }
void render_sprite_list_end(void)   { pvr_list_finish(); }
void render_frame_end(void) { pvr_scene_finish(); }

static void submit_quad(const pvr_poly_hdr_t *hdr, float x0, float y0, float x1, float y1,
                         float u0, float v0, float u1, float v1, uint32_t argb) {
    pvr_vertex_t v;
    const float z = 1.0f;

    pvr_prim((void *)hdr, sizeof(pvr_poly_hdr_t));

    v.flags = PVR_CMD_VERTEX;
    v.x = x0; v.y = y0; v.z = z; v.u = u0; v.v = v0; v.argb = argb; v.oargb = 0;
    pvr_prim(&v, sizeof(v));

    v.x = x0; v.y = y1; v.u = u0; v.v = v1;
    pvr_prim(&v, sizeof(v));

    v.x = x1; v.y = y0; v.u = u1; v.v = v0;
    pvr_prim(&v, sizeof(v));

    v.flags = PVR_CMD_VERTEX_EOL;
    v.x = x1; v.y = y1; v.u = u1; v.v = v1;
    pvr_prim(&v, sizeof(v));
}

static void frame_uv(const bz_texture_t *tex, int frame, int flip_x,
                      float *u0, float *v0, float *u1, float *v1) {
    /* cols/w/h are 0 only if this texture's load failed (see texture.c);
     * SH4 integer division by zero is a CPU trap, not a NaN, so this guard
     * is load-bearing, not decorative. */
    if (tex->cols <= 0 || tex->w <= 0 || tex->h <= 0) {
        *u0 = *v0 = 0.0f; *u1 = *v1 = 0.0f;
        return;
    }
    int col = frame % tex->cols;
    int row = frame / tex->cols;
    float fu0 = (float)(col * tex->frame_w) / (float)tex->w;
    float fv0 = (float)(row * tex->frame_h) / (float)tex->h;
    float fu1 = fu0 + (float)tex->frame_w / (float)tex->w;
    float fv1 = fv0 + (float)tex->frame_h / (float)tex->h;
    if (flip_x) {
        *u0 = fu1; *u1 = fu0;
    } else {
        *u0 = fu0; *u1 = fu1;
    }
    *v0 = fv0; *v1 = fv1;
}

void draw_tint_sprite(const bz_texture_t *tex, int frame, float x, float y, float w, float h,
                       int flip_x, float alpha_mul, float r, float g, float b) {
    /* A texture whose load failed (see texture.c) never got a compiled
     * pvr_poly_hdr_t -- submitting that zeroed header to the TA is at best
     * a mis-render, at worst another way to corrupt the command stream.
     * Skip it outright rather than draw garbage. */
    if (!tex->ptr) return;
    float u0, v0, u1, v1;
    frame_uv(tex, frame, flip_x, &u0, &v0, &u1, &v1);
    uint8_t a = (uint8_t)(bz_clampf(alpha_mul, 0.0f, 1.0f) * 255.0f);
    uint32_t argb = (a << 24) | ((uint8_t)(r * 255) << 16) | ((uint8_t)(g * 255) << 8) | (uint8_t)(b * 255);
    submit_quad(&tex->hdr, x, y, x + w, y + h, u0, v0, u1, v1, argb);
}

void draw_sprite(const bz_texture_t *tex, int frame, float x, float y, float w, float h,
                  int flip_x, float alpha_mul) {
    draw_tint_sprite(tex, frame, x, y, w, h, flip_x, alpha_mul, 1.0f, 1.0f, 1.0f);
}

/* Draws a horizontally-tiling background layer wide enough to cover the
 * screen for any scroll_x, using UV > 1.0 wrap (textures are POT so PVR
 * wraps cleanly) instead of manually stamping multiple quads. */
void draw_bg_scroll(const bz_texture_t *tex, float scroll_x, float y, float draw_w, float draw_h) {
    if (!tex->ptr || tex->w <= 0) return;
    float u_per_px = 1.0f / (float)tex->w;
    float u0 = scroll_x * u_per_px;
    float u1 = u0 + draw_w * u_per_px;
    submit_quad(&tex->hdr, 0, y, draw_w, y + draw_h, u0, 0.0f, u1, 1.0f, 0xFFFFFFFF);
}

/* ---- flat-color quads (bars, HUD panels) -------------------------------*/

static pvr_poly_hdr_t solid_hdr_op, solid_hdr_tr;
static int solid_ready = 0;

static void ensure_solid_headers(void) {
    if (solid_ready) return;
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    cxt.gen.culling = PVR_CULLING_NONE; /* vertex winding isn't guaranteed CW/CCW here; never cull */
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    pvr_poly_compile(&solid_hdr_op, &cxt);

    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    cxt.gen.culling = PVR_CULLING_NONE; /* vertex winding isn't guaranteed CW/CCW here; never cull */
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    cxt.gen.alpha = PVR_ALPHA_ENABLE;
    cxt.blend.src = PVR_BLEND_SRCALPHA;
    cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    pvr_poly_compile(&solid_hdr_tr, &cxt);
    solid_ready = 1;
}

/* Always submitted into the translucent list, even at alpha=255: PVR's
 * pipeline renders OP_POLY *before* TR_POLY regardless of submission
 * order within a frame, and every HUD caller (bars, panels) needs to sit
 * visually on top of the (translucent, ARGB4444) game-world sprites. Call
 * HUD draw_quad/draw_bar/draw_text* only between render_sprite_list_begin
 * and render_sprite_list_end, after drawing world sprites, so they also
 * win the same-list submission-order tiebreak (see beelz.h). */
void draw_quad(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    ensure_solid_headers();
    uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;
    (void)solid_hdr_op;
    submit_quad(&solid_hdr_tr, x, y, x + w, y + h, 0, 0, 0, 0, argb);
}

void draw_bar(float x, float y, float w, float h, float pct, uint8_t r, uint8_t g, uint8_t b) {
    pct = bz_clampf(pct, 0.0f, 1.0f);
    draw_quad(x, y, w, h, 40, 40, 40, 255);          /* backing plate */
    draw_quad(x + 2, y + 2, (w - 4) * pct, h - 4, r, g, b, 255);
    draw_quad(x, y, w, 2, 230, 230, 230, 255);        /* border */
    draw_quad(x, y + h - 2, w, 2, 230, 230, 230, 255);
    draw_quad(x, y, 2, h, 230, 230, 230, 255);
    draw_quad(x + w - 2, y, 2, h, 230, 230, 230, 255);
}

/* ---- HUD text: bfont rendered into small persistent PVR textures -------
 *
 * A texture handed to pvr_prim() is only *referenced* by the TA command
 * list -- the GPU doesn't actually read it until rasterization, which
 * happens asynchronously after pvr_scene_finish(). Freeing/reusing the
 * VRAM right after submitting the quad (as a naive "build texture, draw
 * it, free it" helper would) races the GPU and shows up as corrupted or
 * flickering text. So each HUD text slot owns its own persistent texture
 * and we only re-upload (replacing, never freeing-while-in-flight) when
 * the string actually changes. */

#define TXT_MAX_CHARS 32
#define TXT_CHAR_PX 12   /* BFONT_THIN_WIDTH -- glyph width in pixels */
#define TXT_CHAR_H 24    /* BFONT_HEIGHT -- glyph height in pixels. Using
                           * TXT_CHAR_PX for both used to under-allocate
                           * the buffer by exactly half: bfont_draw_str
                           * unconditionally writes BFONT_HEIGHT (24) rows
                           * regardless of the buffer height you hand it,
                           * so a 12-row buffer took a same-size (100%)
                           * heap overflow on every text draw. */
#define TXT_SLOTS 8

typedef struct {
    char text[TXT_MAX_CHARS + 1];
    pvr_ptr_t vram;
    pvr_poly_hdr_t hdr;
    int bufw;            /* logical (on-screen) text width in pixels    */
    int pot_w, pot_h;     /* actual POT texture dimensions on the GPU     */
    int used;
} bz_text_slot_t;

static bz_text_slot_t g_text_slots[TXT_SLOTS];

static int next_pow2(int v) {
    int p = 1;
    while (p < v) p *= 2;
    return p;
}

static void upload_text_slot(bz_text_slot_t *slot, const char *str) {
    /* PVR textures -- even PVR_TXRFMT_NONTWIDDLED ones -- must be
     * power-of-two in both dimensions; a texture sized to the literal
     * (non-POT) string width/BFONT_HEIGHT corrupts pvr_poly_cxt_txr's
     * size encoding and crashes the GPU command stream. So the actual
     * buffer/texture is rounded up to POT, bfont renders into it using
     * THAT as the row stride, and the final quad only samples the
     * (bufw / pot_w, TXT_CHAR_H / pot_h) fraction that holds real text. */
    static uint16_t buf[512 * 32]; /* worst case: pot_w for 32 chars (512) x pot_h (32) */
    int len = (int)strlen(str);
    if (len > TXT_MAX_CHARS) len = TXT_MAX_CHARS;
    if (len <= 0) len = 1;
    int bufw = TXT_CHAR_PX * len;
    int pot_w = next_pow2(bufw);
    int pot_h = next_pow2(TXT_CHAR_H);

    for (int i = 0; i < pot_w * pot_h; i++)
        buf[i] = 0x0861; /* dark charcoal plaque, RGB565 */

    char tmp[TXT_MAX_CHARS + 1];
    memcpy(tmp, str, len);
    tmp[len] = 0;
    bfont_draw_str(buf, pot_w, 1, tmp);

    size_t bytes = (size_t)pot_w * pot_h * 2;
    if (slot->vram && (slot->pot_w != pot_w || slot->pot_h != pot_h)) {
        pvr_mem_free(slot->vram);
        slot->vram = NULL;
    }
    if (!slot->vram)
        slot->vram = pvr_mem_malloc(bytes);
    if (!slot->vram) { slot->used = 0; return; }
    pvr_txr_load(buf, slot->vram, bytes);

    /* TR_POLY, not OP_POLY: PVR always composites OP_POLY before TR_POLY,
     * so an opaque-list text quad would render *behind* the (translucent,
     * ARGB4444) game sprites no matter when it's submitted. Alpha=255 on
     * the vertex color makes the SRCALPHA/INVSRCALPHA blend equivalent to
     * fully opaque, so this still looks like a solid plaque. */
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY,
                      PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                      pot_w, pot_h, slot->vram, PVR_FILTER_NEAREST);
    cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    cxt.gen.culling = PVR_CULLING_NONE; /* vertex winding isn't guaranteed CW/CCW here; never cull */
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    cxt.gen.alpha = PVR_ALPHA_ENABLE;
    cxt.blend.src = PVR_BLEND_SRCALPHA;
    cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    pvr_poly_compile(&slot->hdr, &cxt);

    slot->bufw = bufw;
    slot->pot_w = pot_w;
    slot->pot_h = pot_h;
    strncpy(slot->text, tmp, TXT_MAX_CHARS);
    slot->text[TXT_MAX_CHARS] = 0;
    slot->used = 1;
}

/* slot: which persistent HUD text element this is (0..TXT_SLOTS-1) --
 * callers should use a stable slot per on-screen label (health readout,
 * wave counter, boss name, ...) so re-uploads only happen on real
 * content changes. */
void draw_text_slot(int slot, const char *str, float x, float y, float scale) {
    if (slot < 0 || slot >= TXT_SLOTS) return;
    bz_text_slot_t *s = &g_text_slots[slot];
    if (!s->used || strcmp(s->text, str) != 0)
        upload_text_slot(s, str);
    if (!s->used) return;
    float u1 = (float)s->bufw / (float)s->pot_w;
    float v1 = (float)TXT_CHAR_H / (float)s->pot_h;
    submit_quad(&s->hdr, x, y, x + s->bufw * scale, y + TXT_CHAR_H * scale, 0, 0, u1, v1, 0xFFFFFFFF);
}

void draw_text(const char *str, float x, float y, float scale) {
    draw_text_slot(0, str, x, y, scale);
}
