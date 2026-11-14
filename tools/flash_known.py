#!/usr/bin/env python3
"""Flash a board by port, but only the build that belongs to it.

    python tools/flash_known.py COM33            # reads the MAC, picks the env, flashes
    python tools/flash_known.py COM33 --check    # just says which board and env

Boards move between ports, and a build for the wrong panel leaves a white
or torn screen, or a board that never comes up. Every flash goes through
the MAC first: an unknown board is refused, and the env is never a guess.
Fill BOARDS in from the fleet as it grows; a new board is added here before
it is ever flashed, not after.
"""
import os, subprocess, sys

# MAC -> (env, what it is). Ports are not in this table on purpose.
BOARDS = {
    "d4:8a:fc:c8:e5:e4": ("cyd-fast",    "soak, 2.8in ST7789"),
    "8c:94:df:4e:84:10": ("cyd-fast",    "2.8in ST7789, the newer one"),
    "8c:94:df:4e:ec:ec": ("cyd-ili9341", "2.8in ILI9341, no 80 MHz (the fast build showed a wrong picture, 2026-10-14)"),
    "78:42:1c:94:e7:5c": ("rlphantom-r", "RL Phantom 2.4in, resistive"),
    "78:42:1c:8e:df:f8": ("awok",        "AWOK, touch on the display bus"),
    "88:57:21:2e:ba:34": ("cyd-ili9341", "2.8in ILI9341, no 80 MHz"),
    "8c:94:df:4e:ee:dc": ("cyd-ili9341", "2.8in ILI9341, no 80 MHz"),
    # The 3.5in. It was kept out of this table while it was on hold -- it draws
    # in two bands, and that path corrupted the heap and crashed it every 30 s.
    # Back in on 2026-09-20 to see what the faster drawing did for it; if it
    # goes back on hold, comment it out again rather than leaving a board here
    # that nobody means to flash.
    "a4:f0:0f:8e:3a:88": ("cyd35-fast", "3.5in, 80MHz, two-band drawing"),
    "b0:cb:d8:02:ab:50": ("cyd35-fast", "3.5in resistive (ESP32-3248S035R), the second one (2026-10-05)"),
    "a0:f2:62:e1:29:10": ("twatch-s3",  "LilyGo T-Watch S3, native USB (COM13 is allowed for THIS MAC only)"),
    # The T-Watch S3 Plus: the S3's pins exactly, plus GPS on 41/42 and the
    # PMU rails that feed it (LilyGoLib docs/hardware, 2026-09-28). The watch
    # build never touches either, so it runs the watch build with the GPS off.
    "68:ee:8f:48:1f:e4": ("twatch-s3",  "LilyGo T-Watch S3 Plus, native USB"),
    "d4:e9:f4:c5:0e:e0": ("freenove32", "Freenove 3.2in CYD, ST7789, resistive touch on the display bus"),
    # Sunton ESP32-2432S032C (DIYmalls, Amazon B0CLGDHS16): 3.2in ST7789 IPS,
    # GT911 capacitive touch, a battery charger. Issue #6's board. 2026-09-28.
    "14:33:5c:6c:20:b8": ("cyd32c",     "Sunton ESP32-2432S032C, 3.2in ST7789 IPS, GT911 capacitive touch"),
    "14:33:5c:6d:22:84": ("cyd32c",     "Sunton ESP32-2432S032C, the second one (2026-09-30)"),
    # RockBase NM-CYD-C5 (Amazon B0H1QCHMW6): a 2.8in CYD on an ESP32-C5,
    # PR #21's board. Native USB, its MAC as the serial number. esptool prints
    # an eight-byte MAC first for this chip; the six-byte one is "BASE MAC",
    # which read_mac() prefers. 2026-10-02.
    "3c:dc:75:9d:5d:20": ("nm-cyd-c5",  "RockBase NM-CYD-C5, 2.8in on an ESP32-C5, native USB"),
    # M5Stack StickS3: ESP32-S3-PICO-1-N8R8, 135x240, no touch, native USB with
    # its MAC as the serial number. Bring-up began 2026-10-04; the factory image
    # was read off first.
    "70:04:1d:da:7b:04": ("sticks3",    "M5Stack StickS3, 1.14in 135x240, no touch, native USB"),
    # M5Stack Cardputer ADV (Stamp-S3A, no PSRAM), with an ILI9341 on its EXT
    # header from another project of the owner's. Its old firmware was read
    # off first. Bring-up began 2026-10-04.
    "30:ed:a0:c8:9a:b8": ("cardputer-adv", "M5Stack Cardputer ADV, 1.14in 135x240, keyboard, native USB"),
    # Freenove ESP32-S3 Display 2.8in (Amazon B0FSQF6FKN): ESP32-S3R8, 16 MB
    # flash, ILI9341, FT6336 capacitive touch, native USB with its MAC as the
    # serial number. greasy-electronics' BETA build, first run here 2026-10-09.
    "28:84:85:42:ef:28": ("freenove-s3", "Freenove ESP32-S3 Display 2.8in (FNK0104B), native USB"),
    # Freenove ESP32-S3 Display 3.5in (FNK0104N, Amazon's 3.5in of the same
    # line): ST77922 over QSPI, touch in the panel chip. Bring-up began
    # 2026-10-09.
    "b8:1f:3f:c3:fd:58": ("freenove-s3-35", "Freenove ESP32-S3 Display 3.5in (FNK0104N), native USB"),
    "28:84:85:85:12:0c": ("freenove-s3-35", "Freenove ESP32-S3 Display 3.5in, the second one (identified 2026-11-09: running SquachWatch, 480x320, 8 MB PSRAM)"),
    # Marauder V8 (justcallmekoko): ESP32-C5, 8 MB flash, ILI9341 + resistive
    # touch, GPS, MAX17048 gauge, behind a CP210x. Its Marauder v1.12.1 image
    # was read off first (sim/out/marauder_v8/marauder_v8_factory.bin).
    # Bring-up began 2026-10-12.
    "3c:dc:75:81:0a:7c": ("marauder-v8", "Marauder V8, ESP32-C5 behind a CP210x"),
    # LilyGo T-Deck Plus: ESP32-S3FN16R8, 16 MB flash, 8 MB PSRAM, 2.8in ST7789,
    # GT911 touch, keyboard, trackball, SX1262, GPS. Native USB with its MAC as
    # the serial number. Its factory image was read off first
    # (sim/out/tdeck_plus/tdeck_plus_factory.bin). Bring-up began 2026-10-12.
    "30:ed:a0:a5:c3:f8": ("tdeck-plus",  "LilyGo T-Deck Plus, ESP32-S3, native USB"),
    # M5Stack Tab5: ESP32-P4 with an ESP32-C6 radio over SDIO (esp-hosted),
    # 1280x720 MIPI-DSI, ST7123 panel+touch on this unit. Native USB with its
    # MAC as the serial number. Refused here on purpose from 2026-09-29 until
    # 2026-10-19, when the owner said to build it: its MonsterC5 app is backed
    # up whole in sim/out/tab5/. Builds from ~/.pioarduino like the C5s.
    "30:ed:a0:e3:24:83": ("tab5",        "M5Stack Tab5, ESP32-P4 + C6, native USB"),
    # 88:57:21:2e:e6:e0 runs SquachEmit, not this firmware. It is the only
    # CAPACITIVE 2.8in; to test capacitive touch, list it here as cyd-fast
    # for the test and comment it out again after (done 2026-09-21).
    # d4:d4:da:88:62:b8 is something else entirely (HoloCube?): never
}
NEVER_PORTS = {"COM10"}
# COM13 was the wrong device once (2026-08-30) and is never flashed blind. The
# T-Watch S3 enumerates there as native USB with its MAC as the serial, so
# COM13 is allowed only when the MAC read back is the watch's (2026-09-22).
COM13_ONLY_FOR = "a0:f2:62:e1:29:10"

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PENV = os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts")
PY   = os.path.join(PENV, "python.exe")
PIO  = os.path.join(PENV, "pio.exe")


def read_mac(port):
    out = subprocess.run([PY, "-m", "esptool", "--port", port, "--baud", "115200", "read_mac"],
                         capture_output=True, text=True).stdout
    mac = None
    for line in out.splitlines():
        # A chip with an eight-byte MAC (the C5) prints that as "MAC:" and the
        # six-byte one as "BASE MAC:"; the table is keyed on six bytes.
        if line.startswith("BASE MAC:"):
            return line.split(":", 1)[1].strip().lower()
        if line.startswith("MAC: ") and mac is None:
            mac = line[5:].strip().lower()
    return mac


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    port = sys.argv[1].upper()
    if port in NEVER_PORTS:
        sys.exit("refusing %s: that port is never flashed" % port)
    mac = read_mac(port)
    if not mac:
        sys.exit("no ESP32 answered on %s" % port)
    if port == "COM13" and mac != COM13_ONLY_FOR:
        sys.exit("refusing COM13: %s is not the watch (%s)" % (mac, COM13_ONLY_FOR))
    if mac not in BOARDS:
        sys.exit("unknown board %s on %s: add it to BOARDS first, or leave it alone" % (mac, port))
    env, what = BOARDS[mac]
    # --env NAME: a bench variant of this board's own build, and only that
    # (NAME must extend it, e.g. twatch-s3-loratx on a twatch-s3 board).
    build_env = env
    if "--env" in sys.argv:
        want = sys.argv[sys.argv.index("--env") + 1]
        if not want.startswith(env + "-"):
            sys.exit("refusing --env %s: %s runs %s, and a variant must extend it" % (want, mac, env))
        build_env = want
    print("%s: %s -> %s (%s)" % (port, mac, build_env, what))
    if "--check" in sys.argv:
        return
    if env == "twatch-s3":
        # Native USB: PlatformIO's bundled esptool 4.5 loses the link when its
        # stub changes speed, and fails "Unable to verify flash chip" even
        # without. The system esptool 5 programs it fine at the nominal speed.
        # The bootloader lives at 0x0 on an S3, not 0x1000.
        r = subprocess.run([PIO, "run", "-e", build_env], cwd=ROOT)
        if r.returncode != 0:
            sys.exit("build failed")
        build = os.path.join(ROOT, ".pio", "build", build_env)
        boot_app0 = os.path.join(os.path.expanduser("~"), ".platformio", "packages",
                                 "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")
        r = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port, "--baud", "115200",
                            "--before", "default-reset", "--after", "hard-reset", "write-flash", "-z",
                            "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "16MB",
                            "0x0", os.path.join(build, "bootloader.bin"),
                            "0x8000", os.path.join(build, "partitions.bin"),
                            "0xe000", boot_app0,
                            "0x10000", os.path.join(build, "firmware.bin")], cwd=ROOT)
    elif env in ("nm-cyd-c5", "marauder-v8", "tab5"):
        # The C5's toolchain (pioarduino) installs its own core into whatever
        # penv runs it, and run from the shared one it broke `pio` for every
        # board (2026-10-02). It lives in a core dir of its own, and its
        # installer refuses an MSys shell, so MSYSTEM is dropped from the
        # environment it sees.
        core = os.path.join(os.path.expanduser("~"), ".pioarduino")
        pio  = os.path.join(core, "penv", "Scripts", "pio.exe")
        if not os.path.exists(pio):
            sys.exit("the C5 builds from %s; make that venv first (pip install pioarduino)" % pio)
        e = {k: v for k, v in os.environ.items() if k != "MSYSTEM"}
        e["PLATFORMIO_CORE_DIR"] = core
        # esptool 5 draws its progress with characters a Windows console's
        # code page cannot encode; the print raised and the upload sat hung
        # with the port open (the Marauder V8, 2026-10-12).
        e["PYTHONIOENCODING"] = "utf-8"
        r = subprocess.run([pio, "run", "-e", build_env, "-t", "upload", "--upload-port", port], cwd=ROOT, env=e)
    else:
        r = subprocess.run([PIO, "run", "-e", build_env, "-t", "upload", "--upload-port", port], cwd=ROOT)
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
