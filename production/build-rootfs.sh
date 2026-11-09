#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=2025.02.19
work="$root/build/buildroot"
mkdir -p "$work/downloads"
archive="$work/downloads/buildroot-$version.tar.xz"
checksum=e5e58bff1da91f022f60cefdfe280140c08023cd0aeec1e5f6f9da0ea866d69c
verify() {
 if command -v sha256sum >/dev/null 2>&1; then
  printf '%s  %s\n' "$checksum" "$1" | sha256sum -c -
 else
  printf '%s  %s\n' "$checksum" "$1" | shasum -a 256 -c -
 fi
}
if [ ! -s "$archive" ]; then
 curl -fL --retry 3 "https://buildroot.org/downloads/buildroot-$version.tar.xz" -o "$archive.part"
 verify "$archive.part"
 mv "$archive.part" "$archive"
fi
verify "$archive"
docker build -t minpocket-source-builder:bookworm -f "$root/production/Dockerfile" "$root/production"
docker run --rm -e BUILDROOT_VERSION="$version" -v "$root:/src:ro" -v "$work:/work" \
 minpocket-source-builder:bookworm sh -ec '
 if [ ! -f /work/source/Makefile ]; then
  mkdir -p /work/source
  tar -xJf "/work/downloads/buildroot-$BUILDROOT_VERSION.tar.xz" --strip-components=1 -C /work/source
 fi
 make -C /work/source O=/work/output BR2_EXTERNAL=/src/production minpocket_mp1_defconfig
 make -C /work/source O=/work/output -j"$(nproc)"
 test -x /work/output/target/sbin/minpocket-launcher
 test "$(readlink /work/output/target/init)" = sbin/minpocket-launcher
 test ! -e /work/output/target/bin/sh
 test ! -e /work/output/target/bin/busybox
 '
printf 'Userspace archive: %s/output/images/rootfs.cpio.gz\n' "$work"
