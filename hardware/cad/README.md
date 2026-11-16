# CAD

## VNH5019 enclosure (v3)

Enclosure for the Pololu VNH5019 motor driver carrier (#1451). Designed in Fusion, printed on a Bambu A1 in PETG (0.20 mm layers). Public print profile: [MakerWorld](https://makerworld.com/en/models/3612437-vnh5019-enclosure#profileId-4140255).

| File | What |
|---|---|
| `vnh5019-shell-v3.step` | Top shell (editable in any CAD tool) |
| `vnh5019-base-v3.step` | Base plate |
| `vnh5019-enclosure-v3.3mf` | Bambu Studio project, both parts on the plate |

Design:
- Shell: slotted vents on two sides, four holes for the power and motor leads (VIN, GND, OUTA, OUTB), a slot for the 10-pin logic ribbon, one solid side. Screw posts with heat-set threaded inserts.
- Base plate: standoffs placed from Pololu's board DXF, screw holes to the shell, and windows so the pin silkscreen is readable from underneath.
- PETG for heat: it holds up near the driver at higher currents where PLA (softens ~55-60 °C) wouldn't.

Photos: `../photos/vnh5019-*`.

## CAN transceiver housing (USB-C)

Housing for an SN65HVD230 CAN transceiver board plus a USB-C breakout, so a USB-C cable can carry CAN-H, CAN-L and GND between nodes. "CAN" is embossed on the lid so it doesn't get mistaken for a USB device. Printed in PETG on the Bambu A1. Public print profile: [MakerWorld](https://makerworld.com/en/models/3621895-sn65hvd230-can-enclosure-with-usb-c-panel-port#profileId-4152002).

| File | What |
|---|---|
| `can-sn65hvd230-usb-shell-v2.step` | Shell with the USB-C opening |
| `can-sn65hvd230-base.step` | Lid / base plate |
| `can-sn65hvd230-usb-enclosure.3mf` | Bambu Studio project (PETG profile) |

Photos: `../photos/can-usb-*`. Tested: 10,000 CAN round trips over the cable, 0 errors, plug works both ways up.
