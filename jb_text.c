#include "jb_text.h"
#include <stddef.h>
#include "jb_assets.h"
#include "jb_bmp.h"
#include "jb_consts.h"

#define JB_GLYPH_N       128
#define JB_GLYPH_FIRST   33
#define JB_SMALL_STRIP_W 0x4ba
#define JB_SPACE_X       0x4bc
#ifdef JB_MENU_HALF
#define JB_SPACE_W       3
#else
#define JB_SPACE_W       5
#endif

static int              jb_glyph_w[JB_GLYPH_N];
static int              jb_glyph_x[JB_GLYPH_N];
static jb_sprite        jb_sheet_small;
static jb_sprite        jb_sheet_large;
static const jb_sprite *jb_font;
static int              jb_font_size;

/* JumpyBall.exe Game_Init 0x000113bc at 0x00011a30 walks row 0 of g_fontSprite
   for 0x4ba pixels and takes each 0xff00ff pixel pair as one glyph, starting at
   g_glyphX 0x00027698 index 0x21. */
static void ScanMarkerPairs(const jb_surface *dst, const jb_sprite *font)
{
    uint16_t marker = Color_Pack16If16bpp(dst, 0x00ff00ff);
    int      x, idx = JB_GLYPH_FIRST, pair = 0;

    for (x = 0; x < JB_SMALL_STRIP_W && x < font->w && idx < JB_GLYPH_N; x++) {
        if (font->pixels[x] != marker)
            continue;
        if (pair == 0) {
            jb_glyph_x[idx] = x + 1;
            pair            = 1;
        } else {
            jb_glyph_w[idx] = x - jb_glyph_x[idx];
            pair            = 0;
            idx++;
        }
    }
}

/* JumpyBall.exe Font_Load 0x0001f4d8 g_fontSize == 2 branch tests every one of
   the 0x15 rows of g_fontSprite for the packed colour 0, and the first column of
   each black run k stores g_glyphX 0x00027698 index 0x20 + k while
   g_glyphWidth 0x00027298 index 0x1f + k takes that column minus the previous
   one plus 1. */
static void ScanBlackColumns(const jb_surface *dst, const jb_sprite *font)
{
    uint16_t black = Color_Pack16If16bpp(dst, 0);
    int      x, y, idx = 32, gap = 1;

    for (x = 0; x < font->w && idx < JB_GLYPH_N; x++) {
        for (y = 0; y < font->h; y++) {
            if (font->pixels[y * font->w + x] != black)
                break;
        }
        if (y < font->h) {
            gap = 1;
            continue;
        }
        if (!gap)
            continue;
        gap             = 0;
        jb_glyph_x[idx] = x;
        jb_glyph_w[idx - 1] = x - jb_glyph_x[idx - 1] + 1;
        idx++;
    }
}

void Font_Select(const jb_surface *dst, int size)
{
    const jb_sprite *sheet;

    if (jb_font_size == size)
        return;

    sheet = (size == 2) ? &jb_sheet_large : &jb_sheet_small;
    if (sheet->pixels == NULL)
        return;

    if (size == 2)
        ScanBlackColumns(dst, sheet);
    else
        ScanMarkerPairs(dst, sheet);

    /* JumpyBall.exe Font_Load 0x0001f4d8 tail stores 5 to g_glyphWidth
       0x00027298 index 0x20 and 0x4bc to g_glyphX 0x00027698 index 0x20. */
    jb_glyph_w[' '] = JB_SPACE_W;
    jb_glyph_x[' '] = JB_SPACE_X;

    jb_font      = sheet;
    jb_font_size = size;
}

int Font_Load(const jb_surface *dst)
{
#ifdef JB_MENU_HALF
    if (!Bmp_LoadSpriteHalf(dst, Assets_Bitmap(JB_RES_FONT), &jb_sheet_small))
        return 0;
#else
    if (!Bmp_LoadSprite(dst, Assets_Bitmap(JB_RES_FONT), &jb_sheet_small))
        return 0;
#endif
    if (!Bmp_LoadSprite(dst, Assets_Bitmap(JB_RES_FONT_LARGE), &jb_sheet_large))
        return 0;

    /* JumpyBall.exe Game_Init 0x000113bc at 0x00011a94 calls Font_Load with 1. */
    jb_font_size = 0;
    Font_Select(dst, 1);
    return jb_font != NULL;
}

void Font_Free(void)
{
    Bmp_FreeSprite(&jb_sheet_small);
    Bmp_FreeSprite(&jb_sheet_large);
    jb_font      = NULL;
    jb_font_size = 0;
}

/* JumpyBall.exe Blit_Glyph 0x000239ac is called with h
   ((g_fontSize == 2) + 2) * 7 and source row 0. */
static int GlyphHeight(void)
{
#ifdef JB_MENU_HALF
    if (jb_font_size != 2)
        return 7;
#endif
    return ((jb_font_size == 2) + 2) * 7;
}

void Text_DrawCentered(const jb_surface *dst, int x, int y, const char *s,
                       int align, int r, int g, int b)
{
    int         total = 0;
    int         pen   = 0;
    int         h     = GlyphHeight();
    const char *p;
    int         c;

    if (jb_font == NULL || s == NULL)
        return;

    /* JumpyBall.exe Text_DrawCentered 0x00012878 measures only when align > 0
       and folds 0x61 and above down by 0x20. */
    if (align > 0) {
        for (p = s; *p != '\0'; p++) {
            c = (unsigned char)*p;
            if (c >= JB_GLYPH_N)
                continue;
            total += jb_glyph_w[c - (c > 0x60 ? 0x20 : 0)] * align;
        }
        if (total < 0)
            total++;
    }

    /* JumpyBall.exe 0x00012938 indexes g_glyphWidth and g_glyphX with the raw
       character, without the fold the measure loop applies. */
    for (p = s; *p != '\0'; p++) {
        c = (unsigned char)*p;
        if (c >= JB_GLYPH_N)
            continue;
        Blit_Glyph(dst, pen + (x - (total >> 1)), y, jb_glyph_w[c], h, jb_font,
                   r, g, b, jb_glyph_x[c], 0);
        pen += jb_glyph_w[c];
    }
}

void Text_DrawNumber(const jb_surface *dst, int x, int y, int value,
                     int align, int digits, int r, int g, int b)
{
    int total = 0;
    int pen   = 0;
    int h     = GlyphHeight();
    int v     = value;
    int adj, i, c;

    if (jb_font == NULL)
        return;

    for (i = 0; i < digits; i++) {
        c = 0x30 + v % 10;
        v /= 10;
        total += jb_glyph_w[c & (JB_GLYPH_N - 1)];
    }

    adj = total * align;
    if (adj < 0)
        adj++;

    /* JumpyBall.exe Text_DrawNumber 0x000129b4 emits the least significant
       digit first and walks the pen leftwards. */
    v = value;
    for (i = 0; i < digits; i++) {
        c = (0x30 + v % 10) & (JB_GLYPH_N - 1);
        v /= 10;
        pen += jb_glyph_w[c];
        Blit_Glyph(dst, (total - (adj >> 1)) + x - pen, y, jb_glyph_w[c], h,
                   jb_font, r, g, b, jb_glyph_x[c], 0);
    }
}
