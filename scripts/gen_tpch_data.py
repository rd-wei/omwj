#!/usr/bin/env python3
"""
Generate this system's integer-CSV TPC-H tables from raw dbgen `.tbl` files.

The oblivious-join engine reads CSVs whose every field is an int32 (the table
loader parses with strtol; non-numeric fields become 0 -- see
app/io/table_reader.cpp).  Raw TPC-H `.tbl` output from `dbgen` is pipe-
delimited text with strings, decimals and dates, so it must be encoded first.
This script performs exactly the encoding the committed datasets were built
with:

  * integer keys            -> kept as-is
  * decimals (prices, qty)  -> value * 100, truncated to int (2 dp preserved)
  * dates (YYYY-MM-DD)       -> days since 1900-01-01
  * strings/comments        -> int(md5(value)[:8], 16)  (stable, reproducible)

and then materialises the self-join alias tables the multi-way queries need:

  nation  -> nation1  (N1_ prefix), nation2  (N2_ prefix)
  supplier-> supplier1(S1_ prefix), supplier2(S2_ prefix)
  part    -> part1    (P1_ prefix), part2    (P2_ prefix)

The result is 14 files (customer, orders, lineitem, part{,1,2},
supplier{,1,2}, nation{,1,2}, partsupp, region) matching the committed
input/plaintext/data_<scale>/ layout exactly.  Pure Python stdlib -- no pandas,
no third-party packages.

Usage:
    python3 scripts/gen_tpch_data.py <tbl_dir> <out_dir>

    <tbl_dir>  directory containing customer.tbl, orders.tbl, lineitem.tbl,
               part.tbl, supplier.tbl, partsupp.tbl, nation.tbl, region.tbl
               (produced by `dbgen -s <scale>`)
    <out_dir>  destination for the 14 integer CSV files

See scripts/gen_all_data.sh for the full dbgen -> CSV pipeline across scales.
"""

import csv
import hashlib
import sys
from datetime import datetime
from pathlib import Path

EPOCH = datetime(1900, 1, 1)
DECIMAL_SCALE = 100  # two decimal places preserved as an integer
# The engine stores every attribute as an int32 bounded to [-JOIN_ATTR_MAX,
# JOIN_ATTR_MAX] (see common/enclave_types.h); a raw md5 prefix can reach
# 4.29e9 and overflow, so string hashes are folded into [0, JOIN_ATTR_MAX].
# This keeps engine and SQLite in agreement.  String columns are never join
# keys in these queries, so the exact hash value does not affect any result.
JOIN_ATTR_MAX = 1073741820

SCHEMAS = {
    'nation':   ['N_NATIONKEY', 'N_NAME', 'N_REGIONKEY', 'N_COMMENT'],
    'region':   ['R_REGIONKEY', 'R_NAME', 'R_COMMENT'],
    'part':     ['P_PARTKEY', 'P_NAME', 'P_MFGR', 'P_BRAND', 'P_TYPE',
                 'P_SIZE', 'P_CONTAINER', 'P_RETAILPRICE', 'P_COMMENT'],
    'supplier': ['S_SUPPKEY', 'S_NAME', 'S_ADDRESS', 'S_NATIONKEY', 'S_PHONE',
                 'S_ACCTBAL', 'S_COMMENT'],
    'partsupp': ['PS_PARTKEY', 'PS_SUPPKEY', 'PS_AVAILQTY', 'PS_SUPPLYCOST',
                 'PS_COMMENT'],
    'customer': ['C_CUSTKEY', 'C_NAME', 'C_ADDRESS', 'C_NATIONKEY', 'C_PHONE',
                 'C_ACCTBAL', 'C_MKTSEGMENT', 'C_COMMENT'],
    'orders':   ['O_ORDERKEY', 'O_CUSTKEY', 'O_ORDERSTATUS', 'O_TOTALPRICE',
                 'O_ORDERDATE', 'O_ORDERPRIORITY', 'O_CLERK', 'O_SHIPPRIORITY',
                 'O_COMMENT'],
    'lineitem': ['L_ORDERKEY', 'L_PARTKEY', 'L_SUPPKEY', 'L_LINENUMBER',
                 'L_QUANTITY', 'L_EXTENDEDPRICE', 'L_DISCOUNT', 'L_TAX',
                 'L_RETURNFLAG', 'L_LINESTATUS', 'L_SHIPDATE', 'L_COMMITDATE',
                 'L_RECEIPTDATE', 'L_SHIPINSTRUCT', 'L_SHIPMODE', 'L_COMMENT'],
}

# Field -> encoding.  Anything not listed is treated as a string (hashed).
INT_FIELDS = {
    'N_NATIONKEY', 'N_REGIONKEY', 'R_REGIONKEY', 'P_PARTKEY', 'P_SIZE',
    'S_SUPPKEY', 'S_NATIONKEY', 'PS_PARTKEY', 'PS_SUPPKEY', 'PS_AVAILQTY',
    'C_CUSTKEY', 'C_NATIONKEY', 'O_ORDERKEY', 'O_CUSTKEY', 'O_SHIPPRIORITY',
    'L_ORDERKEY', 'L_PARTKEY', 'L_SUPPKEY', 'L_LINENUMBER',
}
DECIMAL_FIELDS = {
    'P_RETAILPRICE', 'S_ACCTBAL', 'PS_SUPPLYCOST', 'C_ACCTBAL', 'O_TOTALPRICE',
    'L_QUANTITY', 'L_EXTENDEDPRICE', 'L_DISCOUNT', 'L_TAX',
}
DATE_FIELDS = {'O_ORDERDATE', 'L_SHIPDATE', 'L_COMMITDATE', 'L_RECEIPTDATE'}

# nation/supplier/part are duplicated for self-joins with a per-copy prefix.
ALIASES = {
    'nation':   [('N1_', 'nation1'), ('N2_', 'nation2')],
    'supplier': [('S1_', 'supplier1'), ('S2_', 'supplier2')],
    'part':     [('P1_', 'part1'), ('P2_', 'part2')],
}


def string_to_int(value: str) -> int:
    if value is None or value == '':
        return 0
    return int(hashlib.md5(value.encode()).hexdigest()[:8], 16) % (JOIN_ATTR_MAX + 1)


def date_to_days(value: str) -> int:
    try:
        return (datetime.strptime(value, "%Y-%m-%d") - EPOCH).days
    except ValueError:
        return 0


def decimal_to_int(value: str) -> int:
    try:
        return int(float(value) * DECIMAL_SCALE)
    except ValueError:
        return 0


def encode(field: str, value: str) -> int:
    if field in INT_FIELDS:
        try:
            return int(value)
        except ValueError:
            return 0
    if field in DECIMAL_FIELDS:
        return decimal_to_int(value)
    if field in DATE_FIELDS:
        return date_to_days(value)
    return string_to_int(value)


def read_tbl(tbl_path: Path, schema):
    """Yield encoded integer rows from a pipe-delimited .tbl file."""
    with open(tbl_path, newline='') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line:
                continue
            # dbgen writes a trailing '|', so the split has one extra empty cell.
            cells = line.split('|')[:len(schema)]
            if len(cells) < len(schema):
                continue
            yield [encode(field, cell) for field, cell in zip(schema, cells)]


def write_csv(out_path: Path, header, rows):
    with open(out_path, 'w', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(header)
        w.writerows(rows)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    tbl_dir = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    for table, schema in SCHEMAS.items():
        tbl = tbl_dir / f"{table}.tbl"
        if not tbl.exists():
            print(f"ERROR: missing {tbl}", file=sys.stderr)
            return 1
        rows = list(read_tbl(tbl, schema))
        write_csv(out_dir / f"{table}.csv", schema, rows)
        print(f"  {table}.csv  ({len(rows)} rows)")

        # Emit the self-join alias copies with prefixed column names.
        for prefix, alias in ALIASES.get(table, []):
            write_csv(out_dir / f"{alias}.csv",
                      [prefix + c for c in schema], rows)
            print(f"  {alias}.csv  ({len(rows)} rows, {prefix}* columns)")

    print(f"Wrote 14 integer CSV tables to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
