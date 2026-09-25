#include "jb_keyconfig.h"

#include "jb_platform.h"

#include <stdio.h>

#ifdef JB_MENU_HALF
#define JB_MC(v) ((v) / 2)
#else
#define JB_MC(v) (v)
#endif

#define JB_KEYS_FILE "keys.cfg"

static const char *KeysPath(void)
{
    static char path[512];

    snprintf(path, sizeof path, "%s%s", Platform_PrefPath(), JB_KEYS_FILE);
    return path;
}

static void Save(void)
{
    FILE *f = fopen(KeysPath(), "w");
    int   i;

    if (f == NULL)
        return;
    for (i = 0; i < JB_KEY_COUNT; i++)
        fprintf(f, "%d\n", Platform_KeyBinding(i));
    fclose(f);
}

void KeyConfig_Load(void)
{
    FILE *f = fopen(KeysPath(), "r");
    int   code[JB_KEY_COUNT];
    int   i;

    if (f == NULL)
        return;
    for (i = 0; i < JB_KEY_COUNT; i++) {
        if (fscanf(f, "%d", &code[i]) != 1) {
            fclose(f);
            return;
        }
    }
    fclose(f);

    for (i = 0; i < JB_KEY_COUNT; i++) {
        int j;

        if (code[i] == JB_KEY_UNBOUND)
            return;
        for (j = 0; j < i; j++) {
            if (code[j] == code[i])
                return;
        }
    }
    for (i = 0; i < JB_KEY_COUNT; i++)
        Platform_SetKeyBinding(i, code[i]);
}

void KeyConfig_Begin(jb_keyconfig *kc)
{
    int i;

    for (i = 0; i < JB_KEY_COUNT; i++)
        Platform_SetKeyBinding(i, JB_KEY_UNBOUND);
    kc->step   = 0;
    kc->active = 1;
    Platform_FlushRawKeys();
}

int KeyConfig_KeyDown(jb_keyconfig *kc, int code)
{
    int i;

    if (!kc->active || code == JB_KEY_UNBOUND)
        return 0;
    for (i = 0; i < JB_KEY_COUNT; i++) {
        if (Platform_KeyBinding(i) == code)
            return 0;
    }
    Platform_SetKeyBinding(kc->step, code);
    kc->step++;
    if (kc->step == JB_KEY_COUNT) {
        kc->active = 0;
        Save();
    }
    return 1;
}

/* jumpyball JumpyBall.exe FUN_0001a3e8 0x0001a3e8 writes
   Color_Pack16If16bpp(&g_screen, 0) over g_viewW by g_viewH pixels. */
static void FillBlack(const jb_surface *dst, int w, int h)
{
    uint16_t pix = Color_Pack16If16bpp(dst, 0);
    int      x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++)
            dst->pixels[y * dst->y_pitch + x * dst->x_pitch] = pix;
    }
}

void KeyConfig_DrawFrame(const jb_keyconfig *kc)
{
    int x = kc->view_w - JB_MC(0xb0);

    if (x < 0)
        x++;
    x >>= 1;

    FillBlack(kc->screen, kc->view_w, kc->view_h);
    Blit(kc->screen, x, JB_MC(0xf), JB_MC(0xb0), JB_MC(0x2a), kc->panel, kc->key,
         0, 0);
    Blit(kc->screen, x, kc->view_center_x + JB_MC(0x14), JB_MC(0xb0), JB_MC(0x19),
         kc->panel, kc->key, 0, JB_MC(kc->step * 0x19 + 0x2f));
    Blit(kc->screen, x, kc->view_h - JB_MC(0x14), JB_MC(0xb0), JB_MC(0x14),
         kc->panel, kc->key, 0, JB_MC(0xde));
}
