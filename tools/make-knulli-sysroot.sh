#!/bin/sh
# Builds the small sysroot the KNULLI cross build links against: the device's own SDL2 library
# and the headers from the same SDL release.
#
#   tools/make-knulli-sysroot.sh <device-address> [sysroot-dir]
#
# Default sysroot: ~/SDKs/knulli-aarch64-sysroot. Asks for the device's root password (KNULLI's default is "linux").
set -eu
DEVICE="${1:?Give the device, e.g. root@192.168.0.50}"
SYSROOT="${2:-$HOME/SDKs/knulli-aarch64-sysroot}"
SDL_VERSION=2.32.8   # what KNULLI 20260921 ships; check with: ls /usr/lib/libSDL2-2.0.so.0.*

mkdir -p "$SYSROOT/usr/lib" "$SYSROOT/usr/include/SDL2"
LIB="libSDL2-2.0.so.0.$(echo $SDL_VERSION | awk -F. '{print $2 * 100 "." $3}')"
echo "Copying /usr/lib/$LIB from $DEVICE"
sftp -q "$DEVICE:/usr/lib/$LIB" "$SYSROOT/usr/lib/"
ln -sf "$LIB" "$SYSROOT/usr/lib/libSDL2-2.0.so.0"
ln -sf libSDL2-2.0.so.0 "$SYSROOT/usr/lib/libSDL2.so"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
curl -sL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz" \
  | tar xz -C "$TMP"
cp "$TMP/SDL2-$SDL_VERSION"/include/*.h "$SYSROOT/usr/include/SDL2/"
echo "Sysroot ready in $SYSROOT"
