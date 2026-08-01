#!/bin/bash
# Higgs multi-way band-join correctness series: run every higgs_* query through
# sgx_join and verify tuple-exact against SQLite.  Mirrors the twitter_* series
# naming but is correctness-oriented (the NEW-vs-ORIG timing campaign is a
# separate, later pass).
#
# Query family -> data directory (at the chosen day-ladder SCALE 1d/2d/3d):
#   higgs_tw*     -> input/plaintext/higgs_<scale>
#   higgs_hop*    -> input/plaintext/higgs_hops_<scale>
#   higgs_btree*  -> input/plaintext/higgs_hops_<scale>
# All queries use fixed band width W=4 (see REPRODUCE.md, "Higgs workload").
#
# Usage:  SCALE=1d scripts/higgs_series.sh      (default 1d; also 2d, 3d)
set -u
cd "$(dirname "$0")/.."
. scripts/run_env.sh
SCALE="${SCALE:-1d}"
TW="input/plaintext/higgs_${SCALE}"
HOPS="input/plaintext/higgs_hops_${SCALE}"

pass=0; fail=0
run() {   # query datadir
    local q="$1" d="$2"
    [ -f "input/queries/${q}.sql" ] || return 0
    local out="$EJ_RESULT_DIR/hs_${q}.csv" log="$EJ_LOG_DIR/hs.txt"
    ./sgx_join "input/queries/${q}.sql" "$d" "$out" > "$log" 2>&1
    local r v
    r=$(grep -a -o "result: [0-9]* rows" "$log")
    v=$(python3 tests/e2e_sqlite_compare.py "input/queries/${q}.sql" "$d" "$out" 2>&1)
    if echo "$v" | grep -q "PASS"; then
        echo "  PASS  ${q}  (${r})"; pass=$((pass+1))
    else
        echo "  FAIL  ${q}  (${r})  -- $(echo "$v" | tail -c 100)"; fail=$((fail+1))
    fi
}

runglob() {   # datadir  glob...
    local d="$1"; shift
    for f in "$@"; do
        [ -e "$f" ] || continue
        run "$(basename "$f" .sql)" "$d"
    done
}

echo "=== Higgs correctness series (scale ${SCALE}) ==="
# See REPRODUCE.md, "Higgs workload", for what each group means.
echo "-- A. Application band joins (follower graph + time band) --"
runglob "$TW" input/queries/higgs_tw4_*.sql input/queries/higgs_tw5_*.sql
echo "-- B. Band-join scalability microbenchmarks (self-band, k-scaling) --"
runglob "$HOPS" input/queries/higgs_hop*.sql input/queries/higgs_btree*.sql
runglob "$TW"   input/queries/higgs_tw3_*.sql

echo "=== ${pass} passed, ${fail} failed ==="
[ "$fail" -eq 0 ] && echo "HIGGS SERIES DONE" || { echo "HIGGS SERIES FAILED"; exit 1; }
