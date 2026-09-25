/* Game Boy Advance DirectSound backend, the counterpart of jb_audio_win32.c.
   GBATEK (Martin Korth) "GBA Sound Control Registers" and "DMA Transfers":
   DMA1 feeds FIFO A from a buffer, Timer0 clocks one signed 8-bit sample per
   overflow, and a VBlank interrupt swaps a double buffer and refills it with
   Mod_Render mixed against the running sound voices, as jb_audio_win32.c's
   FillBuffer does, here down-converted to the 8-bit PCM the FIFO plays. */
#include "jb_platform.h"
#include "jb_mod.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* GBATEK "GBA I/O Map": sound, DMA1, Timer0 and interrupt registers, plus the
   BIOS interrupt-flag word and user IRQ vector in IWRAM. */
#define REG_SOUNDCNT_H (*(volatile uint16_t *)0x04000082u)
#define REG_SOUNDCNT_X (*(volatile uint16_t *)0x04000084u)
#define REG_DMA1SAD    (*(volatile uint32_t *)0x040000BCu)
#define REG_DMA1DAD    (*(volatile uint32_t *)0x040000C0u)
#define REG_DMA1CNT_H  (*(volatile uint16_t *)0x040000C6u)
#define REG_TM0CNT_L   (*(volatile uint16_t *)0x04000100u)
#define REG_TM0CNT_H   (*(volatile uint16_t *)0x04000102u)
#define REG_DISPSTAT   (*(volatile uint16_t *)0x04000004u)
#define REG_IE         (*(volatile uint16_t *)0x04000200u)
#define REG_IF         (*(volatile uint16_t *)0x04000202u)
#define REG_IME        (*(volatile uint16_t *)0x04000208u)
#define REG_IFBIOS     (*(volatile uint16_t *)0x03007FF8u)
#define REG_ISR_MAIN   (*(volatile uint32_t *)0x03007FFCu)
#define GBA_FIFO_A     0x040000A0u

/* GBATEK "LCD Dimensions and Timings": 228 scanlines x 1232 cycles = 280896
   cycles a frame; a Timer0 period of 924 (reload 65536-924 = 0xFC64) overflows
   exactly 304 times a frame, so one 304-sample buffer plays per frame at
   16777216/924 = 18157 Hz. */
#define JB_GBA_RATE   18157
#define JB_FRAMES     304
#define JB_TM0_RELOAD 0xFC64u

/* GBATEK "Sound Control Registers": DirectSound A 100%, both speakers, clocked
   by Timer0; master enable is SOUNDCNT_X bit 7. */
#define JB_SNDCNT_H 0x0306u
#define JB_SNDCNT_X 0x0080u

/* GBATEK "DMA Transfers": enable | start special (FIFO) | 32-bit | repeat |
   dest fixed at FIFO_A. */
#define JB_DMA1_FIFO 0xB640u

#define JB_VOICES  4
#define JB_WAV_PCM 1

typedef struct {
    short   *data;
    unsigned frames;
    int      volume;
} jb_sound;

typedef struct {
    int      slot;
    unsigned pos;
} jb_voice;

static jb_sound jb_sounds[JB_SOUND_SLOTS];
static jb_voice jb_voices[JB_VOICES];

static int8_t jb_dma_buf[2][JB_FRAMES] __attribute__((aligned(4)));
static short  jb_mix_buf[JB_FRAMES];
static int    jb_play_idx;
static int    jb_audio_on;

static int jb_master_vol = JB_MASTER_VOLUME_MAX;

void Platform_SoundMasterVolume(int volume)
{
    jb_master_vol = volume;
}

static unsigned Lock(void)
{
    unsigned ime = REG_IME;

    REG_IME = 0;
    return ime;
}

static void Unlock(unsigned ime)
{
    REG_IME = (uint16_t)ime;
}

static void MixVoice(short *out, const short *src, unsigned n, int vol)
{
    unsigned i;

    for (i = 0; i < n; i++) {
        int v = out[i] + (int)src[i] * vol / JB_VOLUME_MAX;

        if (v > 32767)
            v = 32767;
        else if (v < -32768)
            v = -32768;
        out[i] = (short)v;
    }
}

static void FillBuffer(int8_t *dst)
{
    int i;

    Mod_Render(jb_mix_buf, JB_FRAMES);
    for (i = 0; i < JB_VOICES; i++) {
        const jb_sound *snd;
        unsigned        left, n;

        if (jb_voices[i].slot < 0)
            continue;
        snd  = &jb_sounds[jb_voices[i].slot];
        left = snd->frames - jb_voices[i].pos;
        n    = (left < (unsigned)JB_FRAMES) ? left : (unsigned)JB_FRAMES;
        MixVoice(jb_mix_buf, snd->data + jb_voices[i].pos, n,
                 snd->volume * jb_master_vol / JB_MASTER_VOLUME_MAX);
        jb_voices[i].pos += n;
        if (jb_voices[i].pos >= snd->frames)
            jb_voices[i].slot = -1;
    }
    for (i = 0; i < JB_FRAMES; i++)
        dst[i] = (int8_t)(jb_mix_buf[i] >> 8);
}

/* GBATEK "DMA Transfers": FIFO DMA ignores the word count and its source does
   not wrap, so re-point DMA1 at the next buffer each frame; the FIFO samples
   still queued cover the reprogramming gap. */
static void StartDma(const int8_t *buf)
{
    REG_DMA1CNT_H = 0;
    REG_DMA1SAD   = (uint32_t)buf;
    REG_DMA1CNT_H = JB_DMA1_FIFO;
}

static void JbAudioISR(void)
{
    unsigned short flags = REG_IF;

    if (flags & 0x0001u) {
        jb_play_idx ^= 1;
        StartDma(jb_dma_buf[jb_play_idx]);
        FillBuffer(jb_dma_buf[jb_play_idx ^ 1]);
    }
    REG_IF     = flags;
    REG_IFBIOS = (uint16_t)(REG_IFBIOS | flags);
}

int Platform_AudioInit(void)
{
    int i;

    for (i = 0; i < JB_VOICES; i++)
        jb_voices[i].slot = -1;

    Mod_Init(JB_GBA_RATE);

    memset(jb_dma_buf, 0, sizeof jb_dma_buf);
    FillBuffer(jb_dma_buf[0]);
    FillBuffer(jb_dma_buf[1]);
    jb_play_idx = 0;

    REG_SOUNDCNT_X = JB_SNDCNT_X;
    REG_SOUNDCNT_H = JB_SNDCNT_H;

    REG_DMA1DAD = GBA_FIFO_A;
    StartDma(jb_dma_buf[0]);

    REG_TM0CNT_L = JB_TM0_RELOAD;
    REG_TM0CNT_H = 0x0080u;

    REG_ISR_MAIN  = (uint32_t)(uintptr_t)JbAudioISR;
    REG_DISPSTAT |= 0x0008u;
    REG_IE       |= 0x0001u;
    REG_IME       = 1;

    jb_audio_on = 1;
    return 1;
}

void Platform_AudioPause(int pause)
{
    if (!jb_audio_on)
        return;
    REG_TM0CNT_H = pause ? 0 : 0x0080u;
}

static unsigned Rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned Rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int LoadWav(const char *path, short **out, unsigned *out_frames)
{
    unsigned char       *f;
    long                 len = 0;
    unsigned             ofs, chunk, size, rate = 0, chans = 0, bits = 0, fmt = 0;
    unsigned             in_frames, i;
    const unsigned char *data = NULL;
    unsigned             data_size = 0;
    short               *pcm;

    f = Platform_ReadFile(path, &len);
    if (f == NULL)
        return 0;
    if (len < 44 || memcmp(f, "RIFF", 4) != 0 || memcmp(f + 8, "WAVE", 4) != 0) {
        free(f);
        return 0;
    }
    ofs = 12;
    while (ofs + 8 <= (unsigned)len) {
        chunk = Rd32(f + ofs + 4);
        if (chunk > (unsigned)len - ofs - 8)
            break;
        if (memcmp(f + ofs, "fmt ", 4) == 0 && chunk >= 16) {
            fmt   = Rd16(f + ofs + 8);
            chans = Rd16(f + ofs + 10);
            rate  = Rd32(f + ofs + 12);
            bits  = Rd16(f + ofs + 22);
        } else if (memcmp(f + ofs, "data", 4) == 0) {
            data      = f + ofs + 8;
            data_size = chunk;
        }
        ofs += 8 + chunk + (chunk & 1u);
    }
    if (fmt != JB_WAV_PCM || data == NULL || rate == 0 ||
        (chans != 1 && chans != 2) || (bits != 8 && bits != 16)) {
        free(f);
        return 0;
    }

    size      = chans * bits / 8;
    in_frames = data_size / size;
    if (in_frames == 0) {
        free(f);
        return 0;
    }
    *out_frames = (unsigned)(((double)in_frames * JB_GBA_RATE) / (double)rate);
    if (*out_frames == 0)
        *out_frames = 1;
    pcm = (short *)malloc((size_t)*out_frames * sizeof(short));
    if (pcm == NULL) {
        free(f);
        return 0;
    }
    for (i = 0; i < *out_frames; i++) {
        unsigned             src = (unsigned)(((double)i * rate) / JB_GBA_RATE);
        const unsigned char *p;
        int                  acc = 0, c;

        if (src >= in_frames)
            src = in_frames - 1;
        p = data + (size_t)src * size;
        for (c = 0; c < (int)chans; c++) {
            if (bits == 8)
                acc += ((int)p[c] - 128) * 256;
            else
                acc += (int)(short)(unsigned short)Rd16(p + c * 2);
        }
        pcm[i] = (short)(acc / (int)chans);
    }
    free(f);
    *out = pcm;
    return 1;
}

int Platform_SoundLoad(int slot, const char *path, int volume)
{
    short   *pcm    = NULL;
    unsigned frames = 0;
    unsigned ime;

    if (slot < 0 || slot >= JB_SOUND_SLOTS)
        return 0;
    if (!LoadWav(path, &pcm, &frames))
        return 0;
    ime = Lock();
    free(jb_sounds[slot].data);
    jb_sounds[slot].data   = pcm;
    jb_sounds[slot].frames = frames;
    jb_sounds[slot].volume = volume;
    Unlock(ime);
    return 1;
}

void Platform_SoundPlay(int slot)
{
    unsigned ime;
    int      i;

    if (slot < 0 || slot >= JB_SOUND_SLOTS || jb_sounds[slot].data == NULL)
        return;
    ime = Lock();
    for (i = 0; i < JB_VOICES; i++) {
        if (jb_voices[i].slot < 0) {
            jb_voices[i].slot = slot;
            jb_voices[i].pos  = 0;
            break;
        }
    }
    Unlock(ime);
}

int Platform_MusicPlay(const char *path, int volume, int loop)
{
    unsigned char *buf;
    long           size = 0;
    int            ok;
    unsigned       ime;

    buf = Platform_ReadFile(path, &size);
    if (buf == NULL)
        return 0;
    if (size > 0x400000) {
        free(buf);
        return 0;
    }
    ime = Lock();
    ok  = Mod_Play(buf, (int)size, volume, loop);
    Unlock(ime);
    return ok;
}

void Platform_MusicStop(void)
{
    unsigned ime = Lock();

    Mod_Stop();
    Unlock(ime);
}

void Platform_MusicMasterVolume(int volume)
{
    unsigned ime = Lock();

    Mod_MasterVolume(volume);
    Unlock(ime);
}

void Platform_AudioShutdown(void)
{
    if (!jb_audio_on)
        return;
    REG_IME        = 0;
    REG_TM0CNT_H   = 0;
    REG_DMA1CNT_H  = 0;
    REG_SOUNDCNT_X = 0;
    REG_IE        &= (uint16_t)~0x0001u;
    Mod_Stop();
    jb_audio_on = 0;
}




