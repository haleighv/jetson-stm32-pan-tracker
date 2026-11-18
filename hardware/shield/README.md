# Nucleo-H723ZG shield, rev A

A shield for the NUCLEO-H723ZG that replaces the hand-soldered jumper hub: CAN module socket, VNH5019 and encoder headers, sensor and end-of-travel inputs, an e-stop with a hardware path to the driver's EN line, and a breakout header that can cut and probe every signal. All through-hole, 2.54 mm headers, 2 layers.

Status: routed (Freerouting, then cleaned up in KiCad 10). ERC clean. DRC clean, with three courtyard overlaps excluded (Nucleo mounting holes next to the Zio rows, as on the Nucleo itself). Not ordered or tested yet.

## Files

| File | What |
|---|---|
| `nucleo-shield.kicad_pro`, `.kicad_sch`, `.kicad_pcb` | KiCad project, now saved by KiCad 10. |
| `gen/spec.py` | Source of truth: parts, nets, footprints, placement. |
| `gen/gen_sch.py`, `gen/gen_pcb.py`, `gen/gen_bom.py` | Generate the schematic, the placed board and `BOM.csv` from the spec. |
| `gen/check_net.py` | Compares the schematic's netlist with the spec. |
| `nucleo-shield-top.png`, `nucleo-shield-bottom.png` | KiCad 3D renders of the routed board. |
| `schematic.pdf` | Schematic. |
| `fit-check-1to1.pdf` | Copper, silkscreen and outline at 1:1, for the paper fit check. |
| `BOM.csv` | Parts list. |

Once the board is routed in KiCad, the KiCad files are the source; the generators are for rev A only.

## Zio geometry

- The Nucleo's mounting holes are the Arduino Uno holes (UM2407 Figure 6: 48.26, 50.80 and 15.24 mm spacings), so the Arduino rows come from KiCad's `Module:Arduino_UNO_R3`.
- Each Zio connector adds a second row one 2.54 mm column inward. Left side: odd pins outer. Right side: even pins outer. Pin 1 at the ST-LINK end.
- CN7 sits 4.06 mm off the 2.54 mm grid from CN10 (the Arduino D7/D8 gap). CN9 and CN10 end on the same row.
- Origin in `spec.py` is CN8 pin 1. Holes at (0, -13.97), (48.26, -12.70), (33.02, 38.10), (1.27, 62.23), (46.99, 62.23).

## Connectors

| Ref | Name | Pins | Goes to |
|---|---|---|---|
| J1 | MOTOR | 3V3, GND, INA, INB, PWM, ENA, ENB, CS | VNH5019 carrier logic side. ENA and ENB are tied on the board. |
| J2 | ENCODER | GND, 3V3, A, B | goBILDA 5203 encoder, through the breakout and R2/R3 (100 Ω) to PD12/PD13 |
| J3 | CAN | 3V3, GND, TX, RX | SN65HVD230 module (TX to its D, RX to its R) |
| J4 | HOME | 3V3, GND, SIG | Slot sensor, PD14 (TIM4 CH3, input capture) |
| J5, J6 | END+, END- | 3V3, GND, SIG | End-of-travel sensors, PF3 and PG1 |
| J7 | AUX | 3V3, GND, SIG | Spare, PD15 (TIM4 CH4) |
| J8 | ESTOP | SW, GND | Normally closed e-stop switch, PG12 through R5 |
| J9 | BREAKOUT | 2x15, left: device side, right: MCU side | One row per signal: ESTOP, HOME, END+, END-, AUX, PWM, INA, INB, EN, CS, ENC A, ENC B, CAN TX, CAN RX, SCOPE (PG14, both pins). A shunt across each row makes the normal connection. |
| J10 | POWER | 5V, 3V3, GND, GND | Meter and spare power |
| J11-J14 | GND | 2 pins each | Probe grounds, one in each corner |
| JP1, JP2 | STOP END+/- | shunt | Shunt fitted: that end sensor also cuts EN in hardware |

Motor current sense: R1 1 kΩ and C1 100 nF between the breakout's CS row and PF4 (corner about 1.6 kHz), same as the hub.

## Breakout

- Normal running: a shunt on all 14 signal rows (SCOPE needs none).
- Pull a shunt to isolate a device from the Nucleo, then probe either side, or drive the Nucleo side from outside.
- Open-top (tall) shunts leave the pin tips exposed, so probe hooks can clip on with the shunt fitted.
- With a shunt pulled, that Nucleo input floats unless firmware turns on its pull-up.

## Hardware stop

- The e-stop switch (normally closed) holds ESTOP_SW low. An open switch or a broken wire lets R4 pull it high.
- ESTOP_SW and the end sensors (high when blocked, through JP1/JP2) are diode-ORed (D1-D3) into Q1's base. Q1 pulls EN low, the VNH5019 turns off, and the firmware sees the line low with EN released, which latches the motor fault.
- The whole stop path sits on the device side of the breakout, so it still works with the breakout shunts pulled.
- R7 keeps Q1 off when every input is low.
- No e-stop wired: put a shunt on J8, or the driver stays off.

## Before ordering

1. Run ERC and DRC. Expected: clean, with the three courtyard overlaps between Zio headers and Nucleo mounting holes excluded.
2. Print `fit-check-1to1.pdf` at 100% (no fit-to-page), check the scale with a ruler, push the paper onto the Nucleo's Zio sockets, and check every pin lines up.
3. Check the J1 order against the Pololu carrier's silkscreen. I wire it with jumpers, so the order only has to match the labels.
4. Zio sockets need male headers with standard 6 mm mating pins. CN7-CN10 go in from the bottom (plastic under the board, long pins down; outlines and labels are on the bottom silkscreen) and solder on top. Solder them with the shield plugged into the Nucleo so they come out straight. Their footprints differ from the library copies for this reason (DRC warning excluded).

## Routing

- Freerouting (KiCad Plugin and Content Manager), then the session imported and fixed by hand: two GND pins on CN10 tied back to the pour.
- Rules: 0.3 mm tracks, 0.2 mm clearance, 0.6/0.3 mm vias. Relaxed to fab limits where the router squeezed: 0.15 mm minimum track (four short necks), 0.3 mm copper to edge, one thermal spoke allowed on crowded GND header pins.
- +3V3 ended up at 0.3 mm (0.225 mm in places): the Power net class pattern was `+3V3` but KiCad names the net `/+3V3`, so it never applied. Fine for the ~100 mA on 3.3 V.

## Regenerate

```
cd hardware/shield/gen
python3 gen_sch.py ../nucleo-shield.kicad_sch
python3 gen_pcb.py place ../nucleo-shield.kicad_pcb
python3 gen_bom.py > ../BOM.csv
```

`gen_pcb.py` overwrites the board, routing included. Needs KiCad's Python module (`pcbnew`) and `sexpdata`.

Author: Haleigh (haleighv). Copyright (c) 2026 haleighv. SPDX-License-Identifier: MIT
