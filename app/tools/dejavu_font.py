#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The screen's fonts in DejaVu Sans (app/fonts, its licence there): fm1-base's gen_font.py, run with one more font set,
writes over the header it made (fm1.toml gen: this runs after it). The six roles at sizes keeping the layout's line
heights near 12 / 16 / 24 / 32."""
import os
import sys
from pathlib import Path

APP = Path(__file__).resolve().parents[1]
GEN_FONT = APP.parent / "base" / "tools" / "gen_font.py"
DEJAVU = '''    "dejavu": [("T", "DejaVuSans-Bold.ttf", 10, 1, False, 32, 95),
               ("XS", "DejaVuSans.ttf", 11, 1, False, 32, 126),
               ("S", "DejaVuSans.ttf", 13, 1, False, 32, 126),
               ("B", "DejaVuSans-Bold.ttf", 13, 1, False, 32, 126),
               ("M", "DejaVuSans-Bold.ttf", 19, 1, False, 32, 95),
               ("L", "DejaVuSans-Bold.ttf", 26, 1, False, 32, 95)],
'''
src = GEN_FONT.read_text()
assert "FONTSETS = {\n" in src, "gen_font.py: its font sets not where they were"
src = src.replace("FONTSETS = {\n", "FONTSETS = {\n" + DEJAVU, 1)
os.environ["X0X_FONTSET"] = "dejavu"
os.environ["X0X_FONT_DIRS"] = str(APP / "fonts")
sys.argv = [str(GEN_FONT), str(Path(sys.argv[1]) / "felucca_font.h")]
exec(compile(src, str(GEN_FONT), "exec"), {"__name__": "__main__", "__file__": str(GEN_FONT)})
