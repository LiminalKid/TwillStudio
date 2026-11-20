# Twill Studio for NextUI

A piano-roll groovebox: write songs note by note with the d-pad, on twenty-four synth engines and a
sampler, then arrange, mix and render them.

Start it from the **Tools** menu in NextUI, or **Apps** in the stock menu. Your songs, samples and renders are kept in `TwillStudio/` at the
top of the SD card, outside the app's folder, so updating or uninstalling it leaves them alone:

- `projects/`: saved songs (the song you're working on is also saved as `autosave.twill`)
- `samples/`: your own WAV or AIFF samples; any folders inside are fine. `Factory/` holds the
  built-in drum kit.
- `renders/`: songs rendered to WAV, and stems
- `log.txt`: what happened on the last run, for bug reports

Every control: https://liminalkid.github.io/TwillStudio/
Source, bug reports and requests: https://github.com/LiminalKid/TwillStudio

Free software under the GNU GPL, version 3 (see `licenses/`). The built-in drum kit is public domain
(CC0), and the music you make is yours.
