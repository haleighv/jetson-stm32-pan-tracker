#!/usr/bin/env bash
# Bring up can0 on the Jetson Orin Nano (J17): pinmux, kernel modules, bit timing.
# Pinmux doesn't survive a reboot, so run this after every boot.
# Usage: sudo ./can_up.sh [bitrate]   (default: 1000000)
# Ref: https://docs.nvidia.com/jetson/archives/r39.2.1/DeveloperGuide/HR/ControllerAreaNetworkCan.html
set -euo pipefail

bitrate="${1:-1000000}"

if [[ $EUID -ne 0 ]]; then
    echo "run with sudo" >&2
    exit 1
fi

# Pinmux: J17 pins to CAN function (values from NVIDIA's CAN guide)
busybox devmem 0x0c303018 w 0xc458      # can0_din  (RX)
busybox devmem 0x0c303010 w 0xc400      # can0_dout (TX)
din=$(busybox devmem 0x0c303018 w)
dout=$(busybox devmem 0x0c303010 w)
if [[ $((din)) -ne $((0xc458)) || $((dout)) -ne $((0xc400)) ]]; then
    echo "pinmux readback wrong: din=$din dout=$dout" >&2
    exit 1
fi

modprobe can
modprobe can_raw
modprobe mttcan

# Settings only change while the interface is down
ip link set can0 down 2>/dev/null || true
ip link set can0 type can bitrate "$bitrate" sample-point 0.8 restart-ms 100 berr-reporting on
ip link set can0 up

ip -details link show can0
