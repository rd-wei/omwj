#!/bin/bash
# Higgs multi-way BAND joins across the two engines that can express them.
#
# OBLIVIATOR is absent by construction, not by omission: it implements a binary
# EQUALITY join, so no query in this family can be written for it at all.  That
# is why this is a two-engine comparison while TM is three.
#
# Env:
#   SCALE   1d | 2d | 3d   (default 1d)
#   REPS    trials per cell (default 3)
#   QUICK   1 = the three application queries only, skipping the k=2..9 sweep
set -u
cd "$(dirname "$0")/.."
. scripts/run_env.sh 2>/dev/null || true
. scripts/measure_lib.sh 2>/dev/null || true

SCALE="${SCALE:-1d}"
REPS="${REPS:-3}"
LOG="${EJ_LOG_DIR:-output/runs/logs}"
mkdir -p "$LOG"
SUMMARY="$LOG/higgs_2engine_${SCALE}.log"
: > "$SUMMARY"

APP="tw3 tw4 tw5"
SWEEP=""
[ "${QUICK:-0}" = "1" ] || SWEEP="hop1 hop2 hop3 hop4 hop5 hop6 hop7 hop8 \
btree1 btree2 btree3 btree4 btree5 btree6 btree7 btree8"

say() { echo "$@" | tee -a "$SUMMARY"; }

say "Higgs two-engine comparison (band joins -- OBLIVIATOR N/A)"
say "  scale=$SCALE  reps=$REPS"
say ""

data_for() {   # tw* use the follower graph; hop/btree use the k=9 user chain
    case "$1" in
        tw*) echo "input/plaintext/higgs_${SCALE}" ;;
        *)   echo "input/plaintext/higgs_hops_${SCALE}" ;;
    esac
}

say "######## single-ecall"
for q in $APP $SWEEP; do
    D=$(data_for "$q")
    Q="input/queries/higgs_${q}_w4.sql"
    [ -d "$D" ] && [ -f "$Q" ] || { say "  $q  SKIPPED (no data/query)"; continue; }
    for r in $(seq 1 "$REPS"); do
        OUT="${EJ_RESULT_DIR:-output/runs/results}/higgs_${q}_${SCALE}_se_r${r}.csv"
        mkdir -p "$(dirname "$OUT")"
        L="$LOG/higgs_${q}_${SCALE}_se_r${r}.log"
        T0=$(date +%s.%N)
        ./sgx_join "$Q" "$D" "$OUT" > "$L" 2>&1
        RC=$?; T1=$(date +%s.%N)
        WALL=$(python3 -c "print('%.2f'%($T1-$T0))")
        INIT=$(grep -oP 'ENCLAVE_INIT:\s*\K[0-9.]+' "$L" | head -1)
        ECALL=$(grep -oP 'ECALL_TOTAL:\s*\K[0-9.]+' "$L" | head -1)
        EXEC=$(python3 -c "i='${INIT:-}'; print('%.2f'%(${WALL}-float(i)) if i else 'n/a')")
        ROWS=$(( $(wc -l < "$OUT" 2>/dev/null || echo 1) - 1 ))
        V="-"
        # Higgs runs on plaintext, so ground truth is the same directory.
        if [ "$RC" -eq 0 ] && [ "$r" -eq 1 ] && [ "$SCALE" = "1d" ]; then
            EJ_VERIFY=FAIL; ej_verify "higgs_${q}_w4" "$D" "$OUT"; V="$EJ_VERIFY"
        fi
        say "  $(printf '%-9s r%d  rows=%-9s total=%8s init=%8s exec=%8s ecall=%8s %s' \
                 "$q" "$r" "$ROWS" "$WALL" "${INIT:-n/a}" "$EXEC" "${ECALL:-n/a}" "$V")"
    done
done
say ""

say "######## batching"
BATCH="${BATCHING_SRC:-baselines/batching}"
if [ ! -x "$BATCH/sgx_app" ]; then
    say "  SKIPPED -- not built.  Run: baselines/build_batching.sh"
else
    for q in $APP $SWEEP; do
        D=$(data_for "$q"); Q="input/queries/higgs_${q}_w4.sql"
        [ -d "$D" ] && [ -f "$Q" ] || continue
        for r in $(seq 1 "$REPS"); do
            OUT="$PWD/${EJ_RESULT_DIR:-output/runs/results}/higgs_${q}_${SCALE}_ba_r${r}.csv"
            L="$LOG/higgs_${q}_${SCALE}_ba_r${r}.log"
            T0=$(date +%s.%N)
            ( cd "$BATCH" && ./sgx_app "$PWD/../../$Q" "$PWD/../../$D" "$OUT" ) > "$L" 2>&1
            RC=$?; T1=$(date +%s.%N)
            # Verify the baseline on the same terms as the single-ecall side.  A
            # timing for an engine whose output was never checked is not a
            # comparison -- and a silently truncated result reads as "fast".
            V="-"
            if [ "$RC" -eq 0 ] && [ "$r" -eq 1 ] && [ "$SCALE" = "1d" ]; then
                EJ_VERIFY=FAIL; ej_verify "higgs_${q}_w4" "$D" "$OUT"; V="$EJ_VERIFY"
            fi
            say "  $(printf '%-9s r%d  total=%8s %s' "$q" "$r" \
                     "$(python3 -c "print('%.2f'%($T1-$T0))")" "$V")"
        done
    done
fi

say ""
say "summary: $SUMMARY"
