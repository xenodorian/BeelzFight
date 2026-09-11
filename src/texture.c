#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "beelz.h"

/* Reads our own tiny "BPVR" container (see scripts/png_to_pvr.py) straight
 * into PVR VRAM. Both formats we emit are already PVR-native non-twiddled
 * 16-bit layouts, so this is a straight DMA copy -- no CPU-side twiddling
 * needed. */
static int load_raw(bz_texture_t *out, const char *path, int frame_w, int frame_h, int cols) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        dbglog(DBG_ERROR, "beelz: could not open texture %s\n", path);
        return -1;
    }

    char magic[4];
    uint16_t w, h;
    uint8_t fmt, reserved;

    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "BPVR", 4) != 0) {
        dbglog(DBG_ERROR, "beelz: bad texture magic in %s\n", path);
        fclose(f);
        return -1;
    }
    fread(&w, sizeof(w), 1, f);
    fread(&h, sizeof(h), 1, f);
    fread(&fmt, sizeof(fmt), 1, f);
    fread(&reserved, sizeof(reserved), 1, f);
    (void)reserved;

    size_t datasize = (size_t)w * (size_t)h * 2;
    void *buf = malloc(datasize);
    if (!buf) {
        dbglog(DBG_ERROR, "beelz: OOM loading %s (%u bytes)\n", path, (unsigned)datasize);
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, datasize, f) != datasize) {
        dbglog(DBG_ERROR, "beelz: short read on %s\n", path);
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    pvr_ptr_t vram = pvr_mem_malloc(datasize);
    if (!vram) {
        dbglog(DBG_ERROR, "beelz: pvr_mem_malloc failed for %s (%u bytes)\n",
               path, (unsigned)datasize);
        free(buf);
        return -1;
    }
    pvr_txr_load(buf, vram, datasize);
    free(buf);

    out->ptr = vram;
    out->w = w;
    out->h = h;
    out->frame_w = frame_w ? frame_w : w;
    out->frame_h = frame_h ? frame_h : h;
    out->cols = cols ? cols : 1;
    out->fmt = fmt;

    int list = (fmt == 1) ? PVR_LIST_OP_POLY : PVR_LIST_TR_POLY;
    int pvrfmt = (fmt == 1 ? PVR_TXRFMT_RGB565 : PVR_TXRFMT_ARGB4444) | PVR_TXRFMT_NONTWIDDLED;

    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, list, pvrfmt, w, h, vram, PVR_FILTER_NEAREST);
    /* Pure 2D: draw order alone decides layering, not the depth buffer. */
    cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    cxt.gen.culling = PVR_CULLING_NONE; /* vertex winding isn't guaranteed CW/CCW here; never cull */
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    if (fmt != 1) {
        cxt.gen.alpha = PVR_ALPHA_ENABLE;
        cxt.blend.src = PVR_BLEND_SRCALPHA;
        cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    }
    pvr_poly_compile(&out->hdr, &cxt);
    return 0;
}

int texture_load(bz_texture_t *out, const char *path, int frame_w, int frame_h, int cols) {
    return load_raw(out, path, frame_w, frame_h, cols);
}

int texture_load_sheet(bz_texture_t *out, const bz_sheet_t *sheet) {
    return load_raw(out, sheet->pvr_file, sheet->frame_w, sheet->frame_h, sheet->cols);
}

int texture_load_bg(bz_texture_t *out, const bz_bg_t *bg) {
    return load_raw(out, bg->pvr_file, bg->w, bg->h, 1);
}

/* ---- animation ---------------------------------------------------------*/

void anim_play(bz_animator_t *a, const bz_texture_t *tex, const bz_anim_t *def) {
    a->tex = tex;
    a->start = def->start;
    a->count = def->count;
    a->fps = def->fps;
    a->loop = def->loop;
    a->cur = 0;
    a->timer = 0.0f;
    a->finished = 0;
}

void anim_update(bz_animator_t *a, float dt) {
    if (!a->tex || a->count <= 1 || (a->finished && !a->loop))
        return;
    a->timer += dt;
    float frame_time = 1.0f / (a->fps > 0 ? a->fps : 1);
    while (a->timer >= frame_time) {
        a->timer -= frame_time;
        a->cur++;
        if (a->cur >= a->count) {
            if (a->loop) {
                a->cur = 0;
            } else {
                a->cur = a->count - 1;
                a->finished = 1;
                break;
            }
        }
    }
}

int anim_frame(const bz_animator_t *a) {
    return a->start + a->cur;
}
