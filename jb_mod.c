#include <stdlib.h>
#include <string.h>

#include "jb_mod.h"
#include "jb_mod_priv.h"

#ifdef JB_BACKEND_GBA
/* IWRAM (32 KB) cannot hold the ~26 KB mixer state alongside .bss/.data/stack;
   put it in EWRAM. Mod_Init initializes it, and the gba crt0 zero-fills .sbss. */
mod_state jb_M __attribute__((section(".sbss")));
#else
mod_state jb_M;
#endif

static int BeU16(const unsigned char *p)
{
    return (p[0] << 8) | p[1];
}

int Mod_Period(int idx)
{
    if (idx < 0) idx = 0;
    if (idx >= MOD_NOTES_ALL) idx = MOD_NOTES_ALL - 1;
    return (int)jb_mod_period[idx / JB_MOD_NOTES][idx % JB_MOD_NOTES];
}

unsigned int Mod_Step(int period)
{
    if (period < 0) period = 0;
    if (period > MOD_STEPMAX) period = MOD_STEPMAX;
    return jb_M.step[period];
}

unsigned int Mod_Cell(int ch)
{
    const unsigned char *p =
        jb_M.pat + (jb_M.cellofs + jb_M.patno * jb_M.chans * 0x40 + ch) * 4;

    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* jumpyball hss.dll hssSpeaker::findBestNoteIndex 0x00103604 */
static int FindBestNote(int period)
{
    int i, best = 0, bestd = -1;

    for (i = 0; i < JB_MOD_NOTES; i++) {
        int v = (int)jb_mod_period[0][i];
        int d = v < period ? period - v : v - period;

        if (v == period) return i;
        if (bestd < 0 || d < bestd) {
            bestd = d;
            best  = i;
        }
    }
    return best;
}

/* jumpyball hss.dll hssMusic::translatePTSign 0x00102648 */
static int TranslateSign(const unsigned char *p)
{
    int n = 0;

    if ((p[0] | 0x20) == 'm' && (p[2] | 0x20) == 'k') return 4;
    if ((p[1] | 0x20) != 'c' && (p[2] | 0x20) != 'c') return 0;
    if (p[0] >= '0' && p[0] <= '9') n = p[0] - '0';
    if (p[1] >= '0' && p[1] <= '9') n = n * 10 + (p[1] - '0');
    return n;
}

/* jumpyball hss.dll hssSpeaker::createTables 0x00103664 */
void Mod_Init(int out_rate)
{
    int p;

    jb_M.outrate   = out_rate;
    jb_M.mastervol = 0x40;
    for (p = 0; p < 0x1000; p++) jb_M.note[p] = (short)FindBestNote(p);
    jb_M.step[0] = 0;
    for (p = 1; p <= MOD_STEPMAX; p++)
        jb_M.step[p] =
            (unsigned int)((double)(MOD_CLOCK / p) * 65536.0 / (double)out_rate);
}

/* jumpyball hss.dll hssSpeaker::updateModSFX 0x00104370 */
void Mod_SetTempo(void)
{
    int d = ((MOD_BASEFREQ << 8) / 0xac44) * jb_M.tickhz;

    if (d <= 0) d = 1;
    jb_M.spt = (int)(((unsigned int)jb_M.outrate << 8) / (unsigned int)d);
}

/* jumpyball hss.dll hssSpeaker::volumeMusics 0x0010567c */
void Mod_MasterVolume(int volume)
{
    if (volume > 0x40) volume = 0x40;
    if (volume < 0) volume = 0;
    jb_M.mastervol = volume;
    jb_M.volbase   = (jb_M.mastervol * jb_M.trackvol) >> 6;
}

/* jumpyball hss.dll hssSpeaker::stopMusics 0x001042f0 */
void Mod_Stop(void)
{
    jb_M.playing = 0;
    free(jb_M.file);
    jb_M.file = 0;
    jb_M.pat  = 0;
}

/* jumpyball hss.dll hssSpeaker::updateModSFX 0x00104370 */
static int MixFrame(void)
{
    int acc = 0, ch;

    for (ch = 0; ch < jb_M.chans; ch++) {
        mod_track  *t = &jb_M.tr[ch];
        mod_sample *s = &jb_M.smp[t->sample];

        if (s->data == 0) continue;
        if (s->replen != 0 && s->repstart + s->replen <= t->pos)
            t->pos -= s->replen;
        if (t->pos < s->len) {
            acc    += (int)s->data[t->pos >> 16] * t->mixvol;
            t->pos += t->step;
        }
    }
    return acc;
}

/* jumpyball hss.dll hssSpeaker::updateModSFX 0x00104370 */
static void Tick(void)
{
    if (jb_M.tick == jb_M.speed) {
        if (jb_M.patdelay == 0) {
            Mod_ProcessRow();
            Mod_AdvanceRow();
            jb_M.tick = 0;
        } else {
            jb_M.tick = 0;
            jb_M.patdelay--;
        }
    } else if (jb_M.sctr < jb_M.spt) {
        jb_M.sctr++;
    } else {
        jb_M.sctr = 0;
        jb_M.tick++;
        if (jb_M.tick != jb_M.speed) Mod_TickEffects();
    }
}

/* jumpyball hss.dll hssSpeaker::playMusic 0x00103f1c */
static void LoadSamples(unsigned char *data, int size, unsigned char *sampbase)
{
    unsigned int running = 0;
    int i;

    for (i = 1; i < 32; i++) {
        const unsigned char *h = data + 30 * i + 12;
        mod_sample          *s = &jb_M.smp[i];
        long avail = (long)(data + size - sampbase) - (long)running;
        unsigned int bytes = (unsigned int)BeU16(h) * 2;

        if (avail < 0) avail = 0;
        if (bytes > (unsigned int)avail) bytes = (unsigned int)avail;
        s->data     = (const signed char *)(sampbase + running);
        s->len      = bytes << 16;
        s->finetune = h[2];
        s->vol      = h[3];
        s->repstart = (unsigned int)BeU16(h + 4) << 17;
        s->replen   = BeU16(h + 6) < 2 ? 0 : (unsigned int)BeU16(h + 6) << 17;
        running    += bytes;
    }
}

/* jumpyball hss.dll hssSpeaker::playMusic 0x00103f1c */
/* jumpyball hss.dll hssMusic_setVolume 0x00106228 */
int Mod_Play(unsigned char *data, int size, int volume, int loop)
{
    int chans, songlen, numpat, i;
    unsigned char *sampbase;

    Mod_Stop();
    if (data == 0) return 0;
    if (size < 0x43c) { free(data); return 0; }
    chans = TranslateSign(data + 0x438);
    if (chans < 1 || chans > 0x20) { free(data); return 0; }
    songlen = data[0x3b6];
    if (songlen < 1 || songlen > 0x80) { free(data); return 0; }
    numpat = 0;
    for (i = 0; i < songlen; i++)
        if (data[MOD_ORDER_OFS + i] > numpat) numpat = data[MOD_ORDER_OFS + i];
    numpat++;
    sampbase = data + 0x43c + numpat * chans * 0x100;
    if (sampbase > data + size) { free(data); return 0; }

    memset(jb_M.tr, 0, sizeof jb_M.tr);
    memset(jb_M.smp, 0, sizeof jb_M.smp);
    memset(jb_M.loopcnt, 0, sizeof jb_M.loopcnt);
    memset(jb_M.loopstart, 0, sizeof jb_M.loopstart);
    LoadSamples(data, size, sampbase);

    jb_M.file      = data;
    jb_M.pat       = data + 0x43c;
    jb_M.chans     = chans;
    jb_M.songlen   = songlen;
    jb_M.restart   = data[0x3b7];
    if (jb_M.restart == 0x7f || jb_M.restart >= songlen) jb_M.restart = 0;
    jb_M.orderidx  = 0;
    jb_M.cellofs   = 0;
    jb_M.patno     = data[MOD_ORDER_OFS];
    jb_M.speed     = 6;
    jb_M.tick      = 6;
    jb_M.tickhz    = 0x32;
    jb_M.sctr      = 0;
    jb_M.jumporder = MOD_NOJUMP;
    jb_M.jumpcell  = MOD_NOJUMP;
    jb_M.patdelay  = 0;
    jb_M.loop      = loop;
    jb_M.trackvol  = volume > 0x40 ? 0x40 : (volume < 0 ? 0 : volume);
    jb_M.volbase   = (jb_M.mastervol * jb_M.trackvol) >> 6;
    Mod_SetTempo();
    jb_M.playing   = 1;
    return 1;
}

/* jumpyball hss.dll hssSpeaker::updateModSFX 0x00104370 */
void Mod_Render(short *out, int frames)
{
    int i;

    for (i = 0; i < frames; i++) {
        int v;

        if (!jb_M.playing) { out[i] = 0; continue; }
        Tick();
        v = MixFrame();
        if (v < -0x8000) v = -0x8000;
        if (v > 0x7fff)  v = 0x7fff;
        out[i] = (short)v;
    }
}
