#!/usr/bin/env python3
"""Read src/signatures.cpp against the IEEE MA-L registry.

signatures_test.cpp says, correctly:

    This cannot re-check the registry (CI has no business fetching 40,000
    rows on every push), so it checks the properties that hold regardless
    of what the registry says, plus a few anchors from the audit.

That is right about every push and not about never. This does the fetch on a
schedule instead, so the registry half of the audit stops being a thing a
person has to remember to redo. The audit done by hand found a block labelled
"Vigilant" that belongs to Sonos, after eleven releases; the one this script
was written for found `BC:DD:C2` labelled "Arlo" and registered to Espressif,
which made every ESP32 in that block -- SquachWatch included -- read as a
covert camera.

It deliberately does NOT just diff the label against the registrant. Most of
this table is module vendors on purpose: Liteon blocks really are in Flock
cameras, Amazon's umbrella block really does cover Ring, and the HC-05 maker
really is a skimmer part. A naive comparison flags 26 of 110 rows and ~24 of
those are correct and documented -- an audit nobody can get to zero is one
everybody learns to ignore. So the knowingly-indirect labels are declared
below, and anything else is reported.

Usage:
    tools/check_ouis.py --fetch              # download the registry, then check
    tools/check_ouis.py --registry oui.csv   # check against a local copy

Exit status: 1 if a row is a hard error, 0 otherwise. Soft notes do not fail
the job -- they are for a human to rule on, and the ruling goes in ALLOW.
"""

import argparse
import csv
import re
import sys
import urllib.request

REGISTRY_URL = "https://standards-oui.ieee.org/oui/oui.csv"
ROW = re.compile(
    r"\{\{0x([0-9A-Fa-f]{2}),\s*0x([0-9A-Fa-f]{2}),\s*0x([0-9A-Fa-f]{2})\}\s*,"
    r'\s*"([^"]*)"\s*,\s*DetectionType::(\w+)\s*,\s*Confidence::(\w+)'
)

# A label that is NOT the registrant, on purpose, with the registrant it is
# allowed to be. This is the table's institutional memory: every entry here is
# a decision somebody made, so a new mismatch is genuinely new.
ALLOW = {
    "Flock":       ["liteon", None],   # one field-list block is unregistered
    "Flock-Liteo": ["liteon"],
    "Flock-QCA":   ["atheros", "qualcomm"],
    "Flock-SiLab": ["silicon lab"],
    "Flock-UGSI":  ["universal global scientific"],
    # Was "Flock-Sierra" for a long time, which the audit found is neither
    # Sierra nor Flock; relabelled rather than removed.
    "Flock-OEM":   ["spectra", None],
    "Flock-DeFlk": [None],
    "ALPR-Mtrla":  ["motorola"],
    "ALPR-Gentec": ["genetec"],
    "DigitalAlly": ["digital ally"],
    "Axon-Signal": ["wnc"],         # Wistron NeWeb, a module maker
    "Axon-Body":   [None],
    "Ring":        ["ring", "amazon"],
    "Skim-HC05":   ["bolutek"],
    "Skim-CSR":    ["cyber-blue"],
    "Skim-Linvor": [None],
    "Wyze-Mod":    ["murata"],      # labelled -Mod for exactly this reason
    "Blink":       ["blink", "amazon", None],
    "Realtek":     ["realtek"],
    "Hak5-LA":     [None],
    "Tuya":        ["tuya", "telink"],   # see the note in the PR; Low either way
}

# Blocks belonging to the chip this project runs on. DETECTIONS.md says a
# "flag any Espressif OUI as a possible camera" fallback is NOT built; a row
# that matches one anyway is that fallback, shipped by accident.
SELF = ("espressif",)


def load_registry(path=None, fetch=False):
    if fetch:
        with urllib.request.urlopen(REGISTRY_URL, timeout=180) as r:
            text = r.read().decode("utf-8", "replace")
    else:
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
    reg = {}
    for row in csv.DictReader(text.splitlines()):
        a = (row.get("Assignment") or "").strip().upper()
        if len(a) == 6:
            reg[a] = (row.get("Organization Name") or "").strip()
    if len(reg) < 10000:
        sys.exit("check_ouis: that does not look like the MA-L registry (%d rows)" % len(reg))
    return reg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--registry")
    ap.add_argument("--fetch", action="store_true")
    ap.add_argument("--signatures", default="src/signatures.cpp")
    a = ap.parse_args()
    if not a.fetch and not a.registry:
        ap.error("pass --fetch or --registry")

    reg = load_registry(a.registry, a.fetch)
    with open(a.signatures, encoding="utf-8") as f:
        rows = ROW.findall(f.read())
    if not rows:
        sys.exit("check_ouis: parsed no rows out of %s" % a.signatures)

    errors, notes = [], []
    for o1, o2, o3, label, dtype, conf in rows:
        key = (o1 + o2 + o3).upper()
        mac = "%s:%s:%s" % (o1.upper(), o2.upper(), o3.upper())
        org = reg.get(key)
        local = int(o1, 16) & 0x02

        if local:
            if conf != "LOW_CONF":
                errors.append("%s %s is locally administered, so it identifies no "
                              "vendor, but is graded %s" % (mac, label, conf))
            continue

        if org and any(s in org.lower() for s in SELF):
            errors.append("%s is registered to %s and matches %s as %s. This board is "
                          "an ESP32: it would read itself, and DETECTIONS.md says this "
                          "fallback is not built." % (mac, org, dtype, label))
            continue

        if org is None:
            if conf != "LOW_CONF":
                errors.append("%s %s is not in the registry but is graded %s"
                              % (mac, label, conf))
            elif label not in ALLOW or None not in ALLOW[label]:
                notes.append("%s %s is not in the registry (Low, so allowed, but "
                             "undeclared)" % (mac, label))
            continue

        want = ALLOW.get(label)
        if want is not None:
            if any(w and w in org.lower() for w in want):
                continue
            notes.append("%s %s is declared as %s but the registry says %r"
                         % (mac, label, "/".join(w for w in want if w), org))
            continue

        base = label.split("-")[0].lower()
        if base and base not in org.lower():
            notes.append("%s label %r is not corroborated by the registrant %r"
                         % (mac, label, org))

    print("check_ouis: %d rows against %d MA-L assignments" % (len(rows), len(reg)))
    for n in notes:
        print("  note:  %s" % n)
    for e in errors:
        print("  ERROR: %s" % e, file=sys.stderr)
    if errors:
        print("\ncheck_ouis: %d row(s) need fixing." % len(errors), file=sys.stderr)
        return 1
    print("check_ouis: no hard errors%s." % (" (%d note(s))" % len(notes) if notes else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
