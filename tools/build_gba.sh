#!/bin/sh
# Game Boy Advance build with devkitARM.
#
# The GBA has no SDL and no filesystem, so this uses the native backend
# (jb_platform_gba.c: Mode 3 framebuffer, KEYINPUT, cascaded Timer2/3) with a
# silent audio stub (jb_audio_gba.c), and assets are ALWAYS embedded into the
# ROM (tools/gen_embed.py -> jb_embed_data.c) since there is nowhere to read
# files from on a cartridge.
#
# Toolchain defaults to a standard devkitPro install; override with
#   DEVKITARM=/path/to/devkitARM DEVKITPRO=/path/to/devkitpro sh tools/build_gba.sh
set -e

DEVKITARM=${DEVKITARM:-/opt/devkitpro/devkitARM}
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
CC=${CC:-$DEVKITARM/bin/arm-none-eabi-gcc}
OBJCOPY=${OBJCOPY:-$DEVKITARM/bin/arm-none-eabi-objcopy}
GBAFIX=${GBAFIX:-$DEVKITPRO/tools/bin/gbafix}
OUT=${OUT:-build-gba}
PYTHON=${PYTHON:-python3}

if [ ! -x "$CC" ]; then
    echo "no $CC - set DEVKITARM to the devkitARM prefix" >&2
    exit 1
fi

cd "$(dirname "$0")/.."

# devkitARM's newlib headers use "inline" in system headers, which -std=c89
# rejects, so the port is compiled as gnu89: the same C89 code with the keyword
# still a keyword.  tools/c89check.cmd and the Linux CI job cover strict C89.
#
# -specs=gba.specs goes on the link line ONLY (devkitPro convention): it pulls
# in sync-none.specs, and passing it twice in one gcc invocation makes that
# nested spec redefine 'link' and abort with "already defined spec".
CFLAGS="-mthumb -mthumb-interwork -O2 -Wall -Wextra -std=gnu89 \
-DJB_BACKEND_GBA -DJB_EMBED -DJB_TABLES_ROM -fno-strict-aliasing"
LDFLAGS="-specs=gba.specs"

SRC="jb_appassets.c jb_assets.c jb_audio.c jb_ball.c jb_bmp.c jb_gfx.c \
jb_gfx_tile.c jb_keyconfig.c jb_level.c jb_main.c jb_menu.c jb_mod.c \
jb_mod_effects.c jb_player.c jb_stage.c jb_text.c jb_track.c \
jb_trackrow_forest.c jb_trackrow_grass.c jb_trackrow_ice_alt.c \
jb_trackrow_sky.c jb_trackrow_tiled.c jb_platform_gba.c jb_audio_gba.c \
jb_embed.c jb_embed_data.c jb_tables_rom.c"

"$PYTHON" tools/gen_embed.py --root . --out jb_embed_data.c
"$PYTHON" tools/gen_tables.py --out jb_tables_rom.c

rm -rf "$OUT"
mkdir -p "$OUT"

# shellcheck disable=SC2086
"$CC" $CFLAGS -o "$OUT/jumpyball.elf" $SRC $LDFLAGS
"$OBJCOPY" -O binary "$OUT/jumpyball.elf" "$OUT/jumpyball.gba"

# gbafix writes the Nintendo logo and header checksum a real cartridge needs;
# emulators run the raw binary without it, so a missing gbafix is a warning.
if [ -x "$GBAFIX" ]; then
    "$GBAFIX" "$OUT/jumpyball.gba"
else
    echo "warning: no $GBAFIX - $OUT/jumpyball.gba has no valid header" >&2
fi

echo "BUILD OK (assets embedded): $OUT/jumpyball.gba"
echo "Load jumpyball.gba in an emulator (mGBA) or flash it to a cartridge."
