#!/bin/sh
# Cross-compile the examples for the RG DS Plus (aarch64, glibc) on a Debian/Ubuntu host.
#
#   tools/cross_build.sh [SYSROOT_DIR [OUT_DIR]]      defaults: ./build/sysroot-arm64  ./build/aarch64
#
# Prerequisites (once, see docs/CROSS_COMPILE.md): `gcc-aarch64-linux-gnu` installed and an arm64
# apt source configured, so `apt-get download <pkg>:arm64` works. Nothing is installed for arm64:
# the needed -dev packages are only downloaded and unpacked into SYSROOT_DIR.
#
# dualscreen (SDL2) links against an SDL2 library: set STOCK_SDL2_DIR to a directory containing the
# device's libSDL2-2.0.so.0 (for example usr/lib of the extracted stock rootfs). Without it that
# example is skipped.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
SR=$(mkdir -p "${1:-$HERE/build/sysroot-arm64}" && cd "${1:-$HERE/build/sysroot-arm64}" && pwd)
OUT=$(mkdir -p "${2:-$HERE/build/aarch64}" && cd "${2:-$HERE/build/aarch64}" && pwd)
CC=${CROSS_COMPILE:-aarch64-linux-gnu-}gcc
command -v "$CC" >/dev/null || { echo "$CC not found (apt install gcc-aarch64-linux-gnu)"; exit 1; }

if [ ! -f "$SR/usr/include/gbm.h" ]; then
    DEBS=$(mktemp -d)
    ( cd "$DEBS" && apt-get download -q \
        libdrm-dev:arm64 libdrm2:arm64 libgbm-dev:arm64 libgbm1:arm64 libegl-dev:arm64 libegl1:arm64 \
        libgles-dev:arm64 libgles2:arm64 libglvnd-dev:arm64 libglvnd0:arm64 libgl-dev:arm64 \
        libsdl2-dev:arm64 libpciaccess0:arm64 libwayland-server0:arm64 libexpat1:arm64 ) >/dev/null
    for d in "$DEBS"/*.deb; do dpkg -x "$d" "$SR"; done
    rm -rf "$DEBS"
fi

L=$SR/usr/lib/aarch64-linux-gnu
INC="-I$SR/usr/include -I$SR/usr/include/libdrm -I$SR/usr/include/aarch64-linux-gnu"
LNK="-L$L -Wl,-rpath-link,$L"
CFLAGS="-O2 -Wall -Wextra"
cd "$HERE/examples"

$CC $CFLAGS $INC -o "$OUT/kms_dualscreen" kms_dualscreen/kms_dualscreen.c $LNK -ldrm
$CC $CFLAGS $INC -o "$OUT/gbm_egl_dualscreen" gbm_egl_dualscreen/gbm_egl_dualscreen.c \
    gbm_egl_dualscreen/scene.c $LNK -ldrm -lgbm -lEGL -lGLESv2
if [ -n "$STOCK_SDL2_DIR" ]; then
    $CC $CFLAGS $INC -I"$SR/usr/include/SDL2" -D_REENTRANT -o "$OUT/dualscreen" dualscreen/dualscreen.c \
        -L"$STOCK_SDL2_DIR" -Wl,-rpath-link,"$STOCK_SDL2_DIR" -lSDL2
else
    echo "STOCK_SDL2_DIR not set: skipping dualscreen (SDL2)"
fi

echo "built into $OUT:"
for f in "$OUT"/*; do
    printf '  %s  needs glibc %s\n' "$(basename "$f")" \
        "$(${CROSS_COMPILE:-aarch64-linux-gnu-}objdump -T "$f" | grep -oE 'GLIBC_[0-9.]+' | sort -V | tail -1)"
done
