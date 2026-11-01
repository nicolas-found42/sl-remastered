#!/usr/bin/env python3
"""Check a run's cartrace-full.jsonl: speed steps, falling, nonfinite, tags.

Usage: tools/cartrace_check.py <run-dir> [--max-step 5] [--max-fall 20]
Exit 0 when the car never gains more than --max-step m/s in one step, never
drops more than --max-fall metres below its first recorded height, and no
step carries a tag. A car that falls through the ground fails the check even
when the run reports status=ok.
"""
import json
import math
import sys


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opts = dict(zip(argv[1:], argv[2:]))
    max_step = float(opts.get("--max-step", 5))
    max_fall = float(opts.get("--max-fall", 20))
    if len(args) < 1:
        print(__doc__)
        return 2
    path = args[0].rstrip("/") + "/cartrace-full.jsonl"
    prev = {}
    start = {}
    worst_step = 0.0
    worst_fall = 0.0
    problems = []
    rows = 0
    for line in open(path):
        r = json.loads(line)
        rows += 1
        car = r.get("car", "")
        speed = r["spd"][1]
        worst_step = max(worst_step, speed - r["spd"][0])
        if car in prev:
            worst_step = max(worst_step, speed - prev[car])
        prev[car] = speed
        y = r["post"]["p"][1]
        start.setdefault(car, y)
        worst_fall = max(worst_fall, start[car] - y)
        anomaly = r.get("anom") or r.get("tags")
        if anomaly:
            problems.append("tick %s anomaly %s" % (r["tick"], anomaly))
        if not all(math.isfinite(v) for v in (*r["spd"], *r["post"]["p"])):
            problems.append("tick %s nonfinite speed or position" % r["tick"])
    if not rows:
        problems.append("empty trace")
    if worst_step > max_step:
        problems.append("speed step %.2f > %.2f" % (worst_step, max_step))
    if worst_fall > max_fall:
        problems.append("fell %.1f m > %.1f" % (worst_fall, max_fall))
    print("%s rows=%d worst_step=%.2f worst_fall=%.1f %s" %
          (path, rows, worst_step, worst_fall,
           "FAIL " + "; ".join(problems[:3]) if problems else "ok"))
    return 1 if problems else 0


sys.exit(main(sys.argv))
