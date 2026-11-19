#!/usr/bin/env python3
"""
gen_bom.py

BOM.csv from spec.py, grouped by value and footprint.

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import csv
import sys
from collections import defaultdict

import spec

# JLCPCB/LCSC parts used for the rev A assembly order
LCSC = {
    "C1": "C2167231", "C5": "C2167231", "C6": "C2167231", "C7": "C2167231",
    "C4": "C88807",
    "CN7": "C124359", "CN8": "C124389", "CN9": "C42372523", "CN10": "C19630945",
    "D1": "C84410", "D2": "C84410", "D3": "C84410",
    "J1": "C124381", "J2": "C124378", "J3": "C124378", "J10": "C124378",
    "J4": "C124376", "J5": "C124376", "J6": "C124376", "J7": "C124376",
    "J8": "C32713268", "J11": "C32713268", "J12": "C32713268", "J13": "C32713268",
    "J14": "C32713268", "JP1": "C32713268", "JP2": "C32713268",
    "J9": "C42372523",
    "Q1": "C118538",
    "R1": "C57435", "R5": "C57435", "R2": "C57438", "R3": "C57438",
    "R4": "C57204", "R6": "C57436", "R7": "C58597",
}

rows = defaultdict(list)
for ref, (n, _, _) in spec.ZIO.items():
    rows[(f"Male header 2x{n // 2}, 2.54 mm (pins down into Zio)", spec.hdr(2, n // 2), "")].append(ref)
for ref, (lib_id, value, fp, _, _, note) in spec.PARTS.items():
    if ref.startswith(("J", "JP")):
        n = len(spec.PARTS[ref][3])
        value = f"Male header {'2x' + str(n // 2) if 'Odd_Even' in lib_id else '1x' + str(n)}, 2.54 mm"
        note = (note + "; " if note else "") + "fit a 2.54 mm shunt" if ref.startswith("JP") else note
    rows[(value, fp, note if ref.startswith(("J", "JP", "Q")) else "")].append(ref)
w = csv.writer(sys.stdout)
w.writerow(["Qty", "Refs", "Value", "Footprint", "LCSC", "Notes"])
key = lambda r: (r.rstrip("0123456789"), int("".join(c for c in r if c.isdigit()) or 0))
for (value, fp, note), refs in sorted(rows.items(), key=lambda kv: key(sorted(kv[1], key=key)[0])):
    refs = sorted(refs, key=key)
    w.writerow([len(refs), " ".join(refs), value, fp.split(":")[1], LCSC.get(refs[0], ""), note])
