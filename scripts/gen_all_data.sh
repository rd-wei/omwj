#!/bin/bash
# Regenerate the TPC-H integer-CSV datasets at every scale, from scratch,
# starting from the official TPC-H `dbgen` tool.
#
# The committed repo already ships scales 0.001 and 0.01 as real files, so you
# only need this to (re)build them or to produce the large scale 0.1 (~45 MB
# plaintext) that is intentionally NOT committed.  The engine's table loader
# auto-detects encryption and reads these plaintext CSVs directly, so no
# separate encrypt step is required for correctness runs.
#
# Prerequisite: the TPC-H tools (dbgen).  These are distributed by the TPC
# under their own licence and cannot be redistributed here, so fetch + build
# them once:
#
#   git clone https://github.com/electrum/tpch-dbgen    # or the official TPC kit
#   cd tpch-dbgen && make
#
# then point this script at the directory containing the built `dbgen` binary
# and `dists.dss`:
#
#   DBGEN_DIR=/path/to/tpch-dbgen scripts/gen_all_data.sh
#   DBGEN_DIR=/path/to/tpch-dbgen scripts/gen_all_data.sh 0.1   # single scale
#
set -eu
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

DBGEN_DIR="${DBGEN_DIR:?set DBGEN_DIR to the directory containing the built dbgen binary}"
DBGEN="$DBGEN_DIR/dbgen"
[ -x "$DBGEN" ] || { echo "ERROR: $DBGEN not found or not executable"; exit 1; }

SCALES="${*:-0.001 0.01 0.1}"

# Regenerating a scale that ships is a trap: dbgen's string columns depend on its
# text pool, so regenerated plaintext does not match the committed
# input/encrypted/ for the same scale.  The engine reads the encrypted copy and
# SQLite reads the plaintext one, so the suite then fails with correct row counts
# and mismatched values -- a confusing failure with no obvious cause.  Refuse
# unless FORCE=1, in which case drop the stale ciphertext so run_all.sh falls
# back to plaintext instead of comparing two different datasets.
for sf in $SCALES; do
    us="${sf/./_}"
    if [ -d "$ROOT/input/encrypted/data_${us}" ] && [ "${FORCE:-0}" != "1" ]; then
        echo "ERROR: scale ${sf} ships with this artifact and has encrypted data at" >&2
        echo "       input/encrypted/data_${us}. Regenerating the plaintext would" >&2
        echo "       desynchronise the two and break the correctness suite." >&2
        echo "       Re-run with FORCE=1 to regenerate and drop the stale ciphertext." >&2
        exit 1
    fi
done

for sf in $SCALES; do
    us="${sf/./_}"                     # 0.001 -> 0_001
    out="$ROOT/input/plaintext/data_${us}"
    tmp="$(mktemp -d)"
    echo "=== scale ${sf} -> ${out} ==="
    if [ -d "$ROOT/input/encrypted/data_${us}" ]; then
        echo "    FORCE=1: removing now-stale input/encrypted/data_${us}"
        rm -rf "$ROOT/input/encrypted/data_${us}"
    fi
    # dbgen must run from its own directory (needs dists.dss); DSS_PATH sends
    # the .tbl output to our temp dir.
    ( cd "$DBGEN_DIR" && rm -f ./*.tbl && DSS_PATH="$tmp" ./dbgen -s "$sf" -f -b "$DBGEN_DIR/dists.dss" )
    python3 "$ROOT/scripts/gen_tpch_data.py" "$tmp" "$out"
    rm -rf "$tmp"
done

echo "Done. Regenerated: $SCALES"
echo "Verify e.g.:  ./sgx_join input/queries/tpch_tm1.sql input/plaintext/data_0_001 output/out.csv"
echo "        then: python3 tests/e2e_sqlite_compare.py input/queries/tpch_tm1.sql input/plaintext/data_0_001 output/out.csv"
