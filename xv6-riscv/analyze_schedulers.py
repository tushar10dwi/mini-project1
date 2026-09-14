#!/usr/bin/env python3
"""
analyze_schedulers.py -- section 2.2 / 2.3.3: compare average
Turnaround, Waiting, and Response time for RR, FIFO, and MLFQ over
the identical schedulertest workload.

Workflow (repeat once per scheduler, same test program each time):
  make clean
  make qemu | tee rr.log                        # round-robin (default)
  # inside xv6: schedulertest, then wait for SCHEDTEST_END, Ctrl-A x

  make clean
  make qemu SCHEDULER=FIFO | tee fifo.log
  # same steps

  make clean
  make qemu SCHEDULER=MLFQ | tee mlfq.log
  # same steps

Then:
  python3 analyze_schedulers.py rr.log:RR fifo.log:FIFO mlfq.log:MLFQ

Definitions used (all in ticks, from PROCSTATS lines printed by
kexit() in kernel/proc.c -- same computation regardless of scheduler):
  Turnaround = etime - ctime
  Response   = first_run - ctime
  Waiting    = Turnaround - runtime_ticks
               (runtime_ticks is actual time spent RUNNING, counted
               once per timer tick in trap.c for every scheduler, so
               this automatically excludes time spent voluntarily
               sleeping/blocked -- only genuine ready-queue wait
               counts as "waiting")

Only processes schedulertest itself forked are included in the
average, identified the same way plot_mlfq.py does it: via the
SCHEDTEST_CHILD pid=<pid> role=<role> lines each child prints on
startup. This keeps the comparison to "the same fixed
workload/process set" as the assignment asks, regardless of how
much unrelated shell/init activity happened to be in the log.
"""
import argparse
import re
import sys
from collections import defaultdict

import matplotlib.pyplot as plt

CHILD_RE = re.compile(r"SCHEDTEST_CHILD pid=(\d+) role=(\S+)")
STATS_RE = re.compile(
    r"PROCSTATS pid=(\d+) ctime=(\d+) first_run=(\d+) etime=(\d+) runtime=(\d+)"
)

METRICS = ["Turnaround", "Waiting", "Response"]


def parse_log(path):
    """Return {pid: {'turnaround':, 'waiting':, 'response':}} for
    every pid that announced itself via SCHEDTEST_CHILD."""
    child_pids = set()
    stats = {}

    with open(path) as f:
        for line in f:
            m = CHILD_RE.search(line)
            if m:
                child_pids.add(int(m.group(1)))
                continue
            m = STATS_RE.search(line)
            if m:
                pid, ctime, first_run, etime, runtime = (
                    int(x) for x in m.groups()
                )
                stats[pid] = {
                    "Turnaround": etime - ctime,
                    "Response": first_run - ctime,
                    "Waiting": (etime - ctime) - runtime,
                }

    stats = {pid: v for pid, v in stats.items() if pid in child_pids}
    if not stats:
        sys.exit(
            f"{path}: no PROCSTATS lines matched a SCHEDTEST_CHILD pid.\n"
            "Did schedulertest run to completion (SCHEDTEST_END) while "
            "this log was being captured?"
        )
    missing = child_pids - stats.keys()
    if missing:
        print(
            f"warning: {path}: {len(missing)} child(ren) announced but "
            f"never exited (no PROCSTATS) -- excluded from the average: "
            f"{sorted(missing)}",
            file=sys.stderr,
        )
    return stats


def averages(stats):
    n = len(stats)
    return {
        m: sum(v[m] for v in stats.values()) / n for m in METRICS
    }


def print_per_process(results):
    for name, stats in results.items():
        print(f"\n{name} -- per-process")
        header = f"{'pid':>6}" + "".join(f"{m:>14}" for m in METRICS)
        print(header)
        print("-" * len(header))
        for pid in sorted(stats):
            v = stats[pid]
            row = f"{pid:>6}" + "".join(f"{v[m]:>14}" for m in METRICS)
            print(row)


def print_table(results, averaged):
    name_w = max(len(name) for name in results) + 2
    header = f"{'Scheduler':<{name_w}}" + "".join(
        f"{m:>14}" for m in METRICS
    )
    print(f"\nAverages (same {len(next(iter(results.values())))}-process workload)")
    print(header)
    print("-" * len(header))
    for name, avgs in averaged.items():
        row = f"{name:<{name_w}}" + "".join(
            f"{avgs[m]:>14.2f}" for m in METRICS
        )
        print(row)


def write_csv(results, path):
    import csv
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["scheduler", "pid"] + METRICS)
        for name, stats in results.items():
            for pid in sorted(stats):
                w.writerow([name, pid] + [stats[pid][m] for m in METRICS])
    print(f"wrote {path}")


def plot(results, out_path):
    fig, ax = plt.subplots(figsize=(8, 5))
    schedulers = list(results)
    x = range(len(METRICS))
    width = 0.8 / len(schedulers)

    for i, name in enumerate(schedulers):
        vals = [results[name][m] for m in METRICS]
        offsets = [xi + i * width for xi in x]
        ax.bar(offsets, vals, width=width, label=name)

    ax.set_xticks([xi + width * (len(schedulers) - 1) / 2 for xi in x])
    ax.set_xticklabels(METRICS)
    ax.set_ylabel("Ticks (average over test processes)")
    ax.set_title("Scheduler comparison: Turnaround / Waiting / Response")
    ax.legend()
    ax.grid(True, axis="y", alpha=0.3)

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"\nwrote {out_path}")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "logs",
        nargs="+",
        help="one or more logfile:Label pairs, e.g. rr.log:RR fifo.log:FIFO mlfq.log:MLFQ",
    )
    ap.add_argument(
        "-o", "--out", default="scheduler_comparison.png",
        help="output chart path (default: scheduler_comparison.png)",
    )
    ap.add_argument(
        "--csv", default=None,
        help="also write per-process results to this CSV path",
    )
    args = ap.parse_args()

    results = {}
    for entry in args.logs:
        if ":" not in entry:
            sys.exit(f"expected logfile:Label, got {entry!r}")
        path, label = entry.split(":", 1)
        results[label] = parse_log(path)

    print_per_process(results)
    averaged = {name: averages(stats) for name, stats in results.items()}
    print_table(results, averaged)
    if args.csv:
        write_csv(results, args.csv)
    plot(averaged, args.out)


if __name__ == "__main__":
    main()