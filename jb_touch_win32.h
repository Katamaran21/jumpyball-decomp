#ifndef JB_TOUCH_WIN32_H
#define JB_TOUCH_WIN32_H

#include <windows.h>

void TouchW_Layout(int win_w, int win_h, int game_w, int game_h);
void TouchW_GameRect(int *x, int *y, int *w, int *h);
void TouchW_PointerDown(int x, int y);
void TouchW_PointerMove(int x, int y);
void TouchW_PointerUp(void);
int  TouchW_Down(int key);
void TouchW_Draw(HDC dc);

#endif
