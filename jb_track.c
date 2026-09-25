#include "jb_consts.h"
#include "jb_track.h"

int jb_row_proj_ofs[JB_PROJ_TABLE_N + 1];
int jb_row_height[JB_PROJ_TABLE_N + 1];
#ifndef JB_TABLES_ROM
short jb_tex_vstep[JB_TEX_VSTEP_ROWS * JB_TEX_VSTEP_STRIDE];
#endif

/* JumpyBall.exe Game_Init 0x000113bc 0x000114f4 and Track_DrawFrame 0x0001dd54
   0x0001e1f8: __divs(__muls(t, kProjNum), __adds(__muls(t, kProjDenA), kProjDenB))
   then __muls by kProjScale. */
static float Proj(float t)
{
    return (t * JB_PROJ_NUM) / (t * JB_PROJ_DEN_A + JB_PROJ_DEN_B) * JB_PROJ_SCALE;
}

/* JumpyBall.exe Track_DrawFrame 0x0001e358: __stod, then __subd against the
   r2:r3 pair built by "mov r3,#0x3fc00000; mov r2,#0x0; orr r3,r3,#0x200000"
   (0x3fe0000000000000 == 0.5), then __dtoi and "add r3,r0,#0x1". */
static int RoundHalfUp(float x)
{
    return (int)((double)x - 0.5) + 1;
}

/* JumpyBall.exe Track_DrawFrame 0x0001e2a0: __muls(s, kTileSize) + g_viewCenterX
   - g_mapCols * g_tileHalf. */
static float FullX(const jb_track_state *st, int tile_half, float s)
{
    return s * (float)st->tile_size + (float)st->view_center_x
           - (float)(st->map_cols * tile_half);
}

/* JumpyBall.exe Track_DrawFrame 0x0001e2c0: __muls(s, 4.0) + g_viewCenterX
   - __itos(g_mapCols << 1). */
static float FarX(const jb_track_state *st, float s)
{
    return s * (float)JB_FAR_TILE_SIZE + (float)st->view_center_x
           - (float)(st->map_cols * 2);
}

/* JumpyBall.exe Track_DrawFrame 0x0001e2f0: __divs by __itos(g_rowProjOfsCopy[17]),
   __muls by (320.0 - y), __adds the full-scale x, then the 0x0001e358 rounding. */
static int EdgeX(const jb_track_state *st, int tile_half, float s, float proj)
{
    float full = FullX(st, tile_half, s);

    return RoundHalfUp((FarX(st, s) - full) / (float)JB_PROJ_SPAN * proj + full);
}

void Track_BuildProjTables(void)
{
    int k;

    jb_row_proj_ofs[0] = 0;
    for (k = 0; k < JB_PROJ_TABLE_N; k++)
        jb_row_proj_ofs[k + 1] = (int)Proj((float)k);

    /* JumpyBall.exe Game_Init 0x000113bc: DAT_000614ac = 0x96 before the loop,
       which covers byte offsets 8..0x6c of g_rowHeight 0x000614a8. */
    jb_row_height[0] = 0;
    jb_row_height[1] = 0x96;
    for (k = 2; k < JB_PROJ_TABLE_N; k++)
        jb_row_height[k] = jb_row_proj_ofs[k] - jb_row_proj_ofs[k - 1];
}

/* JumpyBall.exe Game_Init 0x00011560: per row h, __divs(100.0, __itos(h))
   accumulated by __adds and stored through __stoi, floored at 1. */
#ifdef JB_TABLES_ROM
/* jb_tex_vstep is const in cartridge ROM (tools/gen_tables.py). */
void Gfx_BuildTexVStep(void)
{
}
#else
void Gfx_BuildTexVStep(void)
{
    int h, k;

    for (h = 1; h < JB_TEX_VSTEP_ROWS; h++) {
        short *dst  = jb_tex_vstep + h * JB_TEX_VSTEP_STRIDE;
        float  step = 100.0f / (float)h;
        float  acc  = 0.0f;

        for (k = 0; k < h; k++) {
            acc += step;
            dst[k] = (short)(int)acc;
            if (dst[k] < 1)
                dst[k] = 1;
        }
    }
}
#endif

void Track_DrawFrame(const jb_track_state *st, jb_track_row_fn draw, void *user)
{
    static unsigned char tiles[JB_MAP_COLS];
    int   i, tile_half, c;
    float cam;

    /* JumpyBall.exe Track_DrawFrame 0x0001dd94: g_tileHalf = (kTileSize < 0 ?
       kTileSize + 1 : kTileSize) >> 1. */
    tile_half = (st->tile_size < 0 ? st->tile_size + 1 : st->tile_size) >> 1;
    cam = st->cam_pixel_ofs / (float)JB_ROW_PIXELS;

    for (i = 0; i < JB_VISIBLE_ROWS + 1; i++) {
        jb_track_row row;
        float y_near, y_far, proj_near, proj_far, shift_near, shift_far;

        row.row = JB_VISIBLE_ROWS - i;

        y_near = (float)st->view_bottom - Proj((float)row.row - cam);
        y_far  = (float)st->view_bottom - Proj((float)(row.row + 1) - cam);
        /* JumpyBall.exe Track_DrawFrame 0x0001e1c4: on the first iteration the far
           edge is recomputed from __itos(iVar17 + 1) with no camera offset. */
        if (i == 0)
            y_far = (float)st->view_bottom - Proj((float)(row.row + 1));

        proj_near = (float)st->view_bottom - y_near;
        proj_far  = (float)st->view_bottom - y_far;

        shift_far  = st->row_shift[row.row + 1];
        shift_near = st->row_shift[row.row];

        row.x_far_left  = EdgeX(st, tile_half, shift_far + st->ball_prev_x, proj_far);
        row.x_far_right = EdgeX(st, tile_half,
                                (float)st->map_cols + shift_far + st->ball_prev_x,
                                proj_far);
        row.x_near_left = EdgeX(st, tile_half, shift_near + st->ball_prev_x, proj_near);
        row.x_near_right = EdgeX(st, tile_half,
                                 shift_near + (float)st->map_cols + st->ball_prev_x,
                                 proj_near);
        row.y_far  = (int)y_far;
        row.y_near = (int)y_near;

        /* JumpyBall.exe Track_DrawFrame 0x0001e75c: g_mapCols bytes from
           g_tileGrid + (g_camRow + iVar17) * g_mapCols into DAT_000613b8. */
        for (c = 0; c < st->map_cols && c < JB_MAP_COLS; c++)
            tiles[c] = st->tile_grid[(st->cam_row + row.row) * st->map_cols + c];
        row.tiles = tiles;

        draw(&row, user);
    }
}
