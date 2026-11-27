#!/bin/sh
# Twill Studio's launcher on TrimUI devices, for both NextUI (Tools/<platform>/Twill Studio.pak/) and
# the stock menu (Apps/Twill/): NextUI runs on the stock system, so the two are the same underneath.
PAK_DIR="$(cd "$(dirname "$0")" && pwd)"

# Songs, samples and renders live outside the app's folder, in one place for both menus: the Pak
# Store unzips updates over the pak and deletes it on uninstall. Twill keeps its files in the
# folder it starts in.
DATA_DIR="/mnt/SDCARD/TwillStudio"
mkdir -p "$DATA_DIR" 2>/dev/null || DATA_DIR="$PAK_DIR"
cd "$DATA_DIR" || exit 1

# A log of the last run, next to the songs, so a problem can be reported without a network.
exec > "$DATA_DIR/log.txt" 2>&1
echo "Twill Studio pak, $(date 2>/dev/null); firmware $(head -n1 /etc/version 2>/dev/null)"

# The Pak Store doesn't keep the executable bit.
chmod +x "$PAK_DIR/twill.aarch64" 2>/dev/null

# The device's own SDL2, built for its GPU, isn't on the default library path.
export LD_LIBRARY_PATH="/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# The built-in pad reports itself as an Xbox controller with Nintendo-style labels: map A, B, X and Y
# by their labels, not their positions (two GUIDs, as SDL versions write them differently).
P=",a:b1,b:b0,x:b3,y:b2,back:b6,start:b7,guide:b8,leftshoulder:b4,rightshoulder:b5,leftstick:b9,rightstick:b10,lefttrigger:a2,righttrigger:a5,leftx:a0,lefty:a1,rightx:a3,righty:a4,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,platform:Linux,"
export SDL_GAMECONTROLLERCONFIG="030003f05e0400008e02000014010000,TRIMUI Player1$P
030000005e0400008e02000014010000,TRIMUI Player1$P${SDL_GAMECONTROLLERCONFIG:+
$SDL_GAMECONTROLLERCONFIG}"

# Full CPU speed and no sleeping while it plays (stay_alive: the stock menu's own apps also set it);
# all put back however it ends.
GOV="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)"
restore() {
  rm -f /tmp/stay_awake /tmp/stay_alive
  [ -n "$GOV" ] && for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo "$GOV" > "$g"; done 2>/dev/null
}
trap restore EXIT INT TERM HUP QUIT
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo performance > "$g"; done 2>/dev/null
echo 1 > /tmp/stay_awake 2>/dev/null
echo 1 > /tmp/stay_alive 2>/dev/null

# Recording samples from a USB input (a synth, a mic, an interface): Twill reads the card itself.
export TWILL_CAPTURE=1

"$PAK_DIR/twill.aarch64"
echo "exited: $?"
