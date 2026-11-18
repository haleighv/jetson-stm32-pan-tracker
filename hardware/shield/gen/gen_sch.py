#!/usr/bin/env python3
"""
gen_sch.py

Writes nucleo-shield.kicad_sch from spec.py. Every pin gets a net label at
its end (no wires), unused Zio pins get no-connect flags. Symbol UUIDs are
derived from the reference so the board generator can link footprints to
them (KiCad matches symbols and footprints by these paths).

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import sys
import uuid
from pathlib import Path

import sexpdata
from sexpdata import Symbol as S

import spec

LIBDIR = Path("/usr/share/kicad/symbols")
NS = uuid.UUID("6f1c2a52-6a7e-4c3e-9a51-2f4d9b1e7c00")
PROJECT = "nucleo-shield"


def uid(*parts):
    return str(uuid.uuid5(NS, "/".join(str(p) for p in parts)))


def U(*parts):
    """uuid token (unquoted in the file)"""
    return S(uid(*parts))


ROOT = uid("root")

_libs = {}


def lib_symbol(lib_id):
    lib, name = lib_id.split(":")
    if lib not in _libs:
        _libs[lib] = sexpdata.loads((LIBDIR / f"{lib}.kicad_sym").read_text())
    for item in _libs[lib][1:]:
        if isinstance(item, list) and item[0] == S("symbol") and item[1] == name:
            sym = [S("symbol"), lib_id] + item[2:]
            return sym
    raise KeyError(lib_id)


def pins_of(sym):
    """[(number, x, y, rot)] from all unit sub-symbols."""
    out = []

    def walk(node):
        for it in node:
            if isinstance(it, list) and it and it[0] == S("symbol"):
                walk(it)
            elif isinstance(it, list) and it and it[0] == S("pin"):
                at = next(x for x in it if isinstance(x, list) and x[0] == S("at"))
                num = next(x for x in it if isinstance(x, list) and x[0] == S("number"))
                out.append((str(num[1]), float(at[1]), float(at[2]), int(at[3]) if len(at) > 3 else 0))
    walk(sym)
    return out


def effects(size=1.27, justify=None, hide=False):
    e = [S("effects"), [S("font"), [S("size"), size, size]]]
    if justify:
        e.append([S("justify"), S(justify)])
    if hide:
        e.append(S("hide"))
    return e


def prop(name, value, x, y, hide=False, justify=None, pid=0):
    return [S("property"), name, value, [S("at"), x, y, 0], effects(justify=justify, hide=hide)]


def snap(v):
    return round(round(v / 1.27) * 1.27, 4)


def main(out):
    used_libs = {}
    items = []
    labels = []
    nocon = []

    def place(ref, lib_id, value, footprint, pinnets, x, y, desc=""):
        sym = lib_symbol(lib_id)
        used_libs[lib_id] = sym
        su = uid("sym", ref)
        x, y = snap(x), snap(y)
        body = [S("symbol"), [S("lib_id"), lib_id], [S("at"), x, y, 0], [S("unit"), 1],
                [S("in_bom"), S("yes")], [S("on_board"), S("yes")], [S("dnp"), S("yes") if value.startswith("DNP") else S("no")],
                [S("uuid"), S(su)],
                prop("Reference", ref, x, y - 3.0, justify="left"),
                prop("Value", value, x, y + 3.0, justify="left"),
                prop("Footprint", footprint, x, y, hide=True),
                prop("Datasheet", "~", x, y, hide=True)]
        if desc:
            body.append(prop("Description", desc, x, y, hide=True))
        for num, px, py, rot in pins_of(sym):
            body.append([S("pin"), num, [S("uuid"), U("pin", ref, num)]])
            ax, ay = round(x + px, 4), round(y - py, 4)
            net = pinnets.get(num)
            if net is None:
                nocon.append([S("no_connect"), [S("at"), ax, ay], [S("uuid"), U("nc", ref, num)]])
                continue
            # label points away from the symbol body
            ang, just = {0: (180, "right"), 180: (0, "left"), 90: (270, "right"), 270: (90, "left")}[rot]
            labels.append([S("label"), net, [S("at"), ax, ay, ang], [S("fields_autoplaced")],
                           effects(justify=just), [S("uuid"), U("lbl", ref, num)]])
        body.append([S("instances"), [S("project"), PROJECT,
                     [S("path"), f"/{ROOT}", [S("reference"), ref], [S("unit"), 1]]]])
        items.append(body)

    # Zio connectors, column 1
    zx, zy = 30.48, 40.64
    for ref in ("CN7", "CN8", "CN9", "CN10"):
        n = spec.ZIO[ref][0]
        nets = {str(i): spec.ZIO_NETS[f"{ref}-{i}"] for i in range(1, n + 1) if f"{ref}-{i}" in spec.ZIO_NETS}
        h = (n // 2) * 2.54
        place(ref, f"Connector_Generic:Conn_02x{n // 2:02d}_Odd_Even", f"Zio {ref}",
              spec.hdr(2, n // 2), nets, zx, zy + h / 2, "Nucleo-144 Zio connector (male pins down)")
        zy += h + 17.78
        if ref == "CN8":
            zx, zy = 101.6, 40.64

    # other parts in columns
    cols = [172.72, 233.68, 294.64]
    col_y = [40.64, 40.64, 40.64]
    order = sorted(spec.PARTS, key=lambda r: (r.rstrip("0123456789"), int(r.lstrip("JPRCDQ") or 0)))
    for i, ref in enumerate(order):
        lib_id, value, fp, pinnets, _xyr, note = spec.PARTS[ref]
        sym = lib_symbol(lib_id)
        ys = [p[2] for p in pins_of(sym)]
        h = (max(ys) - min(ys)) if ys else 5.08
        c = min(range(3), key=lambda k: col_y[k])
        place(ref, lib_id, value, fp, {str(k): v for k, v in pinnets.items()},
              cols[c], col_y[c] + h / 2 + 2.54, note)
        col_y[c] += h + 15.24

    for j, ref in enumerate(spec.MOUNTS):
        place(ref, "Mechanical:MountingHole", "M3", spec.FP_HOLE, {}, 172.72 + j * 12.7, 270.0)

    notes = [
        "Nucleo-H723ZG pan-tracker shield, rev A. Generated from gen/spec.py: edit the spec, then rerun gen_sch.py and gen_pcb.py.",
        "E-stop: NC switch on J8 holds ESTOP_SW low. Open switch or broken wire -> R4 pulls high -> Q1 pulls EN low -> VNH5019 off, firmware sees the fault.",
        "JP1/JP2 shunts let END+ / END- sensors (high when blocked) cut EN through the same path. Remove a shunt to disable that hardware stop.",
        "No e-stop fitted: put a shunt on J8, or the driver stays disabled.",
    ]
    texts = [[S("text"), t, [S("at"), 30.48, 266.7 + i * 5.08, 0], effects(justify="left"),
              [S("uuid"), U("note", i)]] for i, t in enumerate(notes)]

    sch = [S("kicad_sch"), [S("version"), 20230121], [S("generator"), S("eeschema")],
           [S("uuid"), S(ROOT)], [S("paper"), "A2"],
           [S("title_block"), [S("title"), "Nucleo-H723ZG pan-tracker shield"], [S("date"), "2026-11-18"],
            [S("rev"), "A"], [S("company"), "haleighv"],
            [S("comment"), 1, "Copyright (c) 2026 haleighv. SPDX-License-Identifier: MIT"]],
           [S("lib_symbols")] + list(used_libs.values())]
    sch += nocon + labels + texts + items
    sch.append([S("sheet_instances"), [S("path"), "/", [S("page"), "1"]]])
    Path(out).write_text(sexpdata.dumps(sch) + "\n")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "nucleo-shield.kicad_sch")
