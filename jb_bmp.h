#ifndef JB_BMP_H
#define JB_BMP_H

#include "jb_gfx.h"

/* Reproduces the JumpyBall.exe Sprite_LoadResourceBitmap 0x00021a04 ->
   Bitmap_LockBits 0x00024054 -> Surface_ImportPixels 0x00023f30 chain: the
   result is w*h packed RGB565 pixels, stride w, top row first. */
int Bmp_LoadSprite(const jb_surface *dst, const char *path, jb_sprite *out);

#ifdef JB_MENU_HALF
/* Points the sprite at the half-size ROM variant (gen_assets_rom.py --half) when
   one exists for the path, else defers to Bmp_LoadSprite for the full sprite. */
int Bmp_LoadSpriteHalf(const jb_surface *dst, const char *path, jb_sprite *out);
#endif

void Bmp_FreeSprite(jb_sprite *spr);

#endif /* JB_BMP_H */
