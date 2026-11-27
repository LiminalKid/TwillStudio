#!/bin/sh
# Starts Twill Studio from the sndstation launcher. Its log goes to ~/TwillStudio/log.txt, with the
# songs, samples and renders.
DIR="$(dirname "$(readlink -f "$0")")"
# The launcher (sndstation 0.15) passes on the AUDIODEV its own sound set, which the SDK then takes
# as chosen outside and plays through SDL's ALSA stream instead of its own. Within the session,
# the unit's Audio settings choose the device.
[ "$SNDSTATION_SESSION" = 1 ] && unset AUDIODEV
# Recording samples, from a USB input (a synth, a mic, an interface) or the unit's own mic.
export TWILL_CAPTURE=1
mkdir -p "$HOME/TwillStudio"
exec "$DIR/twill" 2> "$HOME/TwillStudio/log.txt" > "$HOME/TwillStudio/sdk-log.txt"
