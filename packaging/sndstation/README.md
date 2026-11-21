# Twill Studio for sndstation

Twill Studio is a piano-roll groovebox: synths, drums and samples on eight tracks, with a piano
roll, an arranger and a mixer, played from the handheld's buttons or a MIDI keyboard.

Start it from the sndstation launcher. Songs, samples and renders are kept in `~/TwillStudio`,
which the launcher's Storage page and file browser show as Home; the log of the last run is
`~/TwillStudio/log.txt`.

Twill plays through the unit's own sound settings: the output, buffer size and volume chosen in the
launcher's Audio page, and the MIDI ports chosen in its MIDI page. Vol+ and Vol- change the volume
as everywhere else. If the sound clicks now and then, choose a larger buffer size in Audio.

Menu + Start quits back to the launcher. On a handheld without a Menu key, a stick click is Menu.

Twill Studio is free software under the GPL-3.0: https://github.com/LiminalKid/TwillStudio.
The factory kit samples are public domain (CC0). The other licences are in `licenses/`.
