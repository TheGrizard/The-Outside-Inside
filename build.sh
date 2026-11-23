#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build The Outside Inside: ./build.sh (the firmware), ./build.sh --host (the simulator), ./build.sh --release X.Y
# fm1-base is in base/ (a git submodule); the app in app/. The screen is in DejaVu Sans (app/tools/dejavu_font.py,
# run by the build after fm1-base's own fonts).
set -e
cd "$(dirname "$0")"
export X0X_FONTSET="${X0X_FONTSET:-terminus}"
exec python3 base/tools/build.py app "$@"
