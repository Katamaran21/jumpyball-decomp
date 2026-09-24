#include "jb_bmp.h"

#include "jb_platform.h"

#ifdef JB_ASSETS_ROM
#include "jb_assets_rom.h"
#endif

#include <stdlib.h>

static unsigned Rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned Rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static uint32_t Indexed(const unsigned char *pal, unsigned idx)
{
    return ((uint32_t)pal[idx * 4] << 16) | ((uint32_t)pal[idx * 4 + 1] << 8) |
           (uint32_t)pal[idx * 4 + 2];
}

static unsigned SampleIndex(const unsigned char *row, unsigned bpp, int x)
{
    if (bpp == 8)
        return row[x];
    if (bpp == 4)
        return (x & 1) ? (row[x >> 1] & 0xf) : (unsigned)(row[x >> 1] >> 4);
    return (unsigned)((row[x >> 3] >> (7 - (x & 7))) & 1);
}

int Bmp_LoadSprite(const jb_surface *dst, const char *path, jb_sprite *out)
{
    unsigned char *f;
    const unsigned char *pal;
    long len;
    unsigned off, hdr, bpp, comp, stride, pal_bytes;
    int w, h, x, y, top_down;
    uint16_t *px;

#ifdef JB_ASSETS_ROM
    {
        int rw, rh;
        const uint16_t *rom = AssetRom_Find(path, &rw, &rh);

        if (rom) {
            out->pixels = (uint16_t *)rom;
            out->w = rw;
            out->h = rh;
            return 1;
        }
    }
#endif

    f = Platform_ReadFile(path, &len);
    if (!f)
        return 0;
    if (len < 54 || f[0] != 'B' || f[1] != 'M') {
        free(f);
        return 0;
    }
    off  = Rd32(f + 10);
    hdr  = Rd32(f + 14);
    w    = (int)Rd32(f + 18);
    h    = (int)Rd32(f + 22);
    bpp  = Rd16(f + 28);
    comp = Rd32(f + 30);
    top_down = 0;
    if (h < 0) {
        h = -h;
        top_down = 1;
    }
    pal_bytes = (bpp < 24) ? (4u << bpp) : 0u;
    stride = (((unsigned)w * bpp + 31u) / 32u) * 4u;
    pal = f + 14 + hdr;
    if (w <= 0 || h <= 0 || comp != 0 || hdr < 40 ||
        (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24) ||
        14 + hdr + pal_bytes > (unsigned)len ||
        off + stride * (unsigned)h > (unsigned)len) {
        free(f);
        return 0;
    }
    px = (uint16_t *)malloc((size_t)w * (size_t)h * sizeof(uint16_t));
    if (!px) {
        free(f);
        return 0;
    }
    /* JumpyBall.exe Surface_ImportPixels 0x00023f30 walks the DIB bottom row
       first and assembles the COLORREF as (p[0]<<16)|(p[1]<<8)|p[2]. */
    for (y = 0; y < h; y++) {
        const unsigned char *row =
            f + off + stride * (unsigned)(top_down ? y : h - 1 - y);
        uint16_t *o = px + (size_t)y * (size_t)w;
        uint32_t c;

        for (x = 0; x < w; x++) {
            if (bpp == 24)
                c = ((uint32_t)row[x * 3] << 16) |
                    ((uint32_t)row[x * 3 + 1] << 8) | (uint32_t)row[x * 3 + 2];
            else
                c = Indexed(pal, SampleIndex(row, bpp, x));
            o[x] = Color_Pack16(dst, c);
        }
    }
    free(f);
    out->pixels = px;
    out->w = w;
    out->h = h;
    return 1;
}

void Bmp_FreeSprite(jb_sprite *spr)
{
#ifndef JB_ASSETS_ROM
    free(spr->pixels);
#endif
    spr->pixels = NULL;
    spr->w = 0;
    spr->h = 0;
}
