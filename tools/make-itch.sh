#!/bin/sh
# Packs the web version for itch.io, which plays an uploaded zip in its page when the zip has
# index.html at the top: build/twill-web-itch.zip. Build the web version first (BUILDING.md).
#
#     tools/make-itch.sh [web build folder, default build/web]
#
# The website's play/ (tools/make-site.sh) takes its fonts and icons from the site one folder up;
# here the fonts go in the zip, the icons are left out (itch.io's page has its own), and the bar's
# link opens the website in a new tab.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WEB=${1:-$ROOT/build/web}
OUT=$ROOT/build/itch
ZIP=$ROOT/build/twill-web-itch.zip
SITE=https://liminalkid.github.io/TwillStudio/
BUILD=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || date +%s)
for f in twill.html twill.js twill.wasm; do
  [ -f "$WEB/$f" ] || { echo "$WEB/$f is missing: build the web version first" >&2; exit 1; }
done

rm -rf "$OUT" "$ZIP"
mkdir -p "$OUT/fonts"

# The page. Emscripten minifies the shell, so attributes lose their quotes.
sed -E -e "s/@TWILL_BUILD@/$BUILD/" \
       -e "s/src=\"?twill\\.js\"?/src=\"twill.js?v=$BUILD\"/" \
       -e "s#\\.\\./fonts/#fonts/#g" \
       -e "s#<a href=\"?\\.\\./\"? ?>#<a href=\"$SITE\" target=\"_blank\" rel=\"noopener\">#" \
       -e "s#<link [^>]*(favicon|apple-touch-icon)[^>]*>##g" \
       "$WEB/twill.html" > "$OUT/index.html"
grep -q "\"$BUILD\"" "$OUT/index.html" && grep -q "twill.js?v=$BUILD" "$OUT/index.html" \
  && grep -q "href=\"$SITE\"" "$OUT/index.html" && ! grep -q '\.\./' "$OUT/index.html" \
  || { echo "could not adapt the page for itch.io" >&2; exit 1; }
cp "$WEB/twill.js" "$WEB/twill.wasm" "$OUT/"

# The fonts the page uses, with their licences.
F="$ROOT/third_party/fonts"
cp "$F/barlow-condensed/BarlowCondensed-Bold.ttf" "$F/inter/Inter-Regular.ttf" "$OUT/fonts/"
cp "$F/barlow-condensed/OFL.txt" "$OUT/fonts/LICENSE-Barlow-Condensed.txt"
cp "$F/inter/LICENSE.txt" "$OUT/fonts/LICENSE-Inter.txt"

(cd "$OUT" && zip -qrX "$ZIP" .)
echo "built $ZIP (build $BUILD)"
