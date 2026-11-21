#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""
step_log.py

Motor step response from the board's 1 kHz position log. Sends
'motor pwm <pct> <ms> log', waits for the log to fill (2,000 samples, one
per ms: the run, then the brake), reads 'motor dump' and saves it as CSV
in docs/results/<date>-step-<pct>.csv (a leading m for negative duty, e.g.
step-m50).

Also prints a quick look: steady speed over the second half of the run,
time to 90% of that speed, and the final position. The full model fit
(gain, dead band, time constant) works on the saved CSVs.

Close tio (or anything else on the port) first: only one program can hold
the serial port.

Usage (uv installs pyserial on first run, no venv needed):
    tools/step_log.py --pct 50          (pick the port from a list)
    tools/step_log.py -p /dev/cu.usbmodem131403 --pct -30 --ms 1500
    uv run tools/step_log.py --pct 50   (same thing)

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import argparse
import datetime
import pathlib
import sys
import time

from nucleo_cli import BAUD, LOG_DEPTH, RESULTS, Board, dump_log, save_log
from serial_pick import pick_port


def quick_look(rows, run_ms):
    """(steady counts/s, ms to 90% of it or None), from the second half of the run."""
    pos = [p for _, p in rows]
    a, b = run_ms // 2, min(run_ms, len(pos) - 1)
    if b - a < 50:
        return None, None
    steady = (pos[b] - pos[a]) / (b - a)          # counts per ms
    for t in range(5, b - 5):
        if steady and (pos[t + 5] - pos[t - 5]) / 10 / steady >= 0.9:
            return steady * 1000, t
    return steady * 1000, None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    ap.add_argument("-p", "--port", help="serial port (default: pick from a list)")
    ap.add_argument("-b", "--baud", type=int, default=BAUD)
    ap.add_argument("--pct", type=int, required=True, help="duty in percent, -100..100, not 0")
    ap.add_argument("--ms", type=int, default=1000, help="run time in ms (default 1000)")
    ap.add_argument("-o", "--out", type=pathlib.Path, default=RESULTS, help="output folder (default docs/results)")
    ap.add_argument("--date", default=datetime.date.today().isoformat(), help="date in the file name (default today)")
    ap.add_argument("-f", "--force", action="store_true", help="overwrite an existing file")
    args = ap.parse_args()

    if args.pct == 0 or not -100 <= args.pct <= 100:
        sys.exit("--pct must be -100..100 and not 0")
    if not 1 <= args.ms <= 5000:
        sys.exit("--ms must be 1..5000 (MOTOR_RUN_MS_MAX)")
    name = f"{args.date}-step-{'m' if args.pct < 0 else ''}{abs(args.pct)}.csv"
    path = args.out / name
    if path.exists() and not args.force:
        sys.exit(f"{path} exists, use -f to overwrite")

    port = args.port or pick_port()
    with Board(port, args.baud) as board:
        board.sync()
        out = board.cmd(f"motor pwm {args.pct} {args.ms} log")
        if "refused" in out or "usage" in out:
            sys.exit(f"run not started:\n{out.strip()}")
        print(f"motor pwm {args.pct} {args.ms} log: waiting for the log to fill")

        # The log runs LOG_DEPTH ms from the start of the run, whatever the run time
        time.sleep(LOG_DEPTH / 1000 + 0.5)
        rows, problems = dump_log(board)

    if not rows:
        sys.exit("no samples in the dump: " + "; ".join(problems))
    for p in problems:
        print(f"warning: {p}")

    path.parent.mkdir(parents=True, exist_ok=True)
    save_log(path, rows)

    steady, t90 = quick_look(rows, args.ms)
    print(f"saved {len(rows)} rows to {path}")
    if steady is not None:
        print(f"steady {steady:+.0f} counts/s over {args.ms // 2}-{args.ms} ms")
    if t90 is not None:
        print(f"90% of steady speed at {t90} ms")
    print(f"final position {rows[-1][1]}")


if __name__ == "__main__":
    main()
