#!/usr/bin/env python3
"""
Join-order planner for the OBLIVIATOR comparison harness.

WHY THIS EXISTS
---------------
OBLIVIATOR is a binary-join *operator*: its enclave entry point is
`ecall_scalable_oblivious_join(input_path, len)`, which reads two relation
lengths and joins two relations.  Verified against the pristine Zenodo artifact
(10.5281/zenodo.14723872), it contains **no plan logic at all** -- no join-order
selection, no join graph, no pipeline.  Every multi-way plan decision in this
comparison, including the previous left-deep-only restriction, was made by *our*
wrapper (`run_obliviator_queries.py`), which is not part of their artifact.

That matters for fairness.  Choosing a good join order requires cardinality
statistics, and consulting them is a **data-dependent decision an oblivious
engine cannot make without leaking**.  So evaluating a binary-join engine under
its *optimal* order credits it with information a secure deployment could not
legitimately obtain.  The honest model for a system that ships no planner is a
*blind* one -- the expected cost over plans.

MODES
-----
  leftdeep  1 run   seed from join_conditions[0], extend greedily (the old
                    behaviour; kept so existing numbers stay reproducible)
  optimal   1 run   best plan by total intermediate volume, chosen from exact
                    cardinalities.  PRIVACY-VIOLATING -- an oracle no oblivious
                    system could consult.  Reference point only.
  random    1 run   uniformly-sampled plan (seeded, reproducible)
  average   N runs  every valid plan; reports mean +/- stddev, plus min/max

PLAN REPRESENTATION
-------------------
A plan is a binary tree over table indices.  Each node carries its rows as flat
value-lists plus a `schema` of qualified (table, column) names, where a join
node's schema is simply `left.schema + right.schema`.  This replaces the old
linear model (a single `current_intermediate` plus ColumnTracker's
accumulated_left/accumulated_right), which could hold only one live intermediate
and therefore could not express bushy plans at all.

Only *connected* plans are enumerated -- every node must be joinable through a
predicate, so no cross products.
"""

from __future__ import annotations

import random
import statistics
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

COLUMN_DELIM = "|"
TABLE_DELIM = "||"

# Catalan growth is brutal; refuse to enumerate a query we cannot finish.
MAX_PLANS = 200


# =============================================================================
# Plan tree
# =============================================================================

@dataclass(frozen=True)
class PlanNode:
    """A binary plan tree.  Leaves carry a table index; internal nodes carry
    their two children.  `tables` is the frozenset of table indices covered,
    which is what connectivity is checked against."""
    tables: frozenset
    left: Optional["PlanNode"] = None
    right: Optional["PlanNode"] = None
    table_idx: Optional[int] = None

    @property
    def is_leaf(self) -> bool:
        return self.table_idx is not None

    def label(self, names: Sequence[str]) -> str:
        if self.is_leaf:
            return names[self.table_idx]
        return f"({self.left.label(names)}⋈{self.right.label(names)})"

    def internal_nodes(self) -> List["PlanNode"]:
        """Every join node, children first (post-order)."""
        if self.is_leaf:
            return []
        return self.left.internal_nodes() + self.right.internal_nodes() + [self]


def enumerate_plans(n_tables: int, edges: Sequence[Tuple[int, int]],
                    max_plans: int = MAX_PLANS) -> List[PlanNode]:
    """All connected binary plans over the join graph.

    `edges` are (i, j) table-index pairs that carry a join predicate.  A split
    is admissible only when the two halves are joined by some predicate, which
    is what rules out cross products.  For a k-table chain this yields the
    Catalan(k-1) parenthesisations; for other shapes it yields the connected
    subset, so stars and trees work too.
    """
    adj = [set() for _ in range(n_tables)]
    for i, j in edges:
        adj[i].add(j)
        adj[j].add(i)

    def connected(ts: frozenset) -> bool:
        if not ts:
            return False
        seen = {next(iter(ts))}
        stack = list(seen)
        while stack:
            u = stack.pop()
            for v in adj[u]:
                if v in ts and v not in seen:
                    seen.add(v)
                    stack.append(v)
        return seen == set(ts)

    def joinable(a: frozenset, b: frozenset) -> bool:
        return any(j in b for i in a for j in adj[i])

    @lru_cache(maxsize=None)
    def build(ts: frozenset) -> Tuple[PlanNode, ...]:
        if len(ts) == 1:
            i = next(iter(ts))
            return (PlanNode(tables=ts, table_idx=i),)
        out: List[PlanNode] = []
        members = sorted(ts)
        # iterate proper non-empty subsets containing the smallest member, so
        # each unordered split is generated once
        first, rest = members[0], members[1:]
        for mask in range(1 << len(rest)):
            sub = frozenset([first] + [rest[b] for b in range(len(rest))
                                       if mask >> b & 1])
            comp = ts - sub
            if not comp or not connected(sub) or not connected(comp):
                continue
            if not joinable(sub, comp):
                continue
            for l in build(sub):
                for r in build(comp):
                    out.append(PlanNode(tables=ts, left=l, right=r))
        return tuple(out)

    all_tables = frozenset(range(n_tables))
    if not connected(all_tables):
        raise ValueError("join graph is disconnected (query has a cross product)")
    plans = list(build(all_tables))
    if len(plans) > max_plans:
        raise ValueError(f"{len(plans)} plans exceeds max_plans={max_plans}; "
                         f"raise the cap deliberately if you mean to run this")
    return plans


def leftdeep_plan(n_tables: int, edges: Sequence[Tuple[int, int]],
                  order: Sequence[Tuple[int, int]]) -> PlanNode:
    """Rebuild the harness's historical plan: seed from the first join
    condition and extend greedily with any edge touching what is already
    joined.  `order` is the join-condition list in SQL order."""
    adj_edges = list(order)
    first = adj_edges[0]
    node = PlanNode(tables=frozenset(first),
                    left=PlanNode(tables=frozenset({first[0]}), table_idx=first[0]),
                    right=PlanNode(tables=frozenset({first[1]}), table_idx=first[1]))
    joined = set(first)
    remaining = adj_edges[1:]
    while remaining:
        for e in list(remaining):
            new = [t for t in e if t not in joined]
            if len(new) == len(e):          # touches nothing yet joined
                continue
            remaining.remove(e)
            if not new:                      # both ends already in -> no-op edge
                break
            t = new[0]
            node = PlanNode(tables=node.tables | {t}, left=node,
                            right=PlanNode(tables=frozenset({t}), table_idx=t))
            joined.add(t)
            break
        else:
            e = remaining.pop(0)
            for t in e:
                if t not in joined:
                    node = PlanNode(tables=node.tables | {t}, left=node,
                                    right=PlanNode(tables=frozenset({t}), table_idx=t))
                    joined.add(t)
    return node


# =============================================================================
# Costing (for `optimal` -- an oracle, see module docstring)
# =============================================================================

def plan_cost(plan: PlanNode, card) -> int:
    """Total intermediate volume: the summed size of every join node except the
    root (the root is the query output, which every plan must produce).

    `card` maps a frozenset of table indices to that sub-join's cardinality.
    Supplying exact cardinalities is precisely the privacy violation this module
    exists to quantify -- see the module docstring.
    """
    return sum(card(n.tables) for n in plan.internal_nodes()
               if n.tables != plan.tables)


def choose_plan(plans: List[PlanNode], mode: str, card=None,
                seed: int = 0, order=None) -> List[PlanNode]:
    """Return the plan(s) to execute for `mode`."""
    if mode == "average":
        return plans
    if mode == "random":
        return [random.Random(seed).choice(plans)]
    if mode == "optimal":
        if card is None:
            raise ValueError("optimal mode needs cardinalities")
        return [min(plans, key=lambda p: plan_cost(p, card))]
    raise ValueError(f"unknown plan mode: {mode}")


# =============================================================================
# Packed-row helpers (match run_obliviator_queries.py conventions)
# =============================================================================

def pack_row(values: Sequence[str], group_sizes: Sequence[int]) -> str:
    """Pack a flat value list back into the `a|b||c|d` wire form, where
    `group_sizes` gives the per-source-table column counts."""
    out, at = [], 0
    for n in group_sizes:
        out.append(COLUMN_DELIM.join(values[at:at + n]))
        at += n
    return TABLE_DELIM.join(out)


def unpack_row(packed: str) -> List[str]:
    """Flatten `a|b||c|d` into ['a','b','c','d']."""
    vals: List[str] = []
    for tbl in packed.split(TABLE_DELIM):
        vals.extend(tbl.split(COLUMN_DELIM))
    return vals


# =============================================================================
# Payload sizing guard
# =============================================================================
#
# parallel_enc.c does:
#     strncpy(arr[i].data, strtok(NULL, "\n"), DATA_LENGTH - 1);
# with NO length check.  A payload wider than DATA_LENGTH-1 is silently
# truncated and the join returns wrong rows with no diagnostic.
#
# This bites plan enumeration specifically: payload width is plan-dependent.
# A left-deep plan grows the accumulated row one small table at a time, while a
# right-deep plan may join the two widest tables first and carry that width from
# step one.  Sizing DATA_LENGTH from observed left-deep runs therefore silently
# corrupts other plans -- measured on tm3 @ 0.01, left-deep peaks at 220 chars
# while right-deep peaks at 298, so a build sized for the former returns garbage
# for the latter.

def read_data_length(join_dir: Path) -> int:
    """The DATA_LENGTH the enclave was actually compiled with."""
    import re
    txt = (join_dir / "common" / "elem_t.h").read_text()
    m = re.search(r'#define\s+DATA_LENGTH\s+(\d+)', txt)
    if not m:
        raise ValueError(f"no DATA_LENGTH in {join_dir}/common/elem_t.h")
    return int(m.group(1))


def table_widths(data_dir: Path, names: Sequence[str]) -> Dict[str, int]:
    """Widest packed row per table: len('v1|v2|...')."""
    import csv
    out = {}
    for n in names:
        with (data_dir / f"{n}.csv").open() as f:
            r = csv.reader(f)
            next(r)
            out[n] = max((len(COLUMN_DELIM.join(row)) for row in r), default=0)
    return out


def node_width(node: PlanNode, widths: Dict[str, int], names: Sequence[str]) -> int:
    """Packed width of a node's rows (children joined by TABLE_DELIM)."""
    if node.is_leaf:
        return widths[names[node.table_idx]]
    return (node_width(node.left, widths, names) + len(TABLE_DELIM)
            + node_width(node.right, widths, names))


def plan_max_payload(plan: PlanNode, widths: Dict[str, int],
                     names: Sequence[str]) -> int:
    """Widest payload this plan ever hands to the enclave."""
    if plan.is_leaf:
        return 0
    return max(node_width(plan.left, widths, names),
               node_width(plan.right, widths, names),
               plan_max_payload(plan.left, widths, names),
               plan_max_payload(plan.right, widths, names))


def check_payloads(plans: Sequence[PlanNode], widths: Dict[str, int],
                   names: Sequence[str], data_length: int):
    """(ok, offenders) -- offenders would be silently truncated."""
    usable = data_length - 1
    bad = [(p, plan_max_payload(p, widths, names)) for p in plans
           if plan_max_payload(p, widths, names) > usable]
    return (not bad), bad


def summarise(runs: List[float]) -> Dict[str, float]:
    """mean/stddev/min/max over per-plan measurements."""
    if not runs:
        return {}
    return {
        "mean": statistics.mean(runs),
        "stddev": statistics.stdev(runs) if len(runs) > 1 else 0.0,
        "min": min(runs),
        "max": max(runs),
        "n": len(runs),
    }
