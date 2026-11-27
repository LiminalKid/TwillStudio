# Building Twill Studio

Needs CMake 3.20+ and Ninja.

**macOS** (SDL2 from Homebrew: `brew install sdl2`):

    cmake --preset macos && cmake --build --preset macos
    ./build/macos/twill

Keys: arrows move, X = A, Z = B, S = X, A = Y, Q / W = L / R, 1 / 2 = L2 / R2, Return/Space = Start, Tab = Select,
Escape = Menu (Escape + Return quits).

**KNULLI handhelds** (TrimUI Brick Pro), cross-compiled with Zig (`brew install zig`):

    tools/make-knulli-sysroot.sh root@<device>   # once: the device's SDL2 plus matching headers
    cmake --preset knulli && cmake --build --preset knulli
    tools/deploy-knulli.sh root@<device>         # copies it to the device's Ports folder

Then update the game lists in EmulationStation and start Twill Studio from Ports. Menu + Start
quits. The project autosaves to `autosave.twill` next to the app; saved projects go to `projects/`,
WAV renders to `renders/`, and the factory drum kit (built into the app from `assets/factory/`) to
`samples/Factory/`.

For PortMaster, `tools/make-portmaster-zip.sh` packages that build as `build/portmaster/twill.zip`
(drop it in PortMaster's autoinstall folder).

For muOS, `tools/make-muos-app.sh` packages it as `build/muos/TwillStudio-muOS.muxapp`: copy it into
`ARCHIVE` on the SD card and choose it in Archive Manager, and Twill Studio appears under
Applications. Songs go in `TwillStudio/` at the top of the card.

For NextUI, `tools/make-nextui-pak.sh` packages the same build as a Tools pak,
`build/nextui/TwillStudio.pak.zip`: unzip it into `Tools/tg5040/Twill Studio.pak/` on the SD card,
or install it from the Pak Store, which reads `pak.json` at the top of this repository. The pak's
`launch.sh` (in `packaging/nextui/`) keeps songs in `TwillStudio/` at the top of the card, uses the
device's own SDL2 from `/usr/trimui/lib`, maps the built-in pad, and runs the CPU at full speed
while Twill is open.

For the TrimUI Brick's stock menu, `tools/make-trimui-app.sh` packages the same build and launcher
as `build/trimui/TwillStudio-TrimUI.zip`, holding `Apps/Twill/` (with the menu's `config.json` and
icon from `packaging/trimui/`): unzip it at the top of the SD card and Twill Studio appears under
Apps. Songs go in the same `TwillStudio/` folder as with NextUI.

**sndstation handhelds** (Debian with the sndstation launcher), cross-compiled with Zig. The build
uses the sndstation SDK for sound, buttons and MIDI, built from its source (a checkout, with its
submodules): `-DSNDSTATION_DIR=<folder>`, or `SNDSTATION_DIR` in the environment, else the first of
`~/github_sndstation/sndstation`, `~/src/sndstation`, `~/sndstation` and `~/SDKs/sndstation`.

    tools/make-sndstation-sysroot.sh sndy@<device>   # once: the device's SDL2 and alsa-lib, Debian's headers
    cmake --preset sndstation && cmake --build --preset sndstation --target twill
    tools/make-sndstation-deb.py                     # build/sndstation-deb/twill-studio_<version>_sndstation_<arch>.deb (arm64 on a handheld)

Install the package on the device with `sudo apt install ./twill-studio_<version>_sndstation_<arch>.deb`, then
press X in the launcher's list to read it again. Twill plays through the unit's Audio settings
(output, buffer size, volume) and its MIDI ports, so it has no Audio buffer setting of its own
there. Songs, samples and renders go in `~/TwillStudio`, with the log of the last run,
`log.txt`. The start script, `packaging/sndstation/twill-studio.sh`, also clears the `AUDIODEV`
the launcher passes on (sndstation 0.15), which would otherwise put the sound on SDL's own ALSA
stream. `cmake --preset macos-sndstation` builds the same platform on a Mac (`brew install sdl2
sdl2_ttf sdl2_image`), with the SDK's keyboard map for the pad.

**Web** (Emscripten: `brew install emscripten`):

    emcmake cmake -S . -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/web
    cd build/web && python3 -m http.server 8000   # then open http://localhost:8000/twill.html

Browsers load WebAssembly only from a server, not from a file. The page around the app is
`web/shell.html`; `web/pre.js` keeps the app's files in the browser's IndexedDB. If Emscripten
complains about its Python, set `EMSDK_PYTHON` to Python 3.10 or later; if it can't write its
cache, set `EM_CACHE` to a folder you own.

## Test hooks

- `TWILL_SCREENSHOT=frame.ppm` saves a frame after about a second and quits.
- `TWILL_VIEW=roll` starts in the piano roll.
- `TWILL_UI_SCALE=1.5` overrides the UI scale; `TWILL_FULLSCREEN=1` uses the handheld layout on desktop.
- `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` runs without a display or sound device.
- `TWILL_DATA_DIR=/some/folder` keeps songs, samples and settings there instead of the usual folder.
- `TWILL_POINTER_LOG=1` logs each mouse or touch event, its gesture, and how long the frame took.
- `TWILL_CAPTURE=1` lets a handheld record samples. Every handheld launcher sets it: KNULLI,
  sndstation, TrimUI and NextUI were each tested with a USB synth; PortMaster and muOS weren't.
- A file named `screenshot.request` in the data folder makes the app save the screen to
  `screenshots/` and remove the file.

## Tests

    ./build/macos/twill_tests                 # or: ctest --test-dir build/macos
    ./build/macos/twill_tests --update-golden # after an intended change to the sound

On a device: copy `build/knulli/twill_tests` and `tests/golden/` over and run it with
`--golden-dir <dir>`.

## Tools

- `twill_bench`: drawing benchmark, no display needed.
- `twill_macro_lab render <dir>`: renders each of the 24 macro-oscillator engines to a WAV.
- `twill_macro_lab bench [frames]`: cost per voice for each engine, as a share of one core.

Both run over SSH on a handheld: copy them from `build/knulli/` to the device's `/tmp`.

## Releases

GitHub Actions (`.github/workflows/build.yml`) builds the handheld version and runs the tests on
GitHub's AArch64 Linux machines for every push to `main`. A version tag publishes a release:

1. Set the version in `CMakeLists.txt` (`project(TwillStudio VERSION x.y.z)`) and in `pak.json`
   (`"version": "vx.y.z"`), and push it.
2. Tag that commit with the release notes as the tag's message, and push the tag:
   `git tag -a vx.y.z -m "notes"` then `git push origin vx.y.z`.
3. The workflow checks the tag matches both versions, builds, tests, and publishes "Twill Studio x.y.z"
   with `twill.zip` (the PortMaster package), `TwillStudio.pak.zip` (the NextUI pak) and
   `TwillStudio-TrimUI.zip` (for the TrimUI Brick's and Smart Pro's own menu) attached.
   The Pak Store picks the new version up from `pak.json` once the snapshot reaches GitHub's `main`.
4. The same workflow then pushes the web version, `twill.zip` and the pak to itch.io with butler
   (channels `web`, `portmaster`, `nextui-pak` and `trimui-stock`), when the repository has the secret
   `BUTLER_API_KEY`.

## The website

`site/` holds the landing page; `tools/make-site.sh` puts it together with the fonts, the logo and
the web build (as `play/`) in `build/site`:

    tools/make-site.sh
    cd build/site && python3 -m http.server 8000   # then open http://localhost:8000/

`.github/workflows/pages.yml` does the same on GitHub for every push to `main`, runs the web build's
tests under Node, and publishes the result with GitHub Pages. The screenshots in
`site/images/screens/` are the app's own frames at 1024 x 768, in both themes.

For itch.io, `tools/make-itch.sh` packs the web build on its own, with its fonts, as
`build/twill-web-itch.zip`. Releases upload it themselves (above); on the itch.io page its upload is
marked to be played in the browser, with a 1024 x 768 viewport. The page's cover is `Docs/images/logo/twill-logo-dark-cover-630x500.png`.

## The logo

`tools/make-logo.py` (needs Pillow) draws the logo and writes the launch splash's pixels
(`src/app/gen/logo.cpp`), PortMaster's cover, the images in `Docs/images/logo/` and the website's
copy of its pixels (`site/logo.js`).
