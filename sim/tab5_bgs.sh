#!/bin/bash
# Render every background at the Tab5's 640x360 the way the board draws them
# (bigger buffers, SQW_BG_DENSITY 2) into out/bgs-640new, then rebuild the
# emulator as normal.
cd "$(dirname "$0")"
touch ../src/theme.cpp
make -s squachsim EXTRA_CXXFLAGS="-DSQW_BG_MAX_W=640 -DSQW_BG_DENSITY=2" >/dev/null 2>&1 || echo BUILD FAILED
bash render_bgs.sh out/bgs-640new 640x360 400
touch ../src/theme.cpp
make -s squachsim >/dev/null 2>&1
