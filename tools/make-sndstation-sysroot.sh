#!/bin/sh
# Builds the sysroot the sndstation cross build links against: the device's own SDL2, SDL2_ttf,
# SDL2_image and alsa-lib, with the headers and pkg-config files from the Debian 13 (trixie)
# packages of the same versions.
#
#   tools/make-sndstation-sysroot.sh <device-address> [sysroot-dir]
#
# Default sysroot: ~/SDKs/sndstation-aarch64-sysroot. The device needs SSH access by key.
# The versions are what sndstation's trixie image ships; check with: dpkg -l 'libsdl2*' libasound2t64
set -eu
DEVICE="${1:?Give the device, e.g. sndy@192.168.0.50}"
SYSROOT="${2:-$HOME/SDKs/sndstation-aarch64-sysroot}"
POOL=http://deb.debian.org/debian/pool/main
DEBS="libs/libsdl2/libsdl2-dev_2.32.4+dfsg-1_arm64.deb
libs/libsdl2-ttf/libsdl2-ttf-dev_2.24.0+dfsg-2_arm64.deb
libs/libsdl2-image/libsdl2-image-dev_2.8.8+dfsg-1+deb13u1_arm64.deb
a/alsa-lib/libasound2-dev_1.2.14-1+deb13u1_arm64.deb"
LIBS="libSDL2-2.0.so.0 libSDL2_ttf-2.0.so.0 libSDL2_image-2.0.so.0 libasound.so.2"
LIBDIR=usr/lib/aarch64-linux-gnu

rm -rf "$SYSROOT"
mkdir -p "$SYSROOT/$LIBDIR"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

for deb in $DEBS; do
  echo "Fetching ${deb##*/}"
  curl -sfL "$POOL/$deb" -o "$TMP/p.deb"
  (cd "$TMP" && ar x p.deb && tar xf data.tar.* -C "$SYSROOT" usr/include "$LIBDIR" && rm -f p.deb data.tar.* control.tar.* debian-binary)
done
# Static libraries and CMake packages aren't used: the build links the device's shared libraries.
rm -rf "$SYSROOT/$LIBDIR"/*.a "$SYSROOT/$LIBDIR/cmake" "$SYSROOT/$LIBDIR/installed-tests"

# The shared libraries themselves, from the device (the -dev packages' .so links point at them).
for lib in $LIBS; do
  real=$(ssh -o BatchMode=yes "$DEVICE" "readlink -f /$LIBDIR/$lib")
  echo "Copying $real from $DEVICE"
  scp -q "$DEVICE:$real" "$SYSROOT/$LIBDIR/"
  ln -sf "${real##*/}" "$SYSROOT/$LIBDIR/$lib"
done

# pkg-config files for what SDL2_ttf and SDL2_image name as private requirements: only their
# existence is checked when linking against the shared libraries.
for pc in harfbuzz freetype2 libpng libjpeg libtiff-4 libwebpdemux libwebp; do
  printf 'Name: %s\nDescription: placeholder for the cross build\nVersion: 99\n' "$pc" \
    > "$SYSROOT/$LIBDIR/pkgconfig/$pc.pc"
done
echo "Sysroot ready in $SYSROOT"
