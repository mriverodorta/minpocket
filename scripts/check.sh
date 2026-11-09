#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
out=build/tests
mkdir -p "$out"
python3 design/compile-home.py --check
python3 design/compile-theme.py --check
python3 design/compile-navigation.py --check
python3 native/fonts/monogram/generate.py --check
python3 boot/test-startup-cache.py
compiler=${CC:-cc}
common='native/asset-cache.c native/asset-codec.c native/vendor/lz4/lz4.c native/vendor/lz4/lz4hc.c native/vendor/near-lossless/near-lossless.c native/home.c native/home-view.c native/home-layout-default.c native/game-art.c native/scene.c native/font.c native/ui.c native/style.c native/theme-default.c native/status-bar.c native/battery.c native/wifi.c native/indicators/battery.c native/indicators/wifi.c native/indicators/clock.c'
for name in scene style font battery clock status-registry home-flex home-captions asset-cache; do
 "$compiler" ${CFLAGS:--O1 -g} -pthread -Wall -Wextra -Werror -DMP1_TEST_CATALOG -Inative \
  "native/tests/$name.c" $common ${LDFLAGS:-} -o "$out/$name"
 "$out/$name"
done
