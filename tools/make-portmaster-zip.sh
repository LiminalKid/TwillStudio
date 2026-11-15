#!/bin/sh
# Builds the PortMaster package from the KNULLI build: a zip in the layout PortMaster installs (the
# launcher and the twill/ folder at the top, the metadata inside the folder).
#
#   cmake --build --preset knulli --target twill && tools/make-portmaster-zip.sh [out.zip]
#
# Default output: build/portmaster/twill.zip. The package's files are in packaging/portmaster/;
# the third-party licences are copied from third_party/ so they stay in step with the code.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build/portmaster/twill.zip}"
BIN="$ROOT/build/knulli/twill"
PKG="$ROOT/packaging/portmaster"
[ -f "$BIN" ] || { echo "Build first: cmake --preset knulli && cmake --build --preset knulli"; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
GAME="$STAGE/twill"
mkdir -p "$GAME/licenses"

cp "$PKG/Twill Studio.sh" "$STAGE/"
chmod 755 "$STAGE/Twill Studio.sh"
for f in port.json gameinfo.xml README.md testing_thread.txt screenshot.png cover.png; do
  [ -f "$PKG/twill/$f" ] && cp "$PKG/twill/$f" "$GAME/"
done
cp "$BIN" "$GAME/twill.aarch64"  # already stripped (the toolchain links with -s)
chmod 755 "$GAME/twill.aarch64"

L="$GAME/licenses"
cp "$ROOT/LICENSE" "$L/LICENSE-twillstudio.txt"  # GPL-3.0
cp "$ROOT/assets/factory/LICENSE" "$L/LICENSE-factory-kit.txt"  # CC0
cp "$ROOT/third_party/eurorack/LICENSE" "$L/LICENSE-plaits-clouds.txt"
cp "$ROOT/third_party/eurorack/stmlib/LICENSE" "$L/LICENSE-stmlib.txt"
cp "$ROOT/third_party/fonts/inter/LICENSE.txt" "$L/LICENSE-inter-font.txt"
# dr_wav keeps its licence at the end of its header.
sed -n '/^ALTERNATIVE 1 - Public Domain/,$p' "$ROOT/third_party/dr_libs/dr_wav.h" | sed '$d' |
  { echo "dr_wav (github.com/mackron/dr_libs), used to read WAV files. Either licence applies."; echo; cat; } \
  > "$L/LICENSE-dr_wav.txt"

[ -z "$(find "$STAGE" -type l)" ] || { echo "Symlinks in the package"; exit 1; }
[ -f "$GAME/screenshot.png" ] || echo "Note: no screenshot.png yet (packaging/portmaster/twill/); PortMaster needs one before a PR."

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
# The game folder goes into the zip first: PortMaster puts top-level metadata into the first folder.
(cd "$STAGE" && zip -qrX "$OUT" twill "Twill Studio.sh")
echo "$OUT: $(find "$STAGE" -type f | wc -l | tr -d ' ') files"
unzip -l "$OUT"
