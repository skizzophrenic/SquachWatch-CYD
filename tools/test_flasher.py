#!/usr/bin/env python3
"""The test flasher's two small jobs (see .github/workflows/test-flasher.yml).

    test_flasher.py manifest DIR CHIP BOOT_OFFSET BOARD
        DIR holds bootloader.bin, partitions.bin, boot_app0.bin and firmware.bin
        for one board. Writes DIR/manifest.json for ESP Web Tools: the four
        parts a USB install writes, and nothing over 0x9000-0xdfff, which is
        where the board keeps its settings.

    test_flasher.py index SITE
        Writes SITE/index.html: one install button for each SITE/builds/<board>/
        that has an info.json.

Everything that came from a branch name or a commit is HTML-escaped.
"""
import html
import json
import os
import sys

APP = 0x10000
PARTITIONS = 0x8000
OTADATA = 0xE000


def manifest(d, chip, boot_offset, board):
    parts = [
        ("bootloader.bin", int(boot_offset, 0)),
        ("partitions.bin", PARTITIONS),
        ("boot_app0.bin", OTADATA),
        ("firmware.bin", APP),
    ]
    for name, _ in parts:
        if not os.path.isfile(os.path.join(d, name)):
            sys.exit("missing %s in %s" % (name, d))
    m = {
        "name": "SquachWatch test build (%s)" % board,
        "version": "test",
        "new_install_prompt_erase": False,
        "builds": [{
            "chipFamily": chip,
            "parts": [{"path": n, "offset": o} for n, o in parts],
        }],
    }
    with open(os.path.join(d, "manifest.json"), "w") as f:
        json.dump(m, f, indent=2)
        f.write("\n")


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SquachWatch test builds</title>
<style>
  body {{ font: 16px/1.5 system-ui, sans-serif; max-width: 46rem; margin: 2rem auto; padding: 0 1rem; }}
  .b {{ border: 1px solid #8884; border-radius: 8px; padding: .8rem 1rem; margin: .8rem 0; }}
  .b h2 {{ margin: 0 0 .2rem; font-size: 1.1rem; }}
  .m {{ color: #888; font-size: .85rem; margin-bottom: .5rem; }}
  code {{ background: #8882; padding: 0 .3em; border-radius: 4px; }}
</style></head><body>
<h1>SquachWatch test builds</h1>
<p>Unreleased builds, for trying on a device. Plug the board in over USB and press
Install (Chrome or Edge). The install writes the program and leaves the settings on the
board alone, unless you tick erase. Each board shows its latest test build only.</p>
{cards}
<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
</body></html>
"""

CARD = """<div class="b"><h2>{board}</h2>
<div class="m">branch <code>{ref}</code> &middot; commit <code>{sha}</code> &middot; {built} &middot; <a href="{run}">build log</a></div>
<esp-web-install-button manifest="builds/{board}/manifest.json"></esp-web-install-button></div>"""


def index(site):
    cards = []
    root = os.path.join(site, "builds")
    for board in sorted(os.listdir(root)) if os.path.isdir(root) else []:
        info = os.path.join(root, board, "info.json")
        if not os.path.isfile(info):
            continue
        with open(info) as f:
            i = json.load(f)
        run = str(i.get("run", ""))
        if not run.startswith("https://github.com/"):
            run = "#"
        cards.append(CARD.format(
            board=html.escape(board, quote=True),
            ref=html.escape(str(i.get("ref", "")), quote=True),
            sha=html.escape(str(i.get("sha", "")), quote=True),
            built=html.escape(str(i.get("built", "")), quote=True),
            run=html.escape(run, quote=True)))
    if not cards:
        cards = ["<p>No test builds yet.</p>"]
    with open(os.path.join(site, "index.html"), "w") as f:
        f.write(PAGE.format(cards="\n".join(cards)))


if __name__ == "__main__":
    if len(sys.argv) == 6 and sys.argv[1] == "manifest":
        manifest(*sys.argv[2:6])
    elif len(sys.argv) == 3 and sys.argv[1] == "index":
        index(sys.argv[2])
    else:
        sys.exit(__doc__)
