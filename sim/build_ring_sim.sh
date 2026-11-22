#!/bin/bash
# Build squachsim-ring: the emulator with RING on (SQW_RING=1), as the S3 and
# C5 boards are built, so an AirTag / Samsung / Google / Tile alert carries
# its RING button and LOG's long-press panel its RING IT row. Then put the
# normal squachsim back (the CYDs leave RING off).
cd "$(dirname "$0")"
touch ../src/ui_alert.cpp ../src/ui_log.cpp ../src/tag_ring.cpp ../src/clock.cpp
make -s squachsim EXTRA_CXXFLAGS="-DSQW_RING=1" >/dev/null 2>&1 || { echo "BUILD FAILED"; exit 1; }
cp squachsim squachsim-ring
touch ../src/ui_alert.cpp ../src/ui_log.cpp ../src/tag_ring.cpp ../src/clock.cpp
make -s squachsim >/dev/null 2>&1 || { echo "DEFAULT REBUILD FAILED"; exit 1; }
echo "squachsim-ring built"
