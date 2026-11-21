#!/usr/bin/env python3
"""
nucleo_cli.py

Shared helpers for the tools that talk to the Nucleo's CLI over the
ST-LINK virtual COM port: the Board connection (with a transcript), parsers
for the CLI's replies, and a checked 'motor dump'.

    from nucleo_cli import Board
    with Board(port) as board:
        print(board.cmd("uptime"))

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import pathlib
import re
import time

import serial

BAUD = 1_000_000                       # USART3, nucleo.ioc
PROMPT = b"> "
LOG_DEPTH = 2000                       # MOTOR_LOG_DEPTH in motor.h, samples at 1 ms
ROW = re.compile(r"^(\d+),(-?\d+)$")
RESULTS = pathlib.Path(__file__).resolve().parent.parent / "docs" / "results"


class Board:
    """The CLI over the serial port. Every command and reply goes into
    self.transcript."""

    def __init__(self, port, baud=BAUD):
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.transcript = []

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _read_until_prompt(self, timeout):
        buf = b""
        deadline = time.monotonic() + timeout
        while not buf.endswith(PROMPT):
            if time.monotonic() > deadline:
                raise TimeoutError(f"no prompt, got {buf[-200:]!r}")
            buf += self.ser.read(self.ser.in_waiting or 1)
        return buf.decode(errors="replace")

    def sync(self, timeout=2.0):
        """Get back to a fresh prompt, e.g. after a timeout mid-output."""
        time.sleep(0.2)
        self.ser.reset_input_buffer()
        self.ser.write(b"\r")
        self._read_until_prompt(timeout)

    def cmd(self, line, timeout=2.0):
        self.ser.reset_input_buffer()
        self.ser.write(line.encode() + b"\r")
        try:
            out = self._read_until_prompt(timeout)
        except TimeoutError:
            self.transcript.append(f"> {line}\n(no prompt within {timeout:.1f} s)")
            raise
        self.transcript.append(f"> {line}\n{out}")
        return out

    def close(self):
        self.ser.close()


# --- Parsers ---

def parse_status(out):
    """'motor' reply -> dict of its key/value lines."""
    st = {}
    for line in out.splitlines():
        m = re.match(r"^(\w+):?\s+(\S+)\s*$", line.strip())
        if m:
            k, v = m.groups()
            st[k] = int(v) if re.fullmatch(r"-?\d+", v) else v
    return st


def parse_jitter(out):
    """'jitter' reply -> dict (cycles, ns, us), or {} if no samples yet."""
    j = {}
    m = re.search(r"period min (\d+), max (\d+) cycles \(ideal (\d+)\)", out)
    if m:
        j["period_min_cyc"], j["period_max_cyc"], j["ideal_cyc"] = map(int, m.groups())
    m = re.search(r"jitter (-?\d+) / \+?(-?\d+) ns", out)
    if m:
        j["early_ns"], j["late_ns"] = map(int, m.groups())
    m = re.search(r"tick isr max (\d+) cycles \((\d+)\.(\d+) us\)", out)
    if m:
        j["isr_max_cyc"] = int(m.group(1))
        j["isr_max_us"] = float(f"{m.group(2)}.{m.group(3)}")
    return j


def parse_log(out):
    """'motor dump' reply -> [(ms, position)]"""
    rows = []
    for line in out.splitlines():
        m = ROW.match(line.strip())
        if m:
            rows.append((int(m.group(1)), int(m.group(2))))
    return rows


def log_dropped(board):
    """Log messages the board dropped since boot ('log_level' reply), or None."""
    m = re.search(r"dropped (\d+)", board.cmd("log_level"))
    return int(m.group(1)) if m else None


def log_problems(rows, expected=LOG_DEPTH):
    p = []
    if len(rows) != expected:
        p.append(f"{len(rows)} rows, expected {expected}")
    if any(ms != i for i, (ms, _) in enumerate(rows)):
        p.append("rows missing or out of order")
    return p


def dump_log(board, expected=LOG_DEPTH, retries=1):
    """'motor dump', checked: row count, order, and the board's own dropped
    count. The log stays on the board until the next logged run, so a bad
    dump is retried. Returns (rows, problems); problems is empty when good."""
    # Each line is up to ~13 bytes, 10 bits per byte on the wire; 3x margin
    timeout = expected * 13 * 10 / board.ser.baudrate * 3 + 3
    problems = []
    for attempt in range(retries + 1):
        before = log_dropped(board)
        try:
            rows = parse_log(board.cmd("motor dump", timeout=timeout))
        except TimeoutError:
            rows, problems = [], ["no prompt after the dump"]
            board.sync()
        else:
            problems = log_problems(rows, expected)
            after = log_dropped(board)
            if before is not None and after is not None and after > before:
                problems.append(f"board dropped {after - before} log lines during the dump")
        if not problems:
            break
        if attempt < retries:
            problems = []                 # try again; keep only the last attempt's problems
    return rows, problems


def save_log(path, rows):
    with path.open("w") as f:
        f.write("ms,position\n")
        for ms, p in rows:
            f.write(f"{ms},{p}\n")
