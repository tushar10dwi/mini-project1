#!/usr/bin/env python3
"""
plot_mlfq.py -- turn MLFQLOG output from the modified xv6 kernel into
the timeline/scatter plot required by section 2.3.2 of the MLFQ
mini-project.

Workflow:
  1. make clean && make qemu SCHEDULER=MLFQ | tee run.log
  2. At the xv6 shell prompt: schedulertest
  3. Once it prints SCHEDTEST_END, Ctrl-A then x to quit qemu
  4. python3 plot_mlfq.py run.log yourusername

Expects two kinds of lines in run.log:
  SCHEDTEST_CHILD pid=<pid> role=<role>      -- printed once by each
                                                child in schedulertest.c
  MLFQLOG <tick> <pid> <queue>              -- printed every tick a
                                                process runs, from
                                                mlfq_timer_tick() in
                                                kernel/proc.c
"""
import argparse
import re
import sys
from collections import defaultdict

import matplotlib.pyplot as plt

CHILD_RE = re.compile(r"SCHEDTEST_CHILD pid=(\d+) role=(\S+)")
LOG_RE = re.compile(r"MLFQLOG (\d+) (\d+) (\d+)")

BOOST_INTERVAL = 48  # must match BOOST_INTERVAL in kernel/proc.c


def parse_log(path):
    roles = {}
    points = defaultdict(list)  # pid -> [(tick, queue), ...]

    with open(path) as f:
        for line in f:
            m = CHILD_RE.search(line)
            if m:
                pid, role = int(m.group(1)), m.group(2)
                roles[pid] = role
                continue
            m = LOG_RE.search(line)
            if m:
                tick, pid, queue = (int(x) for x in m.groups())
                points[pid].append((tick, queue))

    # Keep only pids schedulertest actually labeled -- drops any
    # shell/init/other activity that happened to be in the same log.
    points = {pid: pts for pid, pts in points.items() if pid in roles}
    if not points:
        sys.exit(
            "No MLFQLOG lines matched a SCHEDTEST_CHILD pid.\n"
            "Did you run schedulertest and capture its full console "
            "output (make qemu SCHEDULER=MLFQ | tee run.log)?"
        )
    return roles, points


def plot(roles, points, username, out_path):
    fig, ax = plt.subplots(figsize=(11, 6))

    all_ticks = [t for pts in points.values() for t, _ in pts]
    tmin, tmax = min(all_ticks), max(all_ticks)

    # Vertical marker at every priority-boost tick in range, so the
    # "all processes jump back to queue 0" moment is visible.
    boost_tick = (tmin // BOOST_INTERVAL) * BOOST_INTERVAL
    while boost_tick <= tmax:
        if boost_tick >= tmin:
            ax.axvline(boost_tick, color="gray", linestyle="--",
                        linewidth=0.8, alpha=0.6, zorder=0)
        boost_tick += BOOST_INTERVAL

    colors = plt.cm.tab10.colors
    for i, pid in enumerate(sorted(points)):
        pts = sorted(points[pid])
        xs = [t for t, _ in pts]
        ys = [q for _, q in pts]
        color = colors[i % len(colors)]
        ax.step(xs, ys, where="post", color=color, alpha=0.5, linewidth=1)
        ax.scatter(xs, ys, color=color, s=10,
                    label=f"pid {pid} ({roles[pid]})")

    ax.set_xlabel("Time elapsed (ticks since scheduler start)")
    ax.set_ylabel("MLFQ queue")
    ax.set_yticks([0, 1, 2, 3])
    ax.set_ylim(-0.5, 3.5)
    ax.invert_yaxis()  # queue 0 (highest priority) drawn at the top
    ax.set_title("MLFQ scheduling timeline")
    ax.legend(loc="upper right", fontsize=8)
    ax.grid(True, axis="y", alpha=0.3)

    fig.text(0.99, 0.01, username, ha="right", va="bottom",
              fontsize=26, color="gray", alpha=0.25)

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"wrote {out_path}")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("logfile", help="captured qemu console output")
    ap.add_argument(
        "username",
        help="IIIT username (the part before @) for the plot watermark",
    )
    ap.add_argument(
        "-o", "--out", default="mlfq_timeline.png",
        help="output image path (default: mlfq_timeline.png)",
    )
    args = ap.parse_args()

    roles, points = parse_log(args.logfile)
    plot(roles, points, args.username, args.out)


if __name__ == "__main__":
    main()