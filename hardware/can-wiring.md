# CAN wiring

Classic CAN at 1 Mbps between the Nucleo (FDCAN1) and the Jetson (J17, mttcan), with a CANable on the bus as a spare node. Pin lookups: [pinouts.html](pinouts.html).

## Bus

```
Nucleo FDCAN1 --[SN65HVD230 + 120 Ω]==== CAN-H / CAN-L ====[SN65HVD230 + 120 Ω]-- Jetson J17
                                               ||
                                           CANable (termination off)
```

- One SN65HVD230 board per end (3.3 V part, 120 Ω on board). Those two are the bus ends, so they're the only terminators.
- CANable sits in the middle with its termination off. Its USB goes to the Jetson.
- Ground: the two sides share ground through the CANable's USB ground to the Jetson. If the CANable comes off the bus, run a GND wire from the Nucleo-side transceiver to J17 pin 3.
- Twisted pair for CAN-H/CAN-L, short runs (bench, under 1 m).

## Nucleo (FDCAN1)

| Signal | MCU pin | Socket | Transceiver |
|---|---|---|---|
| TX | PD1 (D66) | CN9-27 | TX / D |
| RX | PD0 (D67) | CN9-25 | RX / R |
| GND | - | CN9-23 | GND |
| 3.3 V | - | CN8-7 | VCC |

TX goes to TX and RX to RX: the transceiver's TX pin is its input from the controller. CN8-7's neighbors are NRST (CN8-5) and 5V (CN8-9).

## Jetson (J17)

| J17 pin | Signal | Transceiver |
|---|---|---|
| 1 | CAN_TX | TX / D |
| 2 | CAN_RX | RX / R |
| 3 | GND | GND |
| 4 | 3.3 V | VCC |

- J17 is a footprint on the carrier board (1×4, 2.54 mm); a header was soldered on and broken out of the case on jumpers.
- Pin 1 is TX, matching the carrier board silkscreen and the carrier board spec (SP-11324 v1.3, Table 3-5). NVIDIA's CAN developer guide lists pin 1 as RX.
- With TX and RX swapped, the controller's RX reads the transceiver's TX input, which nothing drives. RX sits low, the controller never sees an idle bus, and it never transmits: no errors, the frames just fill the TX queue ("No buffer space available").

## Checks

| Check | Expected | Measured |
|---|---|---|
| CAN-H to CAN-L, power off | ~60 Ω (two 120 Ω) | 59.6 Ω |
| CAN-H and CAN-L, powered, idle | equal, ~2.3 V (recessive) | 2.22 V / 2.22 V |
| Each controller's RX pin, idle | high | high |

## Bring-up

Jetson, after every boot (pinmux doesn't survive a reboot):
```
sudo tools/jetson/can_up.sh
```
Nucleo: FDCAN1 starts in Normal mode from the supervisor at boot. `can` shows counters and state.
