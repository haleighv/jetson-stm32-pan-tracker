#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""
motor_profile.py

Motor profile: a characterization suite over the CLI. Runs a fixed set of
tests and saves everything in one timestamped folder, so runs can be
compared over time (new firmware, new shield, different supply).

Tests, in order:
1. Board info: version, uptime, supervisor state, motor status.
2. Idle tick timing: jitter clear, wait, jitter (period min/max, ISR max).
3. Steps: 'motor pwm <pct> <ms> log' at each duty, both directions. From
   each 1 kHz position log: steady speed, time to 90% of it, a first-order
   fit (dead time and time constant), when the brake shows up, how far the
   shaft coasts after it. Motor current is sampled mid-run.
4. Model: per direction, a straight-line fit of steady speed vs duty over
   the steps that moved: gain K (counts/s per %) and dead band d0 (%).
5. Reversal: 50% forward, then -50% after 1 s, logged: speed both ways and
   how long the turnaround takes.
6. Tick timing over the whole motor run (jitter, ISR max).

Output: docs/results/<YYYY-MM-DDTHHMMSS>-motor-profile/
    summary.txt     the report printed at the end
    summary.json    the same numbers, for scripts
    transcript.txt  every command sent and every reply
    step-<pct>.csv  one log per step (m for negative), ms,position
    reversal.csv    the reversal log

The motor moves for about 1.5 minutes. Not covered: the fault test (needs
a jumper on EN) and anything that needs the Saleae.

Usage:
    python3 tools/motor_profile.py                  (pick the port from a list)
    python3 tools/motor_profile.py -y --duties 20 50 100
    python3 tools/motor_profile.py -p /dev/cu.usbmodem131403 --ms 1500

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import argparse
import datetime
import json
import math
import pathlib
import subprocess
import sys
import time

from nucleo_cli import (BAUD, LOG_DEPTH, RESULTS, Board, dump_log, parse_jitter,
                        parse_status, save_log)
from serial_pick import pick_port

DEFAULT_DUTIES = [10, 15, 20, 30, 50, 75, 100]


def logged_run(board, pct, run_ms, current_samples=3):
    """Run with the position log; sample the current mid-run. Returns
    (rows, [current_ma], problems). Raises RuntimeError if refused."""
    out = board.cmd(f"motor pwm {pct} {run_ms} log")
    if "refused" in out or "usage" in out:
        raise RuntimeError(f"motor pwm {pct} refused: {out.strip().splitlines()[-1]}")
    t0 = time.monotonic()
    currents = []
    time.sleep(min(0.5, run_ms / 2000))
    for _ in range(current_samples):
        st = parse_status(board.cmd("motor"))
        if "current_ma" in st:
            currents.append(st["current_ma"])
    time.sleep(max(0.0, t0 + LOG_DEPTH / 1000 + 0.5 - time.monotonic()))
    rows, problems = dump_log(board)
    return rows, currents, problems


# --- Analysis ---

def speed_at(pos, t, half=5):
    """counts/s over a 2*half ms window centred on t"""
    a, b = max(0, t - half), min(len(pos) - 1, t + half)
    return (pos[b] - pos[a]) / (b - a) * 1000 if b > a else 0.0


def fit_first_order(pos, run_ms, v):
    """Grid fit of pos(t) = v*(t - t0) - v*tau*(1 - exp(-(t - t0)/tau)) over
    the run. v in counts/ms. Returns (t0 ms, tau ms, rms counts)."""
    best = None
    n = min(run_ms, len(pos))
    for t0 in [x * 0.5 for x in range(0, 21)]:            # 0..10 ms
        for tau in [x * 0.5 for x in range(2, 161)]:       # 1..80 ms
            e = 0.0
            for t in range(n):
                tt = t - t0
                m = 0.0 if tt <= 0 else v * tt - v * tau * (1 - math.exp(-tt / tau))
                e += (m - pos[t]) ** 2
            if best is None or e < best[0]:
                best = (e, t0, tau)
    return best[1], best[2], math.sqrt(best[0] / n)


def analyze_step(rows, run_ms):
    pos = [p for _, p in rows]
    r = {"final_counts": pos[-1] if pos else None}
    if len(pos) < run_ms + 50:
        r["note"] = "log too short"
        return r
    a, b = run_ms // 2, run_ms
    steady = (pos[b] - pos[a]) / (b - a)                    # counts per ms
    r["steady_cps"] = round(steady * 1000, 1)
    moved = next((i for i, p in enumerate(pos[:run_ms]) if p != 0), None)
    r["first_count_ms"] = moved
    if abs(steady) < 0.05:                                  # < 50 counts/s: didn't really move
        r["note"] = "no steady motion"
        return r
    r["t90_ms"] = next((t for t in range(5, run_ms - 5)
                        if speed_at(pos, t) / (steady * 1000) >= 0.9), None)
    t0, tau, rms = fit_first_order(pos, run_ms, steady)
    r["fit_dead_ms"], r["fit_tau_ms"], r["fit_rms_counts"] = t0, tau, round(rms, 2)
    # Brake: first point after the run start where speed drops under half
    r["brake_seen_ms"] = next((t for t in range(run_ms - 20, len(pos) - 5)
                               if abs(speed_at(pos, t)) < abs(steady * 1000) / 2), None)
    r["coast_counts"] = pos[-1] - pos[run_ms]
    stop = next((t for t in range(run_ms, len(pos)) if pos[t] == pos[-1]), None)
    r["stopped_ms_after_run"] = stop - run_ms if stop is not None else None
    return r


def line_fit(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    k = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    return k, my - k * mx


def model(steps, sign):
    """K (counts/s per %) and d0 (%) from the steps of one sign that moved."""
    pts = [(abs(pct), abs(s["steady_cps"])) for pct, s in steps.items()
           if (pct > 0) == (sign > 0) and s.get("steady_cps") is not None and "note" not in s]
    if len(pts) < 2:
        return None
    k, c = line_fit([p for p, _ in pts], [v for _, v in pts])
    resid = [v - (k * p + c) for p, v in pts]
    return {"K_cps_per_pct": round(k, 2), "d0_pct": round(-c / k, 2) if k else None,
            "points": len(pts), "max_resid_cps": round(max(abs(x) for x in resid), 1)}


def analyze_reversal(rows):
    pos = [p for _, p in rows]
    if len(pos) < 1500:
        return {"note": "log too short"}
    peak = max(range(len(pos)), key=lambda i: pos[i])
    fwd = speed_at(pos, max(0, peak - 300), 100)
    rev = speed_at(pos, min(len(pos) - 1, peak + 600), 100)
    t90 = next((t for t in range(peak, len(pos) - 5) if rev and speed_at(pos, t) / rev >= 0.9), None)
    return {"turnaround_ms": peak, "fwd_cps": round(fwd, 1), "rev_cps": round(rev, 1),
            "peak_to_90pct_rev_ms": (t90 - peak) if t90 is not None else None}


# --- Report ---

def fmt(v, unit=""):
    return "-" if v is None else f"{v}{unit}"


def report(info, idle, steps, models, rev, run_jitter, problems):
    L = []
    L.append(f"Motor profile, {info['when']}")
    L.append(f"firmware: {info.get('version', '?')}")
    L.append(f"host repo: {info.get('host_git', '?')}")
    if "dirty" in info.get("version", "") or "dirty" in info.get("host_git", ""):
        L.append("note: uncommitted changes in the firmware build or the repo; this profile")
        L.append("      can't be matched to an exact commit")
    L.append("")
    L.append("Tick timing")
    for name, j in (("idle", idle), ("motor running", run_jitter)):
        L.append(f"  {name:14s} jitter {fmt(j.get('early_ns'))} / +{fmt(j.get('late_ns'))} ns, "
                 f"ISR max {fmt(j.get('isr_max_us'))} us")
    L.append("")
    L.append("Steps (run %d ms)" % info["run_ms"])
    L.append(f"  {'duty':>5} {'counts/s':>9} {'t90 ms':>7} {'dead ms':>8} {'tau ms':>7} "
             f"{'rms':>5} {'brake ms':>9} {'coast':>6} {'mA':>6}  note")
    for pct in sorted(steps, key=lambda p: (p < 0, abs(p))):
        s = steps[pct]
        ma = s.get("current_ma")
        L.append(f"  {pct:>4}% {fmt(s.get('steady_cps')):>9} {fmt(s.get('t90_ms')):>7} "
                 f"{fmt(s.get('fit_dead_ms')):>8} {fmt(s.get('fit_tau_ms')):>7} "
                 f"{fmt(s.get('fit_rms_counts')):>5} {fmt(s.get('brake_seen_ms')):>9} "
                 f"{fmt(s.get('coast_counts')):>6} {fmt(ma):>6}  {s.get('note', '')}")
    L.append("")
    L.append("Model: speed = K * (duty - d0), fitted per direction over steps that moved")
    for name, m in models.items():
        if m:
            L.append(f"  {name:8s} K {m['K_cps_per_pct']} counts/s per %, d0 {m['d0_pct']}%, "
                     f"{m['points']} points, worst residual {m['max_resid_cps']} counts/s")
        else:
            L.append(f"  {name:8s} not enough steps that moved")
    L.append("")
    if rev:
        L.append(f"Reversal 50% -> -50%: {fmt(rev.get('fwd_cps'))} then {fmt(rev.get('rev_cps'))} counts/s, "
                 f"turnaround at {fmt(rev.get('turnaround_ms'))} ms, "
                 f"90% of reverse speed {fmt(rev.get('peak_to_90pct_rev_ms'))} ms after it")
        L.append("")
    if problems:
        L.append("Problems")
        L += [f"  {p}" for p in problems]
    else:
        L.append("No problems")
    return "\n".join(L) + "\n"


# --- Main ---

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    ap.add_argument("-p", "--port", help="serial port (default: pick from a list)")
    ap.add_argument("-b", "--baud", type=int, default=BAUD)
    ap.add_argument("--duties", type=int, nargs="+", default=DEFAULT_DUTIES,
                    help="step duties in percent, run both directions (default %(default)s)")
    ap.add_argument("--ms", type=int, default=1000, help="step run time in ms (default 1000)")
    ap.add_argument("--idle-s", type=float, default=5, help="idle jitter window in s (default 5)")
    ap.add_argument("--no-reversal", action="store_true")
    ap.add_argument("-o", "--out", type=pathlib.Path, default=RESULTS, help="parent folder (default docs/results)")
    ap.add_argument("-y", "--yes", action="store_true", help="don't ask before moving the motor")
    args = ap.parse_args()

    duties = sorted({abs(d) for d in args.duties if d})
    if not duties or max(duties) > 100 or not 50 <= args.ms <= 1800:
        sys.exit("duties must be 1..100, --ms 50..1800 (the log is 2,000 ms)")

    stamp = datetime.datetime.now().strftime("%Y-%m-%dT%H%M%S")
    outdir = args.out / f"{stamp}-motor-profile"
    port = args.port or pick_port()

    if not args.yes:
        n = len(duties) * 2 + (0 if args.no_reversal else 1)
        input(f"{n} motor runs up to 100% duty, about {n * 7 // 60 + 1} min. "
              "Shaft clear? Enter to start, Ctrl-C to stop. ")

    board = Board(port, args.baud)
    problems, steps = [], {}
    info = {"when": stamp, "run_ms": args.ms, "port": port}
    try:
        host = subprocess.run(["git", "describe", "--always", "--dirty"], capture_output=True,
                              text=True, cwd=pathlib.Path(__file__).parent)
        info["host_git"] = host.stdout.strip() or "?"
    except OSError:
        info["host_git"] = "?"
    rev, run_jitter, idle = None, {}, {}
    outdir.mkdir(parents=True, exist_ok=True)

    def attempt(name, fn):
        """Run one test; on failure note it, get back to a prompt, carry on."""
        try:
            return fn()
        except (RuntimeError, TimeoutError, OSError) as e:
            problems.append(f"{name}: {e}")
            try:
                board.cmd("motor stop")
                board.sync()
            except Exception:
                pass
            return None

    try:
        board.sync()
        vers = [l.strip() for l in board.cmd("version").splitlines() if "built" in l]
        info["version"] = vers[0] if vers else "?"
        board.cmd("uptime")
        board.cmd("state")
        st = parse_status(board.cmd("motor"))
        if st.get("fault"):
            raise RuntimeError("motor fault latched: fix it and 'motor clear' first")

        def idle_timing():
            board.cmd("jitter clear")
            time.sleep(args.idle_s)
            j = parse_jitter(board.cmd("jitter"))
            board.cmd("jitter clear")
            return j
        print("idle tick timing...")
        idle = attempt("idle timing", idle_timing) or {}

        for pct in duties + [-d for d in duties]:
            print(f"step {pct:+d}%...", flush=True)
            res = attempt(f"step {pct}%", lambda: logged_run(board, pct, args.ms))
            if res is None:
                steps[pct] = {"note": "failed"}
                continue
            rows, currents, probs = res
            problems += [f"step {pct}%: {p}" for p in probs]
            save_log(outdir / f"step-{'m' if pct < 0 else ''}{abs(pct)}.csv", rows)
            s_ = analyze_step(rows, args.ms)
            if probs:
                s_["note"] = (s_.get("note", "") + " log incomplete").strip()
            if currents:
                s_["current_ma"] = round(sum(currents) / len(currents))
            steps[pct] = s_
            time.sleep(1.0)

        def reversal():
            out = board.cmd("motor pwm 50 3000 log")
            if "refused" in out:
                raise RuntimeError("first run refused")
            t0 = time.monotonic()
            time.sleep(1.0)
            board.cmd("motor pwm -50 1000")
            time.sleep(max(0.0, t0 + LOG_DEPTH / 1000 + 0.5 - time.monotonic()))
            rows, probs = dump_log(board)
            problems.extend(f"reversal: {p}" for p in probs)
            save_log(outdir / "reversal.csv", rows)
            return analyze_reversal(rows)
        if not args.no_reversal:
            print("reversal...", flush=True)
            rev = attempt("reversal", reversal)
            time.sleep(1.0)

        run_jitter = attempt("run timing", lambda: parse_jitter(board.cmd("jitter"))) or {}
        st = parse_status(board.cmd("motor"))
        if st.get("fault"):
            problems.append(f"fault latched during the suite (fault_count {st.get('fault_count')})")
    except KeyboardInterrupt:
        problems.append("stopped early (Ctrl-C)")
    except (RuntimeError, TimeoutError) as e:
        problems.append(str(e))
    finally:
        try:
            board.cmd("motor stop")
        except Exception:
            pass
        board.close()

    models = {"positive": model(steps, +1), "negative": model(steps, -1)}
    text = report(info, idle, steps, models, rev, run_jitter, problems)
    print("\n" + text)
    (outdir / "summary.txt").write_text(text)
    (outdir / "summary.json").write_text(json.dumps(
        {"info": info, "idle_jitter": idle, "run_jitter": run_jitter,
         "steps": {str(k): v for k, v in steps.items()}, "model": models,
         "reversal": rev, "problems": problems}, indent=2) + "\n")
    (outdir / "transcript.txt").write_text("\n".join(board.transcript))
    print(f"saved to {outdir}")


if __name__ == "__main__":
    main()
