#!/usr/bin/env python3
"""
clock_drift.py

Board clock vs laptop clock, from the CLI 'uptime' command. Tells a board
clock error (HSE from the ST-LINK MCO) apart from a logic analyzer timebase
error: the FreeRTOS tick runs off the same PLL as TIM1 and FDCAN, so if the
board's 20 kHz PWM is really 0.5% fast, uptime gains 5 ms per second.

Each sample: note the laptop time, send 'uptime', note the time again when
the prompt comes back. The board's reading is taken to be at the midpoint;
half the round trip is the error bar. A straight-line fit of board time vs
laptop time over all samples gives the rate error in ppm.

The laptop clock is NTP-disciplined (a few ppm at worst), and uptime has
1 ms resolution, so a 5 minute run resolves well under 10 ppm. 0.5% is
5000 ppm.

Usage:
    python3 tools/clock_drift.py -p /dev/cu.usbmodem1103
    python3 tools/clock_drift.py -p /dev/cu.usbmodem1103 -d 600 -i 5

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import argparse
import re
import sys
import time

import serial

PROMPT = b"> "
UPTIME = re.compile(r"^(\d+)\.(\d{3}) s\s*$", re.M)


def read_until_prompt(ser, timeout=2.0):
    buf = b""
    deadline = time.monotonic() + timeout
    while not buf.endswith(PROMPT):
        if time.monotonic() > deadline:
            raise TimeoutError(f"no prompt, got {buf!r}")
        buf += ser.read(ser.in_waiting or 1)
    return buf.decode(errors="replace")


def sample(ser):
    """(laptop s, board s, half round trip s)"""
    ser.reset_input_buffer()
    t0 = time.monotonic()
    ser.write(b"uptime\r")
    out = read_until_prompt(ser)
    t1 = time.monotonic()
    m = UPTIME.search(out)
    if not m:
        raise ValueError(f"no uptime in {out!r}")
    board = int(m.group(1)) + int(m.group(2)) / 1000
    return (t0 + t1) / 2, board, (t1 - t0) / 2


def fit(xs, ys):
    """Least-squares slope and intercept."""
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx
    return slope, my - slope * mx


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    ap.add_argument("-p", "--port", required=True)
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("-d", "--duration", type=float, default=300, help="seconds (default 300)")
    ap.add_argument("-i", "--interval", type=float, default=10, help="seconds between samples (default 10)")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.1)
    ser.write(b"\r")
    read_until_prompt(ser)

    laptop, board, errs = [], [], []
    start = time.monotonic()
    print(f"{'laptop s':>9} {'board s':>10} {'+/- ms':>7} {'drift ms':>9} {'ppm':>8}")
    try:
        while True:
            h, b, err = sample(ser)
            laptop.append(h)
            board.append(b)
            errs.append(err)
            dh, db = h - laptop[0], b - board[0]
            drift_ms = (db - dh) * 1000
            ppm = (db / dh - 1) * 1e6 if dh > 0 else 0.0
            print(f"{dh:9.1f} {b:10.3f} {err * 1000:7.1f} {drift_ms:+9.1f} {ppm:+8.0f}", flush=True)
            if time.monotonic() - start >= args.duration:
                break
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("stopped early")
    finally:
        ser.close()

    if len(laptop) < 3:
        sys.exit("need at least 3 samples")
    slope, _ = fit([h - laptop[0] for h in laptop], [b - board[0] for b in board])
    span = laptop[-1] - laptop[0]
    ppm = (slope - 1) * 1e6
    # Endpoint uncertainty: 1 ms tick plus the worst half round trip, both ends
    limit = (0.001 + max(errs)) * 2 / span * 1e6
    print(f"\n{len(laptop)} samples over {span:.0f} s")
    print(f"board clock {ppm:+.0f} ppm ({ppm / 1e4:+.3f}%) vs laptop, resolution about {limit:.0f} ppm")
    if abs(ppm) > 1000:
        print("board clock is off: check the HSE source (ST-LINK MCO) and the CAN bit timing margin")
    elif abs(ppm) > limit:
        print("board clock is within 0.1% but off by more than the resolution")
    else:
        print("board clock matches the laptop clock within the resolution")


if __name__ == "__main__":
    main()
