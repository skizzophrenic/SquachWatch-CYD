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


if env["PIOENV"] in ("nm-cyd-c5", "marauder-v8"):
    patch_tft_espi_for_c5(env)


# ---------------------------------------------------------------------------
# TFT_eSPI, ESP32-P4. Only for the tab5 environments.
#
# The Tab5 never drives a bus through TFT_eSPI -- the library is kept as the
# TYPE the app's canvas needs (include/tab5_user_setup.h says why) -- so it
# does not need a P4 processor port at all, only something that COMPILES on
# one. Asked to build for a P4, 2.5.43 falls through to the plain-Xtensa
# processor and dies on its SPI registers, exactly as it does on the C5. The
# library's own Generic processor (plain Arduino SPI and digitalWrite) is the
# right answer here: two #elif lines route the P4 to it.
def patch_tft_espi_for_p4(env):
    lib = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"], "TFT_eSPI")
    if not os.path.isdir(lib):
        raise Exception(
            "TFT_eSPI is not installed at %s yet, so the ESP32-P4 dispatch "
            "cannot be added. This script must run AFTER the library manager." % lib
        )
    edits = (
        ("TFT_eSPI.h",
         '#elif defined (ESP32)\n  #include "Processors/TFT_eSPI_ESP32.h"\n',
         '#elif defined(CONFIG_IDF_TARGET_ESP32P4)\n'
         '  #include "Processors/TFT_eSPI_Generic.h"\n'
         '  #define GENERIC_PROCESSOR\n'),
        ("TFT_eSPI.cpp",
         '#if defined (ESP32)\n  #if defined(CONFIG_IDF_TARGET_ESP32S3)\n',
         '#if defined(CONFIG_IDF_TARGET_ESP32P4)\n'
         '  #include "Processors/TFT_eSPI_Generic.c"\n'
         '#elif defined (ESP32)\n  #if defined(CONFIG_IDF_TARGET_ESP32S3)\n'),
    )
    # The Generic processor turns TFT_CS/TFT_DC into digitalWrite() calls
    # whenever the macro is DEFINED -- and the setup defines them as -1 (the
    # library needs TFT_CS defined for its own `if (TFT_CS >= 0)` tests).
    # digitalWrite(-1) is pin 255 on core 3, which logs an error line on every
    # startWrite()/endWrite() the app makes through its TFT_eSPI& references,
    # two a frame. A -1 pin is no pin: make the guards say so.
    gen = os.path.join(lib, "Processors", "TFT_eSPI_Generic.h")
    with open(gen, "r", encoding="utf-8", errors="surrogateescape") as f:
        text = f.read()
    changed = False
    for pin in ("TFT_DC", "TFT_CS"):
        old = "#ifndef %s\n" % pin
        new = "#if !defined(%s) || (%s < 0)\n" % (pin, pin)
        if text.count(old) == 1:
            text = text.replace(old, new)
            changed = True
        elif text.count(new) != 1:
            raise Exception("%s: expected one '%s' guard to relax, found %d" % (gen, pin, text.count(old)))
    if changed:
        with open(gen, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(text)
        print("TFT_eSPI: Generic processor treats TFT_CS/TFT_DC = -1 as no pin")
    # The .h gets the P4 branch added above its anchor; the .cpp has its
    # anchor REPLACED (its leading #if becomes the #elif after the new branch).
    for fname, anchor, addition in edits:
        path = os.path.join(lib, fname)
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        if "CONFIG_IDF_TARGET_ESP32P4" in text:
            continue                      # already patched; runs are idempotent
        if text.count(anchor) != 1:
            raise Exception(
                "%s: expected exactly one anchor for the ESP32-P4 branch, found %d. "
                "TFT_eSPI has changed shape -- check whether it now supports the P4 on its own."
                % (fname, text.count(anchor))
            )
        text = text.replace(anchor, addition + anchor if fname.endswith(".h") else addition)
        with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(text)
        print("TFT_eSPI: added ESP32-P4 (Generic processor) branch to %s" % fname)


if env["PIOENV"] == "tab5" or env["PIOENV"].startswith("tab5-"):
    # the bench builds and the base station (M5GFX only) have no TFT_eSPI at all
    if env["PIOENV"] not in ("tab5-paneltest", "tab5-radiotest", "tab5-base"):
        patch_tft_espi_for_p4(env)
