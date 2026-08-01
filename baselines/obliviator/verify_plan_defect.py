#!/usr/bin/env python3
"""
Minimal reproduction of OBLIVIATOR's plan-dependent wrong answers.

The 14-plan sweep of tm3 @ 0.01 (run_plan_modes.py --mode average --check)
returns 236,250 rows for every plan but four distinct output multisets.  Since
all 14 compute the same relational expression, at least three of those groups
are wrong.  This script shrinks that to the smallest case that still shows it:
three tables, two plan shapes, identical predicates.

    B  (nation JOIN supplier) JOIN customer
    A  nation JOIN (supplier JOIN customer)

Both are compared tuple-exactly against SQLite over the same CSVs, and the
symmetric difference is printed so the failing pair is visible rather than just
a row count.

Usage:
    python3 verify_plan_defect.py [data_dir]
"""

from __future__ import annotations

import sys
from pathlib import Path

import plan_verify as pv
from join_planner import PlanNode
from run_plan_modes import Node, execute_plan, load_table
from run_obliviator_queries import ObliviatorSession, SQLParser

BASE = Path(__file__).resolve().parent

QUERY = """SELECT *
FROM nation, supplier, customer
WHERE nation.N_NATIONKEY = supplier.S_NATIONKEY
AND supplier.S_NATIONKEY = customer.C_NATIONKEY;
"""

# The two parenthesisations of a 3-chain.  (Catalan(2) = 2, so this is the
# entire plan space for this query -- there is nothing else to try.)
def leaf(i):
    return PlanNode(tables=frozenset({i}), table_idx=i)


def join(l, r):
    return PlanNode(tables=l.tables | r.tables, left=l, right=r)


SHAPES = {
    "B  (nation⋈supplier)⋈customer": join(join(leaf(0), leaf(1)), leaf(2)),
    "A  nation⋈(supplier⋈customer)": join(leaf(0), join(leaf(1), leaf(2))),
}


KEY_COLS = ("N_NATIONKEY", "S_NATIONKEY", "C_NATIONKEY", "S_SUPPKEY", "C_CUSTKEY")


def show(label: str, row, canonical) -> str:
    """Print only the key columns -- the payload columns are identical between
    the two sides and just obscure which tuple got mispaired."""
    return f"   {label}:  " + "  ".join(
        f"{c}={v}" for (_, c), v in zip(canonical, row) if c in KEY_COLS)


def main() -> int:
    data_dir = Path(sys.argv[1] if len(sys.argv) > 1 else BASE / "test_data/data_0_01")
    tables, conds = SQLParser(QUERY).parse()
    names = [t.name for t in tables]

    truth, canonical = pv.sqlite_digest(QUERY, data_dir, names)
    print(f"data    : {data_dir}")
    print(f"tables  : {names}")
    print(f"sqlite  : {truth[0]:,} rows\n")

    with ObliviatorSession() as session:
        for label, plan in SHAPES.items():
            node, _ = execute_plan(plan, tables, conds, data_dir, session)
            v = pv.verify_rows(node.rows, node.schema, QUERY, data_dir, names)
            d = v["correctness_details"]
            status = "exact" if v["correctness"] == "PASS" else "*** MISMATCH ***"
            print(f"{label}\n   rows={d['obliviator_count']:,}  "
                  f"only_obl={d.get('only_in_obliviator', 0)}  "
                  f"only_sql={d.get('only_in_sqlite', 0)}   {status}")
            for tag, key in (("ONLY IN OBLIVIATOR", "example_obliviator"),
                             ("ONLY IN SQLITE    ", "example_sqlite")):
                for row in d.get(key, [])[:3]:
                    print(show(tag, row, canonical))
            print()

    # Context for the signature: the defect pairs the LAST element in sorted
    # key order with its predecessor, so report where the offending keys sit in
    # the domain.
    import csv
    with (data_dir / "supplier.csv").open() as f:
        r = csv.reader(f)
        hdr = next(r)
        ki = hdr.index("S_NATIONKEY")
        keys = sorted({int(row[ki]) for row in r})
    print(f"S_NATIONKEY domain: {len(keys)} distinct, max={keys[-1]}, "
          f"top-3={keys[-3:]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
