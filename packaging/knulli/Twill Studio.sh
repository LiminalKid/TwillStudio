#!/bin/bash
# KNULLI Ports launcher. Goes in /userdata/roms/ports/ next to the twill/ folder.
dir="$(dirname "$0")/twill"
cd "$dir" || exit 1

# Portrait panels (Anbernic RG28XX): the app turns the picture the way the kernel says the panel
# is mounted. This line fixes it at 270, known right on the RG28XX; if the picture comes out upside
# down on another device, change 270 to 90.
export TWILL_ROTATE=270

# A copy of the front end's controller config, for setting up a new device from the log.
cp /userdata/system/configs/emulationstation/es_input.cfg ./es_input.cfg 2>/dev/null ||
  cp /usr/share/emulationstation/es_input.cfg ./es_input.cfg 2>/dev/null

# Songs and the autosave from before the app was renamed (Fluorescent) carry the old extension.
for f in *.fluo projects/*.fluo; do
  [ -e "$f" ] && [ ! -e "${f%.fluo}.twill" ] && mv "$f" "${f%.fluo}.twill"
done

# An exFAT card can't mark a file executable; run a copy from memory instead.
app=./twill
if [ ! -x "$app" ]; then
  cp "$app" /tmp/twill && chmod 755 /tmp/twill && app=/tmp/twill
fi
"$app" > log.txt 2>&1
