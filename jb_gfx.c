/* Graphics core ported from JumpyBall.exe (imagebase 0x00010000). */
#include "jb_gfx.h"

#include <string.h>

/* JumpyBall.exe Gfx_CreateBackBuffer 0x00021698, 0x000269e0 / 0x000269e4 /
   0x000269e8 .data initialisers. */
int jb_clip_w     = 240;
int jb_clip_h     = 320;
int jb_clip_h_row = 320;

/* JumpyBall.exe Color_Pack16 0x00023bb4, fmt flag 0x80 branch. */
uint16_t Color_Pack16(const jb_surface *dst, uint32_t colorref)
{
    unsigned v;

    if (!(dst->fmt & JB_FMT_RGB565))
        return 0;
    v = (((colorref & 0xffff) >> 8 & 0xfff8) | ((colorref & 0xf8) << 5)) << 3;
    return (uint16_t)(v | ((colorref >> 16 & 0xff) >> 3));
}

/* JumpyBall.exe Color_Pack16If16bpp 0x00024030 tests the destination word at
   +0x18 against 0x10 before calling Color_Pack16 0x00023bb4. */
uint16_t Color_Pack16If16bpp(const jb_surface *dst, uint32_t colorref)
{
    if (dst->bpp != 16)
        return 0;
    return Color_Pack16(dst, colorref);
}

/* JumpyBall.exe Color_Blend 0x00023c3c. */
uint16_t Color_Blend(const jb_surface *dst, uint16_t src, uint16_t dstpix, int blend)
{
    int sr, sg, sb, dr, dg, db, inv, r, g, b;
    unsigned out;

    if (blend == 0)
        return src;
    if (!(dst->fmt & JB_FMT_RGB565))
        return 0;
    sg = (src >> 3) & 0xfc;
    sr = (src >> 8) & 0xf8;
    sb = (src & 0x1f) << 3;
    dg = (dstpix >> 3) & 0xfc;
    dr = (dstpix >> 8) & 0xf8;
    db = (dstpix & 0x1f) << 3;
    inv = 100 - blend;
    r = (inv * sr + dr * blend) / 100;
    b = (inv * sb + db * blend) / 100;
    g = (inv * sg + dg * blend) / 100;
    out = ((0xfff8 & (unsigned)g) | (unsigned)((r >> 3) << 8)) << 3;
    return (uint16_t)((out | (unsigned)(b >> 3)) & 0xffff);
}

/* JumpyBall.exe FUN_00023ddc 0x00023ddc, the pixel operation of
   Blit_Glyph_Core 0x00022288. */
uint16_t Color_MaskTint(const jb_surface *dst, uint16_t src, uint16_t dstpix,
                        int r, int g, int b)
{
    int dr, dg, db, cov, inv, orr, ogg, obb;
    unsigned out;

    if (!(dst->fmt & JB_FMT_RGB565))
        return 0;
    db = (dstpix & 0x1f) << 3;
    dg = (dstpix >> 3) & 0xfc;
    dr = (dstpix >> 8) & 0xf8;
    cov = (src & 0x1f) << 3;
    inv = 0xff - cov;
    orr = (inv * dr + cov * r) / 0xff;
    obb = (inv * db + cov * b) / 0xff;
    ogg = (inv * dg + cov * g) / 0xff;
    out = ((0xfff8 & (unsigned)ogg) | (unsigned)((orr >> 3) << 8)) << 3;
    return (uint16_t)((out | (unsigned)(obb >> 3)) & 0xffff);
}

/* JumpyBall.exe Blit_Core 0x00021a98. */
int Blit_Core(const jb_surface *dst, int x, int y, int w, int h,
              const jb_sprite *src, int src_x, int src_y, unsigned key)
{
    uint16_t *drow;
    const uint16_t *srow;
    int cw, i;

    if (!dst->pixels || !src || !src->pixels || w < 1 || h < 1 || src_x < 0 || src_y < 0)
        return 0;
    cw = w;
    if (src->w < w + src_x)
        cw = src->w - src_x;
    if (src->h < h + src_y)
        h = src->h - src_y;
    if (x > jb_clip_w || y > jb_clip_h)
        return 1;
    if (x < 0) {
        src_x -= x;
        cw += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        src_y -= y;
        y = 0;
    }
    if (jb_clip_w - x < cw)
        cw = jb_clip_w - x;
    if (jb_clip_h - y < h)
        h = jb_clip_h - y;
    if (dst->bpp != 0x10)
        return 0;
    drow = dst->pixels + dst->x_pitch * x + dst->y_pitch * y;
    srow = src->pixels + src->w * src_y + src_x;
    /* Contiguous unkeyed rows reduce to a memcpy, which devkitARM turns into
       word LDM/STM instead of the per-halfword loop; identical output. */
    if (key == JB_NO_KEY && dst->x_pitch == 1) {
        for (; h > 0; h--) {
            if (cw > 0)
                memcpy(drow, srow, (size_t)cw * sizeof(uint16_t));
            drow += dst->y_pitch;
            srow += src->w;
        }
        return 1;
    }
    for (; h > 0; h--) {
        uint16_t *d = drow;
        const uint16_t *s = srow;
        for (i = cw; i > 0; i--) {
            if (key == JB_NO_KEY || *s != (uint16_t)key)
                *d = *s;
            d += dst->x_pitch;
            s++;
        }
        drow += dst->y_pitch;
        srow += src->w;
    }
    return 1;
}

/* JumpyBall.exe Blit_Keyed 0x000239f8 -> Blit_Keyed_Core 0x00022574. */
int Blit_Keyed(const jb_surface *dst, int x, int y, int w, int h,
               const jb_sprite *src, int blend, unsigned key,
               int src_x, int src_y)
{
    uint16_t *drow;
    const uint16_t *srow;
    int cw, i;

    if (!dst->pixels || !src || !src->pixels || w < 1 || h < 1 || src_x < 0 || src_y < 0)
        return 0;
    if (src->w < w + src_x)
        w = src->w - src_x;
    if (src->h < h + src_y)
        h = src->h - src_y;
    if (x > jb_clip_w || y > jb_clip_h)
        return 1;
    cw = w;
    if (x < 0) {
        cw = x + w;
        src_x -= x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        src_y -= y;
        y = 0;
    }
    if (jb_clip_w - x < cw)
        cw = jb_clip_w - x;
    if (jb_clip_h - y < h)
        h = jb_clip_h - y;
    if (dst->bpp != 0x10)
        return 0;
    drow = dst->pixels + dst->x_pitch * x + dst->y_pitch * y;
    srow = src->pixels + src->w * src_y + src_x;
    for (; h > 0; h--) {
        uint16_t *d = drow;
        const uint16_t *s = srow;
        for (i = cw; i > 0; i--) {
            if (key == JB_NO_KEY || *s != (uint16_t)key)
                *d = Color_Blend(dst, *s, *d, blend);
            d += dst->x_pitch;
            s++;
        }
        drow += dst->y_pitch;
        srow += src->w;
    }
    return 1;
}

/* JumpyBall.exe Blit_Glyph 0x000239ac -> Blit_Glyph_Core 0x00022288; every pixel
   goes through FUN_00023ddc 0x00023ddc and there is no colour key. */
int Blit_Glyph(const jb_surface *dst, int x, int y, int w, int h,
               const jb_sprite *src, int r, int g, int b,
               int src_x, int src_y)
{
    uint16_t *drow;
    const uint16_t *srow;
    int cw, i;

    if (!dst->pixels || !src || !src->pixels || w < 1 || h < 1 || src_x < 0 || src_y < 0)
        return 0;
    if (src->w < w + src_x)
        w = src->w - src_x;
    if (src->h < h + src_y)
        h = src->h - src_y;
    if (x > jb_clip_w || y > jb_clip_h)
        return 1;
    cw = w;
    if (x < 0) {
        cw = x + w;
        src_x -= x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        src_y -= y;
        y = 0;
    }
    if (jb_clip_w - x < cw)
        cw = jb_clip_w - x;
    if (jb_clip_h - y < h)
        h = jb_clip_h - y;
    if (dst->bpp != 0x10)
        return 0;
    drow = dst->pixels + dst->x_pitch * x + dst->y_pitch * y;
    srow = src->pixels + src->w * src_y + src_x;
    for (; h > 0; h--) {
        uint16_t *d = drow;
        const uint16_t *s = srow;
        for (i = cw; i > 0; i--) {
            *d = Color_MaskTint(dst, *s, *d, r, g, b);
            d += dst->x_pitch;
            s++;
        }
        drow += dst->y_pitch;
        srow += src->w;
    }
    return 1;
}

/* JumpyBall.exe Blit_NoKey 0x00023a3c -> Blit_Core 0x00021a98 with key 0xffff. */
int Blit_NoKey(const jb_surface *dst, int x, int y, int w, int h,
               const jb_sprite *src, int src_x, int src_y)
{
    return Blit_Core(dst, x, y, w, h, src, src_x, src_y, JB_NO_KEY);
}

/* JumpyBall.exe Blit 0x00023b78 -> Blit_Core 0x00021a98. */
int Blit(const jb_surface *dst, int x, int y, int w, int h,
         const jb_sprite *src, unsigned key, int src_x, int src_y)
{
    return Blit_Core(dst, x, y, w, h, src, src_x, src_y, key);
}
