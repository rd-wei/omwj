#!/usr/bin/env python3
"""
Build the higgs_* multi-way band-join tables from the real Higgs Twitter
dataset (SNAP) -- the honest replacement for gen_twitter_data.py's fabricated
band attribute.

Emits the SAME schema gen_twitter_data.py does, so every existing tw-shaped
query runs unmodified against this data:
    users1/users2/users3   Ux_USERKEY,Ux_INDEG,Ux_OUTDEG,Ux_ACTIVITY,Ux_JOINDATE
    follows1/follows2      Fx_FOLLOWER,Fx_FOLLOWED
The only change from the Twitter generator is the source of JOINDATE: instead of
`hash(uid) % 36500`, it is the user's *real* first activity time (earliest
RT/MT/RE event as the actor), rebased to seconds since the window start.

Scale points are cut by elapsed time (--cutoff SECONDS): a user is included iff
their first activity falls within the cutoff, and the social graph is restricted
to edges among those users, so nodes and edges shrink together.

With --query-dir, also writes the tw query family with band widths chosen by the
exact-DP W search (higgs_common.search_W) against this data's real timestamp
distribution -- no reused day-scale constants.

Usage:
    gen_higgs_data.py <social_edgelist> <activity_file> <out-dir> \
        [--cutoff SECONDS] [--query-dir DIR] [--targets N,N,N]
"""

import argparse
import sys
from pathlib import Path

import higgs_common as hc


def write_users(outdir, rows):
    for i in (1, 2, 3):
        p = f'U{i}_'
        with open(outdir / f'users{i}.csv', 'w') as f:
            f.write(f'{p}USERKEY,{p}INDEG,{p}OUTDEG,{p}ACTIVITY,{p}JOINDATE\n')
            for r in rows:
                f.write(','.join(str(v) for v in r) + '\n')


def write_follows(outdir, edges):
    for i in (1, 2):
        p = f'F{i}_'
        with open(outdir / f'follows{i}.csv', 'w') as f:
            f.write(f'{p}FOLLOWER,{p}FOLLOWED\n')
            for a, b in edges:
                f.write(f'{a},{b}\n')


def band(child, parent, w):
    """One band edge in the tw-series direction (matches twitter_tw1_w365.sql):
    users{child} is inside a window of width w below users{parent}."""
    return (f'users{child}.U{child}_JOINDATE > users{parent}.U{parent}_JOINDATE - {w}\n'
            f'AND users{child}.U{child}_JOINDATE < users{parent}.U{parent}_JOINDATE')


def emit_tw_queries(qdir, w):
    """Write the multi-way band tw queries at width w: tw3 (k=3 band chain),
    tw4 (k=4 graph+band), tw5 (k=5 graph+band chain).  The non-multi-way tw1
    (k=2) and the equality tw2 were dropped from the workload."""
    qdir = Path(qdir)
    qdir.mkdir(parents=True, exist_ok=True)

    (qdir / f'higgs_tw3_w{w}.sql').write_text(
        'SELECT *\nFROM users1, users2, users3\nWHERE '
        + band(1, 2, w) + '\nAND ' + band(2, 3, w) + ';\n')

    (qdir / f'higgs_tw4_w{w}.sql').write_text(
        'SELECT *\nFROM follows1, users1, users2, follows2\n'
        'WHERE follows1.F1_FOLLOWED = users1.U1_USERKEY\n'
        'AND ' + band(1, 2, w) + '\n'
        'AND follows2.F2_FOLLOWER = users2.U2_USERKEY;\n')

    (qdir / f'higgs_tw5_w{w}.sql').write_text(
        'SELECT *\nFROM follows1, users1, users2, users3, follows2\n'
        'WHERE follows1.F1_FOLLOWED = users1.U1_USERKEY\n'
        'AND ' + band(1, 2, w) + '\n'
        'AND ' + band(2, 3, w) + '\n'
        'AND follows2.F2_FOLLOWER = users3.U3_USERKEY;\n')


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('social')
    ap.add_argument('activity')
    ap.add_argument('outdir')
    ap.add_argument('--cutoff', type=int, default=None,
                    help='include only users whose first activity is within '
                         'this many seconds of the window start (scale point)')
    ap.add_argument('--query-dir', default=None,
                    help='also emit higgs_tw* queries here')
    ap.add_argument('--fixed-w', type=int, default=None,
                    help='emit the tw queries at this fixed band width (seconds), '
                         'the same W for every scale; skips the cardinality search')
    ap.add_argument('--targets', default='10000',
                    help='comma-separated single-band output-cardinality targets '
                         'for the W search (one W per target; sizes the tw band). '
                         'Ignored when --fixed-w is given.')
    args = ap.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    joindate = hc.load_first_activity(args.activity, args.cutoff)
    active = set(joindate)
    indeg, outdeg = hc.degrees(hc.load_social_edges(args.social, active))
    edges = list(hc.load_social_edges(args.social, active))

    rows = [(u, indeg[u], outdeg[u], indeg[u] + outdeg[u], joindate[u])
            for u in sorted(active)]

    # Mandatory overflow guard (D2): every attribute must stay in bounds.
    mx = max((max(r) for r in rows), default=0)
    if mx >= hc.JOIN_ATTR_MAX:
        print(f'ERROR: attribute {mx} >= JOIN_ATTR_MAX {hc.JOIN_ATTR_MAX}',
              file=sys.stderr)
        return 1

    write_users(outdir, rows)
    write_follows(outdir, edges)
    print(f'{outdir}: users={len(rows):,} x3, edges={len(edges):,} x2, '
          f'joindate_max={max((r[4] for r in rows), default=0):,}')

    if args.query_dir:
        times = sorted(r[4] for r in rows)
        if args.fixed_w is not None:
            emit_tw_queries(args.query_dir, args.fixed_w)
            print(f'  tw queries at fixed W={args.fixed_w} '
                  f'(single-band rows here: {hc.chain_output(times, args.fixed_w, 1):,})')
        else:
            ws = []
            for tgt in (int(t) for t in args.targets.split(',')):
                w = hc.search_W(times, tgt, hops=1)
                emit_tw_queries(args.query_dir, w)
                ws.append((tgt, w, hc.chain_output(times, w, 1)))
            print('  tw W search (target -> W -> single-band rows):',
                  ', '.join(f'{t}->{w}->{c:,}' for t, w, c in ws))
    return 0


if __name__ == '__main__':
    sys.exit(main())
