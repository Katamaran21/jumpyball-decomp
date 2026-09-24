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
    args = ap.parse_args()

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

    out.append('const jb_asset_rom_entry jb_assets_rom_table[] = {')
    for logical, sym, w, h in syms:
        out.append('    { "%s", %s, %d, %d },' % (logical, sym, w, h))
    out.append('};')
    out.append('')
    out.append('const int jb_assets_rom_count =')
    out.append('    (int)(sizeof jb_assets_rom_table / sizeof jb_assets_rom_table[0]);')
    out.append('')

    with open(out_path, 'w', newline='\n') as f:
        f.write('\n'.join(out))

    sys.stderr.write('gen_assets_rom: %d bitmaps, %d bytes decoded -> %s\n'
                     % (len(syms), total, out_path))
    return 0


if __name__ == '__main__':
    sys.exit(main())
