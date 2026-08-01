#!/bin/bash
# TPC-H multi-way (TM) Total / Init / Execution breakdown for the single-ecall
# engine, at the 4 GB heap used in every in-enclave/Obliviator comparison.
#
# This is the single-ecall half; the OBLIVIATOR half runs through its own
# harness (baselines/obliviator/run_tests.py), which reports the same three
# metrics.  scripts/run_tm_3engine.sh drives both plus batching.
#
#   Total    = whole-program wall (/usr/bin/time)
#   Init     = ENCLAVE_INIT (enclave creation; commits the whole heap through a
#              378 MB EPC on a no-EDMM kernel, so it is a platform artifact, not
#              a per-query cost)
#   Execution= Total - Init
#
# REPS>1 reports mean +/- stddev per cell.
# Every cell is verified tuple-exact against SQLite in the same pass.
#
# SF 0.1 is regenerate-only in this repo (see REPRODUCE.md), so its data lives
# elsewhere; point DATA_DIR at it.  When DATA_DIR is set it overrides the derived
# encrypted directory, and PLAIN_DIR supplies the plaintext copy for verification.
#
# Usage:  REPS=3 scripts/tm_breakdown.sh
#         REPS=3 SCALES="0_001 0_01" QUERIES="tpch_tm1 tpch_tm3" scripts/tm_breakdown.sh
#         REPS=3 SCALES=0_1 QUERIES="tpch_tm1 tpch_tm2" HEAP=4g \
#             DATA_DIR=/path/to/regenerated/encrypted/data_0_1 \
#             PLAIN_DIR=/path/to/regenerated/plaintext/data_0_1 \
#             scripts/tm_breakdown.sh
set -u
cd "$(dirname "$0")/.."
. scripts/measure_lib.sh

REPS="${REPS:-1}"
HEAP="${HEAP:-4g}"
SCALES="${SCALES:-0_001 0_01}"
QUERIES="${QUERIES:-tpch_tm1 tpch_tm2 tpch_tm3}"
DATA_DIR="${DATA_DIR:-}"
PLAIN_DIR="${PLAIN_DIR:-}"

echo "=== Build HW (VECTORIZE=on), sign heap ${HEAP} ==="
make clean > /dev/null 2>&1
make SGX_MODE=HW VECTORIZE=on SGX_DEBUG=0 \
     Enclave_Config="enclave/trusted/configs/heap_${HEAP}.xml" \
     > "$EJ_LOG_DIR/tmb_build.log" 2>&1 \
    || { echo "BUILD FAILED"; tail -5 "$EJ_LOG_DIR/tmb_build.log"; exit 1; }

echo "=== TM breakdown: heap=${HEAP} reps=${REPS} commit=$(git rev-parse --short HEAD) ==="
ej_header

for scale in $SCALES; do
    enc="${DATA_DIR:-input/encrypted/data_${scale}}"
    plain="${PLAIN_DIR:-input/plaintext/data_${scale}}"
    [ -d "$enc" ] || enc="$plain"
    if [ ! -d "$enc" ]; then
        echo "  SKIP  scale ${scale}: no data at $enc (set DATA_DIR)"
        continue
    fi
    for q in $QUERIES; do
        ej_trials "$REPS" "$q" "$enc" "${q}@${scale}"
        ej_verify "$q" "$plain"
        [ "$EJ_VERIFY" = PASS ] || echo "  !! ${q}@${scale} VERIFY FAILED"
    done
done
echo "=== done (Execution = Total - Init; Init is one-time enclave creation) ==="
