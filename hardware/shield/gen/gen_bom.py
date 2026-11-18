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
w.writerow(["Qty", "Refs", "Value", "Footprint", "Notes"])
key = lambda r: (r.rstrip("0123456789"), int("".join(c for c in r if c.isdigit()) or 0))
for (value, fp, note), refs in sorted(rows.items(), key=lambda kv: key(sorted(kv[1], key=key)[0])):
    refs = sorted(refs, key=key)
    w.writerow([len(refs), " ".join(refs), value, fp.split(":")[1], note])
