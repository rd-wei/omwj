#!/usr/bin/env python3
"""
Report input data sizes per query and per data directory.

Three numbers matter and they are not interchangeable:

  rows   input tuples the query reads (sum over the tables it names -- self-joins
         are materialised as numbered copies, so a k-table query reads k tables)
  disk   the CSV bytes on disk
  wire   rows * sizeof(entry_t) -- what actually crosses the ECALL boundary,
         transferred once for the single-ecall engine

Keeps reported input sizes derived from the committed data rather than
hand-transcribed.

Usage:
  python3 scripts/input_sizes.py                     # Higgs day ladder
  python3 scripts/input_sizes.py --dirs input/plaintext/data_0_01
  python3 scripts/input_sizes.py --queries tpch_tm1 tpch_tm3 \
      --dirs input/plaintext/data_0_001 input/plaintext/data_0_01
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENTRY_T_BYTES = 336          # common/entry_t.h -- layout-critical, do not guess
FROM_RE = re.compile(r'FROM\s+(.*?)\s+WHERE', re.IGNORECASE | re.DOTALL)

HIGGS_RUNGS = ('1d', '2d', '3d')
HIGGS_QUERIES = (['higgs_tw3_w4', 'higgs_tw4_w4', 'higgs_tw5_w4']
                 + [f'higgs_hop{i}_w4' for i in range(1, 9)]
                 + [f'higgs_btree{i}_w4' for i in range(1, 9)])


def table_stats(path):
    """(rows, bytes) for one CSV, excluding the header line."""
    with path.open() as fh:
        rows = sum(1 for _ in fh) - 1
    return rows, path.stat().st_size


def dir_stats(d):
    rows = size = 0
    for f in sorted(d.glob('*.csv')):
        r, b = table_stats(f)
        rows += r
        size += b
    return rows, size


def query_tables(stem):
    sql = (ROOT / 'input' / 'queries' / f'{stem}.sql').read_text()
    m = FROM_RE.search(sql)
    if not m:
        raise SystemExit(f'{stem}.sql: no FROM ... WHERE clause found')
    return [t.strip() for t in m.group(1).split(',')]


def fmt(rows, size):
    return f'{rows:,} / {size/1048576:.2f} MiB / {rows*ENTRY_T_BYTES/1048576:.1f} MiB'


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dirs', nargs='*', help='data directories (default: Higgs ladder)')
    ap.add_argument('--queries', nargs='*', help='query stems (default: Higgs queries)')
    args = ap.parse_args()

    if args.dirs:
        dirs = [Path(d) for d in args.dirs]
    else:
        dirs = ([ROOT / 'input/plaintext' / f'higgs_{r}' for r in HIGGS_RUNGS]
                + [ROOT / 'input/plaintext' / f'higgs_hops_{r}' for r in HIGGS_RUNGS])

    print('=== Directory totals (rows / disk) ===')
    for d in dirs:
        if not d.exists():
            print(f'  {d.name:20s} (missing)')
            continue
        rows, size = dir_stats(d)
        print(f'  {d.name:20s} {rows:10,d} rows  {size/1048576:8.2f} MiB')

    queries = args.queries if args.queries else list(HIGGS_QUERIES)
    if not queries:
        return 0

    print('\n=== Per query: rows / disk / ECALL wire ===')
    print(f'{"query":18s} {"tables":>6s}  ' + '  '.join(f'{d.name:>28s}' for d in dirs
                                                        if d.exists()))
    for q in queries:
        try:
            tables = query_tables(q)
        except SystemExit as e:
            print(f'{q:18s} -- {e}')
            continue
        cells = []
        for d in dirs:
            if not d.exists():
                continue
            rows = size = 0
            missing = False
            for t in tables:
                f = d / f'{t}.csv'
                if not f.exists():
                    missing = True
                    break
                r, b = table_stats(f)
                rows += r
                size += b
            cells.append('n/a'.rjust(28) if missing else fmt(rows, size).rjust(28))
        print(f'{q:18s} {len(tables):6d}  ' + '  '.join(cells))
    return 0


if __name__ == '__main__':
    sys.exit(main())
