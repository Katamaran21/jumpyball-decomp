#ifndef JB_TRACK_H
#define JB_TRACK_H

/* JumpyBall.exe Game_Init 0x000113bc: g_rowProjOfs 0x00061438 indices 1..28,
   copied 0x70 bytes to g_rowProjOfsCopy 0x00061518. */
#define JB_PROJ_TABLE_N 28

/* JumpyBall.exe Level_Generate 0x00013490: 200 bytes of g_rowShiftSrc at g_camRow
   copied to g_rowShift 0x00061930. */
#define JB_ROW_SHIFT_N 50

/* JumpyBall.exe Track_DrawFrame 0x0001dd54 argument order at the
   TrackRow_DrawIce 0x00016e98 call: (x_far_left, y_far, x_near_left, y_near,
   x_far_right, x_near_right, row); tiles is DAT_000613b8. */
typedef struct {
    int x_far_left;
    int x_far_right;
    int y_far;
    int x_near_left;
    int x_near_right;
    int y_near;
    int row;
    const unsigned char *tiles;
} jb_track_row;

/* JumpyBall.exe Track_DrawFrame 0x0001dd54 reads g_camPixelOfs 0x00061a14,
   g_ballPrevX 0x0006118c, g_rowShift 0x00061930, g_camRow 0x000649b8,
   g_tileGrid 0x00031178, g_mapCols 0x0002628c, g_viewCenterX 0x00026268,
   kTileSize 0x00026290. */
typedef struct {
    float                cam_pixel_ofs;
    float                ball_prev_x;
    const float         *row_shift;
    int                  cam_row;
    const unsigned char *tile_grid;
    int                  map_cols;
    int                  view_center_x;
    int                  tile_size;
    int                  view_bottom;
} jb_track_state;

typedef void (*jb_track_row_fn)(const jb_track_row *row, void *user);

/* JumpyBall.exe Game_Init 0x000113bc, the 0x1c-iteration loop and the
   g_rowHeight 0x000614a8 difference loop that follows it. */
void Track_BuildProjTables(void);

extern int jb_row_proj_ofs[JB_PROJ_TABLE_N + 1]; /* g_rowProjOfs 0x00061438 */
extern int jb_row_height[JB_PROJ_TABLE_N + 1];   /* g_rowHeight 0x000614a8 */

/* JumpyBall.exe Game_Init 0x00011560: 0x12a rows of JB_TEX_VSTEP_STRIDE shorts
   at g_texVStep 0x00035238, row h filled with h entries. */
#define JB_TEX_VSTEP_ROWS 299

void Gfx_BuildTexVStep(void);

#ifdef JB_TABLES_ROM
extern const short jb_tex_vstep[JB_TEX_VSTEP_ROWS * JB_TEX_VSTEP_STRIDE];
#else
extern short jb_tex_vstep[JB_TEX_VSTEP_ROWS * JB_TEX_VSTEP_STRIDE];
#endif

/* JumpyBall.exe Track_DrawFrame 0x0001dd54. */
void Track_DrawFrame(const jb_track_state *st, jb_track_row_fn draw, void *user);

#endif /* JB_TRACK_H */
