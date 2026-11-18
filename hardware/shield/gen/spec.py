"""
spec.py

Single source of truth for the Nucleo-H723ZG pan-tracker shield, rev A:
parts, footprints, nets and placement. gen_sch.py and gen_pcb.py both read
this, so the schematic and the board can't disagree.

Coordinates (board, mm): X across the board, Y along it, Y grows away from
the ST-LINK end. Origin = CN8 pin 1 (Arduino power header pin 1).
Zio geometry: Arduino rows from KiCad Module:Arduino_UNO_R3, extra Zio rows
one 2.54 mm column inward. Left side: odd pins outer (X 0). Right side: even
pins outer (X 48.26). Checked against UM2407 Rev 6 Figure 6 (hole spacing
48.26, 50.80, 15.24 mm; CN9 and CN10 last rows level).

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""

P = 2.54

# Zio connectors: ref -> (pins, X of pad 1, Y of pad 1). Pad 2 is at X + 2.54.
ZIO = {
    "CN8":  (16, 0.0,   0.0),     # odd outer (Arduino power), even inner
    "CN9":  (30, 0.0,  22.86),    # odd outer (Arduino A0-A5), even inner
    "CN7":  (20, 45.72, -9.14),   # odd inner, even outer (Arduino D8-D15)
    "CN10": (34, 45.72, 17.78),   # odd inner, even outer (Arduino D0-D7)
}

# Nucleo-144 mounting holes under the Zio area (M3), board coords.
HOLES = [(0.0, -13.97), (48.26, -12.70), (33.02, 38.10), (1.27, 62.23), (46.99, 62.23)]

# Board outline: same width as the Nucleo (70 mm), clear of ST-LINK jumpers
# and the RJ45.
OUTLINE = (-10.87, -18.0, 59.13, 66.5)   # x0, y0, x1, y1

# Zio pin -> net. Unlisted pins are left unconnected (no-connect).
ZIO_NETS = {
    # power
    "CN8-7": "+3V3", "CN8-9": "+5V",
    "CN7-8": "GND", "CN8-11": "GND", "CN8-13": "GND", "CN9-12": "GND",
    "CN9-23": "GND", "CN10-5": "GND", "CN10-17": "GND", "CN10-22": "GND",
    "CN10-27": "GND",
    # motor (VNH5019 carrier)
    "CN10-4": "PWM",      # PE9  TIM1_CH1
    "CN10-6": "INA",      # PE11
    "CN10-8": "INB",      # PE14
    "CN10-10": "EN",      # PE13 open drain, ENA/ENB tied
    "CN10-7": "CS",       # PF4  ADC3_INP9, after the RC filter
    # encoder
    "CN10-21": "ENC_A",   # PD12 TIM4_CH1
    "CN10-19": "ENC_B",   # PD13 TIM4_CH2
    # CAN module
    "CN9-25": "CAN_RX",   # PD0 FDCAN1_RX
    "CN9-27": "CAN_TX",   # PD1 FDCAN1_TX
    # debug
    "CN10-12": "SCOPE",   # PG14 control tick toggle
    # sensors and stops
    "CN7-16": "HOME",     # PD14 TIM4_CH3
    "CN7-18": "AUX",      # PD15 TIM4_CH4
    "CN7-20": "END_POS",  # PF3
    "CN9-30": "END_NEG",  # PG1
    "CN10-2": "ESTOP",    # PG12, through R5
}

# Parts: ref -> (lib_id, value, footprint, {pin: net}, (x, y, rot), note)
FP_R = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal"
FP_C = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm"
FP_CP = "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm"
FP_D = "Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal"
FP_Q = "Package_TO_SOT_THT:TO-92_Inline"
FP_H = "Connector_PinHeader_2.54mm:PinHeader_{r}x{n:02d}_P2.54mm_Vertical"
FP_HOLE = "MountingHole:MountingHole_3.2mm_M3"


def hdr(rows, n):
    return FP_H.format(r=rows, n=n)


# Breakout: one row per signal, device side in the left column (odd pins),
# MCU side in the right column (even pins). A shunt across a row makes the
# normal connection; pull it to isolate a device or probe either side.
BREAKOUT = [
    ("ESTOP", "ESTOP_D", "ESTOP"),
    ("HOME", "HOME_D", "HOME"),
    ("END+", "END_POS_D", "END_POS"),
    ("END-", "END_NEG_D", "END_NEG"),
    ("AUX", "AUX_D", "AUX"),
    ("PWM", "PWM_D", "PWM"),
    ("INA", "INA_D", "INA"),
    ("INB", "INB_D", "INB"),
    ("EN", "EN_D", "EN"),
    ("CS", "CS_D", "CS_RAW"),
    ("ENC A", "ENC_A_D", "ENC_A_RAW"),
    ("ENC B", "ENC_B_D", "ENC_B_RAW"),
    ("CAN TX", "CAN_TX_D", "CAN_TX"),
    ("CAN RX", "CAN_RX_D", "CAN_RX"),
    ("SCOPE", "SCOPE", "SCOPE"),
]
J9_PINS = {}
for i, (_, dev, mcu) in enumerate(BREAKOUT):
    J9_PINS[2 * i + 1] = dev
    J9_PINS[2 * i + 2] = mcu

PARTS = {
    # --- off-board connectors (2.54 mm headers), all on the device side ---
    "J1": ("Connector_Generic:Conn_01x08", "MOTOR", hdr(1, 8),
           {1: "+3V3", 2: "GND", 3: "INA_D", 4: "INB_D", 5: "PWM_D", 6: "EN_D", 7: "EN_D", 8: "CS_D"},
           (56.5, 14.0, 0), "VNH5019 carrier logic: VDD GND INA INB PWM ENA ENB CS"),
    "J2": ("Connector_Generic:Conn_01x04", "ENCODER", hdr(1, 4),
           {1: "GND", 2: "+3V3", 3: "ENC_A_D", 4: "ENC_B_D"},
           (56.5, 40.0, 0), "goBILDA 5203 encoder"),
    "J3": ("Connector_Generic:Conn_01x04", "CAN", hdr(1, 4),
           {1: "+3V3", 2: "GND", 3: "CAN_TX_D", 4: "CAN_RX_D"},
           (-8.3, 46.0, 0), "SN65HVD230 module: VCC GND TX(D) RX(R)"),
    "J4": ("Connector_Generic:Conn_01x03", "HOME", hdr(1, 3),
           {1: "+3V3", 2: "GND", 3: "HOME_D"}, (6.5, -16.3, 90), "Slot sensor"),
    "J5": ("Connector_Generic:Conn_01x03", "END+", hdr(1, 3),
           {1: "+3V3", 2: "GND", 3: "END_POS_D"}, (15.5, -16.3, 90), "End of travel, positive"),
    "J6": ("Connector_Generic:Conn_01x03", "END-", hdr(1, 3),
           {1: "+3V3", 2: "GND", 3: "END_NEG_D"}, (24.5, -16.3, 90), "End of travel, negative"),
    "J7": ("Connector_Generic:Conn_01x03", "AUX", hdr(1, 3),
           {1: "+3V3", 2: "GND", 3: "AUX_D"}, (33.5, -16.3, 90), "Spare sensor input"),
    "J8": ("Connector_Generic:Conn_01x02", "ESTOP", hdr(1, 2),
           {1: "ESTOP_SW", 2: "GND"}, (56.5, 2.0, 0), "NC switch; fit a shunt if no e-stop"),
    "J9": ("Connector_Generic:Conn_02x15_Odd_Even", "BREAKOUT", hdr(2, 15), J9_PINS,
           (22.86, 12.0, 0), "Shunt per row: device side (odd) to MCU side (even)"),
    "J10": ("Connector_Generic:Conn_01x04", "POWER", hdr(1, 4),
            {1: "+5V", 2: "+3V3", 3: "GND", 4: "GND"}, (14.0, 64.6, 90), "Meter and spare power"),
    "J11": ("Connector_Generic:Conn_01x02", "GND", hdr(1, 2), {1: "GND", 2: "GND"},
            (-7.5, -16.5, 0), "Probe ground, top left"),
    "J12": ("Connector_Generic:Conn_01x02", "GND", hdr(1, 2), {1: "GND", 2: "GND"},
            (56.5, -16.5, 0), "Probe ground, top right"),
    "J13": ("Connector_Generic:Conn_01x02", "GND", hdr(1, 2), {1: "GND", 2: "GND"},
            (-7.5, 61.5, 0), "Probe ground, bottom left"),
    "J14": ("Connector_Generic:Conn_01x02", "GND", hdr(1, 2), {1: "GND", 2: "GND"},
            (56.5, 61.5, 0), "Probe ground, bottom right"),
    "JP1": ("Connector_Generic:Conn_01x02", "STOP_EN+", hdr(1, 2),
            {1: "END_POS_D", 2: "END_POS_HW"}, (29.0, -1.5, 90), "Shunt: END+ cuts EN in hardware"),
    "JP2": ("Connector_Generic:Conn_01x02", "STOP_EN-", hdr(1, 2),
            {1: "END_NEG_D", 2: "END_NEG_HW"}, (29.0, 2.5, 90), "Shunt: END- cuts EN in hardware"),
    # --- current sense filter (MCU side of the breakout) ---
    "R1": ("Device:R", "1k", FP_R, {1: "CS_RAW", 2: "CS"}, (30.0, 13.5, 0), "CS RC, ~1.6 kHz"),
    "C1": ("Device:C", "100n", FP_C, {1: "CS", 2: "GND"}, (35.0, 17.5, 0), "CS RC"),
    # --- encoder (series R, optional C), MCU side of the breakout ---
    "R2": ("Device:R", "100", FP_R, {1: "ENC_A_RAW", 2: "ENC_A"}, (39.0, 22.0, 270), "Series"),
    "R3": ("Device:R", "100", FP_R, {1: "ENC_B_RAW", 2: "ENC_B"}, (42.0, 22.0, 270), "Series"),
    "C2": ("Device:C", "DNP 1n", FP_C, {1: "ENC_A", 2: "GND"}, (30.0, 25.5, 0), "Optional"),
    "C3": ("Device:C", "DNP 1n", FP_C, {1: "ENC_B", 2: "GND"}, (30.0, 29.5, 0), "Optional"),
    # --- 3V3 bypass ---
    "C4": ("Device:C_Polarized", "10u", FP_CP, {1: "+3V3", 2: "GND"}, (9.0, 11.5, 0), "Bulk"),
    "C5": ("Device:C", "100n", FP_C, {1: "+3V3", 2: "GND"}, (8.0, 17.5, 0), "Bypass"),
    "C6": ("Device:C", "100n", FP_C, {1: "+3V3", 2: "GND"}, (42.0, -8.5, 270), "Bypass, sensors"),
    "C7": ("Device:C", "100n", FP_C, {1: "+3V3", 2: "GND"}, (30.0, 21.5, 0), "Bypass, motor"),
    # --- e-stop and hardware stop (device side, works with shunts pulled) ---
    "R4": ("Device:R", "4k7", FP_R, {1: "+3V3", 2: "ESTOP_SW"}, (8.0, -9.5, 0), "Pull-up: open switch = stop"),
    "R5": ("Device:R", "1k", FP_R, {1: "ESTOP_SW", 2: "ESTOP_D"}, (8.0, -5.5, 0), "Series to PG12"),
    "D1": ("Device:D", "1N4148", FP_D, {1: "STOP", 2: "ESTOP_SW"}, (8.0, -1.5, 0), "OR into STOP"),
    "D2": ("Device:D", "1N4148", FP_D, {1: "STOP", 2: "END_POS_HW"}, (8.0, 2.5, 0), "OR into STOP"),
    "D3": ("Device:D", "1N4148", FP_D, {1: "STOP", 2: "END_NEG_HW"}, (8.0, 6.5, 0), "OR into STOP"),
    "R6": ("Device:R", "10k", FP_R, {1: "STOP", 2: "Q_B"}, (29.0, -9.5, 0), "Base"),
    "R7": ("Device:R", "100k", FP_R, {1: "STOP", 2: "GND"}, (29.0, -5.5, 0), "Off when all inputs low"),
    "Q1": ("Transistor_BJT:2N3904", "2N3904", FP_Q, {1: "GND", 2: "Q_B", 3: "EN_D"}, (36.0, 1.0, 0),
           "Pulls EN low: VNH5019 off"),
}

MOUNTS = {f"H{i + 1}": xy for i, xy in enumerate(HOLES)}

# Net classes: power gets wide tracks.
POWER_NETS = {"+3V3", "+5V", "GND"}
