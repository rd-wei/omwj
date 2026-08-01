#!/bin/bash
# Single-ecall HW performance for the Higgs multi-way band joins.
# Builds HW (VECTORIZE=on, the optimized/deployed config), signs one enclave
# heap for the scale, then runs each kept query and reports end-to-end wall,
# enclave-creation cost (ENCLAVE_INIT), compute (= e2e - init), the output row
# count, and the in-enclave allocation high-water mark (PEAK_HEAP).
#
# Kept queries (band + multi-way, k>=3): tw3/tw4/tw5, hop1-8, btree1-8.
# All queries use the fixed band width W=4 (see REPRODUCE.md, "Higgs workload").
#
# With REPS>1 every cell is measured REPS times and reported as mean +/- stddev.
# Runs are strictly sequential: two concurrent EPC-heavy jobs corrupt each
# other's timings.
#
# Usage:  SCALE=1d scripts/higgs_perf.sh              (default 1d)
#         REPS=3 SCALE=3d scripts/higgs_perf.sh
#         SCALE=3d HEAP=8g scripts/higgs_perf.sh
set -u
cd "$(dirname "$0")/.."
. scripts/measure_lib.sh

SCALE="${SCALE:-1d}"          # day ladder: 1d / 2d / 3d
REPS="${REPS:-1}"
W=4                           # fixed band width across all scales
# One fixed heap for every query so the enclave-init cost is uniform, and the
# same 4 GB used in every in-enclave/Obliviator comparison paper-wide.  Compute
# (= e2e - init) is heap-independent anyway; 4 GB fits every case up to ~1 M
# rows (btree-8 @ 3d = 3.4 M needs more -- override HEAP=8g there).
HEAP="${HEAP:-4g}"
TW="input/plaintext/higgs_${SCALE}"
HOPS="input/plaintext/higgs_hops_${SCALE}"

echo "=== Build HW (VECTORIZE=on), sign heap ${HEAP} ==="
make clean > /dev/null 2>&1
make SGX_MODE=HW VECTORIZE=on SGX_DEBUG=0 \
     Enclave_Config="enclave/trusted/configs/heap_${HEAP}.xml" \
     > "$EJ_LOG_DIR/hp_build.log" 2>&1 \
    || { echo "BUILD FAILED"; tail -5 "$EJ_LOG_DIR/hp_build.log"; exit 1; }

echo "=== scale=${SCALE} heap=${HEAP} reps=${REPS} commit=$(git rev-parse --short HEAD) ==="
ej_header

# Time, then verify the output the last rep left on disk.  Verification runs
# AFTER ej_trials so it never enters the measured region (same order as
# tm_breakdown.sh).  Higgs data is plaintext, so the input directory is also the
# ground truth.  VERIFY=off skips it when you only want timings.
cell() {   # query datadir
    ej_trials "$REPS" "$1" "$2"
    [ "${VERIFY:-on}" = "off" ] && return
    EJ_VERIFY=FAIL; ej_verify "$1" "$2"
    [ "$EJ_VERIFY" = PASS ] || echo "  !! $1 @ ${SCALE} VERIFY: $EJ_VERIFY"
}

echo "-- Application band joins (graph + time band) --"
for q in higgs_tw4_w${W} higgs_tw5_w${W} higgs_tw3_w${W}; do
    cell "$q" "$TW"
done
echo "-- Scalability: hop chain (k=2..9) --"
for h in 1 2 3 4 5 6 7 8; do
    cell "higgs_hop${h}_w${W}" "$HOPS"
done
echo "-- Scalability: binary tree (k=2..9) --"
for e in 1 2 3 4 5 6 7 8; do
    cell "higgs_btree${e}_w${W}" "$HOPS"
done
echo "=== done: scale=${SCALE} heap=${HEAP} reps=${REPS} (compute = e2e - init) ==="
