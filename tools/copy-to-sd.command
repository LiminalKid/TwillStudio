#!/bin/sh
# Copies the KNULLI build onto a KNULLI SD card mounted on this Mac (the SHARE partition, exFAT),
# for devices without Wi-Fi. Double-click it, or run it from a terminal. Expects the build next to
# it (sd-card/roms/ports/...) or in build/knulli/ of a checkout.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -d "$HERE/sd-card/roms/ports" ]; then
  SRC_BIN="$HERE/sd-card/roms/ports/twill/twill"
  SRC_SH="$HERE/sd-card/roms/ports/Twill Studio.sh"
else
  ROOT="$(cd "$HERE/.." && pwd)"
  SRC_BIN="$ROOT/build/knulli/twill"
  SRC_SH="$ROOT/packaging/knulli/Twill Studio.sh"
fi
CARD=""
for v in /Volumes/*; do
  [ -d "$v/roms/ports" ] && CARD="$v" && break
done
[ -n "$CARD" ] || { echo "No KNULLI card found (looked for /Volumes/*/roms/ports)."; exit 1; }
PORTS="$CARD/roms/ports"
# Before the app was renamed (2026-11-14) it lived in fluorescent/: move its songs and settings over.
if [ -d "$PORTS/fluorescent" ] && [ ! -d "$PORTS/twill" ]; then
  mv "$PORTS/fluorescent" "$PORTS/twill" && rm -f "$PORTS/twill/fluorescent" "$PORTS/Fluorescent.sh"
  find "$PORTS/twill" -name '*.fluo' | while read -r f; do mv "$f" "${f%.fluo}.twill"; done
  echo "Moved the old fluorescent/ folder to twill/"
fi
mkdir -p "$PORTS/twill"
# Keep the device's log and controller config from the last run, next to this script.
for f in log.txt es_input.cfg; do
  [ -f "$PORTS/twill/$f" ] && cp -X "$PORTS/twill/$f" "$HERE/last-run-$f" && echo "Saved last-run-$f"
done
cp -X "$SRC_BIN" "$PORTS/twill/twill.aarch64"
cp -X "$SRC_SH" "$PORTS/Twill Studio.sh"
# macOS leaves ._ files on exFAT, which would show up as extra entries in Ports.
dot_clean -m "$PORTS" 2>/dev/null || true
find "$PORTS" -name '._*' -delete 2>/dev/null || true
diskutil eject "$CARD" >/dev/null && echo "Copied to $CARD and ejected. Put the card back in the device."
