#!/bin/bash
# Twill Studio's launcher for KNULLI's Ports, with nothing else needed (no PortMaster). Goes in
# /userdata/roms/ports/ next to the twill/ folder, which holds the program and everything the app
# saves: the same folder and file names as PortMaster's package, so either one updates the other.
cd "$(dirname "$0")/twill" || exit 1
exec > log.txt 2>&1
echo "Twill Studio for KNULLI, $(date 2>/dev/null); $(cat /usr/share/knulli/knulli.version /usr/share/batocera/batocera.version 2>/dev/null | head -n1); $(cat /proc/device-tree/model 2>/dev/null | tr -d '\0')"
# On the card now, not later: if Twill hangs and the power is held down, the log still has this.
sync

# No helper watches for Select + Start here, as PortMaster's does, so Twill does.
export TWILL_SELECT_START_QUITS=1

# Recording samples: a USB input (a synth, a mic, an interface) records through PipeWire. The
# Brick Pro's own mic doesn't yet under KNULLI's beta (PipeWire can't start it), so it records silence.
export TWILL_CAPTURE=1

# KNULLI's battery saver mutes the sound after minutes without a button press; Twill holds it off
# with this file while a song plays.
PAUSE=/var/run/battery-saver/twill.pause
[ -d "${PAUSE%/*}" ] && export TWILL_KEEP_AWAKE="$PAUSE"

# Full CPU speed while it runs, so the sound keeps up; put back however it ends.
GOV="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)"
set_governor() {
  local g
  for g in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
    [ -w "$g" ] && echo "$1" > "$g"
  done 2>/dev/null
}
restore() {
  [ -n "$GOV" ] && set_governor "$GOV"
  rm -f "$PAUSE" /tmp/twill.aarch64
}
trap restore EXIT
trap 'exit 1' INT TERM HUP QUIT
[ -n "$GOV" ] && set_governor performance
echo "CPU governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null) (was $GOV)"

# Copied over a network share, the program may have lost its executable bit, and some cards can't
# keep one: then it runs from a copy in memory.
app=./twill.aarch64
chmod +x "$app" 2>/dev/null
if [ ! -x "$app" ]; then
  cp "$app" /tmp/twill.aarch64 && chmod 755 /tmp/twill.aarch64 && app=/tmp/twill.aarch64
fi
"$app"
echo "exited: $?"
