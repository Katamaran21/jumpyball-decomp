#include "jb_platform.h"
#include "jb_touch_sdl2.h"

#ifdef JB_EMBED
#include "jb_embed.h"
#endif

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_Window   *jb_win;
static SDL_Renderer *jb_ren;
static SDL_Texture  *jb_tex;
static uint16_t     *jb_pixels;
static jb_surface    jb_back;
static int           jb_w;
static int           jb_h;
static int           jb_keys[JB_KEY_COUNT];
static int           jb_running;
static int           jb_touch;

int Platform_Init(int w, int h, int scale, const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        return 0;
#ifdef __ANDROID__
    jb_touch = 1;
#else
    jb_touch = SDL_getenv("JUMPYBALL_TOUCH") != NULL;
    if (jb_touch)
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
#endif
    jb_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              w * scale, h * scale, jb_touch ? SDL_WINDOW_RESIZABLE : 0);
    if (!jb_win)
        return 0;
    jb_ren = SDL_CreateRenderer(jb_win, -1,
                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!jb_ren)
        jb_ren = SDL_CreateRenderer(jb_win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!jb_ren)
        jb_ren = SDL_CreateRenderer(jb_win, -1, 0);
    if (!jb_ren)
        return 0;
    if (!jb_touch)
        SDL_RenderSetLogicalSize(jb_ren, w, h);
    jb_tex = SDL_CreateTexture(jb_ren, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!jb_tex)
        return 0;
    /* JumpyBall.exe Game_Init 0x000113bc passes 0 to Gfx_CreateBackBuffer
       0x00021698 when g_viewH 0x0002626c is not 0xf0, so g_clipHRow 0x000269e8
       keeps its .data value 0x140 and Blit_TileH admits y == h. */
    jb_pixels = (uint16_t *)calloc((size_t)w * ((size_t)h + 1u), sizeof(uint16_t));
    if (!jb_pixels)
        return 0;
    jb_w = w;
    jb_h = h;
    jb_back.pixels  = jb_pixels;
    jb_back.bpp     = 0x10;
    jb_back.fmt     = JB_FMT_RGB565;
    jb_back.x_pitch = 1;
    jb_back.y_pitch = w;
    jb_clip_w     = w;
    jb_clip_h     = h;
    jb_clip_h_row = h;
    jb_running = 1;
    return 1;
}
void Platform_Shutdown(void)
{
    Platform_AudioShutdown();
    free(jb_pixels);
    jb_pixels = NULL;
    if (jb_tex)
        SDL_DestroyTexture(jb_tex);
    if (jb_ren)
        SDL_DestroyRenderer(jb_ren);
    if (jb_win)
        SDL_DestroyWindow(jb_win);
    jb_tex = NULL;
    jb_ren = NULL;
    jb_win = NULL;
    SDL_Quit();
}

jb_surface *Platform_BackBuffer(void)
{
    return &jb_back;
}

void Platform_Present(void)
{
    SDL_UpdateTexture(jb_tex, NULL, jb_pixels, jb_w * (int)sizeof(uint16_t));
    if (jb_touch) {
        int ww = 0, wh = 0;

        SDL_GetRendererOutputSize(jb_ren, &ww, &wh);
        Touch_Layout(ww, wh, jb_w, jb_h);
        SDL_SetRenderDrawBlendMode(jb_ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(jb_ren, 0, 0, 0, 0xff);
        SDL_RenderClear(jb_ren);
        SDL_RenderCopy(jb_ren, jb_tex, NULL, Touch_GameRect());
        Touch_Draw(jb_ren);
        SDL_RenderPresent(jb_ren);
        return;
    }
    SDL_RenderClear(jb_ren);
    SDL_RenderCopy(jb_ren, jb_tex, NULL, NULL);
    SDL_RenderPresent(jb_ren);
}

int Platform_TouchActive(void)
{
    return jb_touch;
}

static int jb_keymap[JB_KEY_COUNT] = {
    SDLK_LEFT, SDLK_RIGHT, SDLK_UP, SDLK_DOWN, SDLK_SPACE, SDLK_RETURN
};

#define JB_RAWQ 16

static int jb_rawq[JB_RAWQ];
static int jb_rawq_head;
static int jb_rawq_tail;

static int MapKey(SDL_Keycode code)
{
    int k;

    if ((int)code == JB_KEY_UNBOUND)
        return -1;
    for (k = 0; k < JB_KEY_COUNT; k++) {
        if (jb_keymap[k] == (int)code)
            return k;
    }
    return -1;
}

int Platform_KeyBinding(int key)
{
    if (key < 0 || key >= JB_KEY_COUNT)
        return JB_KEY_UNBOUND;
    return jb_keymap[key];
}

void Platform_SetKeyBinding(int key, int code)
{
    if (key < 0 || key >= JB_KEY_COUNT)
        return;
    jb_keymap[key] = code;
    jb_keys[key]   = 0;
}

int Platform_NextRawKey(void)
{
    int code;

    if (jb_rawq_tail == jb_rawq_head)
        return JB_KEY_UNBOUND;
    code         = jb_rawq[jb_rawq_tail];
    jb_rawq_tail = (jb_rawq_tail + 1) % JB_RAWQ;
    return code;
}

void Platform_FlushRawKeys(void)
{
    jb_rawq_tail = jb_rawq_head;
}

int Platform_PollEvents(void)
{
    SDL_Event ev;
    int k;

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            jb_running = 0;
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:
            if (jb_touch)
                Touch_Handle(&ev);
            break;
        case SDL_APP_WILLENTERBACKGROUND:
            Platform_AudioPause(1);
            break;
        case SDL_APP_DIDENTERFOREGROUND:
            Platform_AudioPause(0);
            break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.sym == SDLK_ESCAPE ||
                ev.key.keysym.sym == SDLK_AC_BACK) {
                jb_running = 0;
                break;
            }
            if (ev.key.repeat == 0) {
                int next = (jb_rawq_head + 1) % JB_RAWQ;

                if (next != jb_rawq_tail) {
                    jb_rawq[jb_rawq_head] = (int)ev.key.keysym.sym;
                    jb_rawq_head          = next;
                }
            }
            k = MapKey(ev.key.keysym.sym);
            if (k >= 0)
                jb_keys[k] = 1;
            break;
        case SDL_KEYUP:
            k = MapKey(ev.key.keysym.sym);
            if (k >= 0)
                jb_keys[k] = 0;
            break;
        default:
            break;
        }
    }
    return jb_running;
}

int Platform_KeyDown(int key)
{
    if (key < 0 || key >= JB_KEY_COUNT)
        return 0;
    if (jb_keys[key])
        return 1;
    return jb_touch ? Touch_Down(key) : 0;
}

unsigned Platform_Ticks(void)
{
    return (unsigned)SDL_GetTicks();
}

void Platform_Delay(unsigned ms)
{
    SDL_Delay((Uint32)ms);
}

const char *Platform_BasePath(void)
{
#ifdef __ANDROID__
    return "";
#else
    static char path[512];

    if (path[0] == '\0') {
        char *sdl = SDL_GetBasePath();

        if (sdl != NULL) {
            SDL_strlcpy(path, sdl, sizeof path);
            SDL_free(sdl);
        } else {
            SDL_strlcpy(path, "./", sizeof path);
        }
    }
    return path;
#endif
}

/* SDL_GetBasePath is the only source here, so there is nothing to tell apart;
   the native backend is the one with several. */
const char *Platform_BaseOrigin(void)
{
    return "SDL_GetBasePath";
}

const char *Platform_PrefPath(void)
{
#ifdef __ANDROID__
    static char path[512];

    if (path[0] == '\0') {
        const char *dir = SDL_AndroidGetInternalStoragePath();

        if (dir == NULL)
            return "";
        SDL_snprintf(path, sizeof path, "%s/", dir);
    }
    return path;
#else
    return Platform_BasePath();
#endif
}

int Platform_FileExists(const char *path)
{
    SDL_RWops *rw;

#ifdef JB_EMBED
    {
        long n;
        if (Embed_Find(path, &n))
            return 1;
    }
#endif
    rw = SDL_RWFromFile(path, "rb");
    if (rw == NULL)
        return 0;
    SDL_RWclose(rw);
    return 1;
}

unsigned char *Platform_ReadFile(const char *path, long *out_len)
{
    SDL_RWops     *rw;
    Sint64         size;
    unsigned char *buf;

#ifdef JB_EMBED
    {
        unsigned char *e = Embed_Read(path, out_len);
        if (e)
            return e;
    }
#endif
    rw = SDL_RWFromFile(path, "rb");
    if (rw == NULL)
        return NULL;
    size = SDL_RWsize(rw);
    if (size <= 0 || size > 0x2000000) {
        SDL_RWclose(rw);
        return NULL;
    }
    buf = (unsigned char *)malloc((size_t)size);
    if (buf == NULL) {
        SDL_RWclose(rw);
        return NULL;
    }
    if (SDL_RWread(rw, buf, 1, (size_t)size) != (size_t)size) {
        SDL_RWclose(rw);
        free(buf);
        return NULL;
    }
    SDL_RWclose(rw);
    *out_len = (long)size;
    return buf;
}

void Platform_ShowError(const char *title, const char *text)
{
    fprintf(stderr, "%s: %s\n", title, text);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, jb_win);
}

const char *Platform_LastError(void)
{
    return SDL_GetError();
}
