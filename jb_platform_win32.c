/* Native Win32 / Windows CE host layer.  It is an alternative to
   jb_platform_sdl2.c and stands in for the same JumpyBall.exe GAPI path
   (Gfx_CreateBackBuffer 0x00021698, Gfx_Present 0x000126fc), except that the
   back buffer is a top-down 16bpp DIB section and Gfx_Present is a BitBlt.

   Windows CE only ships the wide API, so every call here is the W form and
   the narrow strings the game passes are widened on the way in. */
#include "jb_platform.h"

#ifdef JB_EMBED
#include "jb_embed.h"
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jb_touch_win32.h"

#define JB_PATH_W 512
#define JB_MSG_W  2048

static HWND      jb_hwnd;
static HDC       jb_memdc;
static HBITMAP   jb_dib;
static HGDIOBJ   jb_oldbmp;
static uint16_t *jb_pixels;
static jb_surface jb_back;
static int       jb_w;
static int       jb_h;
static int       jb_dst_x;
static int       jb_dst_y;
static int       jb_dst_w;
static int       jb_dst_h;
static int       jb_keys[JB_KEY_COUNT];
static int       jb_running;
static int       jb_touch;

/* JumpyBall.exe WndProc 0x0001fd2c compares the incoming VK code against
   g_keyLeft, g_keyRight, g_keyUp, g_keyDown, g_keyJump and g_keyMenu.  These
   are the original defaults - unlike the SDL backend, the codes stored in
   keys.cfg by this backend really are VK codes. */
static int jb_keymap[JB_KEY_COUNT] = {
    VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_SPACE, VK_RETURN
};

#define JB_RAWQ 16

static int jb_rawq[JB_RAWQ];
static int jb_rawq_head;
static int jb_rawq_tail;

static const WCHAR jb_class[] = L"JumpyBallWnd";

/* A CE image can be built without the NLS codepage tables, and then the
   MultiByteToWideChar / WideCharToMultiByte pair fails outright and returns 0.
   Every path and message this port passes through them is ASCII, so a straight
   widen/narrow is both a correct conversion and one that has nothing left to
   fail in.  These back the codepage calls up rather than replacing them, so a
   normal image still goes through the real conversion. */
static void WidenAscii(const char *s, WCHAR *buf, int cch)
{
    int i;

    for (i = 0; i + 1 < cch && s[i] != '\0'; i++)
        buf[i] = (WCHAR)(unsigned char)s[i];
    buf[i] = L'\0';
}

static void NarrowAscii(const WCHAR *s, char *buf, int cch)
{
    int i;

    for (i = 0; i + 1 < cch && s[i] != L'\0'; i++)
        buf[i] = (s[i] < 0x80) ? (char)s[i] : '?';
    buf[i] = '\0';
}

static int WLen(const WCHAR *s)
{
    int n = 0;

    while (s[n] != L'\0')
        n++;
    return n;
}

static void WCopy(WCHAR *dst, int cch, const WCHAR *s)
{
    int i = 0;

    while (i + 1 < cch && s[i] != L'\0') {
        dst[i] = s[i];
        i++;
    }
    dst[i] = L'\0';
}

static void WCat(WCHAR *dst, int cch, const WCHAR *s)
{
    int n = WLen(dst);
    int i = 0;

    while (n + 1 < cch && s[i] != L'\0')
        dst[n++] = s[i++];
    dst[n] = L'\0';
}

static WCHAR *ToWide(const char *s, WCHAR *buf, int cch)
{
    if (MultiByteToWideChar(CP_ACP, 0, s, -1, buf, cch) == 0)
        WidenAscii(s, buf, cch);
    buf[cch - 1] = L'\0';
    return buf;
}

/* Windows CE has no current directory and its file API rejects '/', so the
   paths jb_assets.c builds are rewritten with backslashes here. */
static WCHAR *ToPath(const char *s, WCHAR *buf, int cch)
{
    int i;

    ToWide(s, buf, cch);
    for (i = 0; i < cch && buf[i] != L'\0'; i++) {
        if (buf[i] == L'/')
            buf[i] = L'\\';
    }
    return buf;
}

static int MapKey(int code)
{
    int k;

    if (code == JB_KEY_UNBOUND)
        return -1;
    for (k = 0; k < JB_KEY_COUNT; k++) {
        if (jb_keymap[k] == code)
            return k;
    }
    return -1;
}

static void PushRawKey(int code)
{
    int next = (jb_rawq_head + 1) % JB_RAWQ;

    if (next != jb_rawq_tail) {
        jb_rawq[jb_rawq_head] = code;
        jb_rawq_head          = next;
    }
}

static void BlitToDC(HDC dc)
{
    if (jb_memdc == NULL)
        return;
    if (jb_touch) {
        RECT rc;

        if (GetClientRect(jb_hwnd, &rc))
            FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
    if (jb_dst_w == jb_w && jb_dst_h == jb_h) {
        BitBlt(dc, jb_dst_x, jb_dst_y, jb_w, jb_h, jb_memdc, 0, 0, SRCCOPY);
    } else {
        StretchBlt(dc, jb_dst_x, jb_dst_y, jb_dst_w, jb_dst_h, jb_memdc, 0, 0,
                   jb_w, jb_h, SRCCOPY);
    }
    if (jb_touch)
        TouchW_Draw(dc);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int k;

    switch (msg) {
    case WM_CLOSE:
        jb_running = 0;
        return 0;
    case WM_DESTROY:
        jb_running = 0;
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);

        BlitToDC(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    /* jumpyball JumpyBall.exe WndProc 0x0001fd2c binds on WM_KEYDOWN. */
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if ((int)wp == VK_ESCAPE) {
            jb_running = 0;
            return 0;
        }
        /* lParam bit 30 is the previous key state: set means auto-repeat. */
        if ((lp & 0x40000000L) == 0)
            PushRawKey((int)wp);
        k = MapKey((int)wp);
        if (k >= 0)
            jb_keys[k] = 1;
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        k = MapKey((int)wp);
        if (k >= 0)
            jb_keys[k] = 0;
        return 0;
    case WM_ACTIVATE:
        Platform_AudioPause(LOWORD(wp) == WA_INACTIVE);
        return 0;
    case WM_LBUTTONDOWN:
        if (jb_touch) {
            SetCapture(hwnd);
            TouchW_PointerDown((int)(short)LOWORD(lp), (int)(short)HIWORD(lp));
        }
        return 0;
    case WM_MOUSEMOVE:
        if (jb_touch && (wp & MK_LBUTTON))
            TouchW_PointerMove((int)(short)LOWORD(lp), (int)(short)HIWORD(lp));
        return 0;
    case WM_LBUTTONUP:
        if (jb_touch) {
            ReleaseCapture();
            TouchW_PointerUp();
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

#ifdef JB_WINCE
/* aygshell is missing on plain CE builds and on some Smartphone images, so
   SHFullScreen is resolved at run time and simply skipped when absent. */
typedef BOOL (WINAPI *jb_shfullscreen)(HWND, DWORD);

#define JB_SHFS_HIDETASKBAR   0x0002
#define JB_SHFS_HIDESIPBUTTON 0x0008
#define JB_SHFS_HIDESTARTICON 0x0010

static void HideShell(HWND hwnd)
{
    HMODULE          lib = LoadLibraryW(L"aygshell.dll");
    jb_shfullscreen  fn;

    if (lib == NULL)
        return;
    fn = (jb_shfullscreen)GetProcAddress(lib, L"SHFullScreen");
    if (fn != NULL)
        fn(hwnd, JB_SHFS_HIDETASKBAR | JB_SHFS_HIDESIPBUTTON |
                 JB_SHFS_HIDESTARTICON);
}
#endif

static int TouchWanted(void)
{
#ifdef JB_WINCE
    return 1;
#else
    return getenv("JUMPYBALL_TOUCH") != NULL;
#endif
}

int Platform_Init(int w, int h, int scale, const char *title)
{
    struct {
        BITMAPINFOHEADER h;
        DWORD            mask[3];
    } bi;
    WNDCLASSW wc;
    WCHAR     wtitle[128];
    HDC       dc;
    int       screen_w, screen_h;
    int       client_w, client_h;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = jb_class;
#ifndef JB_WINCE
    /* IDC_ARROW is MAKEINTRESOURCE, which the SDK types for the narrow API
       unless UNICODE is defined; the value is an ordinal, so the cast is the
       portable way to hand it to the W entry point. */
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
#endif
    if (RegisterClassW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);

#ifdef JB_WINCE
    /* The handheld owns the whole screen; the scale argument only decides how
       many whole copies of the 240x320 frame fit on a VGA panel. */
    (void)scale;
    scale = 1;
    while ((scale + 1) * w <= screen_w && (scale + 1) * h <= screen_h)
        scale++;
    client_w = screen_w;
    client_h = screen_h;
    jb_hwnd = CreateWindowExW(0, jb_class, ToWide(title, wtitle, 128),
                              WS_VISIBLE | WS_POPUP, 0, 0, screen_w, screen_h,
                              NULL, NULL, wc.hInstance, NULL);
#else
    if (scale < 1)
        scale = 1;
    while (scale > 1 && (w * scale > screen_w || h * scale > screen_h))
        scale--;
    client_w = w * scale;
    client_h = h * scale;
    {
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT  r;

        r.left   = 0;
        r.top    = 0;
        r.right  = client_w;
        r.bottom = client_h;
        AdjustWindowRect(&r, style, FALSE);
        jb_hwnd = CreateWindowExW(0, jb_class, ToWide(title, wtitle, 128),
                                  style | WS_VISIBLE, CW_USEDEFAULT,
                                  CW_USEDEFAULT, r.right - r.left,
                                  r.bottom - r.top, NULL, NULL, wc.hInstance,
                                  NULL);
    }
#endif
    if (jb_hwnd == NULL)
        return 0;
#ifdef JB_WINCE
    HideShell(jb_hwnd);
#endif

    jb_dst_w = w * scale;
    jb_dst_h = h * scale;
    if (jb_dst_w > client_w)
        jb_dst_w = client_w;
    if (jb_dst_h > client_h)
        jb_dst_h = client_h;
    jb_dst_x = (client_w - jb_dst_w) / 2;
    jb_dst_y = (client_h - jb_dst_h) / 2;

    jb_touch = TouchWanted();
    if (jb_touch) {
        int gx, gy, gw, gh;

        TouchW_Layout(client_w, client_h, w, h);
        TouchW_GameRect(&gx, &gy, &gw, &gh);
        jb_dst_x = gx;
        jb_dst_y = gy;
        jb_dst_w = gw;
        jb_dst_h = gh;
    }

    /* JumpyBall.exe Game_Init 0x000113bc passes 0 to Gfx_CreateBackBuffer
       0x00021698 when g_viewH 0x0002626c is not 0xf0, so g_clipHRow 0x000269e8
       keeps its .data value 0x140 and Blit_TileH admits y == h.  The DIB is one
       row taller than the view for exactly that row. */
    memset(&bi, 0, sizeof bi);
    bi.h.biSize        = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth       = w;
    bi.h.biHeight      = -(h + 1);
    bi.h.biPlanes      = 1;
    bi.h.biBitCount    = 16;
    bi.h.biCompression = BI_BITFIELDS;
    bi.mask[0]         = 0xf800u;
    bi.mask[1]         = 0x07e0u;
    bi.mask[2]         = 0x001fu;

    dc = GetDC(jb_hwnd);
    if (dc == NULL)
        return 0;
    jb_dib = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS,
                              (void **)&jb_pixels, NULL, 0);
    jb_memdc = CreateCompatibleDC(dc);
    ReleaseDC(jb_hwnd, dc);
    if (jb_dib == NULL || jb_memdc == NULL || jb_pixels == NULL)
        return 0;
    jb_oldbmp = SelectObject(jb_memdc, jb_dib);
    memset(jb_pixels, 0, (size_t)w * ((size_t)h + 1u) * sizeof(uint16_t));

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
    jb_running    = 1;

    SetForegroundWindow(jb_hwnd);
    return 1;
}

void Platform_Shutdown(void)
{
    Platform_AudioShutdown();
    if (jb_memdc != NULL) {
        SelectObject(jb_memdc, jb_oldbmp);
        DeleteDC(jb_memdc);
        jb_memdc = NULL;
    }
    if (jb_dib != NULL) {
        DeleteObject(jb_dib);
        jb_dib = NULL;
    }
    jb_pixels = NULL;
    if (jb_hwnd != NULL) {
        DestroyWindow(jb_hwnd);
        jb_hwnd = NULL;
    }
    UnregisterClassW(jb_class, GetModuleHandleW(NULL));
}

jb_surface *Platform_BackBuffer(void)
{
    return &jb_back;
}

/* jumpyball JumpyBall.exe Gfx_Present 0x000126fc */
void Platform_Present(void)
{
    HDC dc;

    if (jb_hwnd == NULL)
        return;
    dc = GetDC(jb_hwnd);
    if (dc == NULL)
        return;
    BlitToDC(dc);
    ReleaseDC(jb_hwnd, dc);
}

int Platform_TouchActive(void)
{
    return jb_touch;
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
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            jb_running = 0;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return jb_running;
}

int Platform_KeyDown(int key)
{
    if (key < 0 || key >= JB_KEY_COUNT)
        return 0;
    return (jb_keys[key] || (jb_touch && TouchW_Down(key))) ? 1 : 0;
}

unsigned Platform_Ticks(void)
{
    return (unsigned)GetTickCount();
}

void Platform_Delay(unsigned ms)
{
    Sleep((DWORD)ms);
}

/* Where Platform_BasePath got its answer, reported by Platform_LastError when
   the assets are missing.  On a cut-down CE image GetModuleFileName can fail,
   and then every probe path in the error dialog looks the same no matter which
   step produced it - this is what tells them apart. */
static const char *jb_base_origin = "unset";

/* CE has no current directory, so a relative path never resolves and the
   desktop ".\\" fallback is useless there.  These are the standard places a
   game gets copied to; the search only accepts one that really holds BITMAP,
   so it cannot pick a wrong-but-existing directory. */
#ifdef JB_WINCE
static const WCHAR *const jb_ce_dirs[] = {
    L"\\Storage Card\\JumpyBall\\",
    L"\\SD Card\\JumpyBall\\",
    L"\\Program Files\\JumpyBall\\",
    L"\\My Documents\\JumpyBall\\",
    L"\\Windows\\JumpyBall\\",
    L"\\JumpyBall\\",
    L"\\Storage Card\\",
    L"\\"
};

/* Platform_FileExists is declared below and takes a narrow path; this probe is
   wide throughout so it needs nothing from the conversions. */
static int HasAssets(const WCHAR *dir)
{
    WCHAR probe[JB_PATH_W];

    WCopy(probe, JB_PATH_W, dir);
    WCat(probe, JB_PATH_W, L"BITMAP\\254.bmp");
    return GetFileAttributesW(probe) != 0xffffffffu;
}
#endif

const char *Platform_BasePath(void)
{
    static char path[JB_PATH_W];

    if (path[0] == '\0') {
        WCHAR wide[JB_PATH_W];
        DWORD n;
        int   i, cut;

        /* A NULL module handle means "the file that created the calling
           process" on both CE and the desktop.  GetModuleHandle(NULL) is the
           desktop idiom for the same thing, but CE does not support a NULL
           module name, so it is only the second try here. */
        wide[0] = L'\0';
        n = GetModuleFileNameW(NULL, wide, JB_PATH_W);
        jb_base_origin = "GetModuleFileName(NULL)";
        if (n == 0 || n >= JB_PATH_W) {
            n = GetModuleFileNameW(GetModuleHandleW(NULL), wide, JB_PATH_W);
            jb_base_origin = "GetModuleFileName(GetModuleHandle(NULL))";
        }

        if (n > 0 && n < JB_PATH_W) {
            wide[n] = L'\0';
            /* Keep the directory, drop the executable name. */
            cut = -1;
            for (i = 0; wide[i] != L'\0'; i++) {
                if (wide[i] == L'\\' || wide[i] == L'/')
                    cut = i;
            }
            /* A bare "jumpyball.exe" with no separator, or a path that shrinks
               to the drive root, is not a directory we can load from. */
            if (cut > 0) {
                wide[cut + 1] = L'\0';
                if (WideCharToMultiByte(CP_ACP, 0, wide, -1, path, JB_PATH_W,
                                        NULL, NULL) == 0)
                    NarrowAscii(wide, path, JB_PATH_W);
                if (path[0] != '\0')
                    return path;
            }
        }

#ifdef JB_WINCE
        /* The module path was unusable.  Find the install directory by looking
           for the assets themselves. */
        for (i = 0; i < (int)(sizeof jb_ce_dirs / sizeof jb_ce_dirs[0]); i++) {
            if (!HasAssets(jb_ce_dirs[i]))
                continue;
            if (WideCharToMultiByte(CP_ACP, 0, jb_ce_dirs[i], -1, path,
                                    JB_PATH_W, NULL, NULL) == 0)
                NarrowAscii(jb_ce_dirs[i], path, JB_PATH_W);
            jb_base_origin = "asset search";
            return path;
        }
        /* Nothing found.  CE cannot resolve a relative path, so say so rather
           than emit a ".\\" that is guaranteed to fail. */
        strcpy(path, "\\");
        jb_base_origin = "no module path and no assets found";
#else
        strcpy(path, ".\\");
        jb_base_origin = "cwd fallback";
#endif
    }
    return path;
}

/* Named so the error dialog can say how the search directory was chosen. */
const char *Platform_BaseOrigin(void)
{
    return jb_base_origin;
}

const char *Platform_PrefPath(void)
{
    return Platform_BasePath();
}

int Platform_FileExists(const char *path)
{
    WCHAR wide[JB_PATH_W];

#ifdef JB_EMBED
    {
        long n;
        if (Embed_Find(path, &n))
            return 1;
    }
#endif
    return GetFileAttributesW(ToPath(path, wide, JB_PATH_W)) != 0xffffffffu;
}

unsigned char *Platform_ReadFile(const char *path, long *out_len)
{
    WCHAR          wide[JB_PATH_W];
    HANDLE         fh;
    DWORD          size, got;
    unsigned char *buf;

#ifdef JB_EMBED
    {
        unsigned char *e = Embed_Read(path, out_len);
        if (e)
            return e;
    }
#endif
    fh = CreateFileW(ToPath(path, wide, JB_PATH_W), GENERIC_READ,
                     FILE_SHARE_READ, NULL, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE)
        return NULL;
    size = GetFileSize(fh, NULL);
    if (size == 0xffffffffu || size == 0 || size > 0x2000000u) {
        CloseHandle(fh);
        return NULL;
    }
    buf = (unsigned char *)malloc((size_t)size);
    if (buf == NULL) {
        CloseHandle(fh);
        return NULL;
    }
    if (!ReadFile(fh, buf, size, &got, NULL) || got != size) {
        CloseHandle(fh);
        free(buf);
        return NULL;
    }
    CloseHandle(fh);
    *out_len = (long)size;
    return buf;
}

void Platform_ShowError(const char *title, const char *text)
{
    static WCHAR wtitle[128];
    static WCHAR wtext[JB_MSG_W];

    MessageBoxW(jb_hwnd, ToWide(text, wtext, JB_MSG_W),
                ToWide(title, wtitle, 128), MB_OK | MB_ICONERROR);
}

const char *Platform_LastError(void)
{
    static char msg[256];
    WCHAR       wide[256];
    DWORD       err = GetLastError();
    int         i;

    if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, err, 0, wide, 256, NULL) == 0) {
        sprintf(msg, "Windows error %lu", (unsigned long)err);
        return msg;
    }
    if (WideCharToMultiByte(CP_ACP, 0, wide, -1, msg, sizeof msg, NULL,
                            NULL) == 0)
        NarrowAscii(wide, msg, (int)sizeof msg);
    for (i = (int)strlen(msg) - 1; i >= 0; i--) {
        if (msg[i] != '\r' && msg[i] != '\n' && msg[i] != ' ')
            break;
        msg[i] = '\0';
    }
    return msg;
}
