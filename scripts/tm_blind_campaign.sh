#!/bin/bash
# Full TM comparison with OUR side under the same blind-order model we apply to
# the baseline: every rooted orientation of the join tree, averaged.
#
# Cells match the published head-to-head: TM1/TM2/TM3 @ 0.001 and 0.01, plus
# TM1 @ 0.1.  TM2/TM3 @ 0.1 are omitted because Obliviator cannot run them.
#
# Usage: scripts/tm_blind_campaign.sh [reps]

set -u
cd "$(dirname "$0")/.."
. scripts/run_env.sh 2>/dev/null || true

REPS="${1:-3}"
LOG="${EJ_LOG_DIR:-output/runs/logs}"
mkdir -p "$LOG"
SUMMARY="$LOG/tm_blind_campaign.log"
: > "$SUMMARY"

# The repo ships SF 0.001 and 0.01.  SF 0.1 is regenerate-only (see REPRODUCE.md):
# produce it with scripts/gen_all_data.sh, or point EJ_ENCRYPTED_0_1 at an
# existing copy.  Cells whose data is absent are skipped, not silently wrong.
data_dir() {
    local s="$1"
    if [ -d "input/encrypted/data_${s}" ]; then
        echo "input/encrypted/data_${s}"
    else
        local var="EJ_ENCRYPTED_${s}"
        echo "${!var:-input/encrypted/data_${s}}"
    fi
}

run_cell() {
    local q="$1" scale="$2"
    local d; d=$(data_dir "$scale")
    if [ ! -d "$d" ]; then
        echo "############ $q @ $scale -- SKIPPED, no data at $d" | tee -a "$SUMMARY"
        return
    fi
    echo "############ $q @ $scale" | tee -a "$SUMMARY"
    COOLDOWN=0 scripts/tree_variants.sh \
        "input/queries/${q}.sql" "$d" "$REPS" 2>&1 | tee -a "$SUMMARY"
    echo | tee -a "$SUMMARY"
}

for scale in 0_001 0_01; do
    for q in tpch_tm1 tpch_tm2 tpch_tm3; do
        run_cell "$q" "$scale"
    done
done
run_cell tpch_tm1 0_1

echo "CAMPAIGN-DONE" | tee -a "$SUMMARY"
