#!/bin/sh
# Assembles the website (GitHub Pages) in build/site: the landing page from site/, the fonts and the
# logo images, and the web build as play/. Build the web version first (BUILDING.md).
#
#     tools/make-site.sh [web build folder, default build/web]
#     cd build/site && python3 -m http.server 8000     # then open http://localhost:8000/
#
# The web build's files get the commit as a query (twill.js?v=abc1234, and twill.wasm through
# locateFile in web/shell.html), so a browser can't pair a cached file from one build with
# another build's.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WEB=${1:-$ROOT/build/web}
OUT=$ROOT/build/site
VERSION=$(sed -n 's/^project(TwillStudio VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
BUILD=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || date +%s)
[ -n "$VERSION" ] || { echo "no version in CMakeLists.txt" >&2; exit 1; }
for f in twill.html twill.js twill.wasm; do
  [ -f "$WEB/$f" ] || { echo "$WEB/$f is missing: build the web version first" >&2; exit 1; }
done

rm -rf "$OUT"
mkdir -p "$OUT/play" "$OUT/fonts" "$OUT/images/logo"
cp -R "$ROOT/site/." "$OUT/"
sed "s/@VERSION@/$VERSION/g" "$ROOT/site/index.html" > "$OUT/index.html"

# The fonts, with their licences.
F="$ROOT/third_party/fonts"
cp "$F/barlow-condensed/BarlowCondensed-Bold.ttf" "$F/barlow-condensed/BarlowCondensed-SemiBold.ttf" \
   "$F/inter/Inter-Regular.ttf" "$F/inter/Inter-SemiBold.ttf" "$OUT/fonts/"
cp "$F/barlow-condensed/OFL.txt" "$OUT/fonts/LICENSE-Barlow-Condensed.txt"
cp "$F/inter/LICENSE.txt" "$OUT/fonts/LICENSE-Inter.txt"

# The logo images the pages use (the icon and the picture shown when the site is linked).
for f in twill-logo-dark-square-512.png twill-logo-dark-social-1280x640.png; do
  cp "$ROOT/Docs/images/logo/$f" "$OUT/images/logo/"
done

# The app.
sed -E -e "s/@TWILL_BUILD@/$BUILD/" \
       -e "s/src=\"?twill\\.js\"?/src=\"twill.js?v=$BUILD\"/" \
       "$WEB/twill.html" > "$OUT/play/index.html"
grep -q "\"$BUILD\"" "$OUT/play/index.html" && grep -q "twill.js?v=$BUILD" "$OUT/play/index.html" \
  || { echo "could not stamp the build into play/index.html" >&2; exit 1; }
cp "$WEB/twill.js" "$WEB/twill.wasm" "$OUT/play/"

touch "$OUT/.nojekyll"  # serve the files as they are
echo "built $OUT (version $VERSION, build $BUILD)"
