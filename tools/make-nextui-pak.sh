#!/bin/sh
# Builds the NextUI pak from the handheld build: a zip of the pak folder's contents (launch.sh, the
# program, the README and the licences), which the Pak Store unzips into
# Tools/<platform>/Twill Studio.pak/. Its name is pak.json's release_filename.
#
#     cmake --build --preset knulli --target twill && tools/make-nextui-pak.sh [out.zip]
#
# Default output: build/nextui/TwillStudio.pak.zip. The pak's own files are in packaging/nextui/.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build/nextui/TwillStudio.pak.zip}"
BIN="$ROOT/build/knulli/twill"
PKG="$ROOT/packaging/nextui"
[ -f "$BIN" ] || { echo "Build first: cmake --preset knulli && cmake --build --preset knulli"; exit 1; }

# pak.json must name this file and carry the version being built, or the Pak Store won't find it.
v=$(sed -n 's/^project(TwillStudio VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
grep -q "\"version\": \"v$v\"" "$ROOT/pak.json" || { echo "pak.json's version isn't v$v (CMakeLists.txt)"; exit 1; }
grep -q "\"release_filename\": \"$(basename "$OUT")\"" "$ROOT/pak.json" || echo "Note: pak.json's release_filename isn't $(basename "$OUT")"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp "$PKG/launch.sh" "$PKG/README.md" "$STAGE/"
cp "$BIN" "$STAGE/twill.aarch64"  # already stripped (the toolchain links with -s)
chmod 755 "$STAGE/launch.sh" "$STAGE/twill.aarch64"
"$ROOT/tools/copy-licenses.sh" "$STAGE/licenses"
[ -z "$(find "$STAGE" -type l)" ] || { echo "Symlinks in the package"; exit 1; }

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
(cd "$STAGE" && zip -qrX "$OUT" .)
echo "$OUT: $(find "$STAGE" -type f | wc -l | tr -d ' ') files"
unzip -l "$OUT"
