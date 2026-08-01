#!/usr/bin/env python3
"""
Run an OBLIVIATOR query under a chosen join-order model.

    python3 run_plan_modes.py <query.sql> <data_dir> --mode {leftdeep,optimal,random,average}

See join_planner.py for why this exists: OBLIVIATOR ships a binary-join operator
with no planner, so the plan is supplied entirely by this harness.  Picking a
*good* plan needs cardinality statistics, and consulting those is a
data-dependent decision an oblivious engine cannot make without leaking -- so
the defensible cost model for a planner-less system is a blind one.

Executes a plan tree directly, holding as many live intermediates as the plan
needs, which is what lets bushy plans run.  Each node's rows are flat value
lists plus a schema of qualified (table, column) names; a join node's schema is
left.schema + right.schema.

CORRECTNESS
-----------
**Every executed plan is verified tuple-exactly against SQLite** (plan_verify.py)
-- not just the one whose output happens to be left on disk.  Two checks run:

  * per-plan vs ground truth -- the authoritative verdict;
  * cross-plan agreement (`--check`) -- free, needs no ground truth, and proves
    some plan is wrong without saying which.  Rows are canonicalised into one
    column order first, because a plan node's schema is left.schema+right.schema
    and so column order is plan-dependent (harmless on a chain, a 100% false
    positive rate on a star).

TIMING
------
Each plan gets its **own enclave**, so `init` / `total` / `exec` are measured per
plan rather than amortised across a shared session.  Plan selection, the payload
guard and all verification sit **outside** the timed region: they are this
harness's work, not the engine's.  CSV loading stays inside, because the legacy
pipeline pays it too and excluding it would break comparability.
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
import time
from collections import Counter
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

import plan_verify as pv
from join_planner import (COLUMN_DELIM, TABLE_DELIM, PlanNode, check_payloads,
                          choose_plan, enumerate_plans, leftdeep_plan,
                          plan_cost, plan_max_payload, read_data_length,
                          summarise, table_widths, unpack_row)
from run_obliviator_queries import (ObliviatorSession, SQLParser, WORK_DIR,
                                    run_obliviator_join)

BASE = Path(__file__).resolve().parent


# =============================================================================
# Node materialisation
# =============================================================================

class Node:
    """A materialised plan node: rows as flat value lists, plus schema."""

    def __init__(self, rows: List[List[str]], schema: List[Tuple[str, str]],
                 groups: List[int]):
        self.rows = rows
        self.schema = schema      # [(table, column), ...] in row order
        self.groups = groups      # per-source-table column counts, in order

    def index_of(self, table: str, column: str) -> int:
        for i, (t, c) in enumerate(self.schema):
            if t == table and c == column:
                return i
        raise ValueError(f"{table}.{column} not in schema {self.schema}")

    def packed(self, row: Sequence[str]) -> str:
        out, at = [], 0
        for n in self.groups:
            out.append(COLUMN_DELIM.join(row[at:at + n]))
            at += n
        return TABLE_DELIM.join(out)


def load_table(data_dir: Path, name: str) -> Node:
    with (data_dir / f"{name}.csv").open() as f:
        r = csv.reader(f)
        cols = next(r)
        rows = [list(x) for x in r]
    return Node(rows, [(name, c) for c in cols], [len(cols)])


# =============================================================================
# Cardinality oracle (for `optimal` only)
# =============================================================================

def cardinality_oracle(query: Path, data_dir: Path, names, conds):
    """Exact cardinality of every connected sub-join, from SQLite.

    THIS IS THE PRIVACY VIOLATION THIS MODULE EXISTS TO QUANTIFY.  No oblivious
    system can consult these -- reading them is a data-dependent decision.  It
    is computed here only so `optimal` has a concrete reference point to measure
    the other modes against.  Cached, because it is a 2^k sweep.
    """
    import json
    import sqlite3
    from itertools import combinations

    cache = BASE / "query_results" / f"card_{query.stem}_{data_dir.name}.json"
    if cache.exists():
        raw = json.loads(cache.read_text())
        return lambda ts: raw["|".join(str(i) for i in sorted(ts))]

    print(f"  computing cardinality oracle (SQLite) -> {cache.name}")
    con = sqlite3.connect(":memory:")
    for n in names:
        with (data_dir / f"{n}.csv").open() as f:
            r = csv.reader(f)
            cols = next(r)
            rows = list(r)
        con.execute(f"CREATE TABLE {n} ({','.join(cols)})")
        con.executemany(f"INSERT INTO {n} VALUES ({','.join('?' * len(cols))})",
                        rows)

    preds = [(c.left_table, c.left_column, c.right_table, c.right_column)
             for c in conds]
    idx = {n: i for i, n in enumerate(names)}
    adj = [set() for _ in names]
    for a, _, x, _ in preds:
        adj[idx[a]].add(idx[x])
        adj[idx[x]].add(idx[a])

    def connected(ts) -> bool:
        seen, stack = {ts[0]}, [ts[0]]
        while stack:
            for v in adj[stack.pop()]:
                if v in ts and v not in seen:
                    seen.add(v)
                    stack.append(v)
        return len(seen) == len(ts)

    raw = {}
    for k in range(1, len(names) + 1):
        for ts in combinations(range(len(names)), k):
            # Only connected sub-joins can appear in a plan node, and counting
            # the disconnected ones means materialising cross products -- which
            # is merely wasteful at 0.01 and intractable at 0.1.
            if not connected(ts):
                continue
            live = {names[i] for i in ts}
            where = [f"{a}.{b}={x}.{y}" for a, b, x, y in preds
                     if a in live and x in live]
            q = f"SELECT COUNT(*) FROM {','.join(sorted(live))}"
            if where:
                q += " WHERE " + " AND ".join(where)
            raw["|".join(str(i) for i in ts)] = con.execute(q).fetchone()[0]

    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps(raw, indent=1))
    return lambda ts: raw["|".join(str(i) for i in sorted(ts))]


# =============================================================================
# Plan execution
# =============================================================================

def find_predicate(conds, left: Node, right: Node):
    """A join condition linking the two nodes, oriented (left_col, right_col)."""
    lt = {t for t, _ in left.schema}
    rt = {t for t, _ in right.schema}
    for c in conds:
        if c.left_table in lt and c.right_table in rt:
            return (c.left_table, c.left_column), (c.right_table, c.right_column)
        if c.right_table in lt and c.left_table in rt:
            return (c.right_table, c.right_column), (c.left_table, c.left_column)
    raise ValueError("no predicate links these nodes (cross product)")


def to_key(v: str) -> int:
    try:
        return int(v)
    except ValueError:
        return abs(hash(v)) % (2 ** 31)


def run_join(node_l: Node, node_r: Node, conds, session, tag: str,
             prefix: str = "plan") -> Tuple[Node, Dict]:
    (lt, lc), (rt, rc) = find_predicate(conds, node_l, node_r)
    li, ri = node_l.index_of(lt, lc), node_r.index_of(rt, rc)

    inp = WORK_DIR / f"{prefix}_{tag}_input.txt"
    with inp.open("w") as f:
        f.write(f"{len(node_l.rows)} {len(node_r.rows)}\n")
        for row in node_l.rows:
            f.write(f"{to_key(row[li])} {node_l.packed(row)}\n")
        for row in node_r.rows:
            f.write(f"{to_key(row[ri])} {node_r.packed(row)}\n")

    res = run_obliviator_join(inp, session=session)
    out_file = Path(str(inp).replace(".txt", "_output.txt"))

    rows: List[List[str]] = []
    if out_file.exists():
        with out_file.open() as f:
            for line in f:
                p = line.rstrip("\n").split(" ")
                if len(p) < 4:
                    continue
                rows.append(unpack_row(p[1]) + unpack_row(p[3]))

    return Node(rows, node_l.schema + node_r.schema,
                node_l.groups + node_r.groups), res


def execute_plan(plan: PlanNode, tables, conds, data_dir: Path,
                 session, prefix: str = "plan") -> Tuple[Node, List[Dict]]:
    """Materialise a plan bottom-up.  Multiple intermediates stay live
    simultaneously, which is what the old linear pipeline could not do.

    Steps are numbered post-order, so the root is always the highest-numbered
    step -- which is what `prefix` is for: naming the files
    `<query>_<scale>_step<N>_input.txt` lets run_tests.py's existing
    `parse_obliviator_output_packed` find the final output unchanged.
    """
    results: List[Dict] = []
    counter = [0]

    def go(n: PlanNode) -> Node:
        if n.is_leaf:
            return load_table(data_dir, tables[n.table_idx].name)
        l, r = go(n.left), go(n.right)
        counter[0] += 1
        node, res = run_join(l, r, conds, session, f"step{counter[0]}", prefix)
        results.append(res)
        return node

    return go(plan), results


def plans_for(query_sql: str, mode: str, seed: int = 0, query: Path = None,
              data_dir: Path = None):
    """(tables, conds, names, plans, chosen) for a query under `mode`."""
    tables, conds = SQLParser(query_sql).parse()
    names = [t.name for t in tables]
    idx = {n: i for i, n in enumerate(names)}
    edges = [(idx[c.left_table], idx[c.right_table]) for c in conds
             if c.left_table in idx and c.right_table in idx]
    plans = enumerate_plans(len(names), edges)

    if mode == "leftdeep":
        chosen = [leftdeep_plan(len(names), edges, edges)]
    elif mode == "optimal":
        card = cardinality_oracle(query, data_dir, names, conds)
        chosen = choose_plan(plans, "optimal", card=card)
    else:
        chosen = choose_plan(plans, mode, seed=seed)
    return tables, conds, names, plans, chosen


def guard_payloads(chosen, names, data_dir: Path):
    """(ok, error_dict).  Run this OUTSIDE any timed region.

    parallel_enc.c strncpy's into elem_t.data with no length check, so an
    over-wide payload is silently truncated and the join returns wrong rows.
    Width is plan-dependent, so a build sized from left-deep runs corrupts
    right-deep plans -- refuse rather than report garbage.  It scans every input
    CSV, and it is *our* plan-safety check rather than work the engine does, so
    charging it to the baseline's runtime would overstate the baseline's cost.
    """
    dl = read_data_length(WORK_DIR)
    widths = table_widths(data_dir, names)
    ok, _ = check_payloads(chosen, widths, names, dl)
    if ok:
        return True, None
    need = max(plan_max_payload(p, widths, names) for p in chosen) + 1
    return False, {"success": False,
                   "error": f"payload up to {need - 1} chars exceeds compiled "
                            f"DATA_LENGTH={dl}; rebuild with DATA_LENGTH >= {need}"}


def run_one_plan(plan, plans, tables, conds, names, data_dir: Path, prefix: str,
                 verify: bool = True, sql: str = None) -> Dict:
    """Execute one plan in its **own enclave** and verify it against SQLite.

    A fresh ObliviatorSession per plan is what makes init/total/exec meaningful
    per plan; a shared session would amortise one init across every plan and
    leave only Core attributable.
    """
    t0 = time.time()
    with ObliviatorSession() as session:
        init_s = session.init_seconds
        node, results = execute_plan(plan, tables, conds, data_dir, session,
                                     prefix)
    total_s = time.time() - t0

    out = {
        "plan_id": plans.index(plan),
        "plan": plan.label(names),
        "rows": len(node.rows),
        "steps": len(results),
        "core_runtime": sum(r.get("core_runtime") or 0 for r in results),
        "actual_runtime": sum(r.get("actual_runtime") or 0 for r in results),
        "init_s": init_s,
        "total_s": total_s,
        "exec_s": total_s - init_s if init_s is not None else None,
        "success": all(r.get("success", False) for r in results),
        "_results": results,
    }

    # Verification is deliberately after total_s is taken -- it is our check,
    # not the engine's work.
    if verify and sql:
        out.update(pv.verify_rows(node.rows, node.schema, sql, data_dir, names))
    # Digest for cross-plan agreement, canonicalised so column order (which is
    # plan-dependent) cannot masquerade as a data difference.
    truth_cols = pv.canonical_columns(data_dir, names)
    out["_digest"] = pv.rows_digest(node.rows,
                                    pv.perm_for(node.schema, truth_cols))
    return out


def execute_query_plan_mode(query_name: str, sql: str, data_dir: Path,
                            mode: str = "leftdeep", seed: int = 0,
                            scale_tag: str = "", verify: bool = True,
                            cooldown: int = 0) -> Tuple[Dict, None]:
    """Drop-in alternative to execute_query_pipeline_packed that runs the query
    under a chosen join-order model.

    Returns the same (result_dict, tracker) contract -- tracker is None because
    run_tests.py discards it.  **Every** executed plan is verified against
    SQLite, so `average` yields 14 verdicts rather than one; the top-level
    correctness is PASS only if all of them pass.
    """
    query = Path(query_name)
    tables, conds, names, plans, chosen = plans_for(
        sql, mode, seed=seed, query=query, data_dir=data_dir)

    ok, err = guard_payloads(chosen, names, data_dir)
    if not ok:
        err["plan_mode"] = mode
        return err, None

    if verify:                      # build ground truth before any timing
        pv.sqlite_digest(sql, data_dir, names)

    prefix = f"{query.stem}_{scale_tag}" if scale_tag else query.stem
    per_plan, all_results = [], []
    for k, plan in enumerate(chosen):
        if k and cooldown:
            time.sleep(cooldown)
        p = run_one_plan(plan, plans, tables, conds, names, data_dir, prefix,
                         verify=verify, sql=sql)
        all_results.extend(p.pop("_results"))
        per_plan.append(p)

    failed = [p["plan_id"] for p in per_plan if p.get("correctness") == "FAIL"]
    core = [p["core_runtime"] for p in per_plan]
    result = {
        "success": all(p["success"] for p in per_plan),
        "actual_runtime": sum(p["actual_runtime"] for p in per_plan) / len(per_plan),
        "core_runtime": sum(core) / len(core),
        "core_stats": summarise(core),
        "total_stats": summarise([p["total_s"] for p in per_plan]),
        "init_stats": summarise([p["init_s"] for p in per_plan
                                 if p["init_s"] is not None]),
        "exec_stats": summarise([p["exec_s"] for p in per_plan
                                 if p["exec_s"] is not None]),
        "steps": per_plan[0]["steps"],
        "plan_mode": mode,
        "plan_seed": seed,
        "plan_count": len(plans),
        "plans_run": len(chosen),
        "plan_id": per_plan[0]["plan_id"] if len(chosen) == 1 else None,
        "plan_label": per_plan[0]["plan"] if len(chosen) == 1 else None,
        "per_plan": [{k: v for k, v in p.items() if not k.startswith("_")}
                     for p in per_plan],
        "step_results": all_results,
    }

    if verify:
        result["plans_failed"] = failed
        result["correctness"] = "PASS" if not failed else "FAIL"
        result["correctness_details"] = {
            "plans_run": len(chosen),
            "plans_failed": len(failed),
            "failed_plan_ids": failed,
            "per_plan": {p["plan_id"]: p.get("correctness")
                         for p in per_plan},
        }
        # Cross-plan agreement: independent of ground truth, so it still says
        # something when SQLite verification is unavailable.
        groups = {}
        for p in per_plan:
            groups.setdefault(p["_digest"], []).append(p["plan_id"])
        result["digest_groups"] = [sorted(v) for v in groups.values()]

    return result, None


# =============================================================================
# Driver
# =============================================================================

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("query")
    ap.add_argument("data_dir")
    ap.add_argument("--mode", default="leftdeep",
                    choices=["leftdeep", "optimal", "random", "average"])
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--cooldown", type=int, default=30,
                    help="settle gap between plan runs (EPC state)")
    ap.add_argument("--no-verify", dest="verify", action="store_false",
                    help="skip the per-plan tuple-exact SQLite check "
                         "(on by default -- it is the point of this tool)")
    args = ap.parse_args()

    sql = Path(args.query).read_text()
    data_dir = Path(args.data_dir)

    print(f"query   : {Path(args.query).name}")
    print(f"data    : {data_dir}")

    tables, conds, names, plans, chosen = plans_for(
        sql, args.mode, seed=args.seed, query=Path(args.query),
        data_dir=data_dir)

    print(f"tables  : {names}")
    print(f"plans   : {len(plans)} connected binary plans")

    if args.mode == "optimal":
        card = cardinality_oracle(Path(args.query), data_dir, names, conds)
        costs = sorted(plan_cost(p, card) for p in plans)
        print(f"  ORACLE: Σ-intermediate best={costs[0]:,} "
              f"mean={sum(costs) // len(costs):,} worst={costs[-1]:,} "
              f"-- optimal is PRIVACY-VIOLATING, reference only")

    dl = read_data_length(WORK_DIR)
    widths = table_widths(data_dir, names)
    need = max(plan_max_payload(p, widths, names) for p in plans) + 1
    print(f"DATA_LENGTH compiled = {dl} ({dl - 1} usable); "
          f"widest plan needs {need}")
    ok, err = guard_payloads(chosen, names, data_dir)
    if not ok:
        print(f"\nABORT: {err['error']}")
        print("Rebuild (elem_t = align4(DATA_LENGTH+2)+16) and re-run.")
        return 2

    print(f"mode    : {args.mode}  ({len(chosen)} plan(s) to run)")
    print(f"verify  : {'tuple-exact vs SQLite, every plan' if args.verify else 'OFF'}")
    print(f"enclave : fresh per plan (init/total/exec are per-plan)\n")

    result, _ = execute_query_plan_mode(
        args.query, sql, data_dir, mode=args.mode, seed=args.seed,
        verify=args.verify, cooldown=args.cooldown)

    if not result.get("per_plan"):
        print(f"ABORT: {result.get('error')}")
        return 2

    for k, p in enumerate(result["per_plan"], 1):
        corr = p.get("correctness", "--")
        print(f"  [{k}/{len(result['per_plan'])}] rows={p['rows']:>9,d} "
              f"core={p['core_runtime']:8.3f}s init={p['init_s']:7.2f}s "
              f"exec={p['exec_s']:7.2f}s total={p['total_s']:7.2f}s "
              f"{corr:<5} {p['plan']}")
        if corr == "FAIL":
            d = p.get("correctness_details", {})
            print(f"        only_in_obliviator={d.get('only_in_obliviator')} "
                  f"only_in_sqlite={d.get('only_in_sqlite')}")

    groups = result.get("digest_groups")
    if groups:
        agree = len(groups) == 1
        print(f"\ncheck   : {len(result['per_plan'])} plans -> {len(groups)} "
              f"distinct output multiset(s)  "
              f"{'(all agree)' if agree else '*** PLANS DISAGREE ***'}")
        if not agree:
            for g in sorted(groups, key=len, reverse=True):
                print(f"           group of {len(g):2d}: plan_ids {g}")

    if args.verify:
        failed = result.get("plans_failed") or []
        print(f"verify  : {len(result['per_plan']) - len(failed)}/"
              f"{len(result['per_plan'])} plans tuple-exact vs SQLite"
              + (f"  ***  FAILED: {failed}" if failed else ""))

    print(f"\n{'metric':8s} {'mean':>10s} {'stddev':>9s} {'min':>10s} {'max':>10s}")
    for key, stats in (("core", result["core_stats"]),
                       ("init", result["init_stats"]),
                       ("exec", result["exec_stats"]),
                       ("total", result["total_stats"])):
        if stats:
            print(f"{key:8s} {stats['mean']:10.3f} {stats['stddev']:9.3f} "
                  f"{stats['min']:10.3f} {stats['max']:10.3f}")

    return 1 if (args.verify and result.get("plans_failed")) else 0


if __name__ == "__main__":
    sys.exit(main())
