#include "jb_assets.h"
#include "jb_appassets.h"
#include "jb_audio.h"
#include "jb_ball.h"
#include "jb_consts.h"
#include "jb_keyconfig.h"
#include "jb_level.h"
#include "jb_menu.h"
#include "jb_platform.h"
#include "jb_player.h"
#include "jb_stage.h"
#include "jb_text.h"
#include "jb_track.h"
#include "jb_trackrow.h"

#if !defined(JB_BACKEND_WIN32) && !defined(JB_BACKEND_GBA)
/* SDL2 renames main to SDL_main on Windows and Android.  The native Win32 and
   GBA backends link no SDL at all and need no shim. */
#include <SDL_main.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JB_MSG_MAX 2048

/* JumpyBall.exe Game_Init 0x000113bc selects the 176x220 layout on its
   g_screenW == 0xb0 branch; on the 240x160 GBA panel the game composites in
   that smaller native layout (about half the per-frame pixels), while the menu
   keeps the 240x320 layout the shared back buffer is sized for. */
#ifdef JB_BACKEND_GBA
#define JB_GAME_VIEW_W        JB_SMALL_VIEW_W
#define JB_GAME_VIEW_H        JB_SMALL_VIEW_H
#define JB_GAME_VIEW_CENTER_X JB_SMALL_VIEW_CENTER_X
#define JB_GAME_TILE_SIZE     JB_SMALL_TILE_SIZE
#define JB_GAME_LAYOUT_MODE   JB_LAYOUT_176x220
#else
#define JB_GAME_VIEW_W        JB_VIEW_W
#define JB_GAME_VIEW_H        JB_VIEW_H
#define JB_GAME_VIEW_CENTER_X JB_VIEW_CENTER_X
#define JB_GAME_TILE_SIZE     JB_TILE_SIZE
#define JB_GAME_LAYOUT_MODE   JB_LAYOUT_240x320
#endif

/* The GBA JB_MENU_HALF build composites the menu at 120x160 - the 240x320
   menu view scaled by exactly 1/2 - so Platform_Present ships it 1:1 with no
   downsample; every other build keeps the 240x320 view and the /2 present. */
#ifdef JB_MENU_HALF
#define JB_MENU_VIEW_W        120
#define JB_MENU_VIEW_H        160
#define JB_MENU_VIEW_CENTER_X 60
#define JB_MENU_PRESENT_SCALE 1
#else
#define JB_MENU_VIEW_W        JB_VIEW_W
#define JB_MENU_VIEW_H        JB_VIEW_H
#define JB_MENU_VIEW_CENTER_X JB_VIEW_CENTER_X
#define JB_MENU_PRESENT_SCALE 2
#endif

/* JumpyBall.exe Player_Respawn 0x00012f18 stores 0x32 zero words at g_rowShift
   0x00061930, and Level_Begin 0x0001376c refills it from g_rowShiftSrc
   0x0002f6d8, whose generator writes 0 to every entry while g_altTrackMode
   0x00064940 is 0. */
static const float jb_row_shift[JB_ROW_SHIFT_N] = { 0.0f };

static jb_trackrow_ctx jb_ctx;
static jb_track_state  jb_st;
static jb_ball_state   jb_ball_st;
static jb_player_state jb_pl;
static jb_stage        jb_stg;
static jb_menu         jb_m;
static jb_keyconfig    jb_kc;
static int             jb_key_prev[JB_KEY_COUNT];

/* The per-frame body runs from a callback so a browser can drive it through
   requestAnimationFrame (emscripten_set_main_loop); the state the loop carried
   as main() locals lives here so the callback keeps it between frames. */
static jb_surface     *jb_back;
static unsigned        jb_prev_ticks;
static const char     *jb_dump_path;
static int             jb_should_quit;

/* JumpyBall.exe Game_Init 0x000113bc leaves g_appMode 0x00064a24 at 1 through
   the first Screen_Set 0x00013678. */
static int jb_mode = JB_MODE_MENU;

/* JumpyBall.exe Track_DrawFrame 0x0001dd54 at 0x0001e9bc draws the HUD while
   g_layoutMode 0x00064938 is 0, with the ":" literal at 0x000269a4. */
static void Timer_DrawHud(const jb_surface *dst, int cam_row, int r, int g, int b)
{
    Text_DrawNumber(dst, 5, 5, cam_row, 0, 3, r, g, b);
    Text_DrawNumber(dst, JB_VIEW_W - 0x39, 5, jb_stg.time_min, 2, 2, r, g, b);
    Text_DrawCentered(dst, JB_VIEW_W - 0x34, 5, ":", 1, r, g, b);
    Text_DrawNumber(dst, JB_VIEW_W - 0x2f, 5, jb_stg.time_sec, 0, 2, r, g, b);
}

/* JumpyBall.exe Track_DrawFrame 0x0001dd54 at 0x0001e858 selects the row
   drawer from g_theme 0x00064944: 0 TrackRow_DrawIce 0x00016e98,
   1 TrackRow_DrawDesert 0x00015b80, 2 TrackRow_DrawForest 0x00017ad4,
   3 TrackRow_DrawSky 0x000167bc, 4 TrackRow_DrawGrass 0x00019198. */
static void DrawRow(const jb_track_row *row, void *user)
{
    const jb_trackrow_ctx *ctx = (const jb_trackrow_ctx *)user;

    switch (jb_stg.theme) {
    case JB_THEME_DESERT:
        TrackRow_DrawDesert(ctx, row);
        break;
    case JB_THEME_FOREST:
        TrackRow_DrawForest(ctx, row);
        break;
    case JB_THEME_SKY:
        TrackRow_DrawSky(ctx, row);
        break;
    case JB_THEME_GRASS:
        TrackRow_DrawGrass(ctx, row);
        break;
    default:
        TrackRow_DrawIce(ctx, row);
        break;
    }
}

/* JumpyBall.exe Level_Begin 0x0001376c fills g_backdrop 0x00061b28 from the
   theme bitmap, and the menu confirm at 0x00021b04 calls Player_Respawn
   0x00012f18 straight after it. */
static void BeginLevel(int level, unsigned ticks)
{
    /* JumpyBall.exe Level_Begin 0x0001376c calls Music_LoadForContext 0x0001db68
       before it clears g_timeSec. */
    Audio_MusicPlay(JB_MUS_GAME);
    Stage_Begin(&jb_stg, level, ticks);
    /* JumpyBall.exe Level_Begin 0x0001376c stores 0 to g_camRow 0x000649b8 ahead
       of its Level_LoadTileMap 0x0001261c call. */
    jb_pl.cam_row = 0;
    Level_LoadTileMap(jb_ctx.screen, jb_stg.level, &jb_a.map1, &jb_a.map2,
                      &jb_a.map3);
    Player_Respawn(&jb_pl);
    /* JumpyBall.exe Player_Respawn 0x00012f18 calls Font_Load 0x0001f4d8 with 2. */
    Font_Select(jb_ctx.screen, 2);
    jb_st.ball_prev_x  = 0.0f;
    jb_ball_st.hud_r   = jb_stg.hud_r;
    jb_ball_st.hud_g   = jb_stg.hud_g;
    jb_ball_st.hud_b   = jb_stg.hud_b;
}

/* JumpyBall.exe WndProc 0x0001fd2c dispatches the key on g_appMode 0x00064a24,
   and the g_appMode != 1 branch answers g_keyMenu with
   Screen_Set(g_screen, g_menuIndex). */
static int PumpKeys(void)
{
    int k, held;
    int act = JB_MENU_NONE;

    for (k = 0; k < JB_KEY_COUNT; k++) {
        held = Platform_KeyDown(k) ? 1 : 0;
        if (held == jb_key_prev[k])
            continue;
        jb_key_prev[k] = held;

        if (jb_mode == JB_MODE_MENU) {
            if (held) {
                int r = Menu_KeyDown(&jb_m, k);

                if (r != JB_MENU_NONE)
                    act = r;
            }
            continue;
        }
        if (held && k == JB_KEY_MENU) {
            Menu_ScreenSet(&jb_m, jb_m.screen_id, jb_m.index);
            /* JumpyBall.exe Screen_Set 0x00013678 takes g_appMode == 0 as its
               Music_LoadForContext 0x0001db68 condition before it stores 1. */
            Audio_MusicPlay(JB_MUS_MENU);
            jb_mode = JB_MODE_MENU;
            continue;
        }
        if (held)
            Player_KeyDown(&jb_pl, k);
        else
            Player_KeyUp(&jb_pl, k);
    }
    return act;
}

/* jumpyball JumpyBall.exe WndProc 0x0001fd2c runs one iteration of the message
   pump, the g_dtMs update at 0x00020920 and Game_DrawFrame per turn; this is
   that turn, split out so a rAF-driven host can call it. */
static void Frame(void)
{
    unsigned ticks;
    float    dt;
    int      act;
    int      i;

    if (!Platform_PollEvents()) {
        jb_should_quit = 1;
        return;
    }

    ticks = Platform_Ticks();

    /* JumpyBall.exe WndProc 0x00020920 takes g_dtMs 0x00064968 as
       GetTickCount() - DAT_0006496c and 0x00020974 scales it by 0.001. */
    dt            = (float)(ticks - jb_prev_ticks) * 0.001f;
    jb_prev_ticks = ticks;

    /* jumpyball JumpyBall.exe WndProc 0x0001fd2c tests g_inKeyConfig before
       the g_appMode dispatch and returns straight after the wizard store. */
    if (jb_kc.active) {
        int code;

        while (jb_kc.active &&
               (code = Platform_NextRawKey()) != JB_KEY_UNBOUND)
            KeyConfig_KeyDown(&jb_kc, code);

        if (!jb_kc.active) {
            for (i = 0; i < JB_KEY_COUNT; i++)
                jb_key_prev[i] = Platform_KeyDown(i) ? 1 : 0;
            Platform_FlushRawKeys();
        }
    } else {
        act = PumpKeys();
        if (act == JB_MENU_QUIT) {
            jb_should_quit = 1;
            return;
        }
        if (act == JB_MENU_KEYCONFIG)
            KeyConfig_Begin(&jb_kc);
        if (act == JB_MENU_PLAY) {
            jb_mode         = JB_MODE_GAME;
            jb_pl.auto_jump = jb_m.auto_jump;
            ticks           = Platform_Ticks();
            jb_prev_ticks   = ticks;
            BeginLevel(jb_m.index, ticks);
        }
    }
    /* JB_FRAME_BODY_TAIL */
    if (jb_mode == JB_MODE_GAME) {
        Stage_Step(&jb_stg, ticks);

        /* JumpyBall.exe WndProc 0x00020934 "cmp r0,#0x0" with blt jumps to
           LAB_00020940 Game_DrawFrame, skipping input, physics and the
           camera while g_timeSec 0x00064a44 is negative. */
        if (!Stage_Frozen(&jb_stg)) {
            Player_Step(&jb_pl, dt);
            if (jb_pl.landed) {
                jb_pl.landed = 0;
                Audio_Play(JB_SND_BOUNCE);
            }
        }

        /* JumpyBall.exe WndProc 0x00020920 "cmp r0,#0x3fe" with bgt sleeps
           1000 ms, raises g_maxLevelUnlocked to g_menuIndex + 1 and calls
           Screen_Set 0x00013678 with 1 and g_menuIndex + 1. */
        if (Stage_Cleared(&jb_stg, jb_pl.cam_row)) {
            int next = jb_stg.level + 1;

            Platform_Delay(JB_LEVEL_CLEAR_PAUSE_MS);
            if (jb_stg.max_unlocked <= next)
                jb_stg.max_unlocked = next;
            jb_m.max_unlocked = jb_stg.max_unlocked;
            Menu_ScreenSet(&jb_m, JB_SCREEN_LEVELS, next);
            Audio_MusicPlay(JB_MUS_MENU);
            jb_mode       = JB_MODE_MENU;
            jb_prev_ticks = Platform_Ticks();
        }
    }

    if (jb_kc.active) {
        KeyConfig_DrawFrame(&jb_kc);
    } else if (jb_mode == JB_MODE_MENU) {
        Menu_DrawFrame(&jb_m);
    } else {
        jb_st.cam_row       = jb_pl.cam_row;
        jb_st.cam_pixel_ofs = jb_pl.cam_pixel_ofs;
        jb_st.ball_prev_x   = jb_pl.ball_prev_x;
        jb_ctx.cam_row      = jb_st.cam_row;
        jb_ctx.anim_ms      = (int)jb_stg.anim_ms;

        /* JumpyBall.exe Level_Begin 0x0001376c fills g_backdrop 0x00061b28
           with Blit_NoKey 0x00023a3c of g_viewW x g_viewH from the theme
           bitmap LoadBitmapW picked for g_theme 0x00064944. */
        Blit_NoKey(jb_back, 0, 0, JB_GAME_VIEW_W, JB_GAME_VIEW_H,
                   (jb_stg.backdrop_res == JB_RES_BACKDROP_DESERT)
                       ? &jb_a.backdrop_desert : &jb_a.backdrop_ice, 0, 0);

        Track_DrawFrame(&jb_st, DrawRow, &jb_ctx);

        jb_ball_st.ball_y     = jb_pl.ball_y;
        jb_ball_st.ball_vel_z = jb_pl.vel_z;
        jb_ball_st.ball_spin  = jb_pl.spin;
        Ball_DrawFrame(&jb_ball_st);
        if (jb_ball_st.layout_mode == JB_LAYOUT_240x320)
            Timer_DrawHud(jb_back, jb_pl.cam_row, jb_ball_st.hud_r,
                          jb_ball_st.hud_g, jb_ball_st.hud_b);
    }

#ifdef JB_BACKEND_GBA
    if (jb_kc.active || jb_mode == JB_MODE_MENU)
        Platform_SetPresentView(JB_MENU_VIEW_W, JB_MENU_VIEW_H,
                                JB_MENU_PRESENT_SCALE);
    else
        Platform_SetPresentView(JB_GAME_VIEW_W, JB_GAME_VIEW_H, 2);
#endif

    Platform_Present();

    if (jb_dump_path && ticks > 1500u) {
        FILE *fp = fopen(jb_dump_path, "wb");

        if (fp) {
            fwrite(jb_back->pixels, sizeof(uint16_t),
                   (size_t)JB_VIEW_W * (size_t)JB_VIEW_H, fp);
            fclose(fp);
        }
        jb_should_quit = 1;
    }
}

int main(int argc, char **argv)
{
    jb_surface *back;
    const char *dump_path = 0;
    int         scale = 0;
    int         start_level = -1;
    int         start_screen = JB_SCREEN_MAIN;
    int         start_index = 0;
    int         i;


    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--level=", 8))
            start_level = atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "--screen=", 9))
            start_screen = atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "--index=", 8))
            start_index = atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "--unlocked=", 11))
            jb_stg.max_unlocked = atoi(argv[i] + 11);
        else if (!scale)
            scale = atoi(argv[i]);
        else if (!dump_path)
            dump_path = argv[i];
    }
    if (scale < 1)
        scale = 2;

    if (!Platform_Init(JB_VIEW_W, JB_VIEW_H, scale, "JumpyBall port")) {
        char msg[JB_MSG_MAX];

        snprintf(msg, sizeof msg, "Could not open the window:\n\n%s",
                 Platform_LastError());
        Platform_ShowError("JumpyBall", msg);
        Platform_Shutdown();
        return 1;
    }
    back = Platform_BackBuffer();


    if (!Assets_Init()) {
        char msg[JB_MSG_MAX];
        /* Windows CE has no process environment, so pointing the player at
           JUMPYBALL_ASSETS there would be advice they cannot act on. */
#ifdef JB_WINCE
        const char *hint = "Copy the BITMAP, Sounds and Musics folders next to\n"
                           "jumpyball.exe.";
#else
        const char *hint = "Copy the BITMAP and Sounds folders next to jumpyball.exe,\n"
                           "or set JUMPYBALL_ASSETS to the folder that holds them.";
#endif

        snprintf(msg, sizeof msg,
                 "JumpyBall cannot find its assets.\n\nLooked for BITMAP/%d.bmp in:\n%s\n"
                 "Install directory from: %s\n\n%s",
                 JB_RES_FONT, Assets_FailureText(), Platform_BaseOrigin(), hint);
        Platform_ShowError("JumpyBall", msg);
        Platform_Shutdown();
        return 1;
    }

    /* JumpyBall.exe Game_Init 0x000113bc opens the speaker and loads both WAVs
       before the first Screen_Set 0x00013678. */
    Audio_Init();


    /* JumpyBall.exe Game_Init 0x000113bc builds g_rowProjOfs 0x00061438 and
       g_texVStep 0x00035238 before the first Track_DrawFrame 0x0001dd54. */
    Gfx_BuildScaleTables();
    Track_BuildProjTables();
    Gfx_BuildTexVStep();


    if (!AppAssets_Load(back)) {
        AppAssets_Free();
        Platform_Shutdown();
        return 1;
    }


    /* JumpyBall.exe Game_Init 0x000113bc calls Font_Load 0x0001f4d8 before the
       first Screen_Set 0x00013678. */
    if (!Font_Load(back)) {
        char msg[JB_MSG_MAX];

        snprintf(msg, sizeof msg,
                 "Font bitmaps missing or unreadable:\n\n%sBITMAP/%d.bmp\n"
                 "%sBITMAP/%d.bmp",
                 Assets_Root(), JB_RES_FONT, Assets_Root(), JB_RES_FONT_LARGE);
        Platform_ShowError("JumpyBall", msg);
        AppAssets_Free();
        Platform_Shutdown();
        return 1;
    }


    jb_ctx.screen      = back;
    jb_ctx.tex_water_h = &jb_a.tex_water_h;
    jb_ctx.tex_water_v = &jb_a.tex_water_v;
    jb_ctx.tex_ice_v   = &jb_a.tex_ice_v;
    jb_ctx.tex_sand_h  = &jb_a.tex_sand_h;
    jb_ctx.tex_sand_v  = &jb_a.tex_sand_v;
    jb_ctx.tex_mesh    = &jb_a.tex_mesh;
    jb_ctx.tex_wood    = &jb_a.tex_wood;
    jb_ctx.tex_grass   = &jb_a.tex_grass;
    jb_ctx.tex_grass_h = &jb_a.tex_grass_h;
    jb_ctx.spr_sign_keep_off = &jb_a.sign_keep_off;

    /* JumpyBall.exe Game_Init 0x000113bc at 0x00011d10 sets DAT_00064b18 to
       Color_Pack16If16bpp(g_screen 0x00061b10, 0x800080), and
       TrackRow_DrawGrass 0x00019198 passes it as the blit colour key. */
    jb_ctx.sign_key    = Color_Pack16If16bpp(back, 0x00800080u);
    jb_ctx.view_w      = JB_GAME_VIEW_W;
    jb_ctx.view_bottom = JB_GAME_VIEW_H;
    jb_ctx.layout_mode = JB_GAME_LAYOUT_MODE;
    jb_ctx.map_cols    = JB_MAP_COLS;

    jb_st.row_shift     = jb_row_shift;
    jb_st.tile_grid     = jb_tile_grid;
    jb_st.map_cols      = JB_MAP_COLS;
    jb_st.view_center_x = JB_GAME_VIEW_CENTER_X;
    jb_st.tile_size     = JB_GAME_TILE_SIZE;
    jb_st.view_bottom   = JB_GAME_VIEW_H;
    jb_st.ball_prev_x   = 0.0f;

    /* JumpyBall.exe .data g_autoJump 0x00026430 1, g_mapCols 0x0002628c 0x10,
       g_mapRows 0x00026274 0x400; g_altTrackMode 0x00064ab4 and DAT_00064940
       are .bss. */
    jb_pl.auto_jump     = 1;
    jb_pl.map_cols      = JB_MAP_COLS;
    jb_pl.tile_grid     = jb_tile_grid;
    jb_pl.checkpoint_x  = jb_checkpoint_x;
    jb_pl.checkpoint_y  = jb_checkpoint_y;
    jb_pl.checkpoint_n  = &jb_checkpoint_n;
    jb_pl.z_axis_damped = 0;
    Player_Respawn(&jb_pl);

    /* JumpyBall.exe Level_Begin 0x0001376c stores 0 to g_hudR 0x00026280,
       g_hudG 0x00026284 and g_hudB 0x00026288 while g_theme 0x00026370 is 0. */
    jb_ball_st.screen          = back;
    jb_ball_st.spr_shadow      = &jb_a.shadow;
    jb_ball_st.spr_ball_frames = &jb_a.ball;
    jb_ball_st.hud_r           = 0;
    jb_ball_st.hud_g           = 0;
    jb_ball_st.hud_b           = 0;
    jb_ball_st.layout_mode     = JB_GAME_LAYOUT_MODE;
    jb_ball_st.view_bottom     = JB_GAME_VIEW_H;
    jb_ball_st.view_center_x   = JB_GAME_VIEW_CENTER_X;

    jb_m.screen        = back;
    jb_m.backdrop      = &jb_a.backdrop_menu;
    jb_m.bar_thin      = &jb_a.bar_thin;
    jb_m.panel         = &jb_a.panel;
    jb_m.title         = &jb_a.title;
    jb_m.button_narrow = &jb_a.button_narrow;
    jb_m.button_wide   = &jb_a.button_wide;
    jb_m.logo_small    = &jb_a.logo_small;
    jb_m.key           = jb_ctx.sign_key;
    jb_m.view_w        = JB_MENU_VIEW_W;
    jb_m.view_h        = JB_MENU_VIEW_H;
    jb_m.view_center_x = JB_MENU_VIEW_CENTER_X;
    jb_m.layout_mode   = JB_LAYOUT_240x320;
    jb_m.max_unlocked  = jb_stg.max_unlocked;
    jb_m.auto_jump     = jb_pl.auto_jump;

    jb_kc.screen        = back;
    jb_kc.panel         = &jb_a.keyconfig_panel;
    jb_kc.key           = jb_ctx.sign_key;
    jb_kc.view_w        = JB_MENU_VIEW_W;
    jb_kc.view_h        = JB_MENU_VIEW_H;
    jb_kc.view_center_x = JB_MENU_VIEW_CENTER_X;
    jb_kc.active        = 0;
    jb_kc.step          = 0;
    KeyConfig_Load();

    jb_back       = back;
    jb_dump_path  = dump_path;
    jb_prev_ticks = Platform_Ticks();
    if (start_level >= 0) {
        jb_mode = JB_MODE_GAME;
        BeginLevel(start_level, jb_prev_ticks);
    } else {
        Menu_ScreenSet(&jb_m, start_screen, start_index);
        Audio_MusicPlay(JB_MUS_MENU);
    }

#ifdef __EMSCRIPTEN__
    /* The browser owns the frame clock: hand Frame to requestAnimationFrame
       (fps 0) and let it drive.  The third argument unwinds main so the page
       keeps calling Frame after this returns. */
    emscripten_set_main_loop(Frame, 0, 1);
#else
    while (!jb_should_quit)
        Frame();
#endif
    AppAssets_Free();
    Platform_Shutdown();
    return 0;
}
