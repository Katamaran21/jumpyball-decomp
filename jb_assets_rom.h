#ifndef JB_ASSETS_ROM_H
#define JB_ASSETS_ROM_H

#include <stdint.h>

/* Bitmap sprites decoded to RGB565 at build time and placed in cartridge ROM
   for the GBA port (JB_ASSETS_ROM).  The generated table lives in
   jb_assets_rom_data.c (tools/gen_assets_rom.py); jb_bmp.c Bmp_LoadSprite
   points a sprite straight at these const pixels instead of malloc-decoding a
   surface, because the ~1.5 MB of decoded sprites cannot fit the GBA's 288 KB
   of RAM.  The decode matches jb_gfx.c Color_Pack16 bit for bit. */

typedef struct {
    const char     *path; /* forward-slash logical path, "BITMAP/261.bmp" */
    const uint16_t *pixels;
    int             w;
    int             h;
} jb_asset_rom_entry;

/* Defined by the generated jb_assets_rom_data.c. */
extern const jb_asset_rom_entry jb_assets_rom_table[];
extern const int                jb_assets_rom_count;

/* Half-size variants of the menu/keyconfig chrome and the small font, emitted
   by gen_assets_rom.py --half for the native-120x160 GBA menu (JB_MENU_HALF).
   Only the paths passed in --half appear here; the game shares none of them. */
extern const jb_asset_rom_entry jb_assets_rom_half_table[];
extern const int                jb_assets_rom_half_count;

/* Zero-copy lookup, same trailing-path match as jb_embed.c Embed_Find: returns
   the ROM pixels and dimensions for a path ending in a table entry's logical
   path, or NULL if absent. */
const uint16_t *AssetRom_Find(const char *path, int *out_w, int *out_h);

/* Same match against the half-size table; NULL when the path has no half
   variant, so the caller falls back to the full-size AssetRom_Find. */
const uint16_t *AssetRom_FindHalf(const char *path, int *out_w, int *out_h);

#endif /* JB_ASSETS_ROM_H */
