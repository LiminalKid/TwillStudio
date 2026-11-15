## Notes

Thanks to Liminal Kid for creating Twill Studio, a groovebox whose piano roll is built for buttons, and to Emilie Gillet of [Mutable Instruments](https://github.com/pichenettes/eurorack) for Plaits, the open-source macro oscillator its synths are built on.

Songs autosave as you work and are saved to `ports/twill/projects`. Put your own WAV samples in `ports/twill/samples`; song renders go to `ports/twill/renders`. A USB MIDI keyboard or synth can be plugged in to play, record or be played.

Tested on the TrimUI Brick Pro and the Anbernic RG28XX (KNULLI).

Twill Studio is free software under the GPL-3.0; its source is at [github.com/LiminalKid/TwillStudio](https://github.com/LiminalKid/TwillStudio). The factory drum kit is public domain (CC0), so its sounds can go in any music.

## Controls

| Button | Action |
|--|--|
| D-Pad | Move the cursor |
| A | Add a note, or select the one under the cursor. Hold with the D-Pad to resize or transpose it |
| B | Delete the note under the cursor. Hold with the D-Pad to move the selected notes |
| X | Open the panel for the note, clip or track under the cursor |
| Y | Hold with the D-Pad to select notes. Y + A selects every note in the clip |
| L1 / R1 | Previous / next track. Hold with the D-Pad to change the grid or page |
| L2 / R2 | Same as L1 / R1 |
| Start | Play / stop |
| L1 or R1 + Start | Play from the cursor |
| Select + Left / Right | Switch view: arrangement, piano roll, sound, mixer |
| Select + B / A | Undo / redo |
| Select + Y / R1 | Copy / paste |
| Select + X | Record from a MIDI keyboard |
| Menu (or a stick click) | Settings: tempo, key, saving and loading songs, About |
| Menu + Start | Quit |

The bar at the bottom of the screen shows what the buttons do as you hold them.

## Compile

Twill Studio is cross-compiled with [Zig](https://ziglang.org) 0.17.1 and CMake (macOS or Linux host). Linking needs an AArch64 `libSDL2-2.0.so.0` and the SDL2 headers from the same release; `tools/make-knulli-sysroot.sh` copies them from a KNULLI device.

```bash
git clone https://github.com/LiminalKid/TwillStudio.git
cd TwillStudio
tools/make-knulli-sysroot.sh root@<device>
cmake --preset knulli
cmake --build --preset knulli --target twill
```
