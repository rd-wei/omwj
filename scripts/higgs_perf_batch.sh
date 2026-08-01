#!/bin/bash
# Batching-engine HW performance for the Higgs multi-way band joins, for the
# single-ecall-vs-batching comparison (supplementary material).  Runs sgx_app
# (batched ecalls, external AES-CTR data) on THIS repo's higgs queries +
# plaintext data by absolute path.  The engine's sources ship in
# baselines/batching/; build it with baselines/build_batching.sh.
#
# Reports end-to-end wall, compute (PHASE_TIMING Total), init (= e2e - compute),
# output rows, and -- for the system-cost table (F7) -- peak host RSS and the
# ECALL count / byte volume across the enclave boundary.
#
# The batching engine issues tens of thousands of ecalls per query and is much
# slower than single-ecall.  TIMEOUT caps each query; set TIMEOUT=0 to run
# uncapped (needed for the tree e=8 @ 3d cell, F6).
#
# Usage:  SCALE=3d scripts/higgs_perf_batch.sh
#         SCALE=3d TIMEOUT=0 QUERIES="higgs_btree8_w4" scripts/higgs_perf_batch.sh
#         SCALE=1d ORIG_DIR=/path/to/other/checkout scripts/higgs_perf_batch.sh
set -u
cd "$(dirname "$0")/.."
. scripts/run_env.sh
NEW_DIR="$(pwd)"
ORIG_DIR="${ORIG_DIR:-$NEW_DIR/baselines/batching}"
SCALE="${SCALE:-1d}"          # day ladder: 1d / 2d / 3d
TIMEOUT="${TIMEOUT:-1800}"    # 0 = uncapped
W=4                           # fixed band width across all scales
TW="${NEW_DIR}/input/plaintext/higgs_${SCALE}"
HOPS="${NEW_DIR}/input/plaintext/higgs_hops_${SCALE}"

[ -x "$ORIG_DIR/sgx_app" ] || { echo "no sgx_app in $ORIG_DIR"; exit 1; }
echo "=== Batching (sgx_app), scale ${SCALE}, timeout ${TIMEOUT:-none}s ==="
# MAX_BATCH_SIZE is a build-time -D flag (the header default 2000 is #ifndef-guarded
# and overridden at compile). This build is MAX_BATCH_SIZE=8000 -- do not read it
# from the header source, which still shows the 2000 default.
echo "MAX_BATCH_SIZE=8000 (build-time override)"

printf '%-18s %12s %9s %9s %10s %10s %12s\n' \
    "query" "rows" "e2e_s" "init_s" "compute_s" "rss_MiB" "ecalls"
run() {   # query datadir
    local q="$1" d="$2" out="${NEW_DIR}/${EJ_RESULT_DIR}/hpb_${q}.csv"
    local log="$EJ_LOG_DIR/hpb_${q}.txt" tfile="$EJ_RESULT_DIR/hpb_${q}.time"
    local tocmd=""
    [ "$TIMEOUT" = "0" ] || tocmd="timeout $TIMEOUT"
    # sgx_app loads its signed enclave from its own cwd, so run from ORIG_DIR.
    # -v gives Maximum resident set size for the host-RSS cost table.
    /usr/bin/time -v -o "$tfile" bash -c \
        "cd '$ORIG_DIR' && $tocmd ./sgx_app '${NEW_DIR}/input/queries/${q}.sql' '$d' '$out'" \
        > "$log" 2>&1
    local rc=$? rows pt wall init rss ecalls
    rows=$(grep -a -oE "Result: [0-9]+" "$log" | grep -oE "[0-9]+" | head -1)
    pt=$(grep -a -oE "PHASE_TIMING:.*Total=[0-9.]+" "$log" | grep -oE "Total=[0-9.]+$" | grep -oE "[0-9.]+")
    ecalls=$(grep -a -oE "ECALL_COUNT: [0-9]+" "$log" | grep -oE "[0-9]+" | head -1)
    wall=$(grep -a "Elapsed (wall clock) time" "$tfile" | grep -oE "[0-9:.]+$")
    rss=$(grep -a "Maximum resident set size" "$tfile" | grep -oE "[0-9]+$")
    [ -n "${rss:-}" ] && rss=$(python3 -c "print(f'{${rss}/1024:.1f}')")
    # /usr/bin/time -v prints [h:]mm:ss.ss -- normalise to seconds.
    wall=$(python3 -c "
p='${wall:-0}'.split(':')
print(f'{sum(float(x)*60**i for i,x in enumerate(reversed(p))):.2f}')" 2>/dev/null)

    if [ "$rc" -eq 124 ]; then
        printf '%-18s %12s %9s %9s %10s %10s %12s\n' \
            "$q" "${rows:-?}" "timeout" "-" "-" "${rss:-?}" "${ecalls:-?}"
    elif [ -z "$pt" ]; then
        printf '%-18s %12s %9s %9s %10s %10s %12s\n' \
            "$q" "${rows:-ERR}" "${wall:-?}" "-" "ERR" "${rss:-?}" "${ecalls:-?}"
    else
        init=$(python3 -c "print(f'{${wall:-0}-${pt}:.2f}')")
        printf '%-18s %12s %9s %9s %10s %10s %12s\n' \
            "$q" "$rows" "${wall:-?}" "$init" "$pt" "${rss:-?}" "${ecalls:-?}"
    fi
}

# Query set: default is the full kept matrix; override QUERIES for a single cell.
if [ -n "${QUERIES:-}" ]; then
    for q in $QUERIES; do
        case "$q" in
            *tw*) run "$q" "$TW" ;;
            *)    run "$q" "$HOPS" ;;
        esac
    done
else
    echo "-- Application band joins --"
    for q in higgs_tw4_w${W} higgs_tw5_w${W} higgs_tw3_w${W}; do run "$q" "$TW"; done
    echo "-- Scalability: hop chain --"
    for h in 1 2 3 4 5 6 7 8; do run "higgs_hop${h}_w${W}" "$HOPS"; done
    echo "-- Scalability: binary tree --"
    for e in 1 2 3 4 5 6 7 8; do run "higgs_btree${e}_w${W}" "$HOPS"; done
fi
echo "=== batching scale=${SCALE} done ==="
