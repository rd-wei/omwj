#!/usr/bin/env python3
"""
Generate the higgs_* hop-series assets -- the real-timestamp replacement for
gen_twitter_hops.py.

Emits the SAME schema (users1..users9, 6 columns each: USERKEY, INDEG, OUTDEG,
ACTIVITY, JOINDATE, REGTIME -> 9x6 = 54 <= 64) so every existing hop/btree query
shape runs unmodified.  The only change is that REGTIME is the user's *real*
first activity time (rebased to seconds since the window start), not the
fabricated `hash(uid) % 10_000_000`.

Band widths are NOT reused from the Twitter series (those were tuned for a
uniform 36,500-"day" fake distribution).  They are chosen by the exact band DP
in higgs_common against the real timestamp distribution: one W for the hop chain
(targets hop-8 output) and one for the binary tree (targets btree-8 output).

Usage:
    gen_higgs_hops.py <social_edgelist> <activity_file> <data-out-dir> \
        <query-dir> [--cutoff SECONDS] [--chain-target N] [--btree-target N]
"""

import argparse
import sys
from pathlib import Path

import higgs_common as hc


def band_pred(child, parent, w):
    return (f'users{child}.U{child}_REGTIME > users{parent}.U{parent}_REGTIME - {w}\n'
            f'AND users{child}.U{child}_REGTIME < users{parent}.U{parent}_REGTIME')


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('social')
    ap.add_argument('activity')
    ap.add_argument('datadir')
    ap.add_argument('qdir')
    ap.add_argument('--cutoff', type=int, default=None)
    ap.add_argument('--chain-target', type=int, default=1_000_000)
    ap.add_argument('--btree-target', type=int, default=1_000_000)
    ap.add_argument('--fixed-w', type=int, default=None,
                    help='emit hop and btree queries at this fixed band width '
                         '(seconds), the same W for every scale; skips the search')
    args = ap.parse_args()

    datadir, qdir = Path(args.datadir), Path(args.qdir)
    datadir.mkdir(parents=True, exist_ok=True)
    qdir.mkdir(parents=True, exist_ok=True)

    joindate = hc.load_first_activity(args.activity, args.cutoff)
    active = set(joindate)
    indeg, outdeg = hc.degrees(hc.load_social_edges(args.social, active))

    # REGTIME == real first-activity time (same real signal as JOINDATE); the
    # richer per-event timestamps are the deferred gen_higgs_events.py upgrade.
    rows = [(u, indeg[u], outdeg[u], indeg[u] + outdeg[u], joindate[u], joindate[u])
            for u in sorted(active)]
    mx = max((max(r) for r in rows), default=0)
    if mx >= hc.JOIN_ATTR_MAX:
        print(f'ERROR: attribute {mx} >= JOIN_ATTR_MAX', file=sys.stderr)
        return 1

    for i in range(1, 10):
        p = f'U{i}_'
        with open(datadir / f'users{i}.csv', 'w') as f:
            f.write(f'{p}USERKEY,{p}INDEG,{p}OUTDEG,{p}ACTIVITY,{p}JOINDATE,{p}REGTIME\n')
            for r in rows:
                f.write(','.join(str(v) for v in r) + '\n')

    times = sorted(r[5] for r in rows)
    if args.fixed_w is not None:
        Wc = Wb = args.fixed_w
    else:
        Wc = hc.search_W(times, args.chain_target, hops=8)
        Wb = hc.search_W_btree(times, args.btree_target, e=8)

    # Chain queries: users1 <- users2 <- ... (each child banded below parent)
    for h in range(1, 9):
        tables = ', '.join(f'users{i}' for i in range(1, h + 2))
        preds = '\nAND '.join(band_pred(i + 1, i, Wc) for i in range(1, h + 1))
        (qdir / f'higgs_hop{h}_w{Wc}.sql').write_text(
            f'SELECT *\nFROM {tables}\nWHERE {preds};\n')

    # Binary-tree queries: first e BFS edges
    for e in range(1, 9):
        tables = ', '.join(f'users{i}' for i in range(1, e + 2))
        preds = '\nAND '.join(band_pred(c, p, Wb) for p, c in hc.BTREE_EDGES[:e])
        (qdir / f'higgs_btree{e}_w{Wb}.sql').write_text(
            f'SELECT *\nFROM {tables}\nWHERE {preds};\n')

    chain = [hc.chain_output(times, Wc, h) for h in range(1, 9)]
    btree = [hc.btree_output(times, Wb, e) for e in range(1, 9)]
    print(f'{datadir}: users={len(rows):,} x9, Wchain={Wc:,}, Wbtree={Wb:,}')
    print('  chain 1..8:', ', '.join(f'{c:,}' for c in chain))
    print('  btree 1..8:', ', '.join(f'{c:,}' for c in btree))
    return 0


if __name__ == '__main__':
    sys.exit(main())
