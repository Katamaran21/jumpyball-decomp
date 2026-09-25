/* Game Boy Advance host layer for the port.  It stands in for JumpyBall.exe's
   GAPI path (Gfx_CreateBackBuffer 0x00021698, Gfx_Present 0x000126fc), drawing
   the 240x320 game into the 240x160 GBA screen at half scale.  Hardware facts
   are from GBATEK (Martin Korth, "GBA Reference"). */
#include "jb_platform.h"

#ifdef JB_EMBED
#include "jb_embed.h"
#endif

#include "jb_consts.h"

#include <stddef.h>
#include <stdint.h>

/* GBATEK "GBA I/O Map": the memory-mapped registers this backend touches. */
#define REG_DISPCNT  (*(volatile uint16_t *)0x04000000u)
#define REG_VCOUNT   (*(volatile uint16_t *)0x04000006u)
#define REG_KEYINPUT (*(volatile uint16_t *)0x04000130u)
#define REG_TM2CNT_L (*(volatile uint16_t *)0x04000108u)
#define REG_TM2CNT_H (*(volatile uint16_t *)0x0400010Au)
#define REG_TM3CNT_L (*(volatile uint16_t *)0x0400010Cu)
#define REG_TM3CNT_H (*(volatile uint16_t *)0x0400010Eu)
#define GBA_VRAM     ((volatile uint16_t *)0x06000000u)

/* GBATEK "GBA System Control": the undocumented Internal Memory Control word at
   0x04000800.  Bits 24-27 are the 256 KB EWRAM wait control, encoded as
   15 - waitstates; the reset value 0x0D leaves EWRAM at 2 wait states, and 0x0E
   drops it to 1, roughly doubling EWRAM bandwidth (0x0F locks the bus up).  Bit
   5 keeps the 256 KB EWRAM mapped.  Every heavy buffer - jb_backbuf here, the
   Mod mixer state and the tile map in .sbss, and the PCM voices - lives in
   EWRAM, so the 1-wait-state setting speeds up the whole frame. */
#define REG_MEMCNT           (*(volatile uint32_t *)0x04000800u)
#define GBA_MEMCNT_EWRAM_1WS 0x0E000020u

/* GBATEK "GBA System Control": Waitstate Control at 0x04000204.  The Game Pak
   sits in wait-state 0 (0x08000000), where both the code and the embedded
   assets are read; 0x4317 sets WS0 to 3/1 cycles (reset is 4/2) and enables the
   Game Pak prefetch buffer (bit 14), so sequential code and asset fetches from
   ROM run faster. */
#define REG_WAITCNT      (*(volatile uint16_t *)0x04000204u)
#define GBA_WAITCNT_FAST 0x4317u

/* GBATEK "GBA Memory Map": the 32 KB IWRAM at 0x03000000 is a 32-bit bus with
   zero wait states, versus the Game Pak's waited 16-bit bus, so ARM code that
   runs there fetches at one cycle per instruction.  ROM (0x08000000) is ~80 MB
   from IWRAM, past the THUMB BL range, so a caller reaches it via long_call;
   the devkitARM gba crt0 copies the .iwram section from ROM at startup. */
#define JB_IWRAM_CODE \
    __attribute__((section(".iwram"), long_call, target("arm")))

#define GBA_SCREEN_W 240
#define GBA_SCREEN_H 160

/* GBATEK "LCD I/O Display Control": DISPCNT bits 0-2 select the BG mode and
   bit 10 enables BG2, the layer Mode 3 draws its framebuffer through. */
#define DISPCNT_MODE3 0x0003u
#define DISPCNT_BG2   0x0400u

/* GBATEK "Keypad Input Registers": each bit reads 0 while its key is held. */
#define GBA_KEY_A      0x0001u
#define GBA_KEY_B      0x0002u
#define GBA_KEY_SELECT 0x0004u
#define GBA_KEY_START  0x0008u
#define GBA_KEY_RIGHT  0x0010u
#define GBA_KEY_LEFT   0x0020u
#define GBA_KEY_UP     0x0040u
#define GBA_KEY_DOWN   0x0080u
#define GBA_KEY_R      0x0100u
#define GBA_KEY_L      0x0200u
#define GBA_KEY_MASK   0x03FFu

/* GBATEK "Timer Registers": control bits 0-1 pick the prescaler (3 = 1/1024),
   bit 2 counts up on the lower timer's overflow, bit 7 enables the timer.  The
   system clock is 2^24 Hz, so a 1/1024 prescaler ticks TM2 at 2^14 = 16384 Hz;
   TM3 cascades on TM2 overflow to form a 32-bit 16384 Hz counter. */
#define GBA_TM_PRESCALE_1024 0x0003u
#define GBA_TM_CASCADE       0x0004u
#define GBA_TM_ENABLE        0x0080u
#define GBA_TIMER_HZ         16384u

/* One row taller than the view, matching the win32 DIB: Game_Init keeps
   g_clipHRow at 320 and Blit_TileH admits y == h.  240 x 321 x 2 is about
   154 KB, far larger than the 32 KB IWRAM that gba.specs gives .bss, so it
   goes to EWRAM (256 KB) via .sbss, which the devkitARM gba crt0 zero-fills. */
static uint16_t   jb_backbuf[JB_VIEW_W * (JB_VIEW_H + 1)]
    __attribute__((section(".sbss")));
static jb_surface jb_back;
static int        jb_w;
static int        jb_h;
static int        jb_present_w;
static int        jb_present_h;

#define JB_RAWQ 32
static int jb_rawq[JB_RAWQ];
static int jb_rawq_head;
static int jb_rawq_tail;

/* Physical key -> game action: the D-pad drives the four directions, A jumps
   and START opens the menu, the mapping the win32 keys backend also uses. */
static int jb_keymap[JB_KEY_COUNT] = {
    GBA_KEY_LEFT, GBA_KEY_RIGHT, GBA_KEY_UP, GBA_KEY_DOWN,
    GBA_KEY_A, GBA_KEY_START
};
static int      jb_keys[JB_KEY_COUNT];
static unsigned jb_prev_keys;

extern volatile unsigned jb_vblank_ticks;

static void PushRawKey(int code)
{
    int next = (jb_rawq_head + 1) % JB_RAWQ;

    if (next != jb_rawq_tail) {
        jb_rawq[jb_rawq_head] = code;
        jb_rawq_head          = next;
    }
}

int Platform_Init(int w, int h, int scale, const char *title)
{
    int i;

    (void)scale;
    (void)title;

    jb_w = w;
    jb_h = h;
    jb_present_w = w;
    jb_present_h = h;

    jb_back.pixels  = jb_backbuf;
    jb_back.bpp     = 0x10;
    jb_back.fmt     = JB_FMT_RGB565;
    jb_back.x_pitch = 1;
    jb_back.y_pitch = w;
    jb_clip_w     = w;
    jb_clip_h     = h;
    jb_clip_h_row = h;

    REG_MEMCNT  = GBA_MEMCNT_EWRAM_1WS;
    REG_WAITCNT = GBA_WAITCNT_FAST;

    /* Clear VRAM once; Platform_Present only ever writes the centred game
       region, so the letterbox side bars stay black afterwards. */
    for (i = 0; i < GBA_SCREEN_W * GBA_SCREEN_H; i++)
        GBA_VRAM[i] = 0;

    REG_DISPCNT = DISPCNT_MODE3 | DISPCNT_BG2;

    REG_TM2CNT_L = 0;
    REG_TM3CNT_L = 0;
    REG_TM3CNT_H = GBA_TM_CASCADE | GBA_TM_ENABLE;
    REG_TM2CNT_H = GBA_TM_PRESCALE_1024 | GBA_TM_ENABLE;

    return 1;
}

void Platform_Shutdown(void)
{
}

jb_surface *Platform_BackBuffer(void)
{
    return &jb_back;
}

void Platform_SetPresentView(int w, int h)
{
    int i;

    if (w == jb_present_w && h == jb_present_h)
        return;
    jb_present_w = w;
    jb_present_h = h;

    for (i = 0; i < GBA_SCREEN_W * GBA_SCREEN_H; i++)
        GBA_VRAM[i] = 0;
}

JB_IWRAM_CODE void Platform_Present(void)
{
    int out_w  = jb_present_w / 2;
    int out_h  = jb_present_h / 2;
    int off_x  = (GBA_SCREEN_W - out_w) / 2;
    int off_y  = (GBA_SCREEN_H - out_h) / 2;
    int dy;

    /* GBATEK "DISPSTAT": scanlines 160..227 are the vertical blank.  The audio
       backend's VBlank ISR can span that whole window, so a main-thread
       while (REG_VCOUNT < 160) poll may never see VCOUNT in vblank and spins
       forever; wait on the ISR's frame counter instead. */
    {
        unsigned t = jb_vblank_ticks;

        while (jb_vblank_ticks == t) {}
    }

    for (dy = 0; dy < out_h; dy++) {
        const uint16_t    *src = jb_backbuf + (dy * 2) * jb_w;
        volatile uint32_t *dst =
            (volatile uint32_t *)(GBA_VRAM + (dy + off_y) * GBA_SCREEN_W + off_x);
        int                dx;

        /* GBATEK "LCD Color Definitions": the framebuffer is BGR555 (red in
           bits 0-4, blue in bits 10-14) and the game buffer is RGB565, so a
           pixel maps red 11..15 -> 0..4, the top five green bits 6..10 -> 5..9
           (the 6-bit green drops its low bit), blue 0..4 -> 10..14.  GBATEK
           "GBA Memory Map": VRAM takes 16- or 32-bit accesses, so two adjacent
           BGR555 pixels pack into one 32-bit store (low halfword first on the
           little-endian ARM7TDMI), halving the write count. */
        for (dx = 0; dx < out_w / 2; dx++) {
            unsigned p0 = src[dx * 4];
            unsigned p1 = src[dx * 4 + 2];
            unsigned c0 = ((p0 >> 11) & 0x001Fu) |
                          ((p0 >> 1) & 0x03E0u) |
                          ((p0 << 10) & 0x7C00u);
            unsigned c1 = ((p1 >> 11) & 0x001Fu) |
                          ((p1 >> 1) & 0x03E0u) |
                          ((p1 << 10) & 0x7C00u);

            dst[dx] = c0 | (c1 << 16);
        }
    }
}

int Platform_PollEvents(void)
{
    static const unsigned phys[10] = {
        GBA_KEY_A, GBA_KEY_B, GBA_KEY_SELECT, GBA_KEY_START,
        GBA_KEY_RIGHT, GBA_KEY_LEFT, GBA_KEY_UP, GBA_KEY_DOWN,
        GBA_KEY_R, GBA_KEY_L
    };
    unsigned cur = (unsigned)(~REG_KEYINPUT) & GBA_KEY_MASK;
    int      k, i;

    for (k = 0; k < JB_KEY_COUNT; k++)
        jb_keys[k] = (cur & (unsigned)jb_keymap[k]) ? 1 : 0;

    for (i = 0; i < 10; i++) {
        if ((cur & phys[i]) && !(jb_prev_keys & phys[i]))
            PushRawKey((int)phys[i]);
    }
    jb_prev_keys = cur;
    return 1;
}

int Platform_KeyDown(int key)
{
    if (key < 0 || key >= JB_KEY_COUNT)
        return 0;
    return jb_keys[key];
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

unsigned Platform_Ticks(void)
{
    unsigned hi, lo, hi2;
    uint64_t raw;

    do {
        hi  = REG_TM3CNT_L;
        lo  = REG_TM2CNT_L;
        hi2 = REG_TM3CNT_L;
    } while (hi != hi2);

    raw = ((uint64_t)hi << 16) | lo;
    return (unsigned)(raw * 1000u / GBA_TIMER_HZ);
}

void Platform_Delay(unsigned ms)
{
    unsigned start = Platform_Ticks();

    while (Platform_Ticks() - start < ms) {}
}

const char *Platform_BasePath(void)
{
    return "";
}

const char *Platform_BaseOrigin(void)
{
    return "embedded assets";
}

const char *Platform_PrefPath(void)
{
    return "";
}

int Platform_FileExists(const char *path)
{
#ifdef JB_EMBED
    long n;

    if (Embed_Find(path, &n))
        return 1;
#endif
    (void)path;
    return 0;
}

unsigned char *Platform_ReadFile(const char *path, long *out_len)
{
#ifdef JB_EMBED
    unsigned char *e = Embed_Read(path, out_len);

    if (e)
        return e;
#endif
    (void)path;
    (void)out_len;
    return NULL;
}

int Platform_TouchActive(void)
{
    return 0;
}

void Platform_ShowError(const char *title, const char *text)
{
    (void)title;
    (void)text;
}

const char *Platform_LastError(void)
{
    return "";
}




