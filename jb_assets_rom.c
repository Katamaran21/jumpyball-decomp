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

const uint16_t *AssetRom_Find(const char *path, int *out_w, int *out_h)
{
    int i;

    for (i = 0; i < jb_assets_rom_count; i++) {
        if (EndsWith(path, jb_assets_rom_table[i].path)) {
            *out_w = jb_assets_rom_table[i].w;
            *out_h = jb_assets_rom_table[i].h;
            return jb_assets_rom_table[i].pixels;
        }
    }
    return NULL;
}
