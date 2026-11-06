#!/usr/bin/env python3
"""The test flasher's helper (see .github/workflows/test-flasher.yml).

    test_flasher.py manifest DIR CHIP BOOT_OFFSET BOARD LABEL VERSION
        DIR holds bootloader.bin, partitions.bin, boot_app0.bin and firmware.bin
        for one board. They are renamed lab-<BOARD>-<part>.bin and
        DIR/manifest-lab-<BOARD>.json is written for ESP Web Tools: the four
        parts a USB install writes, and nothing over 0x9000-0xdfff, which is
        where the board keeps its settings. The names are the ones the real web
        flasher's ?lab=1 list looks for (web-flasher/index.html).

    test_flasher.py page WEB_FLASHER_DIR SITE
        Copies the real web flasher (index.html, emulator/, update/) into SITE,
        changed in three ways and no other:
          * its hit counter is pointed at nothing. It posts the board of every finished
            flash to the project's public "in the wild" count, and a test flash
            is not one. A page that cannot be switched off is not published.
          * it opens with ?lab=1, which is where the test builds are listed.
          * freenove-s3-40mhz joins the lab list, which the page leaves out.
"""
import json
import os
import shutil
import sys

APP = 0x10000
PARTITIONS = 0x8000
OTADATA = 0xE000
PARTS = ("bootloader", "partitions", "boot_app0", "firmware")


def manifest(d, chip, boot_offset, board, label, version):
    for p in PARTS:
        if not os.path.isfile(os.path.join(d, p + ".bin")):
            sys.exit("missing %s.bin in %s" % (p, d))
    names = {}
    for p in PARTS:
        names[p] = "lab-%s-%s.bin" % (board, p)
        os.replace(os.path.join(d, p + ".bin"), os.path.join(d, names[p]))
    offsets = {"bootloader": int(boot_offset, 0), "partitions": PARTITIONS,
               "boot_app0": OTADATA, "firmware": APP}
    m = {
        "name": "TEST %s -- %s" % (label, board),
        "version": version,
        "new_install_prompt_erase": False,
        "builds": [{
            "chipFamily": chip,
            "parts": [{"path": names[p], "offset": offsets[p]} for p in PARTS],
        }],
    }
    with open(os.path.join(d, "manifest-lab-%s.json" % board), "w") as f:
        json.dump(m, f, indent=2)
        f.write("\n")


def replace_once(text, old, new, what, required):
    if text.count(old) != 1:
        if required:
            sys.exit("web-flasher/index.html no longer has the %s this script changes; not publishing" % what)
        print("warning: %s not found; left as it is" % what)
        return text
    return text.replace(old, new)


def page(src, site):
    with open(os.path.join(src, "index.html"), encoding="utf-8") as f:
        t = f.read()
    t = replace_once(t, "var API = 'https://count.squachwatch.com';", "var API = 'about:blank';",
                     "hit counter address", True)
    t = replace_once(t, "<head>",
                     "<head>\n<script>if(!/(?:^|[?&])lab=1(?:&|$)/.test(location.search))"
                     "location.replace(location.pathname+(location.search?location.search+'&':'?')+'lab=1'+location.hash);</script>",
                     "<head> tag", False)
    t = replace_once(t, "'./manifest-lab-tab5.json']",
                     "'./manifest-lab-tab5.json', './manifest-lab-freenove-s3-40mhz.json']",
                     "lab board list", False)
    os.makedirs(site, exist_ok=True)
    with open(os.path.join(site, "index.html"), "w", encoding="utf-8") as f:
        f.write(t)
    for sub in ("emulator", "update"):
        s = os.path.join(src, sub)
        if os.path.isdir(s):
            shutil.rmtree(os.path.join(site, sub), ignore_errors=True)
            shutil.copytree(s, os.path.join(site, sub))


if __name__ == "__main__":
    if len(sys.argv) == 8 and sys.argv[1] == "manifest":
        manifest(*sys.argv[2:8])
    elif len(sys.argv) == 4 and sys.argv[1] == "page":
        page(*sys.argv[2:4])
    else:
        sys.exit(__doc__)
