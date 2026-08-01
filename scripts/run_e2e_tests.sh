#!/bin/bash
# End-to-end regression: run sgx_join on every test query against the
# encrypted data, then verify each output CSV against a SQLite baseline
# built from the plaintext data.
#
# Usage: scripts/run_e2e_tests.sh [scale]     (default scale: 0_001)

set -u
cd "$(dirname "$0")/.."

SCALE="${1:-0_001}"
ENC="input/encrypted/data_${SCALE}"
PLAIN="input/plaintext/data_${SCALE}"
OUT_DIR="output"
mkdir -p "$OUT_DIR"

QUERIES="tpch_tm1 tpch_tm2 tpch_tm3"
FAILED=0

for q in $QUERIES; do
    sql="input/queries/${q}.sql"
    out="${OUT_DIR}/${q}_${SCALE}.csv"
    echo "=== ${q} (scale ${SCALE}) ==="
    if ! ./sgx_join "$sql" "$ENC" "$out"; then
        echo "FAIL: sgx_join exited non-zero for ${q}"
        FAILED=1
        continue
    fi
    if ! python3 tests/e2e_sqlite_compare.py "$sql" "$PLAIN" "$out"; then
        FAILED=1
    fi
done

if [ "$FAILED" -eq 0 ]; then
    echo "ALL E2E TESTS PASSED"
else
    echo "E2E TESTS FAILED"
fi
exit $FAILED
