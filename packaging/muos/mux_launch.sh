#!/bin/sh
# HELP: A piano-roll groovebox: synths, a sampler, automation, a mixer and WAV renders.
# ICON: twill
# GRID: Twill
#
# Twill Studio's launcher for muOS (MustardOS), run from Applications. Follows muOS's application
# runner: https://community.muos.dev/t/application-runner/1282

. /opt/muos/script/var/func.sh

APP_BIN="twill.aarch64"

# "retro": A, B, X and Y as printed on the handheld.
SETUP_APP "$APP_BIN" "retro"

# -----------------------------------------------------------------------------

# muOS passes the application's folder, on whichever card it's installed.
APP_DIR="${1:-$(dirname "$0")}"
APP_DIR="$(cd "$APP_DIR" && pwd)" || exit 1

# Songs, samples and renders live outside the application's folder, so replacing or deleting it
# leaves them: in TwillStudio/ at the top of the first card. If Twill already has songs from
# PortMaster's package (ports/twill/ on either card), it goes on using that folder.
SD1="$(GET_VAR "device" "storage/rom/mount")"
SD2="$(GET_VAR "device" "storage/sdcard/mount")"
DATA_DIR="$SD1/TwillStudio"
if [ ! -d "$DATA_DIR" ]; then
	for PORT in "$SD1/ports/twill" ${SD2:+"$SD2/ports/twill"}; do
		if [ -e "$PORT/autosave.twill" ] || [ -d "$PORT/projects" ]; then
			DATA_DIR="$PORT"
			break
		fi
	done
fi
if [ -z "$SD1" ] || ! mkdir -p "$DATA_DIR" 2>/dev/null; then
	DATA_DIR="$APP_DIR"
fi
cd "$DATA_DIR" || exit 1

# A log of the last run, next to the songs.
exec >"$DATA_DIR/log.txt" 2>&1
echo "Twill Studio for muOS, $(date 2>/dev/null); $(GET_VAR "device" "board/name"), muOS $(GET_VAR "system" "version"); songs in $DATA_DIR"

# No helper watches for Select + Start here, as PortMaster's does, so Twill does.
export TWILL_SELECT_START_QUITS=1

# Recording samples from a USB input (a synth, a mic, an interface): Twill reads the card itself.
export TWILL_CAPTURE=1

# Left alone for two minutes, muOS dims the screen, mutes the sound and slows the CPU; it can also be
# set to go to sleep. These two files, holding the process id of a running program, hold that off;
# Twill keeps them there while a song plays. The power button still puts it to sleep, which muOS's
# "caffeine" file would stop too.
RUN_DIR="${MUOS_RUN_DIR:-/run/muos}"
[ -d "$RUN_DIR" ] && export TWILL_KEEP_AWAKE="$RUN_DIR/idle_game_inhibit:$RUN_DIR/idle_sleep_inhibit"
# muOS's sleep on the H700 handhelds pauses only its own emulators. This file is there while it's
# asleep; Twill stops its sound meanwhile, or it would hold the one core left and the power button
# couldn't wake the handheld.
export TWILL_SLEEP_FILE="$RUN_DIR/quiet_wake"
trap 'rm -f "$RUN_DIR/idle_game_inhibit" "$RUN_DIR/idle_sleep_inhibit"' EXIT
trap 'exit 1' INT TERM HUP QUIT

# Full CPU speed while it runs, so the sound keeps up. muOS sets its default again after any
# application.
for G in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
	[ -w "$G" ] && echo performance >"$G"
done 2>/dev/null
echo "CPU governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)"

chmod +x "$APP_DIR/$APP_BIN" 2>/dev/null
"$APP_DIR/$APP_BIN"
echo "exited: $?"
