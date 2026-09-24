#!/bin/sh
# Web (WebAssembly) build via Emscripten.
#
# The SDL2 backend is reused unchanged: Emscripten ships its own SDL2 port
# (-sUSE_SDL=2), so jb_platform_sdl2.c / jb_audio_sdl2.c / jb_touch_sdl2.c
# compile and link against it exactly as on the desktop.  Assets are always
# embedded (JB_EMBED) - a browser has no folder beside the executable to read
# BITMAP/Sounds/Musics from, so gen_embed.py packs them into the wasm.
#
# Needs the emsdk on PATH (emcc).  Override the output dir with
#   OUT=/path sh tools/build_web.sh
set -e

CC=${CC:-emcc}
OUT=${OUT:-build-web}
PYTHON=${PYTHON:-python3}

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "no $CC - install and activate the emsdk (emsdk_env.sh) first" >&2
    exit 1
fi

cd "$(dirname "$0")/.."

# jb_main.c drives the loop through emscripten_set_main_loop when __EMSCRIPTEN__
# is defined (emcc defines it); gnu89 matches the desktop dialect the port is
# written in without tripping over SDL/emscripten headers.
CFLAGS="-O2 -Wall -Wextra -std=gnu89 -fno-strict-aliasing -sUSE_SDL=2 -DJB_EMBED"
LDFLAGS="-sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1"

SRC="jb_appassets.c jb_assets.c jb_audio.c jb_ball.c jb_bmp.c jb_gfx.c \
jb_gfx_tile.c jb_keyconfig.c jb_level.c jb_main.c jb_menu.c jb_mod.c \
jb_mod_effects.c jb_player.c jb_stage.c jb_text.c jb_track.c \
jb_trackrow_forest.c jb_trackrow_grass.c jb_trackrow_ice_alt.c \
jb_trackrow_sky.c jb_trackrow_tiled.c jb_platform_sdl2.c jb_audio_sdl2.c \
jb_touch_sdl2.c jb_embed.c jb_embed_data.c"

"$PYTHON" tools/gen_embed.py --root . --out jb_embed_data.c

rm -rf "$OUT"
mkdir -p "$OUT"

# shellcheck disable=SC2086
"$CC" $CFLAGS -o "$OUT/index.html" $SRC $LDFLAGS --shell-file tools/web_shell.html

echo "BUILD OK: $OUT/index.html (+ index.js, index.wasm)"
echo "Serve the dir over HTTP, e.g. 'python3 -m http.server -d $OUT', then open"
echo "http://localhost:8000/ - opening index.html from file:// will not load wasm."
