#!/bin/sh
set -e

SDL_VERSION=2.32.10
SDL_DIR=SDL2-$SDL_VERSION
SDL_TARBALL=$SDL_DIR.tar.gz
SDL_URL=https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/$SDL_TARBALL
APP_ID=io.github.katamaran21.jumpyball
VERSION_CODE=3
VERSION_NAME=1.2
MIN_SDK=21
APP_ABI="armeabi-v7a arm64-v8a x86 x86_64"

root=$(cd "$(dirname "$0")/.." && pwd)
work=$root/build-android
deps=$work/.deps
app=$work/app

mkdir -p "$deps"
if [ ! -d "$deps/$SDL_DIR" ]; then
    if [ ! -f "$deps/$SDL_TARBALL" ]; then
        echo "fetching $SDL_TARBALL"
        curl -fsSL -o "$deps/$SDL_TARBALL" "$SDL_URL"
    fi
    tar -xzf "$deps/$SDL_TARBALL" -C "$deps"
fi
sdl=$deps/$SDL_DIR

cp -R "$sdl/android-project/." "$work/"
chmod +x "$work/gradlew"

if [ ! -f "$app/jni/SDL/Android.mk" ]; then
    mkdir -p "$app/jni/SDL"
    cp -R "$sdl/." "$app/jni/SDL/"
fi

mkdir -p "$app/jni/src"
find "$app/jni/src" -maxdepth 1 \( -name '*.c' -o -name '*.h' \) -delete
# The native Win32/WinCE and GBA backends live in the same directory, but their
# files either include <windows.h> or define the same Platform_* symbols as the
# SDL2 backend, and ndk-build compiles whatever it finds in jni/src.
# jb_embed.c is the JB_EMBED asset-in-executable layer; it needs the generated
# jb_embed_data.c and only compiles under -DJB_EMBED.  Android ships the assets
# in the APK (copied into src/main/assets below), so the embed layer has no
# place here.  Android.mk globs *.c, so copying any of these would link it in
# (the GBA backend would collide with jb_platform_sdl2.c's Platform_* symbols).
# Copy everything except those so the glob below cannot pick them up.
for f in "$root"/jb_*.c "$root"/jb_*.h; do
    case ${f##*/} in
    *_win32.c) ;;
    *_gba.c) ;;
    jb_embed.c|jb_embed_data.c) ;;
    *) cp "$f" "$app/jni/src/" ;;
    esac
done
cp "$root/android/Android.mk" "$app/jni/src/Android.mk"
cp "$root/android/AndroidManifest.xml" "$app/src/main/AndroidManifest.xml"
cp "$root/android/strings.xml" "$app/src/main/res/values/strings.xml"

assets=$app/src/main/assets
mkdir -p "$assets"
rm -rf "$assets/BITMAP" "$assets/Sounds" "$assets/Musics"
cp -R "$root/BITMAP" "$root/Sounds" "$root/Musics" "$assets/"

grep -q applicationId "$app/build.gradle" ||
    sed -i "s|^\( *\)minSdkVersion|\1applicationId \"$APP_ID\"\n\1minSdkVersion|" "$app/build.gradle"

sed -i "s|^\( *\)versionCode .*|\1versionCode $VERSION_CODE|;s|^\( *\)versionName .*|\1versionName \"$VERSION_NAME\"|" "$app/build.gradle"

sed -i "s|minSdkVersion [0-9]*|minSdkVersion $MIN_SDK|;s|APP_PLATFORM=android-[0-9]*|APP_PLATFORM=android-$MIN_SDK|" "$app/build.gradle"
sed -i "s|APP_PLATFORM=android-[0-9]*|APP_PLATFORM=android-$MIN_SDK|" "$app/jni/Application.mk"
sed -i "s|^APP_ABI := .*|APP_ABI := $APP_ABI|" "$app/jni/Application.mk"

ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK_LATEST_HOME:-}}}
if [ -n "$ndk" ] && ! grep -q ndkPath "$app/build.gradle"; then
    sed -i "s|^\( *\)compileSdkVersion|\1ndkPath \"$ndk\"\n\1compileSdkVersion|" "$app/build.gradle"
fi

echo "android project ready in $work"
echo "build it with: cd build-android && ./gradlew assembleDebug"
