#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
out=${1:-build/native}
mkdir -p "$out"
compiler=${CC:-cc}
drm_flags=${MP_DRM_CFLAGS:-$(pkg-config --cflags libdrm)}
common='native/runtime-home.c native/vendor/lz4/lz4.c native/vendor/lz4/lz4hc.c native/asset-cache.c native/asset-codec.c native/vendor/near-lossless/near-lossless.c native/asset-platform.c native/home.c native/home-view.c native/scene.c native/game-art.c native/home-layout-default.c native/font.c native/ui.c native/style.c native/theme-default.c'
"$compiler" ${CFLAGS:--O2} -pthread -Wall -Wextra -Werror -Inative $drm_flags \
 native/launcher.c $common native/display.c native/dev.c native/battery.c \
 native/wifi.c native/wifi-linux.c native/status-bar.c native/indicators/battery.c \
 native/indicators/wifi.c native/indicators/clock.c native/indicators/inspector.c \
 ${LDFLAGS:-} -ldrm -o "$out/minpocket-launcher"
"$compiler" ${CFLAGS:--O2} -pthread -Wall -Wextra -Werror -DMP_LIBRARY_TOOL -Inative \
 $common ${LDFLAGS:-} -o "$out/minpocket-library"
