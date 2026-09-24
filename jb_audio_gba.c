/* Silent audio backend for the Game Boy Advance port.  Platform_AudioInit
   returns 0, so Audio_Init (jb_audio.c) early-returns and loads no sounds; the
   rest are safe no-ops the play/volume paths still call unconditionally.  Real
   GBA DirectSound (DMA-fed FIFO clocked by a timer) is a later milestone. */
#include "jb_platform.h"

int Platform_AudioInit(void)
{
    return 0;
}

void Platform_AudioPause(int pause)
{
    (void)pause;
}

int Platform_SoundLoad(int slot, const char *path, int volume)
{
    (void)slot;
    (void)path;
    (void)volume;
    return 0;
}

void Platform_SoundPlay(int slot)
{
    (void)slot;
}

void Platform_SoundMasterVolume(int volume)
{
    (void)volume;
}

int Platform_MusicPlay(const char *path, int volume, int loop)
{
    (void)path;
    (void)volume;
    (void)loop;
    return 0;
}

void Platform_MusicStop(void)
{
}

void Platform_MusicMasterVolume(int volume)
{
    (void)volume;
}

void Platform_AudioShutdown(void)
{
}
