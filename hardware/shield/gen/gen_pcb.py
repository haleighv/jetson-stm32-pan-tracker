#!/usr/bin/env python3
"""
gen_pcb.py

Builds nucleo-shield.kicad_pcb from spec.py: outline, footprints linked to
the schematic symbols, nets, silkscreen pin labels, GND pours. Routing is
done separately (Freerouting), then route_import.py adds the tracks.

Usage: gen_pcb.py place OUT.kicad_pcb
       gen_pcb.py finish IN.kicad_pcb ROUTES.ses OUT.kicad_pcb

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import re
import sys

import pcbnew
from pcbnew import FromMM, VECTOR2I

import spec
from gen_sch import uid

FPDIR = "/usr/share/kicad/footprints"
OX, OY = 60.0, 50.0          # put the board on the sheet
TRACK, CLEAR, VIA, DRILL = 0.3, 0.2, 0.6, 0.3


def P(x, y):
    return VECTOR2I(FromMM(x + OX), FromMM(y + OY))


def load(fpid):
    lib, name = fpid.split(":")
    fp = pcbnew.FootprintLoad(f"{FPDIR}/{lib}.pretty", name)
    if fp is None:
        raise KeyError(fpid)
    return fp


def silk(board, text, x, y, size=0.9, rot=0, layer=pcbnew.F_SilkS, just=0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(P(x, y))
    t.SetLayer(layer)
    t.SetTextSize(VECTOR2I(FromMM(size), FromMM(size)))
    t.SetTextThickness(FromMM(0.15))
    t.SetTextAngleDegrees(rot)
    if just:
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT if just < 0 else pcbnew.GR_TEXT_H_ALIGN_RIGHT)
    board.Add(t)


def line(board, a, b, layer=pcbnew.Edge_Cuts, w=0.1):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(P(*a))
    s.SetEnd(P(*b))
    s.SetLayer(layer)
    s.SetWidth(FromMM(w))
    board.Add(s)


def place():
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetTrackWidth(FromMM(TRACK))
    nc.SetClearance(FromMM(CLEAR))
    nc.SetViaDiameter(FromMM(VIA))
    nc.SetViaDrill(FromMM(DRILL))
    ds.m_MinClearance = FromMM(CLEAR)
    ds.m_TrackMinWidth = FromMM(0.2)
    ds.m_CopperEdgeClearance = FromMM(0.4)

    nets = {}

    def net(name):
        if name not in nets:
            n = pcbnew.NETINFO_ITEM(board, name)
            board.Add(n)
            nets[name] = n
        return nets[name]

    def add(ref, fpid, value, x, y, rot, pinnets):
        fp = load(fpid)
        lib, name = fpid.split(":")
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(ref)
        fp.SetValue(value)
        fp.SetPosition(P(x, y))
        fp.SetOrientationDegrees(rot)
        fp.SetPath(pcbnew.KIID_PATH(f"/{uid('sym', ref)}"))
        board.Add(fp)
        for pad in fp.Pads():
            n = pinnets.get(pad.GetNumber())
            if n:
                pad.SetNet(net(n))
        return fp

    # Zio headers
    for ref, (n, x, y) in spec.ZIO.items():
        pn = {k.split("-")[1]: v for k, v in spec.ZIO_NETS.items() if k.startswith(ref + "-")}
        fp = add(ref, spec.hdr(2, n // 2), f"Zio {ref}", x, y, 0, pn)
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
    for ref, (lib_id, value, fpid, pinnets, (x, y, rot), note) in spec.PARTS.items():
        fp = add(ref, fpid, value, x, y, rot, {str(k): v for k, v in pinnets.items()})
        fp.Value().SetVisible(False)
        if ref.startswith("J"):
            fp.Reference().SetVisible(False)   # silk titles name the connectors
    for ref, (x, y) in spec.MOUNTS.items():
        fp = add(ref, spec.FP_HOLE, "M3", x, y, 0, {})
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)

    # outline
    x0, y0, x1, y1 = spec.OUTLINE
    for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        line(board, a, b)

    # Zio connector names, outside the pin rows
    for ref, (n, x, y) in spec.ZIO.items():
        right = x > 20
        silk(board, ref, x - 1.6, y - 2.2, 0.8, just=1)

    # header pin labels
    def pinlabels(ref, names, side):
        _, _, _, _, (x, y, rot), _ = spec.PARTS[ref]
        for i, nm in enumerate(names):
            if rot == 0:      # vertical header, pins down the Y axis; text beside the pads
                px, py = x, y + i * 2.54
                silk(board, nm, px + (-1.6 if side < 0 else 1.6), py, 0.8, just=-side)
            else:             # rotated 90: pins along +X; text under each pad
                px, py = x + i * 2.54, y
                silk(board, nm, px, py + (1.9 if side > 0 else -1.9), 0.8)

    pinlabels("J1", ["3V3", "GND", "INA", "INB", "PWM", "ENA", "ENB", "CS"], -1)
    pinlabels("J2", ["GND", "3V3", "A", "B"], -1)
    pinlabels("J3", ["3V3", "GND", "TX", "RX"], 1)
    for ref in ("J4", "J5", "J6", "J7"):
        pinlabels(ref, ["3V3", "GND", "SIG"], 1)
    pinlabels("J8", ["SW", "GND"], -1)
    pinlabels("J10", ["5V", "3V3", "GND", "GND"], -1)
    # connector titles
    for ref, title, dx, dy, rot in (("J1", "MOTOR", 0.0, -2.6, 0), ("J2", "ENCODER", 0.0, -2.6, 0),
                                    ("J3", "CAN", 1.0, -2.6, 0), ("J4", "HOME", 2.54, 3.5, 0),
                                    ("J5", "END+", 2.54, 3.5, 0), ("J6", "END-", 2.54, 3.5, 0),
                                    ("J7", "AUX", 2.54, 3.5, 0), ("J8", "ESTOP NC", -2.0, -2.4, 0),
                                    ("J10", "POWER", -4.2, 0.0, 0),
                                    ("JP1", "STOP END+", 1.27, -2.0, 0), ("JP2", "STOP END-", 1.27, 2.4, 0)):
        _, _, _, _, (x, y, _), _ = spec.PARTS[ref]
        silk(board, title, x + dx, y + dy, 0.9)
    for ref in ("J11", "J12", "J13", "J14"):
        _, _, _, _, (x, y, _), _ = spec.PARTS[ref]
        silk(board, "GND", x, y + (4.9 if y < 0 else -2.4), 0.9)
    # breakout: signal name left of each row, column headings on top
    _, _, _, _, (x, y, _), _ = spec.PARTS["J9"]
    for i, (name, _, _) in enumerate(spec.BREAKOUT):
        silk(board, name, x - 1.8, y + i * 2.54, 0.8, just=1)
    silk(board, "DEV", x - 0.6, y - 2.3, 0.8)
    silk(board, "MCU", x + 3.1, y - 2.3, 0.8)
    silk(board, "BREAKOUT", x + 1.27, y + len(spec.BREAKOUT) * 2.54 + 0.4, 0.9)
    silk(board, "NUCLEO-H723ZG SHIELD rev A", 24.0, 60.6, 1.1)
    silk(board, "haleighv 2026 MIT", 33.5, 64.6, 0.8, just=-1)
    silk(board, "ST-LINK END  >>", -9.4, 10.0, 0.9, rot=90)

    # GND pours, both layers
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(net("GND"))
        z.SetLocalClearance(FromMM(0.3))
        z.SetMinThickness(FromMM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetIsRuleArea(False)
        ol = z.Outline()
        ol.NewOutline()
        m = 0.5
        for (x, y) in ((x0 + m, y0 + m), (x1 - m, y0 + m), (x1 - m, y1 - m), (x0 + m, y1 - m)):
            ol.Append(FromMM(x + OX), FromMM(y + OY))
        z.SetZoneName(f"GND_{'F' if layer == pcbnew.F_Cu else 'B'}")
        board.Add(z)
    return board


def finish(board, ses):
    """Add tracks and vias from a Freerouting .ses file, then fill zones."""
    txt = open(ses).read()
    res = re.search(r"\(resolution\s+(\w+)\s+(\d+)\)", txt)
    unit, div = res.group(1), int(res.group(2))
    scale = {"um": 1e-3, "mm": 1.0, "mil": 0.0254}[unit] / div   # file units -> mm
    nets = {n.GetNetname(): n for n in board.GetNetsByName().values()}
    netout = txt[txt.find("(network_out"):]
    count = [0, 0]
    for m in re.finditer(r"\(net\s+(\"[^\"]*\"|\S+)(.*?)(?=\(net\s|\Z)", netout, re.S):
        name = m.group(1).strip('"')
        body = m.group(2)
        n = nets.get(name)
        for w in re.finditer(r"\(path\s+(\S+)\s+(\d+)\s+([-\d\s]+)\)", body):
            layer = pcbnew.F_Cu if w.group(1) in ("F.Cu", "Front") else pcbnew.B_Cu
            width = int(w.group(2)) * scale
            xy = [int(v) * scale for v in w.group(3).split()]
            pts = list(zip(xy[0::2], xy[1::2]))
            for (ax, ay), (bx, by) in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(VECTOR2I(FromMM(ax), FromMM(-ay)))
                t.SetEnd(VECTOR2I(FromMM(bx), FromMM(-by)))
                t.SetWidth(FromMM(width))
                t.SetLayer(layer)
                if n:
                    t.SetNet(n)
                board.Add(t)
                count[0] += 1
        for v in re.finditer(r"\(via\s+(\"[^\"]*\"|\S+)\s+(-?\d+)\s+(-?\d+)", body):
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(VECTOR2I(FromMM(int(v.group(2)) * scale), FromMM(-int(v.group(3)) * scale)))
            via.SetWidth(FromMM(VIA))
            via.SetDrill(FromMM(DRILL))
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            if n:
                via.SetNet(n)
            board.Add(via)
            count[1] += 1
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    print(f"tracks {count[0]}, vias {count[1]}")
    return board


if __name__ == "__main__":
    if sys.argv[1] == "place":
        b = place()
        pcbnew.SaveBoard(sys.argv[2], b)
        pcbnew.ExportSpecctraDSN(b, sys.argv[2].replace(".kicad_pcb", ".dsn"))
    else:
        b = pcbnew.LoadBoard(sys.argv[2])
        finish(b, sys.argv[3])
        pcbnew.SaveBoard(sys.argv[4], b)
