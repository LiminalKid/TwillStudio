#!/bin/sh
# Builds the KNULLI package, which needs no PortMaster: unzipped into roms/ports, it's the launcher
# and the twill/ folder.
#
#   cmake --build --preset knulli --target twill && tools/make-knulli-zip.sh [out.zip]
#
# Default output: build/knulli-zip/TwillStudio-KNULLI.zip. The folder and the program have the names
# the PortMaster package uses, so one can be unzipped over the other and the songs stay.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build/knulli-zip/TwillStudio-KNULLI.zip}"
BIN="$ROOT/build/knulli/twill"
[ -f "$BIN" ] || { echo "Build first: cmake --preset knulli && cmake --build --preset knulli"; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/twill"
cp "$ROOT/packaging/knulli/Twill Studio.sh" "$STAGE/"
cp "$ROOT/packaging/knulli/README.txt" "$STAGE/twill/"
cp "$BIN" "$STAGE/twill/twill.aarch64"  # already stripped (the toolchain links with -s)
chmod 755 "$STAGE/Twill Studio.sh" "$STAGE/twill/twill.aarch64"
"$ROOT/tools/copy-licenses.sh" "$STAGE/twill/licenses"
[ -z "$(find "$STAGE" -type l)" ] || { echo "Symlinks in the package"; exit 1; }

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
(cd "$STAGE" && zip -qrX "$OUT" "Twill Studio.sh" twill)
echo "$OUT: $(find "$STAGE" -type f | wc -l | tr -d ' ') files"
unzip -l "$OUT"
