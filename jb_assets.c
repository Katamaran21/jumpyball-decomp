#include "jb_assets.h"

#include "jb_consts.h"
#include "jb_platform.h"

#ifdef JB_EMBED
#include "jb_embed.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JB_PATH_MAX 512

static char jb_root[JB_PATH_MAX];
static char jb_path[JB_PATH_MAX];
static char jb_fail[4 * JB_PATH_MAX];

static void NoteFailure(const char *path)
{
    size_t n = strlen(jb_fail);

    if (n + strlen(path) + 4 < sizeof jb_fail)
        sprintf(jb_fail + n, "  %s\n", path);
}

static int TryRoot(const char *base, const char *sub)
{
    char probe[JB_PATH_MAX];
    size_t n;

    if (base == NULL)
        return 0;

    snprintf(jb_root, sizeof jb_root, "%s%s", base, sub);
    n = strlen(jb_root);
    if (n > 0 && jb_root[n - 1] != '\\' && jb_root[n - 1] != '/' &&
        n + 1 < sizeof jb_root) {
        jb_root[n]     = '/';
        jb_root[n + 1] = '\0';
    }

    snprintf(probe, sizeof probe, "%sBITMAP/%d.bmp", jb_root, JB_RES_FONT);
    if (Platform_FileExists(probe))
        return 1;

    NoteFailure(probe);
    jb_root[0] = '\0';
    return 0;
}

int Assets_Init(void)
{
    /* Windows CE has no process environment and coredll has no getenv. */
#ifdef JB_WINCE
    const char *env  = NULL;
#else
    const char *env  = getenv("JUMPYBALL_ASSETS");
#endif
    const char *base = Platform_BasePath();

    jb_fail[0] = '\0';
    jb_root[0] = '\0';

#ifdef JB_EMBED
    if (Embed_Count() > 0)
        return 1;
#endif

    if (TryRoot(env, ""))
        return 1;
    if (TryRoot(base, ""))
        return 1;
    if (TryRoot(base, "assets/"))
        return 1;

    /* Windows CE has no current directory, so if the executable's own directory
       did not answer there is nothing relative left to try - only the absolute
       places a game gets copied to.  Platform_BasePath searches these too, but
       it is reached through GetModuleFileName; this covers the case where that
       returned a path which exists yet holds no assets. */
#ifdef JB_WINCE
    {
        static const char *const dirs[] = {
            "\\Storage Card\\JumpyBall\\",
            "\\Storage Card\\JumpyBallBut\\",
            "\\SD Card\\JumpyBall\\",
            "\\Program Files\\JumpyBall\\",
            "\\My Documents\\JumpyBall\\",
            "\\Windows\\JumpyBall\\",
            "\\JumpyBall\\",
            "\\Storage Card\\"
        };
        size_t i;

        for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
            if (TryRoot(dirs[i], ""))
                return 1;
        }
    }
#endif
    return 0;
}

const char *Assets_Root(void)
{
    return jb_root;
}

const char *Assets_FailureText(void)
{
    return jb_fail;
}

const char *Assets_Bitmap(int res)
{
    snprintf(jb_path, sizeof jb_path, "%sBITMAP/%d.bmp", jb_root, res);
    return jb_path;
}

const char *Assets_Sound(const char *name)
{
    snprintf(jb_path, sizeof jb_path, "%sSounds/%s", jb_root, name);
    return jb_path;
}

/* jumpyball JumpyBall.exe Music_LoadForContext 0x0001db68 */
const char *Assets_Music(const char *name)
{
    snprintf(jb_path, sizeof jb_path, "%sMusics/%s", jb_root, name);
    return jb_path;
}
