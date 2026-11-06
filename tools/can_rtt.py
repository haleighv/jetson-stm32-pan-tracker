#!/usr/bin/env python3
"""
can_rtt.py

Round-trip timing for the CAN ping-pong test (`can test <n>` on the Nucleo,
tools/jetson/can_echo.py on the Jetson) from a Saleae Logic 2 capture of the
bus (any transceiver RX pin).

Frames alternate: Nucleo frame, Jetson echo, Nucleo frame, ... The capture
should start before the first frame, so frame 0 is the Nucleo's.

Frames are found from edges alone, no bit decoding: inside a frame the line
is never recessive for more than 5 bits (bit stuffing) plus the CRC/ACK
delimiters, so a high run over 6 us at 1 Mbps means the frame ended.
"End" is the rising edge after the ACK slot. After that the bus must stay
recessive for at least 11 bits (ACK delimiter, 7-bit EOF, 3-bit intermission)
before anyone can start, so turnaround = gap - 11 bit times.

Input: Logic 2 "Export Data -> CSV" of one digital channel (.csv or .csv.gz):
    Time [s],Channel 0
    -0.001000000,1      <- capture start (initial state, not an edge)
    ...
    4.000000000,1       <- end of capture

Usage:
    python3 tools/can_rtt.py -c docs/results/2026-11-06-can-echo-10k.csv.gz
    python3 tools/can_rtt.py -c capture.csv --bitrate 500000

Writes <name>.report.txt next to the capture.

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import argparse
import csv
import gzip
import os
import sys

MIN_IDLE_BITS = 11      # ACK delimiter + EOF + intermission
MAX_HIGH_IN_FRAME = 6   # bits; anything longer is between frames


def read_edges(path):
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", newline="") as f:
        rows = [(float(t), int(v)) for t, v in list(csv.reader(f))[1:]]
    return rows[0][1], rows[1:-1]    # initial state, edges (drop end-of-capture row)


def find_frames(initial, edges, bit_s):
    """[(sof, end)] where end is the rising edge after the ACK slot."""
    frames = []
    start = last_rise = None
    prev_t, prev_v = None, initial
    for t, v in edges:
        if v == 0 and prev_v == 1 and (start is None or t - prev_t > MAX_HIGH_IN_FRAME * bit_s):
            if start is not None:
                frames.append((start, last_rise))
            start = t
        elif v == 1:
            last_rise = t
        prev_t, prev_v = t, v
    if start is not None:
        frames.append((start, last_rise))
    return frames


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p * len(xs)))]


def stats_line(name, xs):
    us = [x * 1e6 for x in xs]
    return (f"{name:<22} n={len(us):<6} min {min(us):8.1f}  p50 {pct(us, .5):8.1f}  "
            f"p90 {pct(us, .9):8.1f}  p99 {pct(us, .99):8.1f}  p99.9 {pct(us, .999):8.1f}  "
            f"max {max(us):8.1f} us")


def main():
    ap = argparse.ArgumentParser(description="CAN ping-pong round-trip timing from a Saleae capture.")
    ap.add_argument("-c", "--capture", required=True, help="Logic 2 CSV export (.csv or .csv.gz)")
    ap.add_argument("--bitrate", type=int, default=1_000_000, help="bus bit rate (default 1000000)")
    args = ap.parse_args()

    bit_s = 1 / args.bitrate
    initial, edges = read_edges(args.capture)
    frames = find_frames(initial, edges, bit_s)
    if len(frames) < 4:
        sys.exit(f"only {len(frames)} frames found, need a ping-pong capture")

    idle = MIN_IDLE_BITS * bit_s
    after_nucleo = [frames[i + 1][0] - frames[i][1] - idle for i in range(0, len(frames) - 1, 2)]
    after_jetson = [frames[i + 1][0] - frames[i][1] - idle for i in range(1, len(frames) - 1, 2)]
    round_trip = [frames[i + 2][0] - frames[i][0] for i in range(0, len(frames) - 2, 2)]
    frame_len = [e - s for s, e in frames]
    span = frames[-1][1] - frames[0][0]

    lines = [
        f"capture: {os.path.basename(args.capture)}",
        f"frames: {len(frames)} ({len(frames) // 2} round trips), span {span:.3f} s, "
        f"bit rate {args.bitrate} (min idle between frames {MIN_IDLE_BITS} bits)",
        "",
        "turnaround = gap after a frame minus the 11-bit minimum idle:",
        stats_line("Jetson (echo)", after_nucleo),
        stats_line("Nucleo (next frame)", after_jetson),
        "",
        stats_line("round trip (SOF->SOF)", round_trip),
        stats_line("frame (SOF->ACK)", frame_len),
        f"mean round trip {span / (len(frames) / 2) * 1e6:.1f} us "
        f"({len(frames) / 2 / span:.0f} round trips/s)",
    ]
    report = "\n".join(lines)
    print(report)

    base = args.capture[:-3] if args.capture.endswith(".gz") else args.capture
    out = os.path.splitext(base)[0] + ".report.txt"
    with open(out, "w") as f:
        f.write(report + "\n")
    print(f"\nwrote {out}")


if __name__ == "__main__":
    main()
