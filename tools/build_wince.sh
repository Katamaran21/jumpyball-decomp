#!/bin/sh
# Windows CE (Pocket PC / Windows Mobile) build with cegcc's mingw32ce.
#
# The native backend is the only option here: there is no SDL2 for Windows CE.
# jb_platform_win32.c draws through GDI and jb_audio_win32.c plays through
# waveOut, and both live in coredll.dll on the device.
#
# The toolchain prefix defaults to the usual cegcc install; override it with
#   CEGCC=/path/to/mingw32ce sh tools/build_wince.sh
# and pick the target arch with TARGET=arm-mingw32ce (or i386-mingw32ce).
set -e

CEGCC=${CEGCC:-/opt/mingw32ce}
TARGET=${TARGET:-arm-mingw32ce}
CC=${CC:-$CEGCC/bin/$TARGET-gcc}
STRIP=${STRIP:-$CEGCC/bin/$TARGET-strip}
OUT=${OUT:-build-wince}

if [ ! -x "$CC" ]; then
    echo "no $CC - set CEGCC to the cegcc prefix or TARGET to the arch" >&2
    exit 1
fi

cd "$(dirname "$0")/.."

# Assets are copied next to the .exe by default.  Set EMBED=1 to instead pack
# BITMAP/Sounds/Musics into the executable (tools/gen_embed.py -> jb_embed_data.c,
# jb_embed.c resolves paths against it) so a single self-contained jumpyball.exe
# runs on a device with nothing beside it.
EMBED=${EMBED:-0}
PYTHON=${PYTHON:-python3}

# cegcc's windows.h uses "inline" in kfuncs.h, which -std=c89 rejects, so the
# port is compiled as gnu89: the same C89 code, with the keyword still a
# keyword.  tools/c89check.cmd and the Linux CI job cover strict conformance.
CFLAGS="-O2 -Wall -Wextra -std=gnu89 -DJB_BACKEND_WIN32 -fno-strict-aliasing"
LDFLAGS="-lcoredll -lm"

SRC="jb_appassets.c jb_assets.c jb_audio.c jb_ball.c jb_bmp.c jb_gfx.c \
jb_gfx_tile.c jb_keyconfig.c jb_level.c jb_main.c jb_menu.c jb_mod.c \
jb_mod_effects.c jb_player.c jb_stage.c jb_text.c jb_track.c \
jb_trackrow_forest.c jb_trackrow_grass.c jb_trackrow_ice_alt.c \
jb_trackrow_sky.c jb_trackrow_tiled.c jb_platform_win32.c jb_audio_win32.c \
jb_touch_win32.c"

if [ "$EMBED" != "0" ]; then
    "$PYTHON" tools/gen_embed.py --root . --out jb_embed_data.c
    CFLAGS="$CFLAGS -DJB_EMBED"
    SRC="$SRC jb_embed.c jb_embed_data.c"
fi

rm -rf "$OUT"
mkdir -p "$OUT"

# shellcheck disable=SC2086
"$CC" $CFLAGS -o "$OUT/jumpyball.exe" $SRC $LDFLAGS
"$STRIP" "$OUT/jumpyball.exe"

if [ "$EMBED" != "0" ]; then
    echo "BUILD OK (assets embedded): $OUT/jumpyball.exe"
    echo "Copy the single jumpyball.exe to the device and run it - no folders needed."
else
    for dir in BITMAP Sounds Musics; do
        mkdir -p "$OUT/$dir"
        cp "$dir"/* "$OUT/$dir/"
    done

    echo "BUILD OK: $OUT/jumpyball.exe"
    echo "Copy $OUT to the device, e.g. \\Program Files\\JumpyBall, and run"
    echo "jumpyball.exe there - it looks for BITMAP/Sounds/Musics next to itself."
fi
