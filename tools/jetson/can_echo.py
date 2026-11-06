#!/usr/bin/env python3
"""
can_echo.py

Echo the Nucleo's CAN self-test frames back so `can test <n>` runs over the
real bus. The test is ping-pong: the Nucleo sends one frame (id 0x7F0, seq +
inverted seq), waits for it to come back, then sends the next. On a real bus
it never hears its own frames, so this script plays the other end.

Runs on the Jetson. Needs python-can (`sudo apt install python3-can`) and
can0 up (`sudo tools/jetson/can_up.sh`).

Notes:
- The kernel filter drops everything but 0x7F0 before Python sees it.
- No per-frame printing: a terminal over SSH is slower than the bus and
  would end up being what the round-trip time measures.
- python-can doesn't hand a socket its own sent frames by default, so the
  echo can't loop on itself.

Usage:
    python3 tools/jetson/can_echo.py            # can0
    python3 tools/jetson/can_echo.py can1
Then on the Nucleo: `can test 10000`, `can`. Ctrl+C here prints the totals.

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import argparse
import time

import can

TEST_ID = 0x7F0         # matches CAN_TEST_ID in firmware/nucleo/app/can.c
PROGRESS_EVERY = 1000   # frames between progress lines


def main() -> None:
    parser = argparse.ArgumentParser(description="Echo CAN self-test frames (id 0x7F0).")
    parser.add_argument("channel", nargs="?", default="can0",
                        help="SocketCAN interface (default: can0)")
    args = parser.parse_args()

    echoed = 0
    failed = 0
    first = None

    try:
        with can.Bus(interface="socketcan", channel=args.channel,
                     can_filters=[{"can_id": TEST_ID, "can_mask": 0x7FF}]) as bus:
            print(f"echoing 0x{TEST_ID:03X} on {args.channel}, Ctrl+C to stop", flush=True)
            while True:
                msg = bus.recv(timeout=1.0)     # timeout so Ctrl+C gets a look in
                if msg is None or msg.arbitration_id != TEST_ID:
                    continue
                if first is None:
                    first = time.monotonic()
                try:
                    bus.send(msg)
                    echoed += 1
                except can.CanError:
                    failed += 1                 # TX queue full; Nucleo will count a gap
                if echoed and echoed % PROGRESS_EVERY == 0:
                    print(f"{echoed} echoed", flush=True)
    except KeyboardInterrupt:
        pass
    except can.CanError as err:
        parser.error(f"can't open {args.channel}: {err}")

    print(f"\nechoed {echoed}, failed {failed}")
    if first is not None and echoed:
        # From the first frame to Ctrl+C, so only meaningful if you stop right
        # after the test ends. The Nucleo's uptime before/after is the real number.
        print(f"time since first frame: {time.monotonic() - first:.3f} s")


if __name__ == "__main__":
    main()
