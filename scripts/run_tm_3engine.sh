#!/bin/bash
# TPC-H multi-way joins (TM1/TM2/TM3) across all three engines.
#
#   single-ecall   this repo -- one ecall runs the whole oblivious pipeline
#   batching       same algorithm, host-orchestrated, fixed-size enclave batches
#   OBLIVIATOR     third-party binary equi-join operator (USENIX Security '25),
#                  driven multi-way by our wrapper
#
# ENGINE AVAILABILITY
#   OBLIVIATOR is an OpenEnclave enclave created with flags=0 -- it has no
#   simulation path, so it requires real SGX hardware.  Under SGX_MODE=SIM this
#   script runs the two Intel-SDK engines and says so, rather than pretending a
#   three-way comparison happened.
#
# PARAMETERS THAT SILENTLY CHANGE BASELINE RESULTS -- both are recorded per run:
#   ELEM_SIZE            OBLIVIATOR's element width.  Its oblivious sort moves
#                        whole elements, so an over-provisioned element makes it
#                        look slower.  320 B is the smallest that fits every join
#                        order evaluated here.
#   OBLIVIATOR_MAX_BUF   host<->enclave buffer.  It must hold the OUTPUT too, and
#                        the enclave does NOT bounds-check it: too small yields an
#                        in-enclave fault and a silent 0-byte result, not an error.
#
# Env:
#   SGX_MODE   HW | SIM              (default HW; SIM skips OBLIVIATOR)
#   SCALE      0_001 | 0_01 | 0_1    (default 0_001)
#   REPS       trials per cell        (default 3)
#   OBLIVIATOR_FIX  1 = use the corrected OBLIVIATOR build
set -u
cd "$(dirname "$0")/.."
. scripts/run_env.sh 2>/dev/null || true
. scripts/measure_lib.sh 2>/dev/null || true

SGX_MODE="${SGX_MODE:-HW}"
SCALE="${SCALE:-0_001}"
REPS="${REPS:-3}"
# Overridable so the TPC-H BAND joins can be measured through the same driver:
#   QUERIES="tpch_tb1 tpch_tb2" scripts/run_tm_3engine.sh
#   QUERIES="tpch_bm1" scripts/run_tm_3engine.sh
# OBLIVIATOR is skipped automatically for any band query -- it implements binary
# EQUALITY joins only and cannot express a band predicate at all.
QUERIES="${QUERIES:-tpch_tm1 tpch_tm2 tpch_tm3}"
LOG="${EJ_LOG_DIR:-output/runs/logs}"
mkdir -p "$LOG"
SUMMARY="$LOG/tm_3engine_${SCALE}.log"
: > "$SUMMARY"

ENC="input/encrypted/data_${SCALE}"
PLAIN="input/plaintext/data_${SCALE}"
[ -d "$PLAIN" ] || { echo "no data for scale $SCALE -- see scripts/gen_all_data.sh" | tee -a "$SUMMARY"; exit 1; }
[ -d "$ENC" ] || ENC="$PLAIN"      # regenerated scales are plaintext-only

say() { echo "$@" | tee -a "$SUMMARY"; }

say "TM three-engine comparison"
say "  scale=$SCALE  reps=$REPS  SGX_MODE=$SGX_MODE"
say ""

# ---------------------------------------------------------------- 1. ours
say "######## single-ecall"
for q in $QUERIES; do
    for r in $(seq 1 "$REPS"); do
        OUT="${EJ_RESULT_DIR:-output/runs/results}/${q}_${SCALE}_se_r${r}.csv"
        mkdir -p "$(dirname "$OUT")"
        L="$LOG/${q}_${SCALE}_se_r${r}.log"
        T0=$(date +%s.%N)
        ./sgx_join "input/queries/${q}.sql" "$ENC" "$OUT" > "$L" 2>&1
        RC=$?; T1=$(date +%s.%N)
        WALL=$(python3 -c "print('%.2f'%($T1-$T0))")
        INIT=$(grep -oP 'ENCLAVE_INIT:\s*\K[0-9.]+' "$L" | head -1)
        ECALL=$(grep -oP 'ECALL_TOTAL:\s*\K[0-9.]+' "$L" | head -1)
        EXEC=$(python3 -c "i='${INIT:-}'; print('%.2f'%(${WALL}-float(i)) if i else 'n/a')")
        V="-"
        if [ "$RC" -eq 0 ] && [ "$r" -eq 1 ]; then
            EJ_VERIFY=FAIL; ej_verify "$q" "$PLAIN" "$OUT"; V="$EJ_VERIFY"
        fi
        say "  $(printf '%-9s r%d  total=%8s init=%8s exec=%8s ecall=%8s %s' \
                 "$q" "$r" "$WALL" "${INIT:-n/a}" "$EXEC" "${ECALL:-n/a}" "$V")"
    done
done
say ""

# ------------------------------------------------------------ 2. batching
say "######## batching"
BATCH="${BATCHING_SRC:-baselines/batching}"
if [ ! -x "$BATCH/sgx_app" ]; then
    say "  SKIPPED -- not built.  Run: baselines/build_batching.sh"
else
    for q in $QUERIES; do
        for r in $(seq 1 "$REPS"); do
            OUT="$PWD/${EJ_RESULT_DIR:-output/runs/results}/${q}_${SCALE}_ba_r${r}.csv"
            L="$LOG/${q}_${SCALE}_ba_r${r}.log"
            T0=$(date +%s.%N)
            # batching loads its signed enclave from its own cwd
            ( cd "$BATCH" && ./sgx_app "$PWD/../../input/queries/${q}.sql" \
                  "$PWD/../../$ENC" "$OUT" ) > "$L" 2>&1
            RC=$?; T1=$(date +%s.%N)
            # Verify the baseline on the same terms as the single-ecall side.  A
            # timing for an engine whose output was never checked is not a
            # comparison -- and a silently truncated result reads as "fast".
            V="-"
            if [ "$RC" -eq 0 ] && [ "$r" -eq 1 ]; then
                EJ_VERIFY=FAIL; ej_verify "$q" "$PLAIN" "$OUT"; V="$EJ_VERIFY"
            fi
            say "  $(printf '%-9s r%d  total=%8s %s' "$q" "$r" \
                     "$(python3 -c "print('%.2f'%($T1-$T0))")" "$V")"
        done
    done
fi
say ""

# ---------------------------------------------------------- 3. OBLIVIATOR
say "######## OBLIVIATOR"
OBL="${OBLIVIATOR_SRC:-external/Parallel-join-ae}"
if [ "$SGX_MODE" = "SIM" ]; then
    say "  SKIPPED -- no simulation path upstream (oe_create_parallel_enclave"
    say "  is called with flags=0), so it needs real SGX.  SIM compares two"
    say "  engines; run on SGX hardware for the three-way comparison."
elif [ ! -x "$OBL/join/host/parallel" ]; then
    say "  SKIPPED -- not built.  Run: baselines/build_obliviator.sh"
else
    ELEM=$(grep -oP '#define ELEM_SIZE \K[0-9]+' "$OBL/join/common/elem_t.h" 2>/dev/null)
    say "  ELEM_SIZE=$ELEM  OBLIVIATOR_FIX=${OBLIVIATOR_FIX:-0}"
    # Its harness is driven by a per-scale config listing the TM equality
    # queries; a band query has no entry there and could not run anyway.
    case "$QUERIES" in
        *tb1*|*tb2*|*bm1*)
            say "  SKIPPED for band queries -- OBLIVIATOR implements binary"
            say "  EQUALITY joins only, so tb1/tb2/bm1 cannot be expressed for it."
            say ""
            say "summary: $SUMMARY"
            exit 0 ;;
    esac
    CFG="baselines/obliviator/configs/test_config_tm123_${SCALE}.txt"
    if [ -f "$CFG" ]; then
        ( cd baselines/obliviator && OBLIVIATOR_SRC="$PWD/../../$OBL" \
            python3 run_tests.py "../../$CFG" --no-resume ) 2>&1 | tee -a "$SUMMARY"
    else
        say "  no config for scale $SCALE ($CFG)"
    fi
fi

say ""
say "summary: $SUMMARY"
