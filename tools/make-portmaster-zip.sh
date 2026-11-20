#!/bin/sh
# Builds the PortMaster package from the KNULLI build: a zip in the layout PortMaster installs (the
# launcher and the twill/ folder at the top, the metadata inside the folder).
#
#   cmake --build --preset knulli --target twill && tools/make-portmaster-zip.sh [out.zip]
#
# Default output: build/portmaster/twill.zip. The package's files are in packaging/portmaster/;
# the licences come from tools/copy-licenses.sh.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build/portmaster/twill.zip}"
BIN="$ROOT/build/knulli/twill"
PKG="$ROOT/packaging/portmaster"
[ -f "$BIN" ] || { echo "Build first: cmake --preset knulli && cmake --build --preset knulli"; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
GAME="$STAGE/twill"
mkdir -p "$GAME"

cp "$PKG/Twill Studio.sh" "$STAGE/"
chmod 755 "$STAGE/Twill Studio.sh"
for f in port.json gameinfo.xml README.md testing_thread.txt screenshot.png cover.png; do
  [ -f "$PKG/twill/$f" ] && cp "$PKG/twill/$f" "$GAME/"
done
cp "$BIN" "$GAME/twill.aarch64"  # already stripped (the toolchain links with -s)
chmod 755 "$GAME/twill.aarch64"

"$ROOT/tools/copy-licenses.sh" "$GAME/licenses"

[ -z "$(find "$STAGE" -type l)" ] || { echo "Symlinks in the package"; exit 1; }
[ -f "$GAME/screenshot.png" ] || echo "Note: no screenshot.png yet (packaging/portmaster/twill/); PortMaster needs one before a PR."

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
# The game folder goes into the zip first: PortMaster puts top-level metadata into the first folder.
(cd "$STAGE" && zip -qrX "$OUT" twill "Twill Studio.sh")
echo "$OUT: $(find "$STAGE" -type f | wc -l | tr -d ' ') files"
unzip -l "$OUT"
