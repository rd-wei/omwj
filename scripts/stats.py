#!/usr/bin/env python3
"""
Summarise repeated measurements as mean +/- stddev.

Used by the measurement drivers to turn N trials per cell into the reported
mean +/- stddev.  `awk` is unavailable on the measurement machine, so float
aggregation lives here rather than in the shell.

Reads whitespace/newline-separated numbers from argv or stdin.  Non-numeric
tokens (e.g. "timeout", "ERR") are counted and reported, never silently
dropped -- a cell with a failed trial must not look like a clean one.

Usage:
  python3 scripts/stats.py 6.48 6.77 6.54
  echo "6.48 6.77 6.54" | python3 scripts/stats.py
  python3 scripts/stats.py --format=full 6.48 6.77 6.54

Formats:
  short (default)  "6.60 +/- 0.15"
  full             "6.60 +/- 0.15  (n=3, min 6.48, max 6.77)"
  csv              "6.60,0.15,3"
"""

import math
import sys


def summarise(values, fmt='short'):
    n = len(values)
    mean = sum(values) / n
    # Sample stddev (n-1); a single trial has no spread to report.
    if n > 1:
        var = sum((v - mean) ** 2 for v in values) / (n - 1)
        sd = math.sqrt(var)
    else:
        sd = 0.0

    if fmt == 'csv':
        return f'{mean:.3f},{sd:.3f},{n}'
    if fmt == 'full':
        return (f'{mean:.2f} +/- {sd:.2f}  '
                f'(n={n}, min {min(values):.2f}, max {max(values):.2f})')
    return f'{mean:.2f} +/- {sd:.2f}'


def main() -> int:
    args = [a for a in sys.argv[1:]]
    fmt = 'short'
    for a in list(args):
        if a.startswith('--format='):
            fmt = a.split('=', 1)[1]
            args.remove(a)

    tokens = args if args else sys.stdin.read().split()

    values, bad = [], []
    for t in tokens:
        try:
            values.append(float(t))
        except ValueError:
            bad.append(t)

    if not values:
        print('n/a' + (f' ({len(bad)} failed trials)' if bad else ''))
        return 1

    out = summarise(values, fmt)
    if bad:
        out += f'  [{len(bad)} failed: {",".join(sorted(set(bad)))}]'
    print(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
