#include "jb_assets_rom.h"

#include <string.h>

static int EndsWith(const char *path, const char *key)
{
    size_t lp = strlen(path);
    size_t lk = strlen(key);
    size_t i;

    if (lk > lp)
        return 0;
    for (i = 0; i < lk; i++) {
        char a = path[lp - lk + i];
        char b = key[i];

        if (a == '\\')
            a = '/';
        if (a != b)
            return 0;
    }
    return 1;
}

static const uint16_t *FindIn(const jb_asset_rom_entry *table, int count,
                              const char *path, int *out_w, int *out_h)
{
    int i;

    for (i = 0; i < count; i++) {
        if (EndsWith(path, table[i].path)) {
            *out_w = table[i].w;
            *out_h = table[i].h;
            return table[i].pixels;
        }
    }
    return NULL;
}

const uint16_t *AssetRom_Find(const char *path, int *out_w, int *out_h)
{
    return FindIn(jb_assets_rom_table, jb_assets_rom_count, path, out_w, out_h);
}

const uint16_t *AssetRom_FindHalf(const char *path, int *out_w, int *out_h)
{
    return FindIn(jb_assets_rom_half_table, jb_assets_rom_half_count,
                  path, out_w, out_h);
}
