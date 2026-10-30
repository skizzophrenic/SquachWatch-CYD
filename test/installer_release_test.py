"""Offline release contract checks; no toolchain, device or network required."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENV = "cyd-elecrow-gps"
MANIFEST = f"manifest-{ENV}.json"
RELEASE = (ROOT / ".github/workflows/release-flasher.yml").read_text()
BUILD = (ROOT / ".github/workflows/build.yml").read_text()


def step(name):
    """Extract one named step from this workflow's fixed indentation."""
    start = RELEASE.index(f"      - name: {name}\n")
    end = RELEASE.find("\n      - ", start + 1)
    return RELEASE[start:end if end != -1 else None]


def script(name):
    return textwrap.dedent(step(name).split("        run: |\n", 1)[1])


class InstallerReleaseTest(unittest.TestCase):
    def test_manifest_layout_and_erase_policy_match_ordinary_cyd(self):
        base = json.loads((ROOT / "web-flasher/manifest-cyd.json").read_text())
        kit = json.loads((ROOT / "web-flasher" / MANIFEST).read_text())
        self.assertIn("BETA", kit["name"])
        self.assertEqual(kit["version"], "0.0.0")
        self.assertIs(kit["new_install_prompt_erase"], True)
        self.assertEqual(kit["new_install_prompt_erase"], base["new_install_prompt_erase"])
        base["builds"][0]["parts"][-1]["path"] = f"{ENV}-firmware.bin"
        self.assertEqual(kit["builds"], base["builds"])
        self.assertEqual([x["offset"] for x in kit["builds"][0]["parts"]],
                         [0x1000, 0x8000, 0xe000, 0x10000])

    def test_counter_allowlist_and_display_name_include_profile(self):
        worker = (ROOT / "tools/install-counter/worker.js").read_text()
        allowlist = worker.split("const BOARDS = new Set([", 1)[1].split("]);", 1)[0]
        self.assertIn(f'"{ENV}"', allowlist)
        page = (ROOT / "web-flasher/index.html").read_text()
        names = page.split("var NAMES = {", 1)[1].split("};", 1)[0]
        self.assertIn(f"'{ENV}':", names)

    def test_explicit_build_ci_copy_sign_release_and_pages(self):
        self.assertRegex(step("Build firmware"), rf"pio run[^\n]* -e {ENV}(?: |\n)")
        self.assertIn(f"run: pio run -e {ENV}\n", BUILD)
        self.assertIn("python3 test/installer_release_test.py", BUILD)
        self.assertIn("node test/installer_picker_test.js", BUILD)
        merge = step("Merge into one flashable image (per board)")
        loops = re.findall(r"for v in ([^;]+); do", merge)
        self.assertTrue(any(ENV in x.split() for x in loops))
        self.assertIn(f"cp web-flasher/{MANIFEST} {MANIFEST}", step("Stamp manifest versions"))
        sign = step("Sign firmware for Bluetooth updates")
        self.assertRegex(sign, rf"for v in [^\n]*\b{ENV}\b")
        self.assertIn('--env "$v"', sign)
        assets = step("Create GitHub Release with build artifacts")
        self.assertIn(f"{ENV}-firmware.bin \\\n", assets)
        self.assertIn(f"{MANIFEST} \\\n", assets)
        pages = step("Assemble Pages site")
        self.assertIn(f"cp web-flasher/{MANIFEST} _site/{MANIFEST}", pages)
        self.assertRegex(pages, rf"for board in [^\n]*\b{ENV}\b[^\n]*; do")
        self.assertIn(f'"{ENV}"', pages)  # lab manifest generator
        self.assertIn('part["path"].startswith("esp32-")', pages)
        self.assertIn('name in shipped', pages)  # preserve released root on lab deploy
        self.assertIn('re.escape(name)', pages)  # prune unreleased options

    def test_version_stamping_executes_for_new_manifest(self):
        # Run the real embedded Python, in a temporary copy of the manifests.
        body = script("Stamp manifest versions")
        py = body.split("python - <<'PY'\n", 1)[1].split("\nPY", 1)[0]
        py = py.replace("${{ steps.ver.outputs.version }}", "99.1.2")
        py = py.replace("${{ steps.ver.outputs.tag }}", "v99.1.2")
        with tempfile.TemporaryDirectory() as td:
            shutil.copytree(ROOT / "web-flasher", Path(td) / "web-flasher",
                            ignore=shutil.ignore_patterns("emulator", "update"))
            subprocess.run(["python3", "-c", py], cwd=td, check=True, capture_output=True)
            stamped = json.loads((Path(td) / "web-flasher" / MANIFEST).read_text())
            self.assertEqual(stamped["version"], "99.1.2")
            self.assertIn("BETA", stamped["name"])

    def test_lab_restore_prunes_unreleased_kit_then_generates_separate_lab_assets(self):
        pages = script("Assemble Pages site")
        embedded = re.findall(r"python - <<'PY'\n(.*?)\nPY", pages, re.S)
        self.assertEqual(len(embedded), 2)
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            site = root / "_site"
            site.mkdir()
            (root / "web-flasher").mkdir()
            for name in ("manifest-cyd.json", MANIFEST):
                shutil.copy(ROOT / "web-flasher" / name, site / name)
                shutil.copy(ROOT / "web-flasher" / name, root / "web-flasher" / name)
            original = json.loads((site / "manifest-cyd.json").read_text())
            for part in original["builds"][0]["parts"]:
                (site / part["path"]).write_bytes(b"released fixture")
            (site / "index.html").write_text((ROOT / "web-flasher/index.html").read_text())
            # Fake only gh's read-only list of assets: the release never shipped the kit.
            fakebin = root / "bin"
            fakebin.mkdir()
            gh = fakebin / "gh"
            gh.write_text('#!/bin/sh\nprintf "manifest-cyd.json\\n"\n')
            gh.chmod(0o755)
            env = dict(os.environ, PATH=str(fakebin) + os.pathsep + os.environ["PATH"],
                       LATEST="v1.0.0", GITHUB_REPOSITORY="offline/test")
            subprocess.run(["python3", "-c", embedded[0]], cwd=td, env=env,
                           check=True, capture_output=True)
            self.assertFalse((site / MANIFEST).exists())
            self.assertNotIn(f'<option value="./{MANIFEST}"', (site / "index.html").read_text())
            self.assertTrue((site / "manifest-cyd.json").exists())
            (root / f"{ENV}-firmware.bin").write_bytes(b"new lab firmware")
            (root / f"{ENV}-firmware.sig").write_bytes(b"lab signature")
            lab = embedded[1].replace("${{ steps.ver.outputs.label }}", "GPS test")
            lab = lab.replace("${{ steps.ver.outputs.version }}", "lab-gps-test")
            subprocess.run(["python3", "-c", lab], cwd=td, check=True, capture_output=True)
            manifest = json.loads((site / f"manifest-lab-{ENV}.json").read_text())
            self.assertEqual(manifest["version"], "lab-gps-test")
            parts = manifest["builds"][0]["parts"]
            self.assertEqual([p["path"] for p in parts[:3]],
                             [p["path"] for p in original["builds"][0]["parts"][:3]])
            self.assertEqual(parts[-1]["path"], f"lab-{ENV}-firmware.bin")
            self.assertEqual((site / parts[-1]["path"]).read_bytes(), b"new lab firmware")
            self.assertTrue((site / f"lab-{ENV}-firmware.sig").exists())
            self.assertEqual((site / "esp32-partitions.bin").read_bytes(), b"released fixture")

    def test_shared_component_gate_includes_accessory_and_rejects_mismatch(self):
        body = script("Merge into one flashable image (per board)")
        begin = body.index("for f in bootloader partitions; do")
        end = body.index("\n# The OTA data partition", begin)
        gate = body[begin:end]
        variants = re.search(r"for v in ([^;]+); do", gate).group(1).split()
        self.assertIn(ENV, variants)
        with tempfile.TemporaryDirectory() as td:
            for v in variants:
                path = Path(td) / ".pio/build" / v
                path.mkdir(parents=True)
                for f in ("bootloader", "partitions"):
                    (path / f"{f}.bin").write_bytes(b"matching test fixture")
            ok = subprocess.run(["bash", "-e", "-c", gate], cwd=td, capture_output=True)
            self.assertEqual(ok.returncode, 0, ok.stderr.decode())
            (Path(td) / f".pio/build/{ENV}/partitions.bin").write_bytes(b"different layout")
            bad = subprocess.run(["bash", "-e", "-c", gate], cwd=td, capture_output=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn(f"{ENV}/partitions.bin differs", bad.stdout.decode())


if __name__ == "__main__":
    unittest.main(verbosity=2)
