#include "jb_menu.h"

#include "jb_audio.h"
#include "jb_consts.h"
#include "jb_platform.h"
#include "jb_text.h"

#define JB_MENU_ITEMS 4

/* GBA JB_MENU_HALF composites the menu natively at 120x160 - half the 240x320
   canvas - to drop the 4x present-time overdraw. Every literal coordinate,
   dimension and source offset below is in 240x320 space and is scaled by
   JB_MC; view_w/view_h/view_center_x already arrive halved from jb_main.c and
   stay raw. Blit_Core clamps w/h to the (halved) source, so sprite extents
   self-correct. Other backends see the identity macro. */
#ifdef JB_MENU_HALF
#define JB_MC(v) ((v) / 2)
#else
#define JB_MC(v) (v)
#endif

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 draws g_screen 0 from 0x00026920
   'Play', 0x00026914 'Settings', 0x0002690c 'About', 0x00026904 'Exit'. */
static const char *const jb_items_main[JB_MENU_ITEMS] = {
    "Play", "Settings", "About", "Exit"
};

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0, g_screen 1: 0x00026874 'Level 1',
   0x0002686c 'Level 2', 0x00026864 'Level 3', 0x0002685c 'Back'. */
static const char *const jb_items_levels[JB_MENU_ITEMS] = {
    "Level 1", "Level 2", "Level 3", "Back"
};

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0, g_screen 4: 0x0002679c 'Sound',
   0x0002678c 'Auto-Jump [ ]', 0x00026770 'Controls', 0x0002685c 'Back'. */
static const char *const jb_items_settings[JB_MENU_ITEMS] = {
    "Sound", "Auto-Jump [ ]", "Controls", "Back"
};

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 swaps in 0x0002677c while
   g_autoJump 0x00026430 is 1. */
#define JB_ITEM_AUTO_JUMP_ON "Auto-Jump [X]"

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 subtitle pairs for g_screen 0:
   0x000268f4 + 0x000268e8, 0x000268d0 + 0x000268bc, 0x000268ac + 0x00026898,
   0x0002688c + 0x0002687c. */
static const char *const jb_subs_main[JB_MENU_ITEMS][2] = {
    { "Choose a track",       "and play" },
    { "Set the sound volume", "and other settings" },
    { "About the team",       "who made this game" },
    { "Return to",            "Windows Mobile" }
};

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 subtitle pairs for g_screen 4:
   0x00026760 + 0x0002674c, 0x00026738 + 0x00026724, 0x0002671c + 0x00026710. */
static const char *const jb_subs_settings[3][2] = {
    { "Volume of sound",    "effects and music" },
    { "Jump when your are", "close to the border" },
    { "Set the",            "controls" }
};

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 uses 0x0002684c + 0x00026840 for
   g_screen 1 indices 0..2, and 0x00026830 + 0x00026824 for the Back item of
   g_screen 1 and g_screen 4. */
static const char *const jb_sub_ready[2] = { "Are you ready?", "Good luck!" };
static const char *const jb_sub_back[2]  = { "Return to the", "main menu" };

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0, g_screen 2: ten Text_DrawCentered
   calls with colour 0,0,0 from 0x00026814, 0x000267fc, 0x000267e8, 0x000267dc,
   0x000267cc, 0x000267c0, 0x000267b4, 0x000267ac, 0x000267a4, 0x000265f4. */
static const struct {
    int         y;
    int         dx;
    int         align;
    const char *text;
} jb_about[] = {
    {  80,  0, 1, "JumpyBall 1.00" },
    { 101,  3, 1, "Coder : Osaris, using" },
    { 116,  0, 1, "Hekkus Sound System" },
    { 141,  0, 1, "Graphics : " },
    { 156, -1, 1, "Team PocketNew" },
    { 181, -8, 2, "Testers : " },
    { 181, -8, 0, "Kisswert" },
    { 196, -8, 0, "FMARC" },
    { 204, -8, 0, ". . ." },
    { 229,  0, 1, "www.pocketnew.net" }
};

#define JB_ABOUT_N (int)(sizeof jb_about / sizeof jb_about[0])

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 item text y chain
   ((g_layoutMode == 0x14) * -10 + 0x80), ((g_layoutMode == 0x14) * -0x14 +
   0xa9), (7 - (g_layoutMode == 0x14)) * 0x1e, over the 0x0001b918 literal
   87.0. */
static const int jb_item_y[JB_MENU_ITEMS] = { 87, 128, 169, 210 };

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 subtitle y from the 0x0001b8ec
   literal 0x406e2000 and the inline 0x40700000. */
#define JB_SUB_Y0 241
#define JB_SUB_Y1 256

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 button y
   ((g_layoutMode == 0x14) * -10 + 0x29) * i + 0x4b. */
static int ButtonY(const jb_menu *m, int i)
{
    return JB_MC(((m->layout_mode == JB_LAYOUT_176x220) * -10 + 0x29) * i + 0x4b);
}

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0: g_backdropMenu 0x00061b48,
   g_sprBarThin tiled over 0x140 rows in steps of 10, g_sprTitleJumpyBall,
   g_sprPanelGradient at g_viewH - 0x30, then g_sprLogoPocketNewSmall at
   g_viewCenterX - 0x57, 0x11e tinted 0xff,0xff,0xff. */
static void DrawChrome(const jb_menu *m)
{
    int y;

    Blit_NoKey(m->screen, 0, 0, m->view_w, m->view_h, m->backdrop, 0, 0);

    for (y = 0; y < JB_MC(0x140); y += JB_MC(10))
        Blit(m->screen, 0, y, m->view_w, JB_MC(10), m->bar_thin, JB_NO_KEY, 0, 0);

    if (m->layout_mode == JB_LAYOUT_176x220)
        Blit(m->screen, 0, 0, m->view_w, JB_MC(0x3c), m->title, JB_NO_KEY,
             JB_MC(0x32), JB_MC(0x14));
    else
        Blit(m->screen, 0, 0, m->view_w, JB_MC(0x4d), m->title, JB_NO_KEY, 0, 0);

    Blit(m->screen, 0, m->view_h - JB_MC(0x30), m->view_w, JB_MC(0x30), m->panel,
         JB_NO_KEY, 0, 0);
    Blit_Glyph(m->screen, m->view_center_x - JB_MC(0x57), JB_MC(0x11e), JB_MC(0xaf),
               JB_MC(0x17), m->logo_small, 0xff, 0xff, 0xff, 0, 0);
}

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 blits g_sprButtonNarrow for every
   item with width 0xa0 - (g_layoutMode >> 1), then g_sprButtonWide for
   g_menuIndex with width 200 - g_layoutMode and src_x 10; both are skipped
   while g_screen is 2. */
static void DrawButtons(const jb_menu *m)
{
    int i;

    if (m->screen_id == JB_SCREEN_ABOUT)
        return;

    for (i = 0; i < JB_MENU_ITEMS; i++)
        Blit(m->screen, 0, ButtonY(m, i), JB_MC(0xa0 - (m->layout_mode >> 1)),
             JB_MC(32), m->button_narrow, m->key, 0, 0);

    Blit(m->screen, 0, ButtonY(m, m->index), JB_MC(200 - m->layout_mode),
         JB_MC(32), m->button_wide, m->key, JB_MC(10), 0);
}

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 item text x
   (g_menuIndex == i) * 0x10 + 0x23, align 0. */
static void DrawItem(const jb_menu *m, int i, const char *s, int r, int g,
                     int b)
{
    Text_DrawCentered(m->screen, JB_MC((m->index == i) * 0x10 + 0x23),
                      JB_MC(jb_item_y[i]), s, 0, r, g, b);
}

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 subtitle pair, both align 1 and
   colour 0,0,0, centred on g_viewCenterX. */
static void DrawSub(const jb_menu *m, const char *const pair[2], int dx1)
{
    Text_DrawCentered(m->screen, m->view_center_x, JB_MC(JB_SUB_Y0), pair[0], 1,
                      0, 0, 0);
    Text_DrawCentered(m->screen, m->view_center_x + JB_MC(dx1), JB_MC(JB_SUB_Y1),
                      pair[1], 1, 0, 0, 0);
}

static void DrawMain(const jb_menu *m)
{
    int i;

    for (i = 0; i < JB_MENU_ITEMS; i++)
        DrawItem(m, i, jb_items_main[i], 0xff, 0xff, 0xff);
    DrawSub(m, jb_subs_main[m->index], 0);
}

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 tints 'Level 2' with
   n = min(g_maxLevelUnlocked, 1) and 'Level 3' with
   n = (g_maxLevelUnlocked < 0 ? g_maxLevelUnlocked + 1 : g_maxLevelUnlocked)
   >> 1, then r = (n + 1) * 0x7f and g = b = (n + 1) * 0x7d. */
static void DrawLevels(const jb_menu *m)
{
    int max = m->max_unlocked;
    int n[JB_MENU_ITEMS];
    int i;

    n[0] = 1;
    n[1] = (max < 1) ? max : 1;
    n[2] = ((max < 0) ? max + 1 : max) >> 1;
    n[3] = 1;

    for (i = 0; i < JB_MENU_ITEMS; i++)
        DrawItem(m, i, jb_items_levels[i], (n[i] + 1) * 0x7f,
                 (n[i] + 1) * 0x7d, (n[i] + 1) * 0x7d);

    DrawSub(m, (m->index == 3) ? jb_sub_back : jb_sub_ready,
            (m->index == 3) ? -1 : 0);
}

/* JumpyBall.exe Menu_DrawFrame 0x0001a7d0 at 0x0001cf70 blits g_sprBarThin as
   the volume bar: x (g_menuIndex == 0) * 0x10 + 0x5a, w
   hssSpeaker::volumeSounds * 2, h 5, src_x (0x20 - volumeSounds) * 2; the y
   chain reduces to 87.0 + 4.0 while g_layoutMode is 0. */
static void DrawSettings(const jb_menu *m)
{
    int vol = Audio_Volume();
    int i;

    for (i = 0; i < JB_MENU_ITEMS; i++) {
        const char *s = jb_items_settings[i];

        if (i == 1 && m->auto_jump)
            s = JB_ITEM_AUTO_JUMP_ON;
        DrawItem(m, i, s, 0xff, 0xff, 0xff);
    }

    Blit(m->screen, JB_MC((m->index == 0) * 0x10 + 0x5a), JB_MC(91), JB_MC(vol * 2),
         JB_MC(5), m->bar_thin, m->key, JB_MC((0x20 - vol) * 2), 0);

    if (m->index == 3)
        DrawSub(m, jb_sub_back, -1);
    else
        DrawSub(m, jb_subs_settings[m->index], (m->index == 2) ? 2 : 0);
}

static void DrawAbout(const jb_menu *m)
{
    int i;

    for (i = 0; i < JB_ABOUT_N; i++)
        Text_DrawCentered(m->screen, m->view_center_x + JB_MC(jb_about[i].dx),
                          JB_MC(jb_about[i].y), jb_about[i].text,
                          jb_about[i].align, 0, 0, 0);
}

void Menu_DrawFrame(const jb_menu *m)
{
    DrawChrome(m);
    DrawButtons(m);

    switch (m->screen_id) {
    case JB_SCREEN_LEVELS:
        DrawLevels(m);
        break;
    case JB_SCREEN_ABOUT:
        DrawAbout(m);
        break;
    case JB_SCREEN_SETTINGS:
        DrawSettings(m);
        break;
    default:
        DrawMain(m);
        break;
    }
}

void Menu_ScreenSet(jb_menu *m, int screen_id, int index)
{
    Font_Select(m->screen, 1);
    m->screen_id = screen_id;
    m->index     = index;
}

/* JumpyBall.exe WndProc 0x0001fd2c menu branch: g_screen 2 skips the whole
   g_screen != 2 block and leaves through Screen_Set(0, 0); g_screen 0 index 2
   enters Screen_Set(2, 4). */
static int Confirm(jb_menu *m)
{
    switch (m->screen_id) {
    case JB_SCREEN_ABOUT:
        Menu_ScreenSet(m, JB_SCREEN_MAIN, 0);
        break;

    case JB_SCREEN_LEVELS:
        if (m->index == 3)
            Menu_ScreenSet(m, JB_SCREEN_MAIN, 0);
        else if (m->index <= m->max_unlocked)
            return JB_MENU_PLAY;
        break;

    /* JumpyBall.exe WndProc 0x0001fd2c index 0 calls Volume_Step 0x00012b54
       with 1, index 1 stores 1 - g_autoJump, index 2 sets g_inKeyConfig. */
    case JB_SCREEN_SETTINGS:
        if (m->index == 0)
            Audio_VolumeStep(1);
        else if (m->index == 1)
            m->auto_jump = 1 - m->auto_jump;
        else if (m->index == 2)
            return JB_MENU_KEYCONFIG;
        else if (m->index == 3)
            Menu_ScreenSet(m, JB_SCREEN_MAIN, 0);
        break;

    default:
        if (m->index == 0)
            Menu_ScreenSet(m, JB_SCREEN_LEVELS, 0);
        else if (m->index == 1)
            Menu_ScreenSet(m, JB_SCREEN_SETTINGS, 0);
        else if (m->index == 2)
            Menu_ScreenSet(m, JB_SCREEN_ABOUT, 4);
        else
            return JB_MENU_QUIT;
        break;
    }
    return JB_MENU_NONE;
}

/* JumpyBall.exe WndProc 0x0001fd2c steps g_menuIndex by one with a wrap over
   0..3, plays g_sndMenuClick, then forces 3 while g_screen is 2 or 5. */
static void Step(jb_menu *m, int delta)
{
    Audio_Play(JB_SND_MENU);
    m->index += delta;
    if (m->index < 0)
        m->index = 3;
    else if (m->index > 3)
        m->index = 0;
    if (m->screen_id == JB_SCREEN_ABOUT)
        m->index = 3;
}

/* JumpyBall.exe WndProc 0x0001fd2c left key: g_screen 1, 2 and 3 leave through
   Screen_Set(0, 0), and g_screen 4 with g_menuIndex 0 calls Volume_Step
   0x00012b54 with -1. */
static void StepLeft(jb_menu *m)
{
    Audio_Play(JB_SND_MENU);

    if (m->screen_id == JB_SCREEN_LEVELS || m->screen_id == JB_SCREEN_ABOUT)
        Menu_ScreenSet(m, JB_SCREEN_MAIN, 0);
    else if (m->screen_id == JB_SCREEN_SETTINGS && m->index == 0)
        Audio_VolumeStep(-1);
}

/* JumpyBall.exe WndProc 0x0001fd2c treats g_gxKeyStart, g_keyRight,
   g_gxKeyRight, g_gxKeyA, g_gxKeyB, g_gxKeyC and g_keyJump as the confirm
   keys. */
int Menu_KeyDown(jb_menu *m, int key)
{
    switch (key) {
    case JB_KEY_RIGHT:
    case JB_KEY_JUMP:
        return Confirm(m);
    case JB_KEY_UP:
        Step(m, -1);
        break;
    case JB_KEY_DOWN:
        Step(m, 1);
        break;
    case JB_KEY_LEFT:
        StepLeft(m);
        break;
    default:
        break;
    }
    return JB_MENU_NONE;
}
