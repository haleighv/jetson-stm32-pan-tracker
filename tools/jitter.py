#!/usr/bin/env python3
"""
jitter.py

Control tick jitter from a Saleae Logic 2 capture of the scope pin (PG14).
The pin toggles on every tick, so every edge is one tick and every
edge-to-edge interval is one period (~1 ms).

Input: Logic 2 "Export Data -> CSV" of one digital channel:
    Time [s],Channel 0
    0.000000000,1          <- capture start (initial state, not an edge)
    0.000936070,0
    ...
    90.2281,0              <- end of capture (same state repeated, not an edge)

Reports:
- rate offset: mean period vs the ideal, in ppm (needs an accurate clock on
  one side, see worklog 2026-11-05)
- jitter: each period's deviation from the mean period, so a rate offset
  doesn't show up as jitter. p99 of |deviation| is the plan's metric.
- anomalies: periods more than 10% off the median (missed ticks, glitches),
  counted and left out of the stats

Usage:
    python3 tools/jitter.py -c docs/results/2026-11-05-tim6-jitter-idle_90s.csv
    python3 tools/jitter.py -c capture.csv --period-us 1000 -v
    python3 tools/jitter.py -d docs/results

-d runs every *.csv in the directory, writes a report next to each one
(<name>.report.txt) and prints a summary table. Files that aren't Logic 2
CSVs are skipped with a warning.

Author: Haleigh (haleighv)
Copyright (c) 2026 haleighv
SPDX-License-Identifier: MIT
"""
import argparse
import csv
import statistics
import sys
from pathlib import Path

ANOMALY_FRACTION = 0.10     # a period this far off the median is not a normal tick


def read_edges(path):
    """Return edge times (s) from a Logic 2 CSV. Skips the capture-start row
    and stops at the end-of-capture row (state repeated)."""
    with open(path, newline="") as f:
        reader = csv.reader(f)
        header = next(reader, [])
        if len(header) < 2 or "Time" not in header[0]:
            raise ValueError(f"{path}: not a Logic 2 CSV (header {header})")

        rows = [(float(r[0]), r[1].strip()) for r in reader if len(r) >= 2]

    edges = []
    last_state = None
    for i, (t, state) in enumerate(rows[1:], start=1):     # rows[0] = initial state
        if state == last_state:
            if i != len(rows) - 1:
                print(f"warning: repeated state at {t:.9f} s (row {i + 1}), stopping there",
                      file=sys.stderr)
            break                                           # end-of-capture marker
        edges.append(t)
        last_state = state
    return edges


def percentile(sorted_vals, p):
    """Nearest-rank percentile of an already sorted list, p in 0..100."""
    if not sorted_vals:
        return float("nan")
    k = min(len(sorted_vals) - 1, max(0, int(round(p / 100 * len(sorted_vals))) - 1))
    return sorted_vals[k]


def analyze(edges, ideal_s):
    """Periods, anomalies, rate offset and jitter stats."""
    periods = [b - a for a, b in zip(edges, edges[1:])]
    if len(periods) < 2:
        raise ValueError("need at least 3 edges")

    median = statistics.median(periods)
    good = [p for p in periods if abs(p - median) <= ANOMALY_FRACTION * median]
    anomalies = [(i, p) for i, p in enumerate(periods)
                 if abs(p - median) > ANOMALY_FRACTION * median]

    mean = statistics.fmean(good)
    absdev = sorted(abs(p - mean) for p in good)

    return {
        "edges": len(edges),
        "periods": len(periods),
        "duration_s": edges[-1] - edges[0],
        "anomalies": anomalies,
        "good": len(good),
        "min": min(good),
        "max": max(good),
        "mean": mean,
        "median": median,
        "ppm": (ideal_s - mean) / ideal_s * 1e6,     # + means the board runs fast
        "std": statistics.pstdev(good),
        "p50": percentile(absdev, 50),
        "p99": percentile(absdev, 99),
        "p999": percentile(absdev, 99.9),
        "dev_min": min(good) - mean,
        "dev_max": max(good) - mean,
    }


def us(x):
    return f"{x * 1e6:.3f} us"


def ns(x):
    return f"{x * 1e9:.0f} ns"


def report_lines(path, r, ideal_s):
    """Human-readable report for one capture, as a list of lines."""
    lines = [
        f"file:       {path}",
        f"edges:      {r['edges']}, periods {r['periods']}, over {r['duration_s']:.3f} s",
        f"anomalies:  {len(r['anomalies'])} (more than {ANOMALY_FRACTION:.0%} off the median, left out)",
    ]
    for i, p in r["anomalies"][:10]:
        lines.append(f"            period {i}: {us(p)}")
    lines += [
        "",
        f"period:     mean {us(r['mean'])}, median {us(r['median'])}, "
        f"min {us(r['min'])}, max {us(r['max'])}",
        f"rate:       {r['ppm']:+.0f} ppm vs ideal {us(ideal_s)} (+ = fast)",
        "",
        "jitter (deviation from the mean period):",
        f"  range     {ns(r['dev_min'])} / +{ns(r['dev_max'])}, "
        f"peak-to-peak {ns(r['dev_max'] - r['dev_min'])}",
        f"  std       {ns(r['std'])}",
        f"  |dev| p50 {ns(r['p50'])}, p99 {ns(r['p99'])}, p99.9 {ns(r['p999'])}",
    ]
    return lines


def run_dir(directory, ideal_s):
    """Analyze every CSV in directory, write <name>.report.txt next to each,
    print a summary table. Returns the number of files that failed."""
    csvs = sorted(Path(directory).glob("*.csv"))
    if not csvs:
        print(f"no .csv files in {directory}", file=sys.stderr)
        return 1

    summary = []
    failed = 0
    for path in csvs:
        try:
            edges = read_edges(path)
            r = analyze(edges, ideal_s)
        except (OSError, ValueError) as e:
            print(f"skipped: {e}", file=sys.stderr)
            failed += 1
            continue

        out = path.with_suffix(".report.txt")
        out.write_text("\n".join(report_lines(path.name, r, ideal_s)) + "\n")
        summary.append((path.name, r))
        print(f"wrote {out}")

    if summary:
        name_w = max(len(n) for n, _ in summary)
        print()
        print(f"{'capture':<{name_w}}  {'periods':>8}  {'anom':>4}  {'ppm':>6}  "
              f"{'std ns':>7}  {'p99 ns':>7}  {'p99.9 ns':>8}  {'max ns':>7}")
        for name, r in summary:
            worst = max(abs(r["dev_min"]), abs(r["dev_max"]))
            print(f"{name:<{name_w}}  {r['periods']:>8}  {len(r['anomalies']):>4}  "
                  f"{r['ppm']:>+6.0f}  {r['std'] * 1e9:>7.0f}  {r['p99'] * 1e9:>7.0f}  "
                  f"{r['p999'] * 1e9:>8.0f}  {worst * 1e9:>7.0f}")
    return failed


def main():
    parser = argparse.ArgumentParser(description="Control tick jitter from a Saleae Logic 2 CSV.")
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("-c", "--csv", help="one Logic 2 CSV export (one digital channel)")
    src.add_argument("-d", "--dir", help="every *.csv in this directory; writes <name>.report.txt for each")
    parser.add_argument("--period-us", type=float, default=1000.0,
                        help="ideal tick period in us (default 1000)")
    parser.add_argument("-v", "--verbose", action="store_true", help="also print every period")
    args = parser.parse_args()

    ideal_s = args.period_us * 1e-6

    if args.dir:
        return 1 if run_dir(args.dir, ideal_s) else 0

    try:
        edges = read_edges(args.csv)
        result = analyze(edges, ideal_s)
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    print("\n".join(report_lines(args.csv, result, ideal_s)))
    if args.verbose:
        print()
        print("all periods (us):")
        for a, b in zip(edges, edges[1:]):
            print(f"{(b - a) * 1e6:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
