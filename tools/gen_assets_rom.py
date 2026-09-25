#!/usr/bin/env python3
# Emit every BITMAP/*.bmp decoded to RGB565 as a const C array for the GBA
# port, so the sprites land in cartridge ROM (.rodata) instead of being
# malloc-decoded into the console's ~256 KB of RAM.  On the desktop/SDL2/win32/
# wince/android backends Bmp_LoadSprite (jb_bmp.c) reads the file and mallocs a
# decoded surface at runtime; on the GBA the resident decoded surfaces total
# ~1.1 MB, roughly 4x the whole 288 KB of GBA RAM, so JB_ASSETS_ROM swaps that
# runtime decode for the const arrays this script generates and Bmp_LoadSprite
# points the sprite straight at ROM.
#
# The decode is a byte-exact port of jb_bmp.c Bmp_LoadSprite plus jb_gfx.c
# Color_Pack16 (fmt 0x80 / RGB565 branch), so a ROM sprite equals what the
# runtime decode would have produced - the font marker scan (jb_text.c) and the
# tile-map colour tests (jb_level.c) compare against the same packing and must
# match bit for bit.
import argparse
import os
import sys


def rd16(b, o):
    return b[o] | (b[o + 1] << 8)


def rd32(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


def pack16(colorref):
    # jb_gfx.c Color_Pack16 0x00023bb4, fmt 0x80 branch; colorref is 0x00BBGGRR.
    v = ((((colorref & 0xffff) >> 8) & 0xfff8) | ((colorref & 0xf8) << 5)) << 3
    return (v | (((colorref >> 16) & 0xff) >> 3)) & 0xffff


def sample_index(row, bpp, x):
    # jb_bmp.c SampleIndex.
    if bpp == 8:
        return row[x]
    if bpp == 4:
        return (row[x >> 1] & 0xf) if (x & 1) else (row[x >> 1] >> 4)
    return (row[x >> 3] >> (7 - (x & 7))) & 1


def indexed(pal, idx):
    # jb_bmp.c Indexed: palette is BGRA, assembled as (B<<16)|(G<<8)|R.
    return (pal[idx * 4] << 16) | (pal[idx * 4 + 1] << 8) | pal[idx * 4 + 2]


def decode(data):
    # jb_bmp.c Bmp_LoadSprite: BI_RGB (comp 0), 1/4/8/24 bpp, bottom-up unless
    # the height is negative.  Returns (w, h, [rgb565]) or None on reject.
    if len(data) < 54 or data[0] != ord('B') or data[1] != ord('M'):
        return None
    off = rd32(data, 10)
    hdr = rd32(data, 14)
    w = rd32(data, 18)
    h = rd32(data, 22)
    if w >= 0x80000000:
        w -= 0x100000000
    if h >= 0x80000000:
        h -= 0x100000000
    bpp = rd16(data, 28)
    comp = rd32(data, 30)
    top_down = 0
    if h < 0:
        h = -h
        top_down = 1
    pal_bytes = (4 << bpp) if bpp < 24 else 0
    stride = (((w * bpp) + 31) // 32) * 4
    pal_off = 14 + hdr
    if (w <= 0 or h <= 0 or comp != 0 or hdr < 40 or
            bpp not in (1, 4, 8, 24) or
            14 + hdr + pal_bytes > len(data) or
            off + stride * h > len(data)):
        return None
    pal = data[pal_off:]
    px = [0] * (w * h)
    for y in range(h):
        row = data[off + stride * (y if top_down else h - 1 - y):]
        base = y * w
        for x in range(w):
            if bpp == 24:
                c = ((row[x * 3] << 16) | (row[x * 3 + 1] << 8) | row[x * 3 + 2])
            else:
                c = indexed(pal, sample_index(row, bpp, x))
            px[base + x] = pack16(c)
    return w, h, px


def downscale_nearest(w, h, px, s):
    # Point-sample every s-th pixel on each axis, matching jb_platform_gba.c
    # Platform_Present 0x00021698, which samples every 2nd pixel when it packs
    # the 240x320 buffer into the 120x160 Mode 3 framebuffer.
    nw = w // s
    nh = h // s
    out = [0] * (nw * nh)
    for y in range(nh):
        srow = (y * s) * w
        drow = y * nw
        for x in range(nw):
            out[drow + x] = px[srow + x * s]
    return nw, nh, out


def halve_small_font(w, h, px, s):
    # jb_text.c ScanMarkerPairs 0x00011a30 reads row 0 of BITMAP/254 for the
    # 0xff00ff marker (packed 0xf81f) and takes each marker pair as one glyph
    # (glyph_x = open + 1, glyph_w = close - open - 1).  Rebuild the sheet at
    # half glyph size: downsample each glyph's pixels and re-lay the marker
    # pairs so the same scan yields halved glyph_x / glyph_w with no code change.
    marker = pack16(0x00ff00ff)
    glyphs = []
    pair = 0
    start = 0
    for x in range(min(w, 0x4ba)):
        if px[x] != marker:
            continue
        if pair == 0:
            start = x + 1
            pair = 1
        else:
            glyphs.append((start, x - start))
            pair = 0

    nh = h // s
    cols = []
    for gx, gw in glyphs:
        ngw = max(1, (gw + 1) // s)
        cols.append(('m', 0))
        for k in range(ngw):
            cols.append(('g', gx + k * s))
        cols.append(('m', 0))
    nw = len(cols)
    out = [0] * (nw * nh)
    for cx, (kind, src_x) in enumerate(cols):
        for y in range(nh):
            out[y * nw + cx] = marker if kind == 'm' else px[(y * s) * w + src_x]
    return nw, nh, out


def emit_array(out, sym, values):
    out.append('static const unsigned short %s[%d] = {' % (sym, len(values)))
    line = []
    for v in values:
        line.append(str(v))
        if len(line) == 20:
            out.append('    ' + ','.join(line) + ',')
            line = []
    if line:
        out.append('    ' + ','.join(line) + ',')
    out.append('};')
    out.append('')


def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument('--root', default=os.path.dirname(here))
    ap.add_argument('--out', default=None)
    # BITMAP basenames (no extension) to additionally emit at half size for the
    # GBA native-120x160 menu; BITMAP/254 (the small font) gets marker-aware
    # reconstruction, the rest a point-sampled halving.
    ap.add_argument('--half', default='')
    args = ap.parse_args()

    half_set = set(n for n in args.half.split(',') if n)

    root = args.root
    out_path = args.out or os.path.join(root, 'jb_assets_rom_data.c')
    bmp_dir = os.path.join(root, 'BITMAP')
    if not os.path.isdir(bmp_dir):
        sys.stderr.write('gen_assets_rom: no BITMAP dir under %s\n' % root)
        return 1

    names = sorted(n for n in os.listdir(bmp_dir) if n.lower().endswith('.bmp'))
    if not names:
        sys.stderr.write('gen_assets_rom: no .bmp under %s\n' % bmp_dir)
        return 1

    out = ['#include "jb_assets_rom.h"', '']
    syms = []
    half_syms = []
    total = 0
    for idx, name in enumerate(names):
        with open(os.path.join(bmp_dir, name), 'rb') as f:
            data = f.read()
        dec = decode(data)
        if dec is None:
            sys.stderr.write('gen_assets_rom: cannot decode %s\n' % name)
            return 1
        w, h, px = dec
        sym = 'jb_ar_%d' % idx
        emit_array(out, sym, px)
        syms.append(('BITMAP/' + name, sym, w, h))
        total += w * h * 2

        base = os.path.splitext(name)[0]
        if base in half_set:
            if base == '254':
                hw, hh, hpx = halve_small_font(w, h, px, 2)
            else:
                hw, hh, hpx = downscale_nearest(w, h, px, 2)
            hsym = 'jb_arh_%d' % idx
            emit_array(out, hsym, hpx)
            half_syms.append(('BITMAP/' + name, hsym, hw, hh))

    out.append('const jb_asset_rom_entry jb_assets_rom_table[] = {')
    for logical, sym, w, h in syms:
        out.append('    { "%s", %s, %d, %d },' % (logical, sym, w, h))
    out.append('};')
    out.append('')
    out.append('const int jb_assets_rom_count =')
    out.append('    (int)(sizeof jb_assets_rom_table / sizeof jb_assets_rom_table[0]);')
    out.append('')

    out.append('const jb_asset_rom_entry jb_assets_rom_half_table[] = {')
    for logical, sym, w, h in half_syms:
        out.append('    { "%s", %s, %d, %d },' % (logical, sym, w, h))
    out.append('};')
    out.append('')
    out.append('const int jb_assets_rom_half_count =')
    out.append('    (int)(sizeof jb_assets_rom_half_table / sizeof jb_assets_rom_half_table[0]);')
    out.append('')

    with open(out_path, 'w', newline='\n') as f:
        f.write('\n'.join(out))

    sys.stderr.write('gen_assets_rom: %d bitmaps (%d half), %d bytes decoded -> %s\n'
                     % (len(syms), len(half_syms), total, out_path))
    return 0


if __name__ == '__main__':
    sys.exit(main())
