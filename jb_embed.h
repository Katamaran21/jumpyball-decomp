#ifndef JB_EMBED_H
#define JB_EMBED_H

/* Assets compiled into the executable (JB_EMBED build).  The generated table
   lives in jb_embed_data.c (tools/gen_embed.py); Platform_ReadFile /
   Platform_FileExists consult it, so jb_assets.c / jb_bmp.c / the audio
   backends resolve a path such as "BITMAP/261.bmp" against the table. */

typedef struct {
    const char          *path; /* forward-slash logical path, "BITMAP/261.bmp" */
    const unsigned char *data;
    long                 len;
} jb_embed_entry;

/* Defined by the generated jb_embed_data.c. */
extern const jb_embed_entry jb_embed_table[];
extern const int            jb_embed_count;

int Embed_Count(void);

/* Zero-copy lookup: matches when the query path ends with an entry's logical
   path, so it works whether the caller passes "BITMAP/261.bmp" or an absolute
   "\\Program Files\\JumpyBall\\BITMAP\\261.bmp".  Returns NULL if absent. */
const unsigned char *Embed_Find(const char *path, long *out_len);

/* malloc'd copy of an embedded asset, matching the free() contract every
   Platform_ReadFile caller already uses.  NULL if absent or out of memory. */
unsigned char *Embed_Read(const char *path, long *out_len);

#endif /* JB_EMBED_H */
