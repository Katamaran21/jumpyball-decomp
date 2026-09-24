/* GDI mirror of the SDL touch pad in jb_touch_sdl2.c: the same on-screen dpad
   for the native Win32 / Windows CE backend, drawn through GDI and driven by
   the stylus (WM_LBUTTON* in WndProc).  A resistive CE panel is single-touch,
   so one button is held at a time - unlike the SDL path's 8 fingers. */
#include "jb_touch_win32.h"

#include "jb_platform.h"

typedef struct {
    int x, y, w, h;
} jb_rect;

static jb_rect jb_game;
static jb_rect jb_pad[JB_KEY_COUNT];
static int     jb_down[JB_KEY_COUNT];
static int     jb_win_w;
static int     jb_win_h;

static void SetPad(int key, int x, int y, int w, int h)
{
    jb_pad[key].x = x;
    jb_pad[key].y = y;
    jb_pad[key].w = w;
    jb_pad[key].h = h;
}

static int HitTest(int x, int y)
{
    int k;

    for (k = 0; k < JB_KEY_COUNT; k++) {
        if (x >= jb_pad[k].x && x < jb_pad[k].x + jb_pad[k].w &&
            y >= jb_pad[k].y && y < jb_pad[k].y + jb_pad[k].h)
            return k;
    }
    return -1;
}

void TouchW_Layout(int win_w, int win_h, int game_w, int game_h)
{
    int pad_h, view_h, cell, margin, side;
    int gw, gh;

    if (win_w == jb_win_w && win_h == jb_win_h)
        return;
    jb_win_w = win_w;
    jb_win_h = win_h;

    cell = (win_w < win_h ? win_w : win_h) / 7;
    if (cell > win_h / 9)
        cell = win_h / 9;
    margin = cell / 4;
    side   = cell + cell / 2;
    pad_h  = 3 * cell + 2 * margin;
    view_h = win_h - pad_h;

    gw = win_w;
    gh = game_h * gw / game_w;
    if (gh > view_h) {
        gh = view_h;
        gw = game_w * gh / game_h;
    }
    jb_game.x = (win_w - gw) / 2;
    jb_game.y = (view_h - gh) / 2;
    jb_game.w = gw;
    jb_game.h = gh;

    SetPad(JB_KEY_UP, margin + cell, view_h, cell, cell);
    SetPad(JB_KEY_LEFT, margin, view_h + cell, cell, cell);
    SetPad(JB_KEY_RIGHT, margin + 2 * cell, view_h + cell, cell, cell);
    SetPad(JB_KEY_DOWN, margin + cell, view_h + 2 * cell, cell, cell);
    SetPad(JB_KEY_JUMP, win_w - margin - side, win_h - margin - side, side,
            side);
    SetPad(JB_KEY_MENU, win_w - margin - cell, view_h + margin, cell, cell);
}

void TouchW_GameRect(int *x, int *y, int *w, int *h)
{
    *x = jb_game.x;
    *y = jb_game.y;
    *w = jb_game.w;
    *h = jb_game.h;
}

static void SetDown(int key)
{
    int k;

    for (k = 0; k < JB_KEY_COUNT; k++)
        jb_down[k] = 0;
    if (key >= 0)
        jb_down[key] = 1;
}

void TouchW_PointerDown(int x, int y)
{
    SetDown(HitTest(x, y));
}

void TouchW_PointerMove(int x, int y)
{
    SetDown(HitTest(x, y));
}

void TouchW_PointerUp(void)
{
    SetDown(-1);
}

int TouchW_Down(int key)
{
    if (key < 0 || key >= JB_KEY_COUNT)
        return 0;
    return jb_down[key];
}

static void Chevron(HDC dc, const jb_rect *r, int dx, int dy)
{
    POINT p[3];
    int   cx = r->x + r->w / 2;
    int   cy = r->y + r->h / 2;
    int   a  = r->w / 5;

    p[0].x = cx - dy * a - dx * a / 2;
    p[0].y = cy - dx * a - dy * a / 2;
    p[1].x = cx + dx * a;
    p[1].y = cy + dy * a;
    p[2].x = cx + dy * a - dx * a / 2;
    p[2].y = cy + dx * a - dy * a / 2;
    Polyline(dc, p, 3);
}

static void Bars(HDC dc, const jb_rect *r)
{
    int i, x0, x1, y;

    x0 = r->x + r->w / 4;
    x1 = r->x + r->w - r->w / 4;
    for (i = 0; i < 3; i++) {
        y = r->y + r->h / 4 + i * r->h / 4;
        MoveToEx(dc, x0, y, NULL);
        LineTo(dc, x1, y);
    }
}

void TouchW_Draw(HDC dc)
{
    static const int dir_x[JB_KEY_COUNT] = { -1, 1, 0, 0, 0, 0 };
    static const int dir_y[JB_KEY_COUNT] = { 0, 0, -1, 1, -1, 0 };
    HBRUSH  down_br = CreateSolidBrush(RGB(0x60, 0x90, 0xd0));
    HBRUSH  up_br   = CreateSolidBrush(RGB(0x20, 0x20, 0x28));
    HPEN    pen     = CreatePen(PS_SOLID, 1, RGB(0xd0, 0xd0, 0xd8));
    HGDIOBJ oldpen  = SelectObject(dc, pen);
    HGDIOBJ oldbr   = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int     k;

    for (k = 0; k < JB_KEY_COUNT; k++) {
        RECT rc;

        rc.left   = jb_pad[k].x;
        rc.top    = jb_pad[k].y;
        rc.right  = jb_pad[k].x + jb_pad[k].w;
        rc.bottom = jb_pad[k].y + jb_pad[k].h;
        FillRect(dc, &rc, jb_down[k] ? down_br : up_br);
        Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
        if (k == JB_KEY_MENU)
            Bars(dc, &jb_pad[k]);
        else
            Chevron(dc, &jb_pad[k], dir_x[k], dir_y[k]);
    }

    SelectObject(dc, oldbr);
    SelectObject(dc, oldpen);
    DeleteObject(pen);
    DeleteObject(down_br);
    DeleteObject(up_br);
}
