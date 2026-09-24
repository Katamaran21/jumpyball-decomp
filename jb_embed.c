#include "jb_embed.h"

#include <stdlib.h>
#include <string.h>

int Embed_Count(void)
{
    return jb_embed_count;
}

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

const unsigned char *Embed_Find(const char *path, long *out_len)
{
    int i;

    for (i = 0; i < jb_embed_count; i++) {
        if (EndsWith(path, jb_embed_table[i].path)) {
            *out_len = jb_embed_table[i].len;
            return jb_embed_table[i].data;
        }
    }
    return NULL;
}

unsigned char *Embed_Read(const char *path, long *out_len)
{
    long                 len = 0;
    const unsigned char *src = Embed_Find(path, &len);
    unsigned char       *buf;

    if (src == NULL)
        return NULL;
    buf = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
    if (buf == NULL)
        return NULL;
    memcpy(buf, src, (size_t)len);
    *out_len = len;
    return buf;
}
