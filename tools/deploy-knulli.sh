#!/bin/sh
# Copies the KNULLI build to a device's Ports folder.
#
#   tools/deploy-knulli.sh <device-address>
#
# Then refresh the game lists in EmulationStation and start "Twill Studio" from Ports.
# The app's log is written to /userdata/roms/ports/twill/log.txt.
set -eu
DEVICE="${1:?Give the device, e.g. root@192.168.0.50}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/knulli/twill"
[ -f "$BIN" ] || { echo "Build first: cmake --preset knulli && cmake --build --preset knulli"; exit 1; }

sftp -q -b - "$DEVICE" <<EOF
-mkdir /userdata/roms/ports/twill
put "$BIN" /userdata/roms/ports/twill/twill
put "$ROOT/packaging/knulli/Twill Studio.sh" "/userdata/roms/ports/Twill Studio.sh"
chmod 755 /userdata/roms/ports/twill/twill
chmod 755 "/userdata/roms/ports/Twill Studio.sh"
EOF
echo "Deployed to $DEVICE:/userdata/roms/ports/"
