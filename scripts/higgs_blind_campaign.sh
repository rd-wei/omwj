#!/bin/bash
# Higgs under a blind join-order model: every cell re-measured across join-tree
# orientations rather than the single tree the FROM clause implies.
#
# Same argument as the TM campaign (see REPRODUCE.md, "Join order"): choosing a
# good tree needs cardinality statistics, and consulting them is a
# data-dependent decision an oblivious engine cannot make without leaking -- so
# the defensible figure is the mean over the orders it could legitimately pick.
#
# Orientation counts run 2..128 (btree8).  Full enumeration of all 370 across
# three rungs is ~83 h, dominated by btree8 @ 3d at ~34 min per run, so cells
# above the cap are SAMPLED (seeded, always including variant 0).
#
# The comparison engine here is batching, not Obliviator -- these are band
# joins, which Obliviator cannot express at all.  Batching is NOT re-run blind;
# its published numbers stay single-order.  That asymmetry is conservative for
# us: if blind ordering costs batching anything, our speedups are understated.
#
# Usage: scripts/higgs_blind_campaign.sh [cap] [reps]

set -u
cd "$(dirname "$0")/.."
. scripts/run_env.sh 2>/dev/null || true

CAP="${1:-4}"
REPS="${2:-1}"
LOG="${EJ_LOG_DIR:-output/runs/logs}"
mkdir -p "$LOG"
SUMMARY="$LOG/higgs_blind_campaign.log"
: > "$SUMMARY"

# Refuse to start if anything else is on the enclave -- concurrent HW runs
# corrupt each other's timings, and these cells are paging-sensitive.
if pgrep -x sgx_join > /dev/null; then
    echo "ABORT: an sgx_join run is already in flight" | tee -a "$SUMMARY"
    exit 1
fi

TW="tw3 tw4 tw5"
HOPS="hop1 hop2 hop3 hop4 hop5 hop6 hop7 hop8"
TREES="btree1 btree2 btree3 btree4 btree5 btree6 btree7 btree8"

run_cell() {
    local q="$1" data="$2" verify="$3"
    if [ ! -d "$data" ]; then
        echo "######## $q on $data -- SKIPPED (no data)" | tee -a "$SUMMARY"
        return
    fi
    echo "######## $q  $data  (cap $CAP)" | tee -a "$SUMMARY"
    MAX_VARIANTS="$CAP" VERIFY="$verify" COOLDOWN=0 \
        scripts/tree_variants.sh "input/queries/higgs_${q}_w4.sql" "$data" "$REPS" \
        2>&1 | tee -a "$SUMMARY"
    echo | tee -a "$SUMMARY"
}

for rung in 1d 2d 3d; do
    # Every orientation is verified against SQLite at EVERY rung.  This used to
    # be 1d-only, with a cross-variant content digest standing in at 2d/3d --
    # but agreement between orientations is not correctness (a bug that hit
    # every orientation identically would pass unnoticed), so the digest was
    # dropped in favour of real ground truth everywhere.
    #
    # The deep rungs are expensive: btree8 @ 3d is 3.4 M rows out of a 9-table
    # band join that SQLite has to compute itself.  Set VERIFY=off explicitly if
    # you need timings without correctness on a constrained machine.
    V="${VERIFY:-first}"
    for q in $TW;              do run_cell "$q" "input/plaintext/higgs_${rung}"      "$V"; done
    for q in $HOPS $TREES;     do run_cell "$q" "input/plaintext/higgs_hops_${rung}" "$V"; done
done

echo "HIGGS-BLIND-DONE" | tee -a "$SUMMARY"
