#!/bin/bash
# Build squachsim-tab5: the emulator with the Tab5's own build settings that
# change what is drawn (font 4 loaded, the bigger background buffers), then
# put the normal squachsim back. Render with it at --size 640x360.
cd "$(dirname "$0")"
touch ../src/theme.cpp
make -s squachsim EXTRA_CXXFLAGS="-DSQW_BG_MAX_W=640 -DSQW_BG_DENSITY=2 -DLOAD_FONT4" >/dev/null 2>&1 || { echo "BUILD FAILED"; exit 1; }
cp squachsim squachsim-tab5
touch ../src/theme.cpp
make -s squachsim >/dev/null 2>&1 || { echo "DEFAULT REBUILD FAILED"; exit 1; }
echo "squachsim-tab5 built"
