#!/usr/bin/env python3
"""
Unified Obliviator test runner with batch execution and correctness verification.

Usage:
    python run_tests.py <config_file> [--no-resume] [--clean] [--no-verify]

Examples:
    python run_tests.py test_config_small.txt --clean        # Quick smoke test
    python run_tests.py test_config.txt                      # Standard (0.001 + 0.01)
    python run_tests.py test_config_full.txt --no-verify     # All scales, skip verification
"""

import sys
import os
import json
import time
import re
import csv
import sqlite3
import subprocess
import argparse
from pathlib import Path
from datetime import datetime
from typing import Set, Tuple, List, Dict, Any
from collections import Counter

# Paths.  All of these are env-overridable so this harness can live inside the
# artifact while the OBLIVIATOR tree it drives is fetched to external/ and the
# dataset is the repo's single copy -- there is no second copy of the CSVs.
#
#   OBLIVIATOR_SRC  unpacked Parallel-join-ae            (default: ../../external/...)
#   EJ_DATA_DIR     plaintext CSV root                   (default: ../../input/plaintext)
#   EJ_QUERY_DIR    .sql files                           (default: ../../input/queries)
#   OE_PREFIX       OpenEnclave install (for oesign)     (default: ~/openenclave-install)
BASE_DIR = Path(__file__).resolve().parent          # baselines/obliviator/
REPO_DIR = BASE_DIR.parent.parent                   # artifact root

OBLIVIATOR_SRC = Path(os.environ.get(
    "OBLIVIATOR_SRC", REPO_DIR / "external" / "Parallel-join-ae"))
WORK_DIR = OBLIVIATOR_SRC / "join"
ENCLAVE_DIR = WORK_DIR / "enclave"
ENCLAVE_CONF = ENCLAVE_DIR / "parallel.conf"
ENCLAVE_KEY = ENCLAVE_DIR / "parallel.pem"
ENCLAVE_BIN = ENCLAVE_DIR / "parallel_enc"
OESIGN = Path(os.environ.get(
    "OE_PREFIX", Path.home() / "openenclave-install")) / "bin" / "oesign"
DATA_DIR = Path(os.environ.get("EJ_DATA_DIR", REPO_DIR / "input" / "plaintext"))
QUERY_DIR = Path(os.environ.get("EJ_QUERY_DIR", REPO_DIR / "input" / "queries"))
OUTPUT_DIR = Path(os.environ.get("EJ_RESULT_DIR", BASE_DIR / "query_results"))

# Import core pipeline execution
sys.path.insert(0, str(BASE_DIR))
from run_obliviator_queries import (
    execute_query_pipeline_packed,
    COLUMN_DELIM, TABLE_DELIM,
    SQLParser,
    unpack_tables, unpack_columns,
)

# NumHeapPages * 4KB = heap size
ENCLAVE_SIZES = {
    "64MB":  16384,
    "128MB": 32768,
    "256MB": 65536,
    "512MB": 131072,
    "640MB": 163840,
    "768MB": 196608,
    "896MB": 229376,
    "1GB":   262144,
    "2GB":   524288,
    "4GB":   1048576,
    "8GB":   2097152,
    "16GB":  4194304,
}

# Queries with SQLite-verifiable ground truth (equi-joins only)
VERIFIABLE_QUERIES = {
    "tm1": {
        "sql": """
            SELECT * FROM customer, orders, lineitem
            WHERE customer.C_CUSTKEY = orders.O_CUSTKEY
            AND orders.O_ORDERKEY = lineitem.L_ORDERKEY
        """,
        "tables": ["customer", "orders", "lineitem"],
    },
    "tm2": {
        "sql": """
            SELECT * FROM supplier, customer, nation1, nation2
            WHERE supplier.S_NATIONKEY = nation1.N1_N_NATIONKEY
            AND customer.C_NATIONKEY = nation2.N2_N_NATIONKEY
            AND nation1.N1_N_REGIONKEY = nation2.N2_N_REGIONKEY
        """,
        "tables": ["supplier", "customer", "nation1", "nation2"],
    },
    "tm3": {
        "sql": """
            SELECT * FROM nation, supplier, customer, orders, lineitem
            WHERE nation.N_NATIONKEY = supplier.S_NATIONKEY
            AND supplier.S_NATIONKEY = customer.C_NATIONKEY
            AND customer.C_CUSTKEY = orders.O_CUSTKEY
            AND orders.O_ORDERKEY = lineitem.L_ORDERKEY
        """,
        "tables": ["nation", "supplier", "customer", "orders", "lineitem"],
    },
}

# Track current enclave size to avoid unnecessary re-signing
current_enclave_size = None


# =============================================================================
# Enclave management
# =============================================================================

def get_current_enclave_size() -> str:
    """Read current enclave size from config file."""
    if not ENCLAVE_CONF.exists():
        return None
    with open(ENCLAVE_CONF, 'r') as f:
        content = f.read()
    match = re.search(r'NumHeapPages=(\d+)', content)
    if match:
        pages = int(match.group(1))
        for name, p in ENCLAVE_SIZES.items():
            if p == pages:
                return name
    return None


def update_enclave_config(size_str: str) -> bool:
    """Update enclave config with new heap size."""
    size_str = size_str.strip().upper()
    if size_str not in ENCLAVE_SIZES:
        return False
    num_pages = ENCLAVE_SIZES[size_str]
    with open(ENCLAVE_CONF, 'r') as f:
        content = f.read()
    new_content = re.sub(r'NumHeapPages=\d+', f'NumHeapPages={num_pages}', content)
    with open(ENCLAVE_CONF, 'w') as f:
        f.write(new_content)
    return True


def sign_enclave() -> bool:
    """Re-sign the enclave after config change."""
    try:
        result = subprocess.run(
            [str(OESIGN), "sign", "-e", str(ENCLAVE_BIN),
             "-k", str(ENCLAVE_KEY), "-c", str(ENCLAVE_CONF)],
            cwd=str(ENCLAVE_DIR),
            capture_output=True, text=True, timeout=60,
        )
        if result.returncode != 0:
            print(f"    Warning: oesign failed: {result.stderr}")
            return False
        return True
    except Exception as e:
        print(f"    Warning: Failed to sign enclave: {e}")
        return False


def set_enclave_size(size_str: str) -> bool:
    """Set enclave size and re-sign if needed."""
    global current_enclave_size
    if current_enclave_size == size_str:
        return True
    actual = get_current_enclave_size()
    if actual == size_str:
        current_enclave_size = size_str
        return True
    print(f"    Setting enclave size: {size_str}...", end="", flush=True)
    if not update_enclave_config(size_str):
        print(" failed (config)")
        return False
    if not sign_enclave():
        print(" failed (sign)")
        return False
    current_enclave_size = size_str
    print(" done")
    return True


# =============================================================================
# Config parsing
# =============================================================================

def parse_config_file(config_path: Path) -> List[Tuple[str, str, str, str]]:
    """Parse config file. Returns list of (query_name, query_sql, data_size, enclave_size)."""
    tests = []
    with open(config_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split(',')
            if len(parts) < 2:
                continue
            query_file = parts[0].strip()
            data_dir = parts[1].strip()
            enclave_size = parts[2].strip() if len(parts) >= 3 else "8GB"

            query_name = Path(query_file).stem.replace("tpch_", "")
            data_size = Path(data_dir).name.replace("data_", "")

            # Configs name queries by path for readability, but the file is
            # resolved against QUERY_DIR so one config works regardless of where
            # the dataset lives.  A bare name works too.
            query_path = QUERY_DIR / Path(query_file).name
            if not query_path.exists():
                print(f"Warning: Query file not found: {query_path}")
                continue
            with open(query_path, 'r') as qf:
                query_sql = qf.read()

            tests.append((query_name, query_sql, data_size, enclave_size))
    return tests


# =============================================================================
# Correctness verification
# =============================================================================

def get_sqlite_groundtruth(query_name: str, scale: str) -> Set[Tuple]:
    """Execute query in SQLite and return result as set of tuples."""
    query_info = VERIFIABLE_QUERIES[query_name]
    scale_underscore = scale.replace(".", "_")
    scale_dir = DATA_DIR / f"data_{scale_underscore}"

    conn = sqlite3.connect(":memory:")
    cursor = conn.cursor()

    for table_name in query_info["tables"]:
        csv_file = scale_dir / f"{table_name}.csv"
        if not csv_file.exists():
            raise FileNotFoundError(f"Table not found: {csv_file}")
        with open(csv_file, 'r') as f:
            reader = csv.reader(f)
            headers = next(reader)
            col_defs = ", ".join([f'"{h}" TEXT' for h in headers])
            cursor.execute(f'CREATE TABLE {table_name} ({col_defs})')
            placeholders = ", ".join(["?" for _ in headers])
            for row in reader:
                cursor.execute(f'INSERT INTO {table_name} VALUES ({placeholders})', row)

    conn.commit()
    cursor.execute(query_info["sql"])
    results = cursor.fetchall()
    conn.close()
    return set(results)


def extract_clean_columns_packed(line: str) -> Tuple:
    """Extract clean columns from packed Obliviator output.

    Packed format: key1 left_packed key2 right_packed
    Where packed uses "|" for columns and "||" for tables.
    """
    parts = line.strip().split(' ', 3)
    if len(parts) < 4:
        return tuple(line.strip().split())

    key1, left_packed, key2, right_packed = parts
    combined = left_packed + TABLE_DELIM + right_packed
    all_tables = unpack_tables(combined)

    all_cols = []
    for t in all_tables:
        all_cols.extend(unpack_columns(t))
    return tuple(all_cols)


def parse_obliviator_output_packed(query_name: str, data_size: str) -> Set[Tuple]:
    """Parse final Obliviator output file with packed format."""
    query_info = VERIFIABLE_QUERIES[query_name]
    num_steps = len(query_info["tables"]) - 1

    if num_steps == 1:
        output_file = WORK_DIR / f"{query_name}_{data_size}_input_output.txt"
    else:
        output_file = WORK_DIR / f"{query_name}_{data_size}_step{num_steps}_input_output.txt"

    if not output_file.exists():
        return set()

    rows = set()
    with open(output_file, 'r') as f:
        for line in f:
            line = line.strip()
            if line:
                rows.add(extract_clean_columns_packed(line))
    return rows


def compare_results(obliviator_rows, sqlite_rows) -> Dict[str, Any]:
    """LEGACY comparison, kept only as a diagnostic baseline.

    Two weaknesses, both deliberate to preserve: it is fed `set`s (so duplicate
    rows collapse) and it normalises each row with `tuple(sorted(values))` (so
    column identity is discarded -- right values in wrong columns pass).  The
    authoritative check is now the strict one in plan_verify.py; this verdict is
    recorded alongside it as `correctness_legacy` so we can see exactly where
    the old check was too lenient.
    """
    def normalize_row(row):
        return tuple(sorted(str(v) for v in row))

    ob_normalized = Counter(normalize_row(r) for r in obliviator_rows)
    sq_normalized = Counter(normalize_row(r) for r in sqlite_rows)

    only_in_ob = ob_normalized - sq_normalized
    only_in_sq = sq_normalized - ob_normalized

    return {
        "exact_match": len(only_in_ob) == 0 and len(only_in_sq) == 0,
        "obliviator_count": len(obliviator_rows),
        "sqlite_count": len(sqlite_rows),
        "only_in_obliviator": sum(only_in_ob.values()),
        "only_in_sqlite": sum(only_in_sq.values()),
    }


def parse_obliviator_rows(query_name: str, data_size: str,
                          n_tables: int) -> List[Tuple]:
    """Final output as an ordered LIST -- duplicates preserved.

    `parse_obliviator_output_packed` returns a set, which silently makes a
    multiset comparison impossible.  Column order is whatever the pipeline's
    flattening produced; `pipeline_table_order()` reconstructs it.
    """
    steps = n_tables - 1
    name = (f"{query_name}_{data_size}_input_output.txt" if steps == 1
            else f"{query_name}_{data_size}_step{steps}_input_output.txt")
    path = WORK_DIR / name
    if not path.exists():
        return []
    with open(path) as f:
        return [extract_clean_columns_packed(l) for l in f if l.strip()]


def pipeline_table_order(sql: str) -> List[str]:
    """Left-to-right table sequence the linear pipeline actually emits.

    Each step writes `left_packed || right_packed`, so the output order is the
    left source's order followed by the right source's, with `result_N`
    expanding to step N's order.

    NOTE: this deliberately does *not* use `ColumnTracker.accumulated_left`,
    which is wrong whenever the intermediate lands on the right-hand side --
    `add_left_table` appends the fresh table to the end regardless.  TM2's third
    step is `customer JOIN result_2`, so the tracker reports
    supplier,nation1,nation2,customer while the file really holds
    customer,supplier,nation1,nation2.
    """
    from run_obliviator_queries import JoinPipelineGenerator

    tables, conds = SQLParser(sql).parse()
    pipeline = JoinPipelineGenerator(tables, conds).generate_pipeline()
    if not pipeline:
        return [t.name for t in tables]

    order, cur = {}, None
    for i, step in enumerate(pipeline, 1):
        left = (order[step.left_source] if step.left_source.startswith("result_")
                else [step.left_source])
        right = (order[step.right_source] if step.right_source.startswith("result_")
                 else [step.right_source])
        cur = left + right
        order[f"result_{i}"] = cur
    return cur


def verify_correctness(query_name: str, data_size: str,
                       query_sql: str = None,
                       table_order: List[str] = None) -> Dict[str, Any]:
    """Strict, column-aware, multiset correctness verification.

    Ground truth comes from the query's own SQL rather than the hardcoded
    `VERIFIABLE_QUERIES` dict, which is what lets non-TPC-H queries be verified
    instead of marked SKIP.

    Verdicts:
      PASS        tuple-exact against SQLite
      FAIL        the engine returned wrong rows
      FAIL-ORDER  every value is right but the columns are assigned differently.
                  That is a flattening bug in *our* pipeline, not a defect in
                  the engine, so it must not be reported as FAIL.
    """
    import plan_verify as pv

    data_dir = DATA_DIR / f"data_{data_size}"
    sql = query_sql or (VERIFIABLE_QUERIES[query_name]["sql"]
                        if query_name in VERIFIABLE_QUERIES else None)
    if sql is None:
        return {"correctness": "SKIP"}

    tables, _ = SQLParser(sql).parse()
    names = [t.name for t in tables]

    rows = parse_obliviator_rows(query_name, data_size, len(names))
    if not rows:
        return {"correctness": "FAIL",
                "correctness_details": {"error": "no output rows found"}}

    truth, canonical = pv.sqlite_digest(sql, data_dir, names)

    if len(rows[0]) != len(canonical):
        return {"correctness": "FAIL",
                "correctness_details": {
                    "error": f"width mismatch: got {len(rows[0])} columns, "
                             f"expected {len(canonical)}"}}

    # The pipeline's output column order is not the FROM-clause order: TM2 emits
    # customer,supplier,nation1,nation2 against a FROM clause of
    # supplier,customer,nation1,nation2, because its last step puts the
    # intermediate on the right.  Reconstruct it from the pipeline so this check
    # stays exact instead of degrading to an order-insensitive one.
    schema = pv.schema_for_table_order(
        data_dir, table_order or pipeline_table_order(sql))
    ident = pv.perm_for(schema, canonical)

    out = {}
    if pv.rows_digest(rows, ident) == truth:
        out["correctness"] = "PASS"
        out["correctness_details"] = {"sqlite_count": truth[0],
                                      "obliviator_count": len(rows),
                                      "exact_match": True}
    elif pv.classify_unordered(rows, sql, data_dir, names):
        out["correctness"] = "FAIL-ORDER"
        out["correctness_details"] = {
            "sqlite_count": truth[0], "obliviator_count": len(rows),
            "exact_match": False,
            "note": "values all correct; column assignment differs -- "
                    "harness flattening, not an engine defect",
        }
    else:
        out["correctness"] = "FAIL"
        out["correctness_details"] = pv.diff_detail(rows, ident, sql,
                                                    data_dir, names)

    # Legacy verdict recorded alongside, to show where the old check was lenient.
    if query_name in VERIFIABLE_QUERIES:
        legacy = compare_results(set(rows),
                                 get_sqlite_groundtruth(query_name,
                                                        data_size.replace("_", ".")))
        out["correctness_legacy"] = "PASS" if legacy["exact_match"] else "FAIL"

    return out


# =============================================================================
# Test execution
# =============================================================================

# Host I/O buffer per data scale (bytes), sized to the largest input/output
# file observed at that scale plus headroom. The EDL marshals the whole
# buffer into the enclave per ecall, so smaller buffer = lower enclave floor.
#
# WARNING: this must hold the OUTPUT as well as the input, and the enclave does
# NOT check it -- ecall_scalable_oblivious_join() opens with `(void)len;` and
# writes the result into the same buffer regardless of size.  Overflowing it
# faults inside the enclave; the host then leaves a silent 0-byte output file
# (it opens the file "w" before the ecall and skips the write on failure).  The
# defaults below are too small for outputs above ~256 MB, which is why TM2/TM3
# @ 0.1 fail here -- set OBLIVIATOR_MAX_BUF in the environment to override.
MAX_BUF_BY_SCALE = {
    "0_001": 16 * 1024 * 1024,
    "0_01":  128 * 1024 * 1024,
    "0_1":   256 * 1024 * 1024,
}


def run_single_test(query_name: str, query_sql: str, data_size: str,
                    enclave_size: str, verify: bool = True,
                    plan_mode: str = None,
                    plan_seed: int = 0,
                    plan_cooldown: int = 0) -> Dict[str, Any]:
    """Run a single test with optional correctness verification.

    `plan_mode` selects the join-order model (see join_planner.py).  `None` --
    the default -- routes through the original linear pipeline untouched, so
    every existing config and every published number reproduces.  Any explicit
    mode, *including* `leftdeep`, goes through the plan-tree executor; passing
    `leftdeep` therefore gives a like-for-like executor baseline to compare the
    other modes against, rather than conflating plan shape with executor.
    """
    # A caller-supplied OBLIVIATOR_MAX_BUF wins over the per-scale default, so a
    # cell whose output exceeds the default can be retested without editing the
    # table.  Recorded in the result so a number can never be read without the
    # buffer size that produced it.
    buf_override = os.environ.get("OBLIVIATOR_MAX_BUF_OVERRIDE")
    buf_bytes = buf_override or MAX_BUF_BY_SCALE.get(data_size)
    if buf_bytes:
        os.environ["OBLIVIATOR_MAX_BUF"] = str(buf_bytes)
    else:
        os.environ.pop("OBLIVIATOR_MAX_BUF", None)

    if not set_enclave_size(enclave_size):
        return {
            "success": False,
            "error": "Failed to set enclave size",
            "enclave_size": enclave_size,
            "correctness": "N/A",
        }

    try:
        # True end-to-end wall time: fresh enclave (daemon) launch + init +
        # all pipeline steps + host processing, measured the same black-box
        # way we time our own program (start of run to final result).
        if plan_mode is not None:
            # Resolve the plan (and, for `optimal`, build the SQLite cardinality
            # oracle) BEFORE the timer starts.  Planning is our offline work,
            # not the engine's -- and the oracle in particular is the
            # privacy-violating step the baseline could never perform, so
            # charging its cost to the baseline's runtime would be wrong in
            # both directions.  The result is cached, so the timed call re-uses
            # it for free.
            from run_plan_modes import plans_for
            plans_for(query_sql, plan_mode, seed=plan_seed,
                      query=Path(query_name),
                      data_dir=DATA_DIR / f"data_{data_size}")

        _wall_t0 = time.time()
        tracker = None
        if plan_mode is None:
            result, tracker = execute_query_pipeline_packed(query_name,
                                                            query_sql, data_size)
        else:
            from run_plan_modes import execute_query_plan_mode
            result, _ = execute_query_plan_mode(
                query_name, query_sql, DATA_DIR / f"data_{data_size}",
                mode=plan_mode, seed=plan_seed, scale_tag=data_size,
                verify=verify, cooldown=plan_cooldown)
        result["wall_total"] = time.time() - _wall_t0
        result["enclave_size"] = enclave_size
        result["max_buf"] = int(buf_bytes) if buf_bytes else None
        result.setdefault("plan_mode", plan_mode)
    except Exception as e:
        return {
            "success": False,
            "error": str(e),
            "enclave_size": enclave_size,
            "correctness": "N/A",
        }

    # Correctness verification.  Plan-mode runs already verified EVERY plan
    # in-memory against SQLite, which is strictly more than the disk-based check
    # can do (the file only holds the last plan's output) -- so do not overwrite
    # that verdict here.
    if result.get("success") and verify and plan_mode is not None:
        pass
    elif result.get("success") and verify:
        try:
            result.update(verify_correctness(query_name, data_size, query_sql))
        except Exception as e:
            result["correctness"] = "ERROR"
            result["correctness_details"] = {"error": str(e)}
    elif not result.get("success"):
        result["correctness"] = "N/A"
    else:
        result.setdefault("correctness", "SKIP")

    return result


def is_test_done(result: Dict) -> bool:
    """Check if a cached test result counts as 'done' for resume purposes."""
    if not result.get("success"):
        return False
    corr = result.get("correctness", "")
    return corr in ("PASS", "SKIP")


# =============================================================================
# Batch execution
# =============================================================================

def run_all_tests(config_path: Path, resume: bool = True,
                  verify: bool = True, plan_mode: str = None,
                  plan_seed: int = 0, plan_cooldown: int = 0) -> Dict[str, Any]:
    """Run all tests from config file."""
    global current_enclave_size

    config_name = config_path.stem
    if plan_mode:
        # Keep plan-mode sweeps in their own results file so they can never be
        # confused with, or resumed against, the published left-deep numbers.
        config_name = f"{config_name}_plan_{plan_mode}"
        if plan_mode == "random":
            config_name += f"{plan_seed}"
    OUTPUT_DIR.mkdir(exist_ok=True)

    print("=" * 85)
    print(f"Obliviator Test Runner — {config_name}")
    print(f"Hardware mode, single-threaded")
    if plan_mode:
        note = " (PRIVACY-VIOLATING oracle, reference only)" if plan_mode == "optimal" else ""
        print(f"Join-order model: {plan_mode}"
              f"{f' seed={plan_seed}' if plan_mode == 'random' else ''}{note}")
    print("=" * 85)

    current_enclave_size = get_current_enclave_size()
    print(f"Current enclave size: {current_enclave_size or 'unknown'}")

    tests = parse_config_file(config_path)
    print(f"Loaded {len(tests)} test(s)")

    if not tests:
        print("No tests to run!")
        return {}

    # Show plan
    print(f"\n  {'#':<3} {'Query':<6} {'Scale':<6} {'Enclave':<8}")
    print("  " + "-" * 28)
    for i, (q, _, s, e) in enumerate(tests, 1):
        print(f"  {i:<3} {q:<6} {s.replace('_', '.'):<6} {e:<8}")

    # Load existing results
    all_results = {}
    results_file = OUTPUT_DIR / f"results_{config_name}.json"
    if resume and results_file.exists():
        with open(results_file, 'r') as f:
            all_results = json.load(f)
        print(f"\nLoaded {len(all_results)} cached result(s)")

    # Header
    print("\n" + "=" * 85)
    print(f"{'#':<4} {'Query':<8} {'Scale':<8} {'Enclave':<10} {'Status':<10} "
          f"{'Correct':<9} {'Steps':<6} {'Core(s)':<12} {'Total(s)':<10}")
    print("-" * 85)

    start_time = time.time()

    for i, (query_name, query_sql, data_size, enclave_size) in enumerate(tests, 1):
        key = f"{query_name}_{data_size}"

        # Skip if already done
        if key in all_results and is_test_done(all_results[key]):
            r = all_results[key]
            _print_row(i, query_name, data_size, r.get("enclave_size", enclave_size),
                       r, cached=True)
            continue

        # Run the test
        print(f"{i:<4} {query_name:<8} {data_size.replace('_', '.'):<8} "
              f"{enclave_size:<10} ", end="", flush=True)

        result = run_single_test(query_name, query_sql, data_size, enclave_size,
                                 verify, plan_mode=plan_mode, plan_seed=plan_seed,
                                 plan_cooldown=plan_cooldown)
        all_results[key] = result

        # Print result
        _print_row(i, query_name, data_size, enclave_size, result, overwrite=True)

        # Save incrementally
        with open(results_file, 'w') as f:
            json.dump(all_results, f, indent=2, default=str)

    # Summary
    elapsed = time.time() - start_time
    successful = sum(1 for r in all_results.values() if r.get("success"))
    verified = sum(1 for r in all_results.values() if r.get("correctness") == "PASS")
    verifiable = sum(1 for (q, _, _, _) in tests if q in VERIFIABLE_QUERIES)

    print("\n" + "=" * 85)
    print(f"Tests: {successful}/{len(tests)} passed, {verified}/{verifiable} verified correct")
    print(f"Total time: {elapsed:.1f}s")
    print(f"Results: {results_file}")

    save_summary_txt(all_results, config_name)
    return all_results


def _print_row(idx: int, query: str, data_size: str, enclave: str,
               result: Dict, cached: bool = False, overwrite: bool = False):
    """Print a single result row."""
    status = "Cached" if cached else ("Pass" if result.get("success") else "Fail")
    corr = result.get("correctness", "--")
    # For a multi-plan run, "FAIL" alone hides how many plans were wrong.
    # The column is 9 chars, so "FAIL 4/14" is the widest that fits.
    n_run = result.get("plans_run") or 0
    n_bad = len(result.get("plans_failed") or [])
    if n_run > 1 and corr in ("PASS", "FAIL"):
        corr = f"FAIL {n_bad}/{n_run}" if n_bad else f"PASS {n_run}/{n_run}"
    steps = result.get("steps", 1)
    core = result.get("core_runtime")
    total = result.get("actual_runtime")

    core_str = f"{core:.6f}" if core else "N/A"
    total_str = f"{total:.2f}" if total else "N/A"

    line = (f"{idx:<4} {query:<8} {data_size.replace('_', '.'):<8} {enclave:<10} "
            f"{status:<10} {corr:<9} {steps:<6} {core_str:<12} {total_str:<10}")

    if overwrite:
        print(f"\r{line}")
    else:
        print(line)


def save_summary_txt(results: Dict, config_name: str):
    """Save human-readable summary."""
    summary_file = OUTPUT_DIR / f"summary_{config_name}.txt"
    with open(summary_file, 'w') as f:
        f.write(f"Obliviator Test Results — {config_name}\n")
        f.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Hardware mode, single-threaded\n")
        f.write("=" * 85 + "\n\n")
        f.write(f"{'Query':<8} {'Scale':<8} {'Enclave':<10} {'Status':<10} "
                f"{'Correct':<9} {'Steps':<6} {'Core(s)':<12} {'Total(s)':<10} "
                f"{'Wall(s)':<10}\n")
        f.write("-" * 96 + "\n")

        for key in sorted(results.keys()):
            parts = key.split('_', 1)
            query_name = parts[0]
            data_size = parts[1] if len(parts) > 1 else "unknown"
            r = results[key]

            status = "Pass" if r.get("success") else "Fail"
            corr = r.get("correctness", "--")
            steps = r.get("steps", 1)
            core = r.get("core_runtime")
            total = r.get("actual_runtime")
            enclave = r.get("enclave_size", "N/A")

            wall = r.get("wall_total")
            core_str = f"{core:.6f}" if core else "N/A"
            total_str = f"{total:.2f}" if total else "N/A"
            wall_str = f"{wall:.2f}" if wall else "N/A"

            f.write(f"{query_name:<8} {data_size.replace('_', '.'):<8} {enclave:<10} "
                    f"{status:<10} {corr:<9} {steps:<6} {core_str:<12} {total_str:<10} "
                    f"{wall_str:<10}\n")

    print(f"Summary: {summary_file}")


# =============================================================================
# Artifact cleanup
# =============================================================================

def clean_artifacts():
    """Delete generated artifacts (input/output files and query results)."""
    count = 0
    for f in WORK_DIR.glob("*.txt"):
        f.unlink()
        count += 1
    for f in OUTPUT_DIR.glob("*"):
        f.unlink()
        count += 1
    print(f"Cleaned {count} artifact(s)")


# =============================================================================
# CLI
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Obliviator unified test runner with correctness verification")
    parser.add_argument("config_file", help="Config file (e.g. test_config.txt)")
    parser.add_argument("--no-resume", action="store_true",
                        help="Re-run all tests, ignoring cached results")
    parser.add_argument("--clean", action="store_true",
                        help="Delete generated artifacts before running")
    parser.add_argument("--no-verify", action="store_true",
                        help="Skip SQLite correctness verification")
    parser.add_argument("--plan-mode", default=None,
                        choices=["leftdeep", "optimal", "random", "average"],
                        help="join-order model; 'optimal' needs a cardinality "
                             "oracle and is PRIVACY-VIOLATING (see join_planner.py)")
    parser.add_argument("--plan-seed", type=int, default=0,
                        help="seed for --plan-mode random")
    parser.add_argument("--plan-cooldown", type=int, default=0,
                        help="settle gap in seconds between plans (EPC state); "
                             "each plan gets a fresh enclave")
    args = parser.parse_args()

    if args.clean:
        clean_artifacts()

    config_path = Path(args.config_file)
    if not config_path.is_absolute():
        config_path = BASE_DIR / args.config_file

    if not config_path.exists():
        print(f"Error: Config file not found: {config_path}")
        sys.exit(1)

    run_all_tests(config_path, resume=not args.no_resume,
                  verify=not args.no_verify, plan_mode=args.plan_mode,
                  plan_seed=args.plan_seed, plan_cooldown=args.plan_cooldown)


if __name__ == "__main__":
    main()
