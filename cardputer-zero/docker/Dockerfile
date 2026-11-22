# SquachWatch for the Cardputer Zero -- the build environment.
#
# The Cardputer Zero runs Debian 13 (trixie) on arm64, so the app is built
# in an arm64 trixie container: the binary then links against exactly the
# library versions the device has. On an x86-64 desktop the container runs
# under qemu user-mode emulation (see README.md, "Build").
#
#   docker build --platform linux/arm64 -t squachwatch-cz-build docker/
#
# Everything the build, the tests and the package script need:
#   g++, make            the compiler and the build
#   git                  `git describe` stamps the firmware version (Makefile)
#   libsdl2-dev          the display/keyboard harness (SDL window and --fb)
#   libasound2-dev       the speaker (ALSA)
#   libmbedtls-dev       the firmware's real mesh cipher (linked statically)
#   (dpkg-deb, for scripts/build-deb.sh, is part of the base image)
FROM debian:trixie

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        g++ make git ca-certificates \
        libsdl2-dev libasound2-dev libmbedtls-dev \
 && rm -rf /var/lib/apt/lists/*

# The firmware checkout is mounted here (from cardputer-zero/:
# docker run -v "$PWD/..":/src -w /src/cardputer-zero); git refuses to
# work in a directory owned by another uid without this.
RUN git config --system --add safe.directory '*'

WORKDIR /src
