#!/usr/bin/env python3
"""
Shared helpers for the Higgs band-join generators (gen_higgs_data.py,
gen_higgs_hops.py).

The Higgs Twitter dataset (SNAP) ships:
  * higgs-social_network.edgelist   "follower followed"      (no timestamps)
  * higgs-activity_time.txt         "userA userB ts kind"    (real Unix time,
                                     kind in {RT, MT, RE}, one row per event)

Real July-2012 timestamps (~1.341e9) exceed JOIN_ATTR_MAX (1,073,741,820), so
every timestamp is rebased to seconds since the window start (2012-07-01
00:00:00 UTC), giving values in [0, ~604800] -- safely in bounds and directly
usable as band widths in seconds.  This module centralises that rebasing, the
activity/graph loaders, and the exact band-cardinality DP (window_sums) ported
from scripts/gen_twitter_hops.py so W can be chosen against the *real* timestamp
distribution instead of the old fabricated day-scale constants.
"""

import bisect
from collections import Counter

WINDOW_START = 1341100800      # 2012-07-01 00:00:00 UTC (Higgs window origin)
JOIN_ATTR_MAX = 1073741820     # common/constants.h -- attributes must stay below


def load_first_activity(activity_path, cutoff=None):
    """Map each user (as the actor `userA`) to their first activity time,
    rebased to seconds since WINDOW_START.  With `cutoff` (seconds), keep only
    users whose first activity falls in [0, cutoff) -- this is how time-based
    scale points are cut (D5).  Returns {uid: joindate}."""
    first = {}
    with open(activity_path) as f:
        for line in f:
            p = line.split()
            if len(p) < 4:
                continue
            uid = int(p[0])
            t = int(p[2]) - WINDOW_START
            if t < first.get(uid, 1 << 62):
                first[uid] = t
    if cutoff is not None:
        first = {u: t for u, t in first.items() if 0 <= t < cutoff}
    else:
        first = {u: t for u, t in first.items() if t >= 0}
    return first


def load_social_edges(edgelist_path, keep):
    """Yield (follower, followed) edges from the social graph, restricted to
    edges whose *both* endpoints are in `keep` (the active-user set for the
    current cutoff), so the graph shrinks coherently with the scale point."""
    with open(edgelist_path) as f:
        for line in f:
            p = line.split()
            if len(p) < 2:
                continue
            a, b = int(p[0]), int(p[1])
            if a in keep and b in keep:
                yield a, b


def degrees(edges):
    """(indeg, outdeg) Counters over an edge iterable."""
    indeg, outdeg = Counter(), Counter()
    for a, b in edges:
        outdeg[a] += 1
        indeg[b] += 1
    return indeg, outdeg


def window_sums(times, weights, W):
    """Exact band DP (ported verbatim from gen_twitter_hops.py:95-104).
    For each t in the sorted `times`, sum the weights of points in the band
    (t-W, t).  Returns a per-t list."""
    pre = [0]
    for w in weights:
        pre.append(pre[-1] + w)
    out = []
    for t in times:
        lo = bisect.bisect_right(times, t - W)
        hi = bisect.bisect_left(times, t)
        out.append(pre[hi] - pre[lo])
    return out


# Binary-tree BFS edge order (identical to gen_twitter_hops.py:29).
BTREE_EDGES = [(1, 2), (1, 3), (2, 4), (2, 5), (3, 6), (3, 7), (4, 8), (4, 9)]


def chain_output(times, W, hops):
    """Total output rows of an `hops`-edge band chain (k = hops+1 tables), the
    same iteration gen_twitter_hops.py uses.  For the single-band self-join
    (TW1, k=2) call with hops=1."""
    cnt = [1] * len(times)
    total = 0
    for _ in range(hops):
        cnt = window_sums(times, cnt, W)
        total = sum(cnt)
    return total


def btree_output(times, W, e):
    """Total output rows of the balanced binary tree with the first `e` BFS
    band edges (ported from gen_twitter_hops.py:112-123)."""
    children = {}
    for p, c in BTREE_EDGES[:e]:
        children.setdefault(p, []).append(c)

    def val(node):
        v = [1] * len(times)
        for c in children.get(node, []):
            wc = window_sums(times, val(c), W)
            v = [a * b for a, b in zip(v, wc)]
        return v
    return sum(val(1))


def _search_W(times, target, card_fn, w_hi=None):
    """Smallest W (seconds) whose cardinality `card_fn(times, W)` reaches
    `target`.  Cardinality is monotonic non-decreasing in W, so binary search.
    `w_hi` defaults to the full timestamp span."""
    if not times:
        return 0
    lo, hi = 1, (w_hi if w_hi is not None else (times[-1] - times[0] + 1))
    if card_fn(times, hi) < target:
        return hi
    while lo < hi:
        mid = (lo + hi) // 2
        if card_fn(times, mid) >= target:
            hi = mid
        else:
            lo = mid + 1
    return lo


def search_W(times, target, hops=1, w_hi=None):
    """W for a band chain of `hops` edges hitting `target` output rows
    (TW1 single band => hops=1)."""
    return _search_W(times, target, lambda t, w: chain_output(t, w, hops), w_hi)


def search_W_btree(times, target, e=8, w_hi=None):
    """W for the `e`-edge binary tree hitting `target` output rows."""
    return _search_W(times, target, lambda t, w: btree_output(t, w, e), w_hi)
