#!/bin/sh
# Builds Twill Studio for the TrimUI Brick's stock menu from the handheld build: a zip holding
# Apps/Twill/ (config.json, icon.png, launch.sh, the program, the README and the licences), to unzip
# at the top of the SD card. The menu lists it under Apps; songs go in TwillStudio/ on the card, as
# with the NextUI pak, which shares the launcher.
#
#     cmake --build --preset knulli --target twill && tools/make-trimui-app.sh [out.zip]
#
# Default output: build/trimui/TwillStudio-TrimUI.zip.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build/trimui/TwillStudio-TrimUI.zip}"
BIN="$ROOT/build/knulli/twill"
[ -f "$BIN" ] || { echo "Build first: cmake --preset knulli && cmake --build --preset knulli"; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
APP="$STAGE/Apps/Twill"
mkdir -p "$APP"
cp "$ROOT/packaging/trimui/config.json" "$ROOT/packaging/trimui/icon.png" "$APP/"
cp "$ROOT/packaging/nextui/launch.sh" "$ROOT/packaging/nextui/README.md" "$APP/"
cp "$BIN" "$APP/twill.aarch64"  # already stripped (the toolchain links with -s)
chmod 755 "$APP/launch.sh" "$APP/twill.aarch64"
"$ROOT/tools/copy-licenses.sh" "$APP/licenses"
[ -z "$(find "$STAGE" -type l)" ] || { echo "Symlinks in the package"; exit 1; }

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
(cd "$STAGE" && zip -qrX "$OUT" Apps)
echo "$OUT: $(find "$STAGE" -type f | wc -l | tr -d ' ') files"
unzip -l "$OUT"
