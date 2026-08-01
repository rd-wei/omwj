#!/bin/bash
# Verify result CSVs that are already on disk, without re-running the enclave.
#
# The blind-order campaign originally recorded row counts only, so its timed
# outputs were never tuple-checked against SQLite.  The result files are still
# there, so the check can be done retroactively -- a re-run would prove nothing
# extra and costs an hour.
#
# Usage: scripts/retro_verify.sh <glob-of-result-csvs> <plaintext_dir> <query_stem>
#   e.g. scripts/retro_verify.sh 'output/runs/results/tpch_tm3_data_0_01_v*_r1.csv' \
#          input/plaintext/data_0_01 tpch_tm3

set -u
cd "$(dirname "$0")/.."
. scripts/run_env.sh 2>/dev/null || true
. scripts/measure_lib.sh 2>/dev/null || true

PATTERN="${1:?usage: retro_verify.sh <csv-glob> <plaintext_dir> <query_stem>}"
PLAIN="${2:?}"
STEM="${3:?}"

FAIL=0
N=0
for f in $PATTERN; do
    [ -f "$f" ] || continue
    N=$((N+1))
    ROWS=$(( $(wc -l < "$f") - 1 ))
    EJ_VERIFY=FAIL
    ej_verify "$STEM" "$PLAIN" "$f"
    printf "%-52s rows=%-9s %s\n" "$(basename "$f")" "$ROWS" "$EJ_VERIFY"
    [ "$EJ_VERIFY" = "PASS" ] || FAIL=1
done
echo
echo "$N file(s) checked; $([ "$FAIL" -eq 0 ] && echo 'all PASS' || echo '*** FAILURES ***')"
exit "$FAIL"
