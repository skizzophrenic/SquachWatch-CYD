# Stamps the current git tag/commit into the firmware as FIRMWARE_VERSION,
# shown on the Diary screen (see src/ui_diary.cpp). Falls back to "unknown"
# if git isn't available or this isn't a git checkout at all -- never
# breaks the build over a missing version string.
Import("env")
import os
import subprocess


def get_version():
    # A bench override: SQW_VERSION=9.9.9 makes a build claim a version, so a
    # squad update nudge from it counts as newer on a board built from the
    # same tree. Never set in a release build.
    forced = os.environ.get("SQW_VERSION", "").strip()
    if forced:
        return forced
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
        return v if v else "unknown"
    except Exception:
        return "unknown"


env.Append(BUILD_FLAGS=['-DFIRMWARE_VERSION=\\"%s\\"' % get_version()])

# The environment name, as SQW_ENV. A Bluetooth update is signed for exactly one
# build and the board checks the signature against its own name, so an image
# for a different board -- a different display driver, say -- is refused
# rather than installed as a white screen. See include/ota_ble.h.
env.Append(BUILD_FLAGS=['-DSQW_ENV=\\"%s\\"' % env["PIOENV"]])

# C++ exceptions off, for everything this build compiles: our code, the
# Arduino core and the libraries built from source. The framework turns them
# on, and that keeps an unwind table for every function so a throw can find
# its way to a catch -- but there is no catch anywhere in the firmware (a
# build with them off would refuse to compile one), so a throw has only ever
# ended in abort() and a restart, and still does. The tables were 61.7 KB of
# the 3.2in build (measured 2026-10-06). -fno-exceptions goes last, so it
# wins over the framework's -fexceptions (as a build flag: CXXFLAGS from
# a pre: script land before the framework's and lose). C accepts it too.
env.Append(BUILD_FLAGS=["-fno-exceptions"])


# ---------------------------------------------------------------------------
# TFT_eSPI, ESP32-C5. Only for the nm-cyd-c5 environment.
#
# TFT_eSPI 2.5.43 is upstream's newest release (March 2024) and has no C5
# branch, so it falls through to the plain-Xtensa processor and dies on
# VSPI_HOST and SPI_MOSI_DLEN_REG. This drops RockBase's C5 processor port
# into the copy PlatformIO downloaded and adds the two #elif lines that reach
# it. See boards/nm-cyd-c5-tft_espi/README.md.
#
# It raises rather than warns on anything unexpected. A display driver that
# silently did not get patched is a solid white screen on a board that is
# otherwise running perfectly, which is a miserable thing to debug -- far
# better for the BUILD to stop and say so.
def patch_tft_espi_for_c5(env):
    import shutil

    proj = env.subst("$PROJECT_DIR")
    src = os.path.join(proj, "boards", "nm-cyd-c5-tft_espi")
    lib = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"], "TFT_eSPI")

    if not os.path.isdir(lib):
        raise Exception(
            "TFT_eSPI is not installed at %s yet, so the ESP32-C5 processor "
            "port cannot be applied. This script must run AFTER the library "
            "manager." % lib
        )

    for name in ("TFT_eSPI_ESP32_C5.c", "TFT_eSPI_ESP32_C5.h"):
        s = os.path.join(src, name)
        d = os.path.join(lib, "Processors", name)
        if not os.path.isfile(s):
            raise Exception("missing %s -- boards/nm-cyd-c5-tft_espi is incomplete" % s)
        # Copy every build: the library can be reinstalled underneath us, and
        # copying 50 KB is cheaper than the failure mode of not copying it.
        shutil.copyfile(s, d)

    # The two dispatch lines. Anchored on the C3 branch that is immediately
    # above where C5 belongs in both files.
    edits = (
        ("TFT_eSPI.h",
         '  #include "Processors/TFT_eSPI_ESP32_C3.h"\n',
         '#elif defined(CONFIG_IDF_TARGET_ESP32C5)\n'
         '  #include "Processors/TFT_eSPI_ESP32_C5.h"\n'),
        ("TFT_eSPI.cpp",
         '    #include "Processors/TFT_eSPI_ESP32_C3.c" // Tested with SPI (8-bit parallel will probably work too!)\n',
         '  #elif defined(CONFIG_IDF_TARGET_ESP32C5)\n'
         '    #include "Processors/TFT_eSPI_ESP32_C5.c"\n'),
    )
    for fname, anchor, addition in edits:
        path = os.path.join(lib, fname)
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        if "TFT_eSPI_ESP32_C5" in text:
            continue                      # already patched; runs are idempotent
        if text.count(anchor) != 1:
            raise Exception(
                "%s: expected exactly one ESP32-C3 include to anchor the C5 "
                "branch to, found %d. TFT_eSPI has changed shape -- check "
                "whether it now supports the C5 on its own."
                % (fname, text.count(anchor))
            )
        text = text.replace(anchor, anchor + addition)
        with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(text)
        print("TFT_eSPI: added ESP32-C5 branch to %s" % fname)


if env["PIOENV"] == "nm-cyd-c5":
    patch_tft_espi_for_c5(env)
