#!/bin/sh
# Builds the muOS (MustardOS) package: a .muxapp, the zip its Archive Manager unpacks into the
# applications folder. It holds one folder, "Twill Studio", with the launcher, the program and its icon.
#
#   cmake --build --preset knulli --target twill && tools/make-muos-app.sh [out.muxapp]
#
# Default output: build/muos/TwillStudio-muOS.muxapp.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build/muos/TwillStudio-muOS.muxapp}"
BIN="$ROOT/build/knulli/twill"
PKG="$ROOT/packaging/muos"
[ -f "$BIN" ] || { echo "Build first: cmake --preset knulli && cmake --build --preset knulli"; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
APP="$STAGE/Twill Studio"
mkdir -p "$APP/glyph"
cp "$PKG/mux_launch.sh" "$PKG/mux_lang.ini" "$PKG/README.txt" "$APP/"
cp "$PKG/twill.png" "$APP/glyph/twill.png"  # mux_launch.sh's ICON
cp "$BIN" "$APP/twill.aarch64"  # already stripped (the toolchain links with -s)
chmod 755 "$APP/mux_launch.sh" "$APP/twill.aarch64"
"$ROOT/tools/copy-licenses.sh" "$APP/licenses"
# Archive Manager refuses a package with links in it.
[ -z "$(find "$STAGE" -type l)" ] || { echo "Symlinks in the package"; exit 1; }

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
(cd "$STAGE" && zip -qrX "$OUT" "Twill Studio")
echo "$OUT: $(find "$STAGE" -type f | wc -l | tr -d ' ') files"
unzip -l "$OUT"
