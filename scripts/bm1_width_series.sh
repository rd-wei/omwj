#!/bin/bash
# BM1 band-width series (5-way multi-way band join, band width W):
#   W in {100,400,1000,10000} at SF 0.01, and {100,400,1000} at SF 0.1
#   (W=10000 @ 0.1 expands to ~38 GB -- infeasible for both systems, skipped).
#
# Single-ecall is measured with REPS trials per cell (mean +/- stddev) at the
# paper-wide 4 GB heap.  The batching engine is supplementary and single-pass;
# set BATCHING=1 to run it too.  Never two EPC-heavy jobs at once -- everything
# here is sequential.
#
# SF 0.1 data is not committed in this repo (regenerate-only, see REPRODUCE.md);
# point DATA_0_1 at a directory that has it.
#
# Usage:  REPS=3 scripts/bm1_width_series.sh
#         REPS=3 DATA_0_1=/path/to/regenerated/encrypted/data_0_1 scripts/bm1_width_series.sh
#         BATCHING=1 scripts/bm1_width_series.sh
set -u
cd "$(dirname "$0")/.."
. scripts/measure_lib.sh

NEW_DIR="$(pwd)"
REPS="${REPS:-1}"
HEAP="${HEAP:-4g}"
BATCHING="${BATCHING:-0}"
# The batching engine ships in this repo; build it with baselines/build_batching.sh.
ORIG_DIR="${ORIG_DIR:-$NEW_DIR/baselines/batching}"
DATA_0_1="${DATA_0_1:-$NEW_DIR/input/encrypted/data_0_1}"

query_file() {   # W -> query stem
    case "$1" in
        10000) echo tpch_bm1 ;;
        *)     echo "tpch_bm1_band$1" ;;
    esac
}

echo "=== Build HW (VECTORIZE=on), sign heap ${HEAP} ==="
make clean > /dev/null 2>&1
make SGX_MODE=HW VECTORIZE=on SGX_DEBUG=0 \
     Enclave_Config="enclave/trusted/configs/heap_${HEAP}.xml" \
     > "$EJ_LOG_DIR/bm1_build.log" 2>&1 \
    || { echo "BUILD FAILED"; tail -5 "$EJ_LOG_DIR/bm1_build.log"; exit 1; }

echo "=== BM1 width series: heap=${HEAP} reps=${REPS} commit=$(git rev-parse --short HEAD) ==="
ej_header

for W in 100 400 1000 10000; do
    q=$(query_file "$W")
    ej_trials "$REPS" "$q" "input/encrypted/data_0_01" "W=${W}@0.01"
    ej_verify "$q" "input/plaintext/data_0_01"
    [ "$EJ_VERIFY" = PASS ] || echo "  !! W=${W}@0.01 VERIFY FAILED"
done

if [ -d "$DATA_0_1" ]; then
    for W in 100 400 1000; do
        q=$(query_file "$W")
        ej_trials "$REPS" "$q" "$DATA_0_1" "W=${W}@0.1"
    done
else
    echo "  SKIP  SF 0.1 (no $DATA_0_1 -- see REPRODUCE.md, regenerate-only)"
fi

# Supplementary: the batching engine, single-pass.  It loads its signed enclave
# from its own cwd, so it must run from ORIG_DIR.
if [ "$BATCHING" = "1" ]; then
    if [ ! -x "$ORIG_DIR/sgx_app" ]; then
        echo "  SKIP batching -- no sgx_app in $ORIG_DIR (run baselines/build_batching.sh)"
        echo "SERIES DONE"; exit 0
    fi
    echo "-- Batching engine (supplementary, MAX_BATCH_SIZE=8000, single pass) --"
    for W in 100 400 1000 10000; do
        q=$(query_file "$W")
        echo "=== batching W=${W} @0.01 ==="
        ( cd "$ORIG_DIR" && /usr/bin/time -f "ORIG_WALL=%e s" \
            ./sgx_app "${NEW_DIR}/input/queries/${q}.sql" \
            "input/encrypted/data_0_01" "${NEW_DIR}/${EJ_RESULT_DIR}/orig_bw_${W}_0_01.csv" 2>&1 ) \
            > "$EJ_LOG_DIR/bm1_orig.txt" 2>&1
        grep -a "PHASE_TIMING\|Result:\|ORIG_WALL" "$EJ_LOG_DIR/bm1_orig.txt"
    done
fi

echo "SERIES DONE"
