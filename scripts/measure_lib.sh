#!/bin/bash
# Shared measurement helpers for the single-ecall engine.
#
# Source this from a driver script; it provides one-shot and repeated-trial
# measurement of a query, reporting the metrics the paper defines
# (see baselines/RUNTIME.md):
#
#   e2e      whole-program wall time, including one-time enclave creation
#   init     enclave creation (ENCLAVE_INIT, printed by the engine)
#   exec     e2e - init: ALL query work after the enclave is up -- in-enclave
#            compute PLUS host-side CSV load, marshalling, and result writing.
#            This is the metric that is comparable to Obliviator's Total - Init,
#            which is likewise a whole-process wall.
#   ecall    ECALL_TOTAL: in-enclave compute alone, excluding host-side I/O.
#            A SUBSET of exec -- reporting it as if it were exec understates our
#            cost and makes the cross-system comparison apples-to-oranges.
#
# plus output row count and PEAK_HEAP (in-enclave allocation high-water mark).
#
# Repeated trials (REPS) are summarised as mean +/- stddev via scripts/stats.py
# -- `awk` is unavailable on the measurement machine.  Failed or timed-out
# trials are reported, never silently dropped.
#
# Usage:
#   . scripts/measure_lib.sh
#   ej_header
#   ej_trials 3 tpch_tm3 input/encrypted/data_0_01
#
# Env respected: TIMEOUT (per-trial cap, seconds; default 2400),
# plus EJ_RUN_DIR etc. from run_env.sh (all output goes there, never /tmp).

. "$(dirname "${BASH_SOURCE[0]}")/run_env.sh"

EJ_TIMEOUT="${TIMEOUT:-2400}"

# Idle gap after every measured run.  Cells whose working set sits near the
# 378 MB physical EPC are sensitive to how settled the machine is: the same
# query, build, data and heap measured 48.50 s in one session and 41.17 s in
# another (15% apart) while each session's internal spread was under 1%.
# Letting the EPC and ksgxd quiesce between runs keeps a campaign comparable
# with itself.  Set COOLDOWN=0 to disable.
EJ_COOLDOWN="${COOLDOWN:-30}"

ej_cooldown() { [ "$EJ_COOLDOWN" -gt 0 ] && sleep "$EJ_COOLDOWN"; return 0; }

# Run one trial.  Sets EJ_ROWS EJ_WALL EJ_INIT EJ_EXEC EJ_ECALL EJ_PEAK_MIB EJ_RC.
# EJ_WALL/EJ_EXEC/EJ_ECALL are "timeout" / "ERR" when the trial did not complete.
ej_measure() {   # query datadir [outfile]
    local q="$1" d="$2" out="${3:-$EJ_RESULT_DIR/ej_${1}.csv}"
    local log="$EJ_LOG_DIR/ej_measure.txt" wallfile="$EJ_RESULT_DIR/ej_measure.wall"

    /usr/bin/time -f "%e" -o "$wallfile" timeout "$EJ_TIMEOUT" \
        ./sgx_join "input/queries/${q}.sql" "$d" "$out" > "$log" 2>&1
    EJ_RC=$?

    EJ_ROWS=$(grep -a -oE "result: [0-9]+" "$log" | grep -oE "[0-9]+" | head -1)
    EJ_INIT=$(grep -a -oE "ENCLAVE_INIT: [0-9.]+" "$log" | grep -oE "[0-9.]+" | head -1)
    EJ_ECALL=$(grep -a -oE "ECALL_TOTAL: [0-9.]+" "$log" | grep -oE "[0-9.]+" | head -1)
    EJ_PEAK_MIB=$(grep -a -oE "PEAK_HEAP: [0-9.]+" "$log" | grep -oE "[0-9.]+" | head -1)
    EJ_WALL=$(cat "$wallfile" 2>/dev/null)

    if [ "$EJ_RC" -eq 124 ]; then
        EJ_WALL="timeout"; EJ_EXEC="timeout"; EJ_ECALL="timeout"
    elif [ "$EJ_RC" -ne 0 ] || [ -z "${EJ_ROWS:-}" ]; then
        EJ_WALL="${EJ_WALL:-ERR}"; EJ_EXEC="ERR"; EJ_ECALL="${EJ_ECALL:-ERR}"
        EJ_ROWS="${EJ_ROWS:-ERR}"
    else
        EJ_EXEC=$(python3 -c "print(f'{${EJ_WALL:-0}-${EJ_INIT:-0}:.3f}')")
    fi
}

# Verify the last measured output against SQLite.  Sets EJ_VERIFY to PASS/FAIL.
ej_verify() {   # query plaintext_dir [outfile]
    local q="$1" pd="$2" out="${3:-$EJ_RESULT_DIR/ej_${1}.csv}"
    local v
    v=$(python3 tests/e2e_sqlite_compare.py "input/queries/${q}.sql" "$pd" "$out" 2>&1)
    # Both engines write ciphertext; the comparator decrypts through the enclave
    # and then compares tuple-exact, so PASS means the values were checked.
    # NODEC is called out separately because it is an infrastructure failure
    # (decrypt_result missing or erroring), not a wrong result -- reporting it
    # as FAIL would send someone hunting a correctness bug that isn't there.
    case "$v" in
        *PASS*)                      EJ_VERIFY=PASS ;;
        *decrypt_result*|*not\ built*) EJ_VERIFY=NODEC ;;
        *)                           EJ_VERIFY=FAIL ;;
    esac
}

ej_header() {
    printf '%-20s %12s %17s %17s %17s %17s %9s\n' \
        "query" "rows" "e2e_s" "init_s" "exec_s" "ecall_s" "peak_MiB"
}

# Run N trials of one cell and print mean +/- stddev per metric.
ej_trials() {   # reps query datadir [label]
    local reps="$1" q="$2" d="$3" label="${4:-$2}"
    local walls="" inits="" execs="" ecalls="" rows="" peak=""
    local i

    for i in $(seq 1 "$reps"); do
        ej_cooldown
        ej_measure "$q" "$d"
        walls="$walls $EJ_WALL"
        inits="$inits ${EJ_INIT:-ERR}"
        execs="$execs $EJ_EXEC"
        ecalls="$ecalls ${EJ_ECALL:-ERR}"
        rows="$EJ_ROWS"
        peak="${EJ_PEAK_MIB:-?}"
    done

    printf '%-20s %12s %17s %17s %17s %17s %9s\n' "$label" "$rows" \
        "$(python3 scripts/stats.py $walls)" \
        "$(python3 scripts/stats.py $inits)" \
        "$(python3 scripts/stats.py $execs)" \
        "$(python3 scripts/stats.py $ecalls)" \
        "$peak"
}
