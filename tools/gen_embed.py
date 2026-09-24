#!/usr/bin/env python3
"""Generate jb_embed_data.c from the BITMAP/Sounds/Musics asset folders.

The output is a C89 source: one static byte array per asset plus a
jb_embed_table[] mapping the logical forward-slash path ("BITMAP/261.bmp") to
that array.  jb_embed.c looks paths up in it, so a JB_EMBED build carries every
asset inside the executable and needs no BITMAP/Sounds/Musics folder beside it.

Usage: gen_embed.py [--root DIR] [--out FILE]
Defaults: root = repo root (parent of tools/), out = <root>/jb_embed_data.c
"""
import argparse
import os
import sys

DIRS = (("BITMAP", (".bmp",)),
        ("Sounds", (".wav",)),
        ("Musics", (".tkm",)))


def collect(root):
    items = []
    for sub, exts in DIRS:
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(exts):
                continue
            items.append((sub + "/" + name, os.path.join(d, name)))
    return items


def emit_array(out, sym, data):
    out.write("static const unsigned char %s[] = {" % sym)
    for i, b in enumerate(data):
        if i % 16 == 0:
            out.write("\n    ")
        out.write("%d," % b)
    out.write("\n};\n\n")


def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    default_root = os.path.dirname(here)
    ap.add_argument("--root", default=default_root)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    root = args.root
    out_path = args.out or os.path.join(root, "jb_embed_data.c")

    items = collect(root)
    if not items:
        sys.stderr.write("gen_embed: no assets found under %s\n" % root)
        return 1

    with open(out_path, "w", newline="\n") as out:
        out.write('#include "jb_embed.h"\n\n')
        syms = []
        for idx, (logical, disk) in enumerate(items):
            with open(disk, "rb") as f:
                data = f.read()
            sym = "jb_e_%d" % idx
            syms.append((logical, sym, len(data)))
            emit_array(out, sym, data)

        out.write("const jb_embed_entry jb_embed_table[] = {\n")
        for logical, sym, length in syms:
            out.write('    { "%s", %s, %d },\n' % (logical, sym, length))
        out.write("};\n\n")
        out.write("const int jb_embed_count =\n")
        out.write("    (int)(sizeof jb_embed_table / sizeof jb_embed_table[0]);\n")

    total = sum(length for _, _, length in syms)
    sys.stderr.write("gen_embed: %d assets, %d bytes -> %s\n"
                     % (len(syms), total, out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
