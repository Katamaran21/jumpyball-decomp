/* Host layer for the port.  It stands in for JumpyBall.exe's GAPI path
   (Gfx_CreateBackBuffer 0x00021698, Gfx_Present 0x000126fc). */
#ifndef JB_PLATFORM_H
#define JB_PLATFORM_H

#include "jb_gfx.h"

/* Windows CE, whether the compiler is cegcc/mingw32ce (UNDER_CE) or a
   Microsoft embedded SDK (_WIN32_WCE).  It gates the places where CE is not a
   subset of Win32: no process environment, no GdiFlush, a full-screen window
   instead of a sized one. */
#if defined(_WIN32_WCE) || defined(UNDER_CE)
#define JB_WINCE 1
#endif

enum {
    JB_KEY_LEFT,
    JB_KEY_RIGHT,
    JB_KEY_UP,
    JB_KEY_DOWN,
    JB_KEY_JUMP,
    JB_KEY_MENU,
    JB_KEY_COUNT
};

int Platform_Init(int w, int h, int scale, const char *title);
void Platform_Shutdown(void);

/* The 16bpp surface every blitter in jb_gfx.h draws into. */
jb_surface *Platform_BackBuffer(void);

void Platform_Present(void);

#ifdef JB_BACKEND_GBA
/* Selects the sub-rectangle of the back buffer the GBA present samples this
   frame: the 240x320 buffer is shared, the menu shows all of it while the game
   shows its JumpyBall.exe Game_Init 0x000113bc 176x220 layout, letterboxed. */
void Platform_SetPresentView(int w, int h, int scale);
#endif

/* Returns 0 once the window has been closed. */
int Platform_PollEvents(void);

int Platform_KeyDown(int key);

/* jumpyball JumpyBall.exe WndProc 0x0001fd2c compares the incoming VK code
   against g_keyLeft, g_keyRight, g_keyUp, g_keyDown, g_keyJump and g_keyMenu;
   the key-config wizard in the same function writes those six slots. */
#define JB_KEY_UNBOUND 0

int Platform_KeyBinding(int key);
void Platform_SetKeyBinding(int key, int code);

/* jumpyball JumpyBall.exe WndProc 0x0001fd2c binds on WM_KEYDOWN. */
int Platform_NextRawKey(void);
void Platform_FlushRawKeys(void);

unsigned Platform_Ticks(void);

void Platform_Delay(unsigned ms);

/* JumpyBall.exe Game_Init 0x000113bc calls hssSpeaker::volumeSounds with 0x20
   and hssSound::volume with 0x20 and 0x40. */
#define JB_VOLUME_MAX 128
#define JB_SOUND_SLOTS 4
#define JB_MASTER_VOLUME_MAX 0x20
#define JB_MUSIC_VOLUME_MAX 0x40

int Platform_AudioInit(void);
void Platform_AudioPause(int pause);
int Platform_SoundLoad(int slot, const char *path, int volume);
void Platform_SoundPlay(int slot);
void Platform_SoundMasterVolume(int volume);
int Platform_MusicPlay(const char *path, int volume, int loop);
void Platform_MusicStop(void);
void Platform_MusicMasterVolume(int volume);
void Platform_AudioShutdown(void);

/* Directory the running executable lives in, with a trailing separator. */
const char *Platform_BasePath(void);

/* How Platform_BasePath chose that directory, for the assets-missing dialog.
   On a cut-down Windows CE image GetModuleFileName can fail, and then every
   probe path looks alike whichever step produced it.  Only the native backend
   has more than one way to answer, so the SDL2 one reports a fixed string. */
const char *Platform_BaseOrigin(void);

const char *Platform_PrefPath(void);

int Platform_FileExists(const char *path);

unsigned char *Platform_ReadFile(const char *path, long *out_len);

int Platform_TouchActive(void);

void Platform_ShowError(const char *title, const char *text);

const char *Platform_LastError(void);

#endif /* JB_PLATFORM_H */
