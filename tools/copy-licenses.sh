#!/bin/sh
# Copies Twill's licence and the third-party ones into a package's licenses/ folder, as
# LICENSE.<component>.txt, taken from third_party/ so they stay in step with the code.
#
#     tools/copy-licenses.sh <folder>
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
L="$1"
mkdir -p "$L"
cp "$ROOT/LICENSE" "$L/LICENSE.twillstudio.txt"  # GPL-3.0
cp "$ROOT/assets/factory/LICENSE" "$L/LICENSE.factory-kit.txt"  # CC0
cp "$ROOT/third_party/eurorack/LICENSE" "$L/LICENSE.eurorack.txt"
cp "$ROOT/third_party/eurorack/stmlib/LICENSE" "$L/LICENSE.stmlib.txt"
cp "$ROOT/third_party/fonts/inter/LICENSE.txt" "$L/LICENSE.inter-font.txt"
cp "$ROOT/third_party/fonts/barlow-condensed/OFL.txt" "$L/LICENSE.barlow-condensed-font.txt"
# dr_wav keeps its licence at the end of its header.
sed -n '/^ALTERNATIVE 1 - Public Domain/,$p' "$ROOT/third_party/dr_libs/dr_wav.h" | sed '$d' |
  { echo "dr_wav (github.com/mackron/dr_libs), used to read WAV files. Either licence applies."; echo; cat; } \
  > "$L/LICENSE.dr_wav.txt"
