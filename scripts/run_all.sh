#!/bin/bash
# One-command correctness reproduction for the single-ecall oblivious-join
# engine.  Builds (if needed), signs a 2 GB enclave, then runs a suite of
# queries and verifies every result multiset tuple-exact against SQLite on the
# committed plaintext data.  Works in HW mode (real SGX) or SIM mode (any x86,
# incl. emulated on Apple Silicon).  Intended entrypoint of the Docker image.
#
# The table loader auto-detects per-file whether a CSV is encrypted, so TPC-H
# runs feed sgx_join the *encrypted* directory (exercising the AES decrypt
# path) and verify against the *plaintext* one; the Twitter graph ships in
# plaintext only, so both sides use the plaintext directory.
#
# Env:
#   SGX_MODE   HW | SIM   (default SIM)
#   VECTORIZE  on | off   (default: off for SIM, on for HW -- see Makefile)
#   SCALE      0_001 | 0_01 | 0_1   (default 0_001; 0_1 must be regenerated
#              first, see scripts/gen_all_data.sh)
#   HEAP       128m..8g   (enclave heap config, default 2g; 0_1 needs >=4g)
#   QUICK      1          (smoke test: tm1@0.001 only, ~30 s)
set -u
cd "$(dirname "$0")/.." || exit 1
. scripts/run_env.sh

SGX_MODE="${SGX_MODE:-SIM}"
SCALE="${SCALE:-0_001}"
HEAP="${HEAP:-2g}"

echo "=== Building (SGX_MODE=$SGX_MODE, heap=$HEAP) ==="
make SGX_MODE="$SGX_MODE" SGX_DEBUG=0 ${VECTORIZE:+VECTORIZE=$VECTORIZE} \
     Enclave_Config="enclave/trusted/configs/heap_${HEAP}.xml" || exit 1

PLAIN="input/plaintext/data_${SCALE}"
# TPC-H ships plaintext+encrypted for 0.001/0.01 (run on encrypted to exercise
# the decrypt path); a regenerated scale (e.g. 0.1 via scripts/gen_all_data.sh)
# is plaintext-only, so fall back to plaintext when no encrypted dir exists.
ENC="input/encrypted/data_${SCALE}"
[ -d "$ENC" ] || ENC="$PLAIN"
pass=0; fail=0

run() {   # query  sgx_input_dir  compare_dir
    local q="$1" sgxdir="$2" cmpdir="$3"
    local out="$EJ_RESULT_DIR/ra_${q}.csv" log="$EJ_LOG_DIR/ra.txt"
    ./sgx_join "input/queries/${q}.sql" "$sgxdir" "$out" > "$log" 2>&1
    local r v
    r=$(grep -a -o "result: [0-9]* rows" "$log")
    v=$(python3 tests/e2e_sqlite_compare.py "input/queries/${q}.sql" "$cmpdir" \
        "$out" 2>&1)
    if echo "$v" | grep -q "PASS\|MATCH"; then
        echo "  PASS  ${q}  (${r})"; pass=$((pass+1))
    else
        echo "  FAIL  ${q}  (${r})  -- $(echo "$v" | tail -c 120)"; fail=$((fail+1))
    fi
}

# QUICK smoke test: confirm the image works without committing to a full run.
if [ "${QUICK:-0}" = "1" ]; then
    echo "=== Quick smoke test (tm1 @ 0.001) ==="
    run tpch_tm1 "input/encrypted/data_0_001" "input/plaintext/data_0_001"
    echo "=== ${pass} passed, ${fail} failed ==="
    [ "$fail" -eq 0 ] && { echo "QUICK CHECK PASSED"; exit 0; } || { echo "QUICK CHECK FAILED"; exit 1; }
fi

# TM1/TM2/TM3 are the TPC-H multi-way joins the paper reports; nothing else
# ships, so nothing else runs here.
#
# tm3 at scale 0.1 does not complete within 2 h at a 16 GB heap (no OOM was
# observed -- see REPRODUCE.md "Known limits"), so it is left out of the default
# 0.1 run rather than reporting a failure the evaluator cannot act on.
case "$SCALE" in
    0_1) QUERIES="tpch_tm1 tpch_tm2" ;;
    *)   QUERIES="tpch_tm1 tpch_tm2 tpch_tm3" ;;
esac

echo "=== Correctness suite (scale ${SCALE}, verified vs SQLite) ==="
for q in $QUERIES; do run "$q" "$ENC" "$PLAIN"; done

# Higgs multi-way band joins (real activity-time band, fixed W=4). tw4 is the
# flagship application band join (follower edges + band); tw3 is a k=3 band
# chain. Pinned to the 1-day rung; see REPRODUCE.md ("Higgs workload") for the
# full scale ladder (1d/2d/3d) and taxonomy.
if [ -d "input/plaintext/higgs_1d" ]; then
    run higgs_tw4_w4  input/plaintext/higgs_1d input/plaintext/higgs_1d
    run higgs_tw3_w4  input/plaintext/higgs_1d input/plaintext/higgs_1d
else
    echo "  SKIP  higgs  (no input/plaintext/higgs_1d)"
fi

echo "=== ${pass} passed, ${fail} failed ==="
if [ "$fail" -ne 0 ]; then echo "SOME CHECKS FAILED"; exit 1; fi
echo "ALL CORRECTNESS CHECKS PASSED"

# ---------------------------------------------------------------------------
# FULL=1 additionally runs the cross-engine measurement campaigns.  These are
# timings, so they are only meaningful on real SGX -- in SIM they would produce
# numbers that mean nothing.  They also take hours, which is why they are opt-in.
# ---------------------------------------------------------------------------
if [ "${FULL:-0}" = "1" ]; then
    if [ "$SGX_MODE" != "HW" ]; then
        echo
        echo "=== FULL=1 ignored: timings require SGX_MODE=HW ==="
        echo "    (SIM measures emulation speed, not enclave performance)"
        exit 0
    fi
    echo
    echo "=== TM: three-engine comparison ==="
    SCALE="$SCALE" scripts/run_tm_3engine.sh || echo "  (TM campaign reported failures)"
    echo
    echo "=== Higgs: two-engine comparison ==="
    SCALE=1d scripts/run_higgs_2engine.sh || echo "  (Higgs campaign reported failures)"
    echo
    echo "Blind join-order campaigns (both engines average over join orders):"
    echo "    scripts/tm_blind_campaign.sh 3"
    echo "    scripts/higgs_blind_campaign.sh 4 1"
fi
