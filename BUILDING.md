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
   with `twill.zip` (the PortMaster package) and `TwillStudio.pak.zip` (the NextUI pak) attached.
   The Pak Store picks the new version up from `pak.json` once the snapshot reaches GitHub's `main`.

## The website

`site/` holds the landing page; `tools/make-site.sh` puts it together with the fonts, the logo and
the web build (as `play/`) in `build/site`:

    tools/make-site.sh
    cd build/site && python3 -m http.server 8000   # then open http://localhost:8000/

`.github/workflows/pages.yml` does the same on GitHub for every push to `main`, runs the web build's
tests under Node, and publishes the result with GitHub Pages. The screenshots in
`site/images/screens/` are the app's own frames at 1024 x 768, in both themes.

For itch.io, `tools/make-itch.sh` packs the web build on its own, with its fonts, as
`build/twill-web-itch.zip`: upload it as an HTML file to be played in the browser (a 1024 x 768
viewport works well). The page's cover is `Docs/images/logo/twill-logo-dark-cover-630x500.png`.

## The logo

`tools/make-logo.py` (needs Pillow) draws the logo and writes the launch splash's pixels
(`src/app/gen/logo.cpp`), PortMaster's cover, the images in `Docs/images/logo/` and the website's
copy of its pixels (`site/logo.js`).
