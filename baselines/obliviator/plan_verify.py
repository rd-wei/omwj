#!/usr/bin/env python3
"""
Strict, column-aware, streaming correctness verification against SQLite.

WHY THIS EXISTS
---------------
Two independent weaknesses in the previous checks made "this plan is correct"
weaker than it sounded:

1. `run_tests.compare_results` compared **sets** (so duplicate rows collapsed)
   after normalising each row with `tuple(sorted(values))` (so column identity
   was discarded).  A result with the right values in the wrong columns passed.

2. `run_plan_modes --check` compared plans to each other by hashing the raw flat
   row.  But a plan node's schema is `left.schema + right.schema`, so **column
   order is plan-dependent**.  On a chain every plan happens to yield the same
   order, which is why it worked; on a 4-table star the 6 plans yield 6 distinct
   orders and every plan would be reported as disagreeing.  It also used Python's
   `hash()`, which is per-process randomised, so digests could never be compared
   across runs.

This module fixes both: rows are permuted into one canonical column order before
hashing, and the hash is a stable blake2b.

THE DIGEST
----------
`Digest = (count, sum mod 2**64, xor)` folded over per-row 64-bit hashes.  Sum
and xor are commutative, so the digest is:

  * **order-insensitive across rows** -- a true multiset comparison, and
    duplicates are counted rather than collapsed;
  * **order-sensitive within a row** -- the row hash is taken over the
    canonically-ordered values, so a column permutation changes it.

It is O(1) in memory, which is what makes 236k rows x 14 plans practical.  The
cost is that a mismatch tells you *that* two multisets differ, not *how* --
`diff_detail` recovers that, and is only worth running for a plan that already
failed.
"""

from __future__ import annotations

import csv
import hashlib
import sqlite3
from collections import Counter
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

# Chosen so a value can never be forged across a column boundary: no CSV field
# in these datasets contains it.
_FIELD_SEP = b"\x1f"

Digest = Tuple[int, int, int]          # (count, sum, xor)
_MASK = (1 << 64) - 1

EMPTY: Digest = (0, 0, 0)

# sqlite_digest is a full table load + join; memoise per (sql, data_dir).
_TRUTH_CACHE: Dict[Tuple[str, str], Tuple[Digest, List[Tuple[str, str]]]] = {}


# =============================================================================
# Canonical column order
# =============================================================================

def table_columns(data_dir: Path, name: str) -> List[str]:
    with (data_dir / f"{name}.csv").open() as f:
        return next(csv.reader(f))


def canonical_columns(data_dir: Path,
                      names: Sequence[str]) -> List[Tuple[str, str]]:
    """(table, column) pairs in FROM-clause order.

    This is exactly the order SQLite's `SELECT *` produces for the same FROM
    clause, so ground truth needs no permutation -- only the plans do.
    """
    return [(n, c) for n in names for c in table_columns(data_dir, n)]


def schema_for_table_order(data_dir: Path,
                           table_order: Sequence[str]) -> List[Tuple[str, str]]:
    """(table, column) pairs for a given left-to-right table sequence.

    A plan's output columns appear in the in-order traversal of its tree, which
    is generally *not* the FROM-clause order: TM2's FROM clause is
    supplier,customer,nation1,nation2 while its left-deep plan emits
    supplier,nation1,nation2,customer, because the join order follows the chain.
    Knowing the real order is what lets the legacy path be checked exactly
    rather than falling back to an order-insensitive comparison.
    """
    return [(t, c) for t in table_order for c in table_columns(data_dir, t)]


def perm_for(schema: Sequence[Tuple[str, str]],
             canonical: Sequence[Tuple[str, str]]) -> List[int]:
    """Indices that reorder a row in `schema` order into `canonical` order."""
    pos = {tc: i for i, tc in enumerate(schema)}
    missing = [tc for tc in canonical if tc not in pos]
    if missing:
        raise ValueError(f"schema is missing {missing[:3]} (have {list(pos)[:5]}...)")
    return [pos[tc] for tc in canonical]


# =============================================================================
# Digests
# =============================================================================

def row_hash(values: Iterable[str]) -> int:
    """Stable 64-bit hash of one row, sensitive to value order.

    blake2b rather than Python's hash(): the latter is salted per process, so
    digests from two runs -- or a cached digest -- could not be compared.
    """
    h = hashlib.blake2b(_FIELD_SEP.join(str(v).encode() for v in values),
                        digest_size=8)
    return int.from_bytes(h.digest(), "big")


def fold(hashes: Iterable[int]) -> Digest:
    count = total = xor = 0
    for h in hashes:
        count += 1
        total = (total + h) & _MASK
        xor ^= h
    return (count, total, xor)


def rows_digest(rows: Iterable[Sequence[str]], perm: Sequence[int]) -> Digest:
    """Digest of plan output, permuted into canonical column order."""
    return fold(row_hash([r[i] for i in perm]) for r in rows)


# =============================================================================
# Ground truth
# =============================================================================

def _load_sqlite(data_dir: Path, names: Sequence[str]) -> sqlite3.Connection:
    con = sqlite3.connect(":memory:")
    for n in names:
        with (data_dir / f"{n}.csv").open() as f:
            r = csv.reader(f)
            cols = next(r)
            con.execute(f'CREATE TABLE {n} ({",".join(chr(34) + c + chr(34) for c in cols)})')
            con.executemany(f'INSERT INTO {n} VALUES ({",".join("?" * len(cols))})', r)
    return con


def _sql_one_line(query_sql: str) -> str:
    return " ".join(query_sql.split()).rstrip(";")


def sqlite_digest(query_sql: str, data_dir: Path,
                  names: Sequence[str]) -> Tuple[Digest, List[Tuple[str, str]]]:
    """(digest, canonical_columns) of the query's true result.

    Streams `SELECT *` and folds row by row -- the result set is never
    materialised, so this stays flat in memory at 236k+ rows.
    """
    key = (_sql_one_line(query_sql), str(data_dir))
    if key in _TRUTH_CACHE:
        return _TRUTH_CACHE[key]

    canonical = canonical_columns(data_dir, names)
    con = _load_sqlite(data_dir, names)
    try:
        cur = con.execute(_sql_one_line(query_sql))
        digest = fold(row_hash(row) for row in cur)
    finally:
        con.close()

    _TRUTH_CACHE[key] = (digest, canonical)
    return digest, canonical


# =============================================================================
# Diagnosis (only worth running for something that already failed)
# =============================================================================

def diff_detail(rows: Sequence[Sequence[str]], perm: Sequence[int],
                query_sql: str, data_dir: Path, names: Sequence[str],
                limit: int = 5) -> Dict:
    """Counts and a few examples of the rows that differ.

    Counts hashes rather than tuples: a Counter over 236k x 44-string tuples is
    gigabytes, over 64-bit ints it is megabytes.  A second pass recovers example
    rows for the differing hashes.
    """
    con = _load_sqlite(data_dir, names)
    try:
        truth = Counter(row_hash(r) for r in con.execute(_sql_one_line(query_sql)))
        got = Counter(row_hash([r[i] for i in perm]) for r in rows)

        only_ob, only_sq = got - truth, truth - got
        detail = {
            "obliviator_count": sum(got.values()),
            "sqlite_count": sum(truth.values()),
            "only_in_obliviator": sum(only_ob.values()),
            "only_in_sqlite": sum(only_sq.values()),
            "exact_match": not only_ob and not only_sq,
        }

        if only_ob:
            want = set(only_ob)
            detail["example_obliviator"] = [
                [r[i] for i in perm] for r in rows
                if row_hash([r[i] for i in perm]) in want][:limit]
        if only_sq:
            want = set(only_sq)
            detail["example_sqlite"] = [
                list(r) for r in con.execute(_sql_one_line(query_sql))
                if row_hash(r) in want][:limit]
    finally:
        con.close()
    return detail


# =============================================================================
# Verdicts
# =============================================================================

def verify_rows(rows: Sequence[Sequence[str]],
                schema: Sequence[Tuple[str, str]],
                query_sql: str, data_dir: Path, names: Sequence[str],
                detail_on_fail: bool = True) -> Dict:
    """PASS/FAIL for one plan's in-memory output, tuple-exact against SQLite."""
    truth, canonical = sqlite_digest(query_sql, data_dir, names)
    perm = perm_for(schema, canonical)
    got = rows_digest(rows, perm)

    if got == truth:
        return {"correctness": "PASS",
                "correctness_details": {"obliviator_count": got[0],
                                        "sqlite_count": truth[0],
                                        "exact_match": True}}

    out = {"correctness": "FAIL"}
    if detail_on_fail:
        out["correctness_details"] = diff_detail(rows, perm, query_sql,
                                                 data_dir, names)
    else:
        out["correctness_details"] = {"obliviator_count": got[0],
                                      "sqlite_count": truth[0],
                                      "exact_match": False}
    return out


def classify_unordered(rows: Sequence[Sequence[str]], query_sql: str,
                       data_dir: Path, names: Sequence[str]) -> bool:
    """True if the results match as multisets of *value-sorted* rows.

    Used to separate a column-order problem from a data problem: if the strict
    check fails but this passes, the values are all correct and only their
    column assignment is wrong -- a flattening bug in the harness, not a defect
    in the engine.  Reported as FAIL-ORDER rather than FAIL.
    """
    con = _load_sqlite(data_dir, names)
    try:
        truth = Counter(row_hash(sorted(str(v) for v in r))
                        for r in con.execute(_sql_one_line(query_sql)))
    finally:
        con.close()
    got = Counter(row_hash(sorted(str(v) for v in r)) for r in rows)
    return got == truth
