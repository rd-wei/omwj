#!/usr/bin/env python3
"""
Script to run Obliviator queries using automated pipeline generation for multi-joins.
Automatically parses SQL queries and generates sequence of 2-table joins.
"""

import os
import sys
import csv
import subprocess
import time
import re
import json
from pathlib import Path
from dataclasses import dataclass
from typing import List, Dict, Tuple, Optional

# Configuration.  Env-overridable so the OBLIVIATOR tree can be fetched to
# external/ while the dataset stays the repo's single copy -- see run_tests.py.
BASE_DIR = Path(__file__).resolve().parent          # baselines/obliviator/
REPO_DIR = BASE_DIR.parent.parent                   # artifact root

DATA_DIR = Path(os.environ.get("EJ_DATA_DIR", REPO_DIR / "input" / "plaintext"))
WORK_DIR = Path(os.environ.get(
    "OBLIVIATOR_SRC", REPO_DIR / "external" / "Parallel-join-ae")) / "join"
OUTPUT_DIR = Path(os.environ.get("EJ_RESULT_DIR", BASE_DIR / "query_results"))

# Ensure output directory exists
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

@dataclass
class Table:
    """Represents a table in the query."""
    name: str
    alias: str = None

    def __post_init__(self):
        if self.alias is None:
            self.alias = self.name

@dataclass
class JoinCondition:
    """Represents a join condition between two tables."""
    left_table: str
    left_column: str
    operator: str
    right_table: str
    right_column: str

@dataclass
class JoinStep:
    """Represents a single 2-table join step in the pipeline."""
    left_source: str  # Table name or 'result_N' for intermediate
    right_source: str
    left_key_column: str
    right_key_column: str
    operator: str
    output_name: str
    preserved_columns: List[str]  # Columns to preserve for next joins


# =============================================================================
# Pack/Unpack Utilities for Direct Mode
# =============================================================================

COLUMN_DELIM = "|"      # Separates columns within a table
TABLE_DELIM = "||"      # Separates tables in accumulated data


def pack_columns(values: List[str]) -> str:
    """Pack column values into a delimited string."""
    return COLUMN_DELIM.join(str(v) for v in values)


def unpack_columns(packed: str) -> List[str]:
    """Unpack a delimited string into column values."""
    return packed.split(COLUMN_DELIM)


def pack_tables(table_data_list: List[str]) -> str:
    """Pack multiple tables' data into one string."""
    return TABLE_DELIM.join(table_data_list)


def unpack_tables(packed: str) -> List[str]:
    """Unpack into per-table data strings."""
    return packed.split(TABLE_DELIM)


@dataclass
class ColumnTracker:
    """Track column positions through the join pipeline."""
    table_columns: Dict[str, List[str]]  # table_name -> column_names
    accumulated_left: List[str]   # Tables accumulated on left side
    accumulated_right: List[str]  # Tables on right side (resets each step)

    def add_left_table(self, table_name: str, columns: List[str]):
        self.table_columns[table_name] = columns
        self.accumulated_left.append(table_name)

    def add_right_table(self, table_name: str, columns: List[str]):
        self.table_columns[table_name] = columns
        self.accumulated_right = [table_name]  # Right side is always fresh

    def merge_right_to_left(self):
        """After a join step, right side becomes part of left.

        WARNING: `accumulated_left` does NOT reliably describe the emitted
        column order.  Each step writes `left_packed || right_packed`, so when
        the *intermediate* lands on the right (e.g. TM2 step 3 is
        `customer JOIN result_2`) the fresh table's columns come first in the
        file, while `add_left_table` appends it here at the end.  TM2 emits
        customer,supplier,nation1,nation2 but this reports
        supplier,nation1,nation2,customer.

        Use `run_tests.pipeline_table_order()`, which reconstructs the order
        from the pipeline structure, for anything that needs the true layout.
        """
        self.accumulated_left.extend(self.accumulated_right)
        self.accumulated_right = []

    def get_column_index(self, side: str, table_name: str, column_name: str) -> int:
        """Get column index within the specified side's accumulated data."""
        tables = self.accumulated_left if side == "left" else self.accumulated_right
        idx = 0
        for t in tables:
            cols = self.table_columns[t]
            if t == table_name:
                return idx + cols.index(column_name)
            idx += len(cols)
        raise ValueError(f"Column {table_name}.{column_name} not found in {side}")

    def get_all_columns(self) -> List[str]:
        """Get all column names in order (for final output)."""
        all_cols = []
        for t in self.accumulated_left:
            all_cols.extend(self.table_columns[t])
        return all_cols


class SQLParser:
    """Parse SQL queries to extract tables and join conditions."""

    def __init__(self, sql_query: str):
        self.sql = sql_query.strip()
        self.tables = []
        self.join_conditions = []

    def parse(self):
        """Parse the SQL query."""
        # Extract FROM clause
        self._parse_from_clause()
        # Extract WHERE clause
        self._parse_where_clause()
        return self.tables, self.join_conditions

    def _parse_from_clause(self):
        """Parse the FROM clause to extract tables."""
        # Match FROM ... WHERE or FROM ... to end
        from_match = re.search(r'FROM\s+(.*?)(?:WHERE|$)', self.sql, re.IGNORECASE | re.DOTALL)
        if not from_match:
            raise ValueError("No FROM clause found")

        from_clause = from_match.group(1).strip()

        # Split by comma to get individual tables
        table_parts = from_clause.split(',')

        for part in table_parts:
            part = part.strip()
            # Check for alias (table AS alias or table alias)
            alias_match = re.match(r'(\w+)(?:\s+AS\s+|\s+)(\w+)?', part, re.IGNORECASE)
            if alias_match:
                table_name = alias_match.group(1)
                alias = alias_match.group(2) if alias_match.group(2) else table_name
                self.tables.append(Table(table_name, alias))
            else:
                # Just table name
                self.tables.append(Table(part, part))

    def _parse_where_clause(self):
        """Parse WHERE clause to extract join conditions."""
        where_match = re.search(r'WHERE\s+(.*)', self.sql, re.IGNORECASE | re.DOTALL)
        if not where_match:
            return  # No WHERE clause

        where_clause = where_match.group(1).strip()

        # Split by AND to get individual conditions
        conditions = re.split(r'\s+AND\s+', where_clause, flags=re.IGNORECASE)

        for condition in conditions:
            condition = condition.strip()

            # Parse join condition: table1.column op table2.column
            join_match = re.match(
                r'(\w+)\.(\w+)\s*([<>=!]+)\s*(\w+)\.(\w+)',
                condition
            )

            if join_match:
                self.join_conditions.append(JoinCondition(
                    left_table=join_match.group(1),
                    left_column=join_match.group(2),
                    operator=join_match.group(3),
                    right_table=join_match.group(4),
                    right_column=join_match.group(5)
                ))

class JoinPipelineGenerator:
    """Generate pipeline of 2-table joins from multi-way join query."""

    def __init__(self, tables: List[Table], join_conditions: List[JoinCondition]):
        self.tables = {t.alias: t for t in tables}
        self.join_conditions = join_conditions
        self.pipeline = []
        self.joined_tables = set()
        self.result_schemas = {}  # Track schema of intermediate results

    def generate_pipeline(self) -> List[JoinStep]:
        """Generate optimal sequence of 2-table joins."""
        # Build join graph
        join_graph = self._build_join_graph()

        # Find a good join order (simplified - could use query optimization)
        join_order = self._determine_join_order(join_graph)

        # Generate join steps
        for step_idx, (left, right, condition) in enumerate(join_order):
            # Determine actual sources (tables or intermediate results)
            if left in self.joined_tables:
                left_source = f"result_{step_idx}"
                left_key = self._find_column_in_result(left, condition.left_column, step_idx)
            else:
                left_source = self.tables[left].name
                left_key = condition.left_column

            if right in self.joined_tables:
                right_source = f"result_{step_idx}"
                right_key = self._find_column_in_result(right, condition.right_column, step_idx)
            else:
                right_source = self.tables[right].name
                right_key = condition.right_column

            # Create join step
            step = JoinStep(
                left_source=left_source,
                right_source=right_source,
                left_key_column=left_key,
                right_key_column=right_key,
                operator=condition.operator,
                output_name=f"result_{step_idx + 1}",
                preserved_columns=self._get_preserved_columns(condition)
            )

            self.pipeline.append(step)
            self.joined_tables.update([left, right])

            # Update result schema
            self._update_result_schema(step_idx + 1, left, right, condition)

        return self.pipeline

    def _build_join_graph(self) -> Dict:
        """Build graph of join relationships."""
        graph = {}

        for condition in self.join_conditions:
            left = condition.left_table
            right = condition.right_table

            if left not in graph:
                graph[left] = []
            if right not in graph:
                graph[right] = []

            graph[left].append((right, condition))
            graph[right].append((left, condition))

        return graph

    def _determine_join_order(self, join_graph: Dict) -> List[Tuple]:
        """Determine order of joins (simplified heuristic)."""
        join_order = []
        joined = set()

        # Start with first join condition
        if self.join_conditions:
            first_cond = self.join_conditions[0]
            join_order.append((
                first_cond.left_table,
                first_cond.right_table,
                first_cond
            ))
            joined.update([first_cond.left_table, first_cond.right_table])

        # Add remaining joins
        remaining_conditions = self.join_conditions[1:]

        while remaining_conditions:
            for cond in remaining_conditions:
                # Check if we can join (at least one table already joined)
                left_joined = cond.left_table in joined
                right_joined = cond.right_table in joined

                if left_joined or right_joined:
                    join_order.append((
                        cond.left_table,
                        cond.right_table,
                        cond
                    ))
                    joined.update([cond.left_table, cond.right_table])
                    remaining_conditions.remove(cond)
                    break
            else:
                # No joinable condition found - might be cross product
                if remaining_conditions:
                    cond = remaining_conditions[0]
                    join_order.append((
                        cond.left_table,
                        cond.right_table,
                        cond
                    ))
                    joined.update([cond.left_table, cond.right_table])
                    remaining_conditions.remove(cond)

        return join_order

    def _find_column_in_result(self, table_alias: str, column: str, step_idx: int) -> str:
        """Find column in intermediate result."""
        # This would need proper schema tracking
        # For now, return column name with table prefix
        return f"{table_alias}_{column}"

    def _get_preserved_columns(self, condition: JoinCondition) -> List[str]:
        """Determine which columns to preserve for future joins."""
        preserved = []

        # Check which columns are needed in future conditions
        for future_cond in self.join_conditions:
            if future_cond != condition:
                if future_cond.left_table in [condition.left_table, condition.right_table]:
                    preserved.append(f"{future_cond.left_table}_{future_cond.left_column}")
                if future_cond.right_table in [condition.left_table, condition.right_table]:
                    preserved.append(f"{future_cond.right_table}_{future_cond.right_column}")

        return preserved

    def _update_result_schema(self, result_idx: int, left: str, right: str, condition: JoinCondition):
        """Track schema of intermediate results."""
        # Simplified - would need full schema tracking in production
        self.result_schemas[f"result_{result_idx}"] = {
            'tables': [left, right],
            'join_columns': [(condition.left_column, condition.right_column)]
        }

def get_column_index(csv_file: Path, column_name: str) -> int:
    """Get the index of a column in a CSV file."""
    with open(csv_file, 'r') as f:
        reader = csv.reader(f)
        header = next(reader)

        # Try exact match first
        if column_name in header:
            return header.index(column_name)

        # Try with table prefix
        for i, col in enumerate(header):
            if col.endswith(f"_{column_name}") or col == column_name.upper():
                return i

        # Default to first column
        return 0


# ============================================================================
# Row-ID Based Join Functions (workaround for 13-char data truncation)
# ============================================================================

def load_table_with_row_ids(csv_file: Path, key_column: str) -> Tuple[Dict[int, List[str]], List[Tuple[int, int]], List[str]]:
    """Load a table and create row_id mapping.

    Returns:
        row_map: {row_id -> row_data as list of strings}
        obliviator_rows: [(join_key, row_id), ...] for Obliviator input
        header: column names
    """
    row_map = {}
    obliviator_rows = []

    key_idx = get_column_index(csv_file, key_column)

    with open(csv_file, 'r') as f:
        reader = csv.reader(f)
        header = next(reader)

        for row_id, row in enumerate(reader):
            if len(row) > key_idx:
                # Store full row data
                row_map[row_id] = row

                # Get join key
                key = row[key_idx]
                try:
                    key_int = int(key)
                except ValueError:
                    key_int = abs(hash(key)) % (2**31)

                # For Obliviator: (join_key, row_id as string)
                obliviator_rows.append((key_int, str(row_id)))

    return row_map, obliviator_rows, header


def lookup_join_key(row_map: Dict[int, List[str]], row_id: int, column_idx: int) -> int:
    """Look up a column value from original table using row_id."""
    if row_id not in row_map:
        return 0
    row = row_map[row_id]
    if column_idx >= len(row):
        return 0
    try:
        return int(row[column_idx])
    except ValueError:
        return abs(hash(row[column_idx])) % (2**31)


def parse_obliviator_output_rowids(output_file: Path) -> List[Tuple[int, int]]:
    """Parse Obliviator output to extract (row_id_left, row_id_right) pairs.

    Output format: key1 row_id1 key2 row_id2
    We extract row_id1 and row_id2.
    """
    DATA_LENGTH = 13  # From C code
    pairs = []

    if not output_file.exists():
        return pairs

    with open(output_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # Parse: key1 row_id1 key2 row_id2
            # key1 is first field, row_id1 is second (in data1 position)
            # Since row_id is small, it fits in the 13-char data
            parts = line.split()
            if len(parts) >= 4:
                # parts[0] = key1
                # parts[1] = row_id1 (first field of data1)
                # parts[2] = key2
                # parts[3] = row_id2 (first field of data2)
                try:
                    row_id1 = int(parts[1])
                    row_id2 = int(parts[3])
                    pairs.append((row_id1, row_id2))
                except ValueError:
                    continue

    return pairs


def reconstruct_full_rows(
    final_pairs: List[Tuple],
    row_maps: Dict[str, Dict[int, List[str]]],
    table_order: List[str],
    composite_map: Dict[int, Tuple[int, ...]] = None
) -> List[Tuple]:
    """Reconstruct complete joined rows from row_id pairs.

    Args:
        final_pairs: List of (left_id, right_id) from final join
        row_maps: {table_name -> {row_id -> row_data}}
        table_order: Order of tables in the join
        composite_map: Maps composite_id -> tuple of original row_ids

    Returns:
        List of complete joined rows as tuples
    """
    result_rows = []

    for left_id, right_id in final_pairs:
        full_row = []

        if composite_map and left_id in composite_map:
            # Expand composite ID to original row_ids
            original_ids = composite_map[left_id]
            for i, row_id in enumerate(original_ids):
                if i < len(table_order) - 1:
                    table_name = table_order[i]
                    if table_name in row_maps and row_id in row_maps[table_name]:
                        full_row.extend(row_maps[table_name][row_id])
        else:
            # Simple case: left_id is a single table's row_id
            if len(table_order) >= 1:
                first_table = table_order[0]
                if first_table in row_maps and left_id in row_maps[first_table]:
                    full_row.extend(row_maps[first_table][left_id])

        # Add right table's row
        last_table = table_order[-1]
        if last_table in row_maps and right_id in row_maps[last_table]:
            full_row.extend(row_maps[last_table][right_id])

        result_rows.append(tuple(full_row))

    return result_rows


def csv_to_obliviator_format(csv_file: Path, key_column: str) -> List[Tuple[int, str]]:
    """Convert CSV file to Obliviator input format."""
    rows = []

    # Get column index
    key_idx = get_column_index(csv_file, key_column)

    with open(csv_file, 'r') as f:
        reader = csv.reader(f)
        header = next(reader)  # Skip header

        for row in reader:
            if len(row) > key_idx:
                key = row[key_idx]
                # Convert key to integer (hash if not numeric)
                try:
                    key_int = int(key)
                except ValueError:
                    key_int = abs(hash(key)) % (2**31)

                # Join all columns as data (space-separated for Obliviator)
                data = ' '.join(row)
                rows.append((key_int, data))

    return rows


def csv_to_obliviator_format_packed(csv_file: Path, key_column: str) -> Tuple[List[Tuple[int, str]], List[str]]:
    """Convert CSV file to Obliviator input format with packed columns.

    Uses delimiter-based packing for explicit column boundaries.

    Returns:
        rows: List of (key_int, packed_data) tuples
        columns: List of column names (for tracking)
    """
    rows = []

    # Get column index
    key_idx = get_column_index(csv_file, key_column)

    with open(csv_file, 'r') as f:
        reader = csv.reader(f)
        columns = next(reader)  # Get header

        for row in reader:
            if len(row) > key_idx:
                key = row[key_idx]
                # Convert key to integer (hash if not numeric)
                try:
                    key_int = int(key)
                except ValueError:
                    key_int = abs(hash(key)) % (2**31)

                # Pack all columns with delimiter (preserves column boundaries)
                packed_data = pack_columns(row)
                rows.append((key_int, packed_data))

    return rows, columns


def create_obliviator_input(table1_rows: List, table2_rows: List, output_file: Path):
    """Create Obliviator input file from two tables."""
    with open(output_file, 'w') as f:
        # Write header: length1 length2
        f.write(f"{len(table1_rows)} {len(table2_rows)}\n")

        # Write table1 rows
        for key, data in table1_rows:
            f.write(f"{key} {data}\n")

        # Write table2 rows
        for key, data in table2_rows:
            f.write(f"{key} {data}\n")

def _parse_join_output(stdout: str, actual_runtime: float, input_file: Path) -> Dict:
    """Shared parser for the stdout produced by a single ./host/parallel join.

    Accepts both batch-mode (process-per-join) and daemon-mode (one process
    drives multiple joins) output: caller is responsible for slicing the
    daemon-mode stream to one join's lines before passing them in.
    """
    stdout_lines = [l for l in stdout.strip().split('\n') if l]

    # Parse TIMING line(s) emitted by host/parallel.c. Format:
    #   TIMING enclave_init=X host_pre=X in_enclave=X host_post=X
    phases = {"enclave_init": 0.0, "host_pre": 0.0, "in_enclave": 0.0, "host_post": 0.0}
    timing_seen = False
    non_timing_lines = []
    for line in stdout_lines:
        if line.startswith("TIMING "):
            timing_seen = True
            for kv in line[len("TIMING "):].split():
                if "=" in kv:
                    k, v = kv.split("=", 1)
                    try:
                        phases[k] = float(v)
                    except ValueError:
                        pass
        elif line.startswith("==DONE") or line == "READY":
            # Daemon-mode framing lines; not part of join output.
            continue
        else:
            non_timing_lines.append(line)

    # core_runtime: the float printed by get_time2 (ocall) inside the enclave.
    core_runtime = None
    for line in non_timing_lines:
        try:
            core_runtime = float(line.strip())
            break
        except ValueError:
            continue
    if core_runtime is None:
        match = re.search(r"([\d.]+)", "\n".join(non_timing_lines))
        if match:
            core_runtime = float(match.group(1))

    output_lines = [l for l in non_timing_lines if not l.startswith("Time:")]

    output_file = Path(str(input_file).replace('.txt', '_output.txt'))
    success = output_file.exists() and output_file.stat().st_size > 0

    ret = {
        "success": success,
        "actual_runtime": actual_runtime,
        "core_runtime": core_runtime,
        "output_rows": len(output_lines),
        "output": stdout,
    }
    if timing_seen:
        ret["phases"] = {
            "enclave_init": phases["enclave_init"],
            "host_pre": phases["host_pre"],
            "in_enclave": phases["in_enclave"],
            "host_post": phases["host_post"],
            "host_outside": phases["host_pre"] + phases["host_post"],
        }
    return ret


class ObliviatorSession:
    """Daemon-mode driver for ./host/parallel.

    Spawns one host process per session and reuses its enclave across many
    joins by communicating via the stdin REPL added in parallel.c:
        READY\\n                       <- printed once after enclave init
        <input_path>\\n                <- written per join
        <core_time>\\nTIMING ...\\n==DONE ret=N==\\n   <- printed back per join
        EXIT\\n                        <- written on shutdown

    Use as a context manager:
        with ObliviatorSession() as session:
            r1 = session.join(step1_input)
            r2 = session.join(step2_input)
    """

    READY_MARKER = "READY"
    DONE_PREFIX = "==DONE"

    def __init__(self, timeout_minutes: int = 15):
        self.timeout_minutes = timeout_minutes
        self._proc: Optional[subprocess.Popen] = None
        # Wall time from spawning the host to the enclave printing READY, i.e.
        # process launch + enclave creation + init.  In daemon mode the per-join
        # TIMING line reports enclave_init=None (the enclave is already up), so
        # without this the init term is not measured anywhere -- it only shows
        # up as the unattributed remainder of the end-to-end wall.
        self.init_seconds: Optional[float] = None

    def __enter__(self):
        cmd = ["./host/parallel", "enclave/parallel_enc.signed", "1"]
        print(f"      Starting daemon: {' '.join(cmd)}")
        _t0 = time.time()
        self._proc = subprocess.Popen(
            cmd,
            cwd=WORK_DIR,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        # Wait for the READY marker.
        for line in self._proc.stdout:
            if line.strip() == self.READY_MARKER:
                self.init_seconds = time.time() - _t0
                return self
        self._proc.kill()
        raise RuntimeError("Obliviator host exited before printing READY")

    def __exit__(self, exc_type, exc, tb):
        if self._proc is None:
            return
        try:
            if self._proc.poll() is None:
                self._proc.stdin.write("EXIT\n")
                self._proc.stdin.flush()
        except (BrokenPipeError, OSError):
            pass
        try:
            self._proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait()
        self._proc = None

    def join(self, input_file: Path) -> Dict:
        """Submit one join to the running daemon, return same dict as run_obliviator_join."""
        if self._proc is None or self._proc.poll() is not None:
            raise RuntimeError("ObliviatorSession is not running")

        print(f"      Executing (daemon): {input_file.name}")
        start_time = time.time()
        self._proc.stdin.write(str(input_file) + "\n")
        self._proc.stdin.flush()

        # Read until we see the ==DONE marker for this join.
        collected_lines = []
        for line in self._proc.stdout:
            stripped = line.rstrip("\n")
            collected_lines.append(stripped)
            if stripped.startswith(self.DONE_PREFIX):
                break
        else:
            # Stream closed before ==DONE.
            raise RuntimeError("Obliviator host closed stdout mid-join")

        actual_runtime = time.time() - start_time
        return _parse_join_output("\n".join(collected_lines), actual_runtime, input_file)


def run_obliviator_join(input_file: Path, timeout_minutes: int = 15,
                        session: Optional[ObliviatorSession] = None) -> Dict:
    """Run Obliviator join and capture timing information.

    If `session` is provided, the join is dispatched on the existing daemon
    process (enclave already initialized). Otherwise a fresh process is
    spawned per call, as before.
    """
    if session is not None:
        return session.join(input_file)

    cmd = [
        "./host/parallel",
        "enclave/parallel_enc.signed",
        "1",  # Single thread
        str(input_file)
    ]

    print(f"      Executing: {' '.join(cmd[-2:])}")

    start_time = time.time()
    try:
        result = subprocess.run(
            cmd,
            cwd=WORK_DIR,
            capture_output=True,
            text=True,
            timeout=timeout_minutes * 60
        )
        end_time = time.time()

        actual_runtime = end_time - start_time

        return _parse_join_output(result.stdout, actual_runtime, input_file)
    except subprocess.TimeoutExpired:
        return {
            "success": False,
            "error": "Timeout",
            "actual_runtime": timeout_minutes * 60,
            "core_runtime": None
        }


def parse_obliviator_output_to_rows(output: str) -> List[Tuple[int, str]]:
    """Parse Obliviator output back to row format for next join.

    DEPRECATED: This function has issues with multi-column tables.
    Use read_obliviator_output_file instead.
    """
    rows = []

    for line in output.strip().split('\n'):
        if line and not line.startswith("Time:"):
            # Parse: key1 data1 key2 data2
            parts = line.split(' ', 3)
            if len(parts) >= 4:
                # Combine both sides of join result
                combined_key = abs(hash(f"{parts[0]}_{parts[2]}")) % (2**31)
                combined_data = f"{parts[1]} {parts[3]}"
                rows.append((combined_key, combined_data))

    return rows

def read_obliviator_output_file(output_file: Path, key_position: str = "key1", left_data_cols: int = 0) -> List[Tuple[int, str]]:
    """Read Obliviator output file and convert to row format for next join.

    This function reads the actual output file created by Obliviator,
    which is more reliable than parsing stdout.

    Output format from Obliviator (space-separated values):
        key1 [left_data_cols values] key2 [right data values]

    For equi-join, key1 == key2.

    Args:
        output_file: Path to the Obliviator output file
        key_position: Which key to extract for the next join:
            - "key1": First value (default)
            - "data2_colN": Column N (0-indexed) from right table's data section
              (e.g., "data2_col2" for the 3rd column of right table's data)
        left_data_cols: Number of data columns from left table (excluding key).
            Required when key_position starts with "data2_col".
    """
    rows = []

    if not output_file.exists():
        print(f"        Warning: Output file {output_file} not found")
        return rows

    with open(output_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # Split into space-separated values
            values = line.split()
            if not values:
                continue

            if key_position == "key1":
                # Use first value as key
                try:
                    key = int(values[0])
                except ValueError:
                    key = abs(hash(values[0])) % (2**31)
                data = ' '.join(values[1:]) if len(values) > 1 else ""
                rows.append((key, data))

            elif key_position.startswith("data2_col"):
                # Output structure: key1 [left_data_cols] key2 [right_data...]
                # Extract the column index N from "data2_colN"
                try:
                    col_offset = int(key_position[9:])  # Parse N from "data2_colN"
                except ValueError:
                    col_offset = 0

                # data2_col0 is at index: 1 + left_data_cols + 1 = left_data_cols + 2
                # data2_colN is at index: left_data_cols + 2 + N
                data2_colN_idx = left_data_cols + 2 + col_offset

                if len(values) > data2_colN_idx:
                    try:
                        key = int(values[data2_colN_idx])
                    except ValueError:
                        key = abs(hash(values[data2_colN_idx])) % (2**31)
                    # Keep whole line as data for next join
                    rows.append((key, line))
                else:
                    # Fallback to key1 if line doesn't have enough values
                    try:
                        key = int(values[0])
                    except ValueError:
                        key = abs(hash(values[0])) % (2**31)
                    data = ' '.join(values[1:]) if len(values) > 1 else ""
                    rows.append((key, data))
            else:
                # Default to key1
                try:
                    key = int(values[0])
                except ValueError:
                    key = abs(hash(values[0])) % (2**31)
                data = ' '.join(values[1:]) if len(values) > 1 else ""
                rows.append((key, data))

    return rows


def read_obliviator_output_packed(
    output_file: Path,
    next_key_col_idx: int,
    tracker: ColumnTracker
) -> List[Tuple[int, str]]:
    """Read Obliviator output file with packed format and repack for next join.

    Output format from Obliviator with packed data:
        key1 left_packed key2 right_packed

    Where left_packed and right_packed use COLUMN_DELIM (|) to separate columns
    and TABLE_DELIM (||) to separate accumulated tables.

    Args:
        output_file: Path to the Obliviator output file
        next_key_col_idx: Column index to use as next join key
            (absolute index across all accumulated tables)
        tracker: ColumnTracker with current left/right table info
    """
    rows = []

    if not output_file.exists():
        print(f"        Warning: Output file {output_file} not found")
        return rows

    with open(output_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # Parse: key1 left_packed key2 right_packed
            # Use split with maxsplit=3 to handle packed data with spaces
            parts = line.split(' ', 3)
            if len(parts) < 4:
                continue

            key1, left_packed, key2, right_packed = parts

            # Combine left and right tables with table separator
            combined_packed = pack_tables([left_packed, right_packed])

            # Flatten all columns to find the next join key
            all_tables = unpack_tables(combined_packed)
            all_cols = []
            for t in all_tables:
                all_cols.extend(unpack_columns(t))

            # Extract key from specified column index
            if next_key_col_idx < len(all_cols):
                new_key_str = all_cols[next_key_col_idx]
                try:
                    new_key = int(new_key_str)
                except ValueError:
                    new_key = abs(hash(new_key_str)) % (2**31)
            else:
                # Fallback to key1
                try:
                    new_key = int(key1)
                except ValueError:
                    new_key = abs(hash(key1)) % (2**31)

            rows.append((new_key, combined_packed))

    return rows


def parse_prefixed_column(column_ref: str) -> Tuple[str, str]:
    """Parse a column reference like 'nation1_N1_N_REGIONKEY' into (table, column).

    Returns:
        (table_name, column_name) tuple
    """
    # Try to split on first underscore to get table prefix
    # e.g., "nation1_N1_N_REGIONKEY" -> ("nation1", "N1_N_REGIONKEY")
    if '_' in column_ref:
        parts = column_ref.split('_', 1)
        return (parts[0], parts[1])
    return (column_ref, column_ref)


def execute_query_pipeline(query_name: str, sql: str, data_size: str) -> Dict:
    """Execute a query by automatically generating and running the join pipeline."""
    print(f"\n  Query: {query_name} on data_{data_size}")

    # Parse SQL
    parser = SQLParser(sql)
    tables, join_conditions = parser.parse()

    print(f"    Tables: {[t.name for t in tables]}")
    print(f"    Joins: {len(join_conditions)}")

    data_path = DATA_DIR / f"data_{data_size}"

    # Check if single or multi-join
    if len(join_conditions) <= 1 and len(tables) == 2:
        # Simple 2-table join
        print(f"    Type: Single join")

        # Load tables
        table1_file = data_path / f"{tables[0].name}.csv"
        table2_file = data_path / f"{tables[1].name}.csv"

        if not table1_file.exists() or not table2_file.exists():
            print(f"    Error: Missing input files")
            return {"success": False, "error": "Missing input files"}

        # Determine join columns
        if join_conditions:
            join_col1 = join_conditions[0].left_column
            join_col2 = join_conditions[0].right_column
        else:
            # Default to first column
            join_col1 = join_col2 = "0"

        # Convert to Obliviator format
        table1_rows = csv_to_obliviator_format(table1_file, join_col1)
        table2_rows = csv_to_obliviator_format(table2_file, join_col2)

        # Create input and run
        input_file = WORK_DIR / f"{query_name}_{data_size}_input.txt"
        create_obliviator_input(table1_rows, table2_rows, input_file)

        output_file = OUTPUT_DIR / f"{query_name}_{data_size}_output.txt"
        result = run_obliviator_join(input_file)

        # Save output
        with open(output_file, 'w') as f:
            f.write(result['output'])

        return result

    else:
        # Multi-table join - generate pipeline
        print(f"    Type: Multi-join pipeline")

        generator = JoinPipelineGenerator(tables, join_conditions)
        pipeline = generator.generate_pipeline()

        print(f"    Pipeline: {len(pipeline)} steps")

        all_results = []
        current_intermediate = None

        # Track which tables' columns are on which side of each step's output
        # After step N, the output contains:
        #   - key1 = left table's join key
        #   - data1 = left table's data columns
        #   - key2 = right table's join key
        #   - data2 = right table's data columns
        # The right table's first column (column 0) is in data2's first field
        prev_right_table = None
        prev_right_first_col = None
        current_left_data_cols = 0  # Number of data columns from left table
        current_right_data_cols = 0  # Number of data columns from right table
        prev_output_total_values = 0  # Total values in previous step's output line

        for step_idx, join_step in enumerate(pipeline):
            print(f"      Step {step_idx + 1}: {join_step.left_source} ⋈ {join_step.right_source}")

            # Determine key_position for reading intermediate results
            key_position = "key1"  # Default
            if join_step.left_source.startswith("result_") and step_idx > 0:
                # Check if the join column came from the previous step's right table
                # If so, it's in data2 of the previous output
                if prev_right_table and prev_right_first_col:
                    # Get the column name we need for this join
                    needed_col = join_step.left_key_column

                    # Check if this column is from the previous right table
                    # The first column of the right table is stored as data2's first field
                    right_table_csv = data_path / f"{prev_right_table}.csv"
                    if right_table_csv.exists():
                        with open(right_table_csv, 'r') as f:
                            header = f.readline().strip().split(',')
                            # If the needed column is the first column of the prev right table
                            if header and header[0].upper() == needed_col.upper():
                                key_position = "data2_col0"
                                print(f"        Using key_position='data2_col0' for {needed_col}")

            # Load left table
            if join_step.left_source.startswith("result_"):
                # Use previous output
                if current_intermediate is None:
                    print(f"        Error: No intermediate result available")
                    return {"success": False, "error": "Pipeline error"}
                left_rows = current_intermediate
                # Update current_left_data_cols based on previous output structure
                # Previous output had: key1 + left_data + key2 + right_data = prev_output_total_values
                # When read as intermediate, the key is extracted, so data = prev_output_total_values - 1
                if prev_output_total_values > 0:
                    current_left_data_cols = prev_output_total_values - 1
            else:
                left_file = data_path / f"{join_step.left_source}.csv"
                if not left_file.exists():
                    print(f"        Error: Missing {left_file}")
                    return {"success": False, "error": f"Missing {join_step.left_source}"}
                left_rows = csv_to_obliviator_format(left_file, join_step.left_key_column)
                # Count data columns (csv_to_obliviator_format includes ALL columns in data)
                with open(left_file, 'r') as f:
                    header = f.readline().strip().split(',')
                    current_left_data_cols = len(header)  # All columns are in data

            # Load right table
            if join_step.right_source.startswith("result_"):
                right_rows = current_intermediate
                # Update right_data_cols based on previous output structure
                if prev_output_total_values > 0:
                    current_right_data_cols = prev_output_total_values - 1
            else:
                right_file = data_path / f"{join_step.right_source}.csv"
                if not right_file.exists():
                    print(f"        Error: Missing {right_file}")
                    return {"success": False, "error": f"Missing {join_step.right_source}"}
                right_rows = csv_to_obliviator_format(right_file, join_step.right_key_column)
                # Count right table columns
                with open(right_file, 'r') as f:
                    header = f.readline().strip().split(',')
                    current_right_data_cols = len(header)

            # Create input and run join
            input_file = WORK_DIR / f"{query_name}_{data_size}_step{step_idx + 1}_input.txt"
            create_obliviator_input(left_rows, right_rows, input_file)

            result = run_obliviator_join(input_file)
            all_results.append(result)

            if not result['success']:
                print(f"        Failed at step {step_idx + 1}")
                break

            # Update prev_output_total_values for next step
            # Output format: key1 + left_data + key2 + right_data
            prev_output_total_values = 1 + current_left_data_cols + 1 + current_right_data_cols

            # Track which table was on the right for this step
            if not join_step.right_source.startswith("result_"):
                prev_right_table = join_step.right_source
                # Get first column of right table
                right_csv = data_path / f"{prev_right_table}.csv"
                if right_csv.exists():
                    with open(right_csv, 'r') as f:
                        header = f.readline().strip().split(',')
                        prev_right_first_col = header[0] if header else None

            # Parse output for next step using file-based reading
            if step_idx < len(pipeline) - 1:
                # Read from the actual output file instead of parsing stdout
                output_file = Path(str(input_file).replace('.txt', '_output.txt'))

                # Determine key_position for the NEXT step
                # Check if the intermediate result will be used on LEFT or RIGHT side
                next_step = pipeline[step_idx + 1]
                next_key_position = "key1"  # Default
                needed_col = None

                # Check if result_* is used on LEFT side
                if next_step.left_source.startswith("result_"):
                    needed_col = next_step.left_key_column
                # Check if result_* is used on RIGHT side
                elif next_step.right_source.startswith("result_"):
                    needed_col = next_step.right_key_column

                if needed_col:
                    # Remove table prefix if present (e.g., "orders_O_ORDERKEY" -> "O_ORDERKEY")
                    if '_' in needed_col:
                        needed_col_clean = needed_col.split('_', 1)[1] if '_' in needed_col else needed_col
                    else:
                        needed_col_clean = needed_col

                    # Find which column of the current right table matches the needed column
                    if not join_step.right_source.startswith("result_"):
                        right_csv = data_path / f"{join_step.right_source}.csv"
                        if right_csv.exists():
                            with open(right_csv, 'r') as f:
                                header = f.readline().strip().split(',')
                                # Search all columns for the matching one
                                for col_idx, col_name in enumerate(header):
                                    col_upper = col_name.upper()
                                    # Check both with and without prefix
                                    if col_upper == needed_col.upper() or col_upper == needed_col_clean.upper():
                                        next_key_position = f"data2_col{col_idx}"
                                        print(f"        Next step will use key_position='{next_key_position}' for {needed_col}")
                                        break

                current_intermediate = read_obliviator_output_file(output_file, next_key_position, current_left_data_cols)

            # Save step output
            output_file = OUTPUT_DIR / f"{query_name}_{data_size}_step{step_idx + 1}_output.txt"
            with open(output_file, 'w') as f:
                f.write(result['output'])

        # Aggregate results
        total_actual = sum(r.get('actual_runtime', 0) for r in all_results)
        total_core = sum(r.get('core_runtime', 0) for r in all_results if r.get('core_runtime'))

        return {
            "success": all(r.get('success', False) for r in all_results),
            "actual_runtime": total_actual,
            "core_runtime": total_core if total_core else None,
            "steps": len(all_results),
            "step_results": all_results
        }


def execute_query_pipeline_packed(query_name: str, sql: str, data_size: str) -> Tuple[Dict, ColumnTracker]:
    """Execute a query using packed column format for reliable multi-table joins.

    Uses delimiter-based packing to preserve column boundaries through pipeline.
    Opens a single ObliviatorSession per query so the enclave is initialized
    once and reused across all join steps.

    Returns both the result dict and the ColumnTracker for post-processing.
    """
    print(f"\n  Query: {query_name} on data_{data_size} (packed mode)")

    # Parse SQL
    parser = SQLParser(sql)
    tables, join_conditions = parser.parse()

    print(f"    Tables: {[t.name for t in tables]}")
    print(f"    Joins: {len(join_conditions)}")

    data_path = DATA_DIR / f"data_{data_size}"

    # Check if single or multi-join
    if len(join_conditions) <= 1 and len(tables) == 2:
        # Simple 2-table join
        print(f"    Type: Single join (packed)")

        table1_file = data_path / f"{tables[0].name}.csv"
        table2_file = data_path / f"{tables[1].name}.csv"

        if not table1_file.exists() or not table2_file.exists():
            print(f"    Error: Missing input files")
            return {"success": False, "error": "Missing input files"}, None

        # Determine join columns
        if join_conditions:
            join_col1 = join_conditions[0].left_column
            join_col2 = join_conditions[0].right_column
        else:
            join_col1 = join_col2 = "0"

        # Convert to packed format
        table1_rows, table1_cols = csv_to_obliviator_format_packed(table1_file, join_col1)
        table2_rows, table2_cols = csv_to_obliviator_format_packed(table2_file, join_col2)

        # Create tracker
        tracker = ColumnTracker({}, [], [])
        tracker.add_left_table(tables[0].name, table1_cols)
        tracker.add_right_table(tables[1].name, table2_cols)

        # Create input and run
        input_file = WORK_DIR / f"{query_name}_{data_size}_input.txt"
        create_obliviator_input(table1_rows, table2_rows, input_file)

        output_file = OUTPUT_DIR / f"{query_name}_{data_size}_output.txt"
        with ObliviatorSession() as session:
            result = run_obliviator_join(input_file, session=session)

        # Save output
        with open(output_file, 'w') as f:
            f.write(result['output'])

        tracker.merge_right_to_left()
        return result, tracker

    else:
        # Multi-table join - generate pipeline
        print(f"    Type: Multi-join pipeline (packed)")

        generator = JoinPipelineGenerator(tables, join_conditions)
        pipeline = generator.generate_pipeline()

        print(f"    Pipeline: {len(pipeline)} steps")

        all_results = []
        current_intermediate = None
        tracker = ColumnTracker({}, [], [])

        with ObliviatorSession() as session:
            for step_idx, join_step in enumerate(pipeline):
                print(f"      Step {step_idx + 1}: {join_step.left_source} ⋈ {join_step.right_source}")

                # Load left table
                if join_step.left_source.startswith("result_"):
                    left_rows = current_intermediate
                    # Tracker already has accumulated left tables from merge
                else:
                    left_file = data_path / f"{join_step.left_source}.csv"
                    if not left_file.exists():
                        print(f"        Error: Missing {left_file}")
                        return {"success": False, "error": f"Missing {join_step.left_source}"}, None
                    left_rows, left_cols = csv_to_obliviator_format_packed(left_file, join_step.left_key_column)
                    tracker.add_left_table(join_step.left_source, left_cols)

                # Load right table
                if join_step.right_source.startswith("result_"):
                    right_rows = current_intermediate
                    # This case is unusual - intermediate on right side
                    # The tracker state should already reflect the intermediate
                else:
                    right_file = data_path / f"{join_step.right_source}.csv"
                    if not right_file.exists():
                        print(f"        Error: Missing {right_file}")
                        return {"success": False, "error": f"Missing {join_step.right_source}"}, None
                    right_rows, right_cols = csv_to_obliviator_format_packed(right_file, join_step.right_key_column)
                    tracker.add_right_table(join_step.right_source, right_cols)

                # Create input and run join
                input_file = WORK_DIR / f"{query_name}_{data_size}_step{step_idx + 1}_input.txt"
                create_obliviator_input(left_rows, right_rows, input_file)

                result = run_obliviator_join(input_file, session=session)
                all_results.append(result)

                if not result['success']:
                    print(f"        Failed at step {step_idx + 1}")
                    break

                # Merge right into left for tracking
                tracker.merge_right_to_left()

                # Parse output for next step using packed format
                if step_idx < len(pipeline) - 1:
                    output_file = Path(str(input_file).replace('.txt', '_output.txt'))

                    next_step = pipeline[step_idx + 1]

                    # Determine which column to use as next join key
                    if next_step.left_source.startswith("result_"):
                        # Next step uses intermediate on left
                        table_name, col_name = parse_prefixed_column(next_step.left_key_column)
                        try:
                            next_key_idx = tracker.get_column_index("left", table_name, col_name)
                        except ValueError:
                            # Try without table prefix
                            next_key_idx = find_column_in_tracker(tracker, col_name)
                        print(f"        Next key: {next_step.left_key_column} at index {next_key_idx}")

                    elif next_step.right_source.startswith("result_"):
                        # Next step uses intermediate on right
                        table_name, col_name = parse_prefixed_column(next_step.right_key_column)
                        try:
                            next_key_idx = tracker.get_column_index("left", table_name, col_name)
                        except ValueError:
                            next_key_idx = find_column_in_tracker(tracker, col_name)
                        print(f"        Next key: {next_step.right_key_column} at index {next_key_idx}")
                    else:
                        # Both are fresh tables, use key1 (first column)
                        next_key_idx = 0

                    current_intermediate = read_obliviator_output_packed(output_file, next_key_idx, tracker)

                # Save step output
                output_file = OUTPUT_DIR / f"{query_name}_{data_size}_step{step_idx + 1}_output.txt"
                with open(output_file, 'w') as f:
                    f.write(result['output'])

        # Aggregate results
        total_actual = sum(r.get('actual_runtime', 0) for r in all_results)
        total_core = sum(r.get('core_runtime', 0) for r in all_results if r.get('core_runtime'))

        return {
            "success": all(r.get('success', False) for r in all_results),
            "actual_runtime": total_actual,
            "core_runtime": total_core if total_core else None,
            "steps": len(all_results),
            "step_results": all_results
        }, tracker


def find_column_in_tracker(tracker: ColumnTracker, col_name: str) -> int:
    """Find column index by searching all tables in tracker."""
    idx = 0
    for table_name in tracker.accumulated_left:
        cols = tracker.table_columns[table_name]
        for i, col in enumerate(cols):
            if col.upper() == col_name.upper() or col.upper().endswith('_' + col_name.upper()):
                return idx + i
        idx += len(cols)
    raise ValueError(f"Column {col_name} not found in any table")


def execute_query_pipeline_rowid(query_name: str, sql: str, data_size: str) -> Dict:
    """Execute a query using row-ID based approach to avoid data truncation.

    This function sends only (join_key, row_id) to Obliviator, then reconstructs
    full rows at the end using the original table data.
    """
    print(f"\n  Query: {query_name} on data_{data_size} (row-ID mode)")

    # Parse SQL
    parser = SQLParser(sql)
    tables, join_conditions = parser.parse()

    print(f"    Tables: {[t.name for t in tables]}")
    print(f"    Joins: {len(join_conditions)}")

    data_path = DATA_DIR / f"data_{data_size}"

    # Load all tables with row_id mappings
    row_maps = {}  # table_name -> {row_id -> row_data}
    headers = {}   # table_name -> [column_names]

    for table in tables:
        table_file = data_path / f"{table.name}.csv"
        if not table_file.exists():
            print(f"    Error: Missing {table_file}")
            return {"success": False, "error": f"Missing {table.name}"}

        # Just load the row map and headers for now
        with open(table_file, 'r') as f:
            reader = csv.reader(f)
            headers[table.name] = next(reader)
            row_maps[table.name] = {}
            for row_id, row in enumerate(reader):
                row_maps[table.name][row_id] = row

        print(f"    Loaded {table.name}: {len(row_maps[table.name])} rows")

    # Generate join pipeline
    generator = JoinPipelineGenerator(tables, join_conditions)
    pipeline = generator.generate_pipeline()

    print(f"    Pipeline: {len(pipeline)} steps")

    # Track table order for final reconstruction
    table_order = []

    # Composite ID tracking for multi-step joins
    # Maps composite_id -> tuple of original row_ids
    composite_map = {}
    next_composite_id = 0

    all_results = []
    current_pairs = None  # List of (left_row_id, right_row_id) pairs

    for step_idx, join_step in enumerate(pipeline):
        print(f"      Step {step_idx + 1}: {join_step.left_source} ⋈ {join_step.right_source}")

        # Determine left table rows
        if join_step.left_source.startswith("result_"):
            # Use intermediate result from previous step
            if current_pairs is None:
                print(f"        Error: No intermediate result")
                return {"success": False, "error": "Pipeline error"}

            # Get the join key column for this step
            # Need to find which original table has this column
            needed_col = join_step.left_key_column
            # Remove table prefix if present
            if '_' in needed_col:
                parts = needed_col.split('_', 1)
                source_table = parts[0]
                col_name = parts[1]
            else:
                source_table = None
                col_name = needed_col

            # Find the column index in the source table
            col_idx = 0
            if source_table and source_table in headers:
                header = headers[source_table]
                for i, h in enumerate(header):
                    if h.upper() == col_name.upper():
                        col_idx = i
                        break

            print(f"        Looking up {col_name} (col {col_idx}) from {source_table}")

            # Create left rows from intermediate result
            left_rows = []
            for pair_idx, (left_id, right_id) in enumerate(current_pairs):
                # Get the join key from the appropriate table
                if source_table and source_table in row_maps:
                    # Determine which row_id to use
                    if step_idx == 1:
                        # Step 2: use right_id from step 1 (second table)
                        row_id = right_id
                    else:
                        # For later steps with composite IDs
                        if left_id in composite_map:
                            # Find the right row_id within the composite
                            composite = composite_map[left_id]
                            # The source_table's row_id is at the appropriate index
                            table_idx = table_order.index(source_table) if source_table in table_order else -1
                            if 0 <= table_idx < len(composite):
                                row_id = composite[table_idx]
                            else:
                                row_id = right_id
                        else:
                            row_id = right_id

                    key = lookup_join_key(row_maps[source_table], row_id, col_idx)
                else:
                    # Fallback
                    key = left_id

                # Create composite ID for this pair
                if step_idx == 1:
                    composite_map[next_composite_id] = (left_id, right_id)
                else:
                    # Extend existing composite
                    if left_id in composite_map:
                        composite_map[next_composite_id] = composite_map[left_id] + (right_id,)
                    else:
                        composite_map[next_composite_id] = (left_id, right_id)

                left_rows.append((key, str(next_composite_id)))
                next_composite_id += 1

            # Reset composite map for next iteration
            if step_idx > 0:
                # Keep only the new composite IDs
                old_ids = set(composite_map.keys()) - set(range(next_composite_id - len(current_pairs), next_composite_id))
                for old_id in old_ids:
                    if old_id < next_composite_id - len(current_pairs):
                        del composite_map[old_id]

        else:
            # First step: load left table directly
            left_table = join_step.left_source
            left_key_col = join_step.left_key_column
            table_order.append(left_table)

            left_file = data_path / f"{left_table}.csv"
            _, left_rows, _ = load_table_with_row_ids(left_file, left_key_col)

        # Load right table
        if join_step.right_source.startswith("result_"):
            # Right source is an intermediate result
            # This happens when the join order puts a new table on the left
            if current_pairs is None:
                print(f"        Error: No intermediate result for right side")
                return {"success": False, "error": "Pipeline error"}

            # Get the join key column for this step (from intermediate result)
            needed_col = join_step.right_key_column
            # Remove table prefix if present
            if '_' in needed_col:
                parts = needed_col.split('_', 1)
                source_table = parts[0]
                col_name = parts[1]
            else:
                source_table = None
                col_name = needed_col

            # Find the column index in the source table
            col_idx = 0
            if source_table and source_table in headers:
                header = headers[source_table]
                for i, h in enumerate(header):
                    if h.upper() == col_name.upper():
                        col_idx = i
                        break

            print(f"        Looking up {col_name} (col {col_idx}) from {source_table} (right side)")

            # Create right rows from intermediate result
            right_rows = []
            for pair_idx, (left_id, right_id) in enumerate(current_pairs):
                # Get the join key from the appropriate table
                if source_table and source_table in row_maps:
                    # Determine which row_id to use
                    table_idx = table_order.index(source_table) if source_table in table_order else -1
                    if left_id in composite_map:
                        composite = composite_map[left_id]
                        if 0 <= table_idx < len(composite):
                            row_id = composite[table_idx]
                        else:
                            row_id = right_id
                    else:
                        # For 2-table intermediate, right_id is from the second table
                        if table_idx == 1:
                            row_id = right_id
                        else:
                            row_id = left_id

                    key = lookup_join_key(row_maps[source_table], row_id, col_idx)
                else:
                    # Fallback
                    key = right_id

                # Use the same composite ID tracking as left side
                if step_idx == 1:
                    composite_map[next_composite_id] = (left_id, right_id)
                else:
                    if left_id in composite_map:
                        composite_map[next_composite_id] = composite_map[left_id] + (right_id,)
                    else:
                        composite_map[next_composite_id] = (left_id, right_id)

                right_rows.append((key, str(next_composite_id)))
                next_composite_id += 1

        else:
            right_table = join_step.right_source
            right_key_col = join_step.right_key_column
            if right_table not in table_order:
                table_order.append(right_table)

            right_file = data_path / f"{right_table}.csv"
            _, right_rows, _ = load_table_with_row_ids(right_file, right_key_col)

        # Create input file and run join
        input_file = WORK_DIR / f"{query_name}_{data_size}_rowid_step{step_idx + 1}_input.txt"
        create_obliviator_input(left_rows, right_rows, input_file)

        print(f"        Executing join ({len(left_rows)} x {len(right_rows)} rows)")
        result = run_obliviator_join(input_file)
        all_results.append(result)

        if not result['success']:
            print(f"        Failed at step {step_idx + 1}")
            break

        # Parse output to get row_id pairs
        output_file = Path(str(input_file).replace('.txt', '_output.txt'))
        current_pairs = parse_obliviator_output_rowids(output_file)
        print(f"        Output: {len(current_pairs)} matched pairs")

    # Determine final join configuration
    final_step = pipeline[-1] if pipeline else None
    final_left_is_table = final_step and not final_step.left_source.startswith("result_")
    final_right_is_table = final_step and not final_step.right_source.startswith("result_")
    final_left_table = final_step.left_source if final_left_is_table else None
    final_right_table = final_step.right_source if final_right_is_table else None

    # Reconstruct full rows
    if current_pairs and all(r.get('success', False) for r in all_results):
        print(f"    Reconstructing full rows...")
        print(f"    Final join: left={'table:'+final_left_table if final_left_is_table else 'composite'}, "
              f"right={'table:'+final_right_table if final_right_is_table else 'composite'}")

        # For the final reconstruction, we need to handle both table row_ids and composite IDs
        final_rows = []
        for left_id, right_id in current_pairs:
            full_row = []

            # Handle left side
            if final_left_is_table:
                # Left side is a simple table row_id
                if final_left_table in row_maps and left_id in row_maps[final_left_table]:
                    full_row.extend(row_maps[final_left_table][left_id])
            elif left_id in composite_map:
                # Left side is a composite ID - expand it
                row_ids = composite_map[left_id]
                # Get tables that are in this composite (all except the final tables)
                composite_tables = [t for t in table_order if t != final_left_table and t != final_right_table]
                for i, rid in enumerate(row_ids):
                    if i < len(composite_tables):
                        tbl = composite_tables[i]
                        if tbl in row_maps and rid in row_maps[tbl]:
                            full_row.extend(row_maps[tbl][rid])
            else:
                # Fallback: treat as first table row_id
                if len(table_order) >= 1:
                    first_tbl = table_order[0]
                    if first_tbl in row_maps and left_id in row_maps[first_tbl]:
                        full_row.extend(row_maps[first_tbl][left_id])

            # Handle right side
            if final_right_is_table:
                # Right side is a simple table row_id
                if final_right_table in row_maps and right_id in row_maps[final_right_table]:
                    full_row.extend(row_maps[final_right_table][right_id])
            elif right_id in composite_map:
                # Right side is a composite ID - expand it
                row_ids = composite_map[right_id]
                composite_tables = [t for t in table_order if t != final_left_table and t != final_right_table]
                for i, rid in enumerate(row_ids):
                    if i < len(composite_tables):
                        tbl = composite_tables[i]
                        if tbl in row_maps and rid in row_maps[tbl]:
                            full_row.extend(row_maps[tbl][rid])
            else:
                # Fallback: treat as last table row_id
                last_tbl = table_order[-1]
                if last_tbl in row_maps and right_id in row_maps[last_tbl]:
                    full_row.extend(row_maps[last_tbl][right_id])

            final_rows.append(tuple(full_row))

        print(f"    Reconstructed {len(final_rows)} complete rows")

        # Aggregate results
        total_actual = sum(r.get('actual_runtime', 0) for r in all_results)
        total_core = sum(r.get('core_runtime', 0) for r in all_results if r.get('core_runtime'))

        return {
            "success": True,
            "actual_runtime": total_actual,
            "core_runtime": total_core if total_core else None,
            "steps": len(all_results),
            "step_results": all_results,
            "final_rows": final_rows,
            "table_order": table_order
        }

    else:
        total_actual = sum(r.get('actual_runtime', 0) for r in all_results)
        return {
            "success": False,
            "actual_runtime": total_actual,
            "steps": len(all_results),
            "step_results": all_results
        }


def save_incremental_summary(all_results: Dict):
    """Save current state of results to summary files."""
    # Save JSON
    summary_file = OUTPUT_DIR / "execution_summary.json"
    with open(summary_file, 'w') as f:
        json.dump(all_results, f, indent=2, default=str)

    # Save human-readable text summary
    summary_txt = OUTPUT_DIR / "execution_summary.txt"
    with open(summary_txt, 'w') as f:
        f.write("Obliviator Query Execution Summary\n")
        f.write("=" * 70 + "\n\n")
        f.write(f"{'Query':<8} {'Size':<8} {'Status':<10} {'Steps':<7} {'Core(s)':<12} {'Actual(s)':<12} {'Overhead':<10}\n")
        f.write("-" * 80 + "\n")

        for key in sorted(all_results.keys()):
            parts = key.split('_')
            query_name = parts[0]
            data_size = '_'.join(parts[1:])
            result = all_results[key]

            status = "Success" if result.get('success') else "Failed"
            steps = result.get('steps', 1)
            core_time = result.get('core_runtime')
            actual_time = result.get('actual_runtime')

            if core_time and actual_time:
                overhead = f"{actual_time/core_time:.1f}x"
                core_str = f"{core_time:.6f}"
                actual_str = f"{actual_time:.2f}"
            else:
                overhead = "N/A"
                core_str = str(core_time) if core_time else "N/A"
                actual_str = f"{actual_time:.2f}" if actual_time else "N/A"

            f.write(f"{query_name:<8} {data_size.replace('_', '.'):<8} {status:<10} {steps:<7} {core_str:<12} {actual_str:<12} {overhead:<10}\n")

def print_result_row(query_name: str, data_size: str, result: Dict):
    """Print a single result row."""
    status = "✓ Success" if result.get('success') else "✗ Failed"
    steps = result.get('steps', 1)

    core_time = result.get('core_runtime')
    actual_time = result.get('actual_runtime')

    if core_time and actual_time:
        overhead = f"{actual_time/core_time:.1f}x"
        core_str = f"{core_time:.6f}"
        actual_str = f"{actual_time:.2f}"
    else:
        overhead = "N/A"
        core_str = "N/A"
        actual_str = f"{actual_time:.2f}" if actual_time else "N/A"

    print(f"{query_name:<8} {data_size.replace('_', '.'):<8} {status:<10} {steps:<7} {core_str:<12} {actual_str:<12} {overhead:<10}")

def main():
    """Main execution function."""
    print("=" * 70)
    print("Obliviator Query Runner with Automatic Pipeline Generation")
    print("=" * 70)

    # Load query definitions from SQL files
    query_dir = DATA_DIR / "queries"
    queries = {}

    for query_file in query_dir.glob("*.sql"):
        query_name = query_file.stem.replace("tpch_", "")
        with open(query_file, 'r') as f:
            queries[query_name] = f.read()

    print(f"\nFound {len(queries)} queries: {list(queries.keys())}")

    # Data sizes to test - START WITH SMALLEST
    data_sizes = ["0_001", "0_01", "0_1"]

    # Query order
    query_order = ["tb1", "tb2", "tm1", "tm2", "tm3"]

    print(f"\nWill run queries: {query_order}")
    print(f"On data sizes: {[d.replace('_', '.') for d in data_sizes]}")

    # Results storage
    all_results = {}

    # Check for existing results to resume
    summary_file = OUTPUT_DIR / "execution_summary.json"
    if summary_file.exists():
        print("\nFound existing results, loading...")
        with open(summary_file, 'r') as f:
            all_results = json.load(f)
        print(f"  Loaded {len(all_results)} existing results")

    # Print header for results table
    print("\n" + "=" * 70)
    print("INCREMENTAL RESULTS")
    print("=" * 70)
    print(f"\n{'Query':<8} {'Size':<8} {'Status':<10} {'Steps':<7} {'Core(s)':<12} {'Actual(s)':<12} {'Overhead':<10}")
    print("-" * 80)

    # Run each combination, starting with smallest dataset
    total_experiments = len(query_order) * len(data_sizes)
    completed = 0

    for data_size in data_sizes:
        print(f"\n--- Processing data size: {data_size.replace('_', '.')} ---")

        for query_name in query_order:
            if query_name not in queries:
                print(f"  Warning: Query {query_name} not found, skipping")
                continue

            key = f"{query_name}_{data_size}"

            # Skip if already completed
            if key in all_results and all_results[key].get('success'):
                print(f"  Skipping {query_name} (already completed)")
                print_result_row(query_name, data_size, all_results[key])
                completed += 1
                continue

            # Run the experiment
            print(f"\n  [{completed+1}/{total_experiments}] Running {query_name} on data_{data_size}...")

            try:
                result = execute_query_pipeline(query_name, queries[query_name], data_size)
                all_results[key] = result

                # Print and save result immediately
                print("\n  Result:")
                print_result_row(query_name, data_size, result)

                # Save incremental summary after each experiment
                save_incremental_summary(all_results)

                completed += 1
                print(f"    Saved results ({completed}/{total_experiments} completed)")

            except Exception as e:
                print(f"    ERROR: {str(e)}")
                all_results[key] = {
                    "success": False,
                    "error": str(e),
                    "actual_runtime": None,
                    "core_runtime": None
                }
                save_incremental_summary(all_results)
                completed += 1

            # Flush output
            sys.stdout.flush()

    # Final summary
    print("\n" + "=" * 70)
    print("FINAL EXECUTION SUMMARY")
    print("=" * 70)

    print(f"\n{'Query':<8} {'Size':<8} {'Status':<10} {'Steps':<7} {'Core(s)':<12} {'Actual(s)':<12} {'Overhead':<10}")
    print("-" * 80)

    for data_size in data_sizes:
        for query_name in query_order:
            result_key = f"{query_name}_{data_size}"
            if result_key in all_results:
                print_result_row(query_name, data_size, all_results[result_key])

    print("\n" + "=" * 70)
    print(f"Results saved to: {OUTPUT_DIR}")
    print(f"Completed {completed}/{total_experiments} experiments")
    print("=" * 70)

if __name__ == "__main__":
    main()