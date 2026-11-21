#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""
serial_pick.py

Serial port picker for the tools in this folder. Lists the USB serial ports,
puts the ST-LINK (the Nucleo's virtual COM port) first, and lets you pick
one with the arrow keys and Enter. Used when a tool is run without -p.

    from serial_pick import pick_port
    port = args.port or pick_port()

Keys: up/down (or k/j) to move, Enter to pick, q or Esc to cancel.
Without a terminal (piped, CI), it takes the only ST-LINK port if there is
exactly one, and otherwise exits asking for -p.

Run on its own to just print the pick:
    tools/serial_pick.py

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import os
import select
import sys
import termios
import tty

from serial.tools import list_ports

ST_VID = 0x0483                       # STMicroelectronics: the ST-LINK on the Nucleo


def _ports():
    """[(device, label, is_stlink)]: USB serial ports only, ST-LINK first,
    macOS cu.* only. Bluetooth, debug-console and other built-in ports have
    no USB vendor ID, so they drop out."""
    out = []
    for p in list_ports.comports():
        if p.vid is None:
            continue                  # not USB
        if sys.platform == "darwin" and p.device.startswith("/dev/tty."):
            continue                  # same port as /dev/cu.*; cu doesn't wait for carrier
        st = p.vid == ST_VID
        desc = p.description if p.description and p.description != "n/a" else ""
        label = f"{p.device}  {desc}".rstrip()
        if st:
            label += "  [ST-LINK]"
        out.append((p.device, label, st))
    out.sort(key=lambda x: (not x[2], x[0]))
    return out


def _read_key(fd):
    """One key press. Reads the fd directly: sys.stdin is buffered, so it
    would swallow the rest of an arrow key's escape sequence and the
    select() below would then see nothing and call it a bare Esc."""
    ch = os.read(fd, 1)
    if ch == b"\x1b":                 # escape sequence, or a bare Esc
        if select.select([fd], [], [], 0.05)[0]:
            seq = os.read(fd, 2)
            return {b"[A": "up", b"[B": "down", b"OA": "up", b"OB": "down"}.get(seq, "")
        return "esc"
    if ch in (b"\r", b"\n"):
        return "enter"
    return {b"k": "up", b"j": "down", b"q": "esc", b"\x03": "esc"}.get(ch, "")


def _menu(labels, start):
    """Arrow-key menu on stderr. Returns the chosen index, or None."""
    out = sys.stderr
    fd = sys.stdin.fileno()
    sel = start
    out.write("Pick a serial port (up/down, Enter; q to cancel):\n\x1b[?25l")
    old = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)
        while True:
            for i, label in enumerate(labels):
                mark = "\x1b[7m> " if i == sel else "  "
                out.write(f"\x1b[2K{mark}{label}\x1b[0m\n")
            out.flush()
            key = _read_key(fd)
            if key == "up":
                sel = (sel - 1) % len(labels)
            elif key == "down":
                sel = (sel + 1) % len(labels)
            elif key == "enter":
                return sel
            elif key == "esc":
                return None
            out.write(f"\x1b[{len(labels)}A")   # back up and redraw in place
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        out.write("\x1b[?25h")
        out.flush()


def pick_port():
    """Return the chosen port's device path, or exit."""
    ports = _ports()
    if not ports:
        sys.exit("no USB serial ports found: is the Nucleo plugged in?")
    st = [p for p in ports if p[2]]
    if not (sys.stdin.isatty() and sys.stderr.isatty()):
        if len(st) == 1:
            return st[0][0]
        listing = "\n".join(f"  {label}" for _, label, _ in ports)
        sys.exit(f"pick a port with -p:\n{listing}")
    choice = _menu([label for _, label, _ in ports], 0)
    if choice is None:
        sys.exit("cancelled")
    return ports[choice][0]


if __name__ == "__main__":
    print(pick_port())
