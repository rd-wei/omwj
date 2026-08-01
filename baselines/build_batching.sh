#!/bin/bash
# Build the batching (external-memory) baseline: the same oblivious multi-way
# band-join algorithm as the single-ecall engine, but with the host orchestrating
# and the enclave executing operations in fixed-size batches.
#
# Its sources are OURS and ship in this repo under baselines/batching/ -- there
# is nothing to download.  Everything needed to build sgx_app is here; the data
# is not duplicated, the drivers point it at this artifact's input/.
#
#   MAX_BATCH_SIZE=8000 is a BUILD-TIME -D flag, not a runtime setting.  The
#   header's 2000 is an #ifndef default, so inspecting the source does not tell
#   you what a binary was compiled with -- always rebuild explicitly.
#
# Env:
#   BATCHING_SRC   build a different checkout instead of the vendored sources
#   BATCH_SIZE     batch size to compile in (default 8000, the locked value)
#   SGX_MODE       HW | SIM (default HW -- timings in SIM are meaningless)
set -eu
cd "$(dirname "$0")/.."

BATCH_SIZE="${BATCH_SIZE:-8000}"
SGX_MODE="${SGX_MODE:-HW}"
SRC="${BATCHING_SRC:-$(pwd)/baselines/batching}"

[ -f "$SRC/Makefile" ] || { echo "ERROR: no Makefile in $SRC" >&2; exit 1; }

# Completeness check.  app/debug/ is easy to lose: a bare "debug/" .gitignore
# rule matches it, and then `make` fails deep in the link step with
# "No rule to make target 'app/debug/debug_util.o'" -- which reads like a broken
# build rather than missing sources.  Name the real cause here instead.
for f in app/debug/debug_util.cpp app/debug/debug_manager.cpp; do
    [ -f "$SRC/$f" ] || {
        echo "ERROR: $SRC is missing $f, which its Makefile requires." >&2
        echo "       The source tree is incomplete, not the build." >&2
        exit 1
    }
done

echo "=== Building batching engine (SGX_MODE=$SGX_MODE, MAX_BATCH_SIZE=$BATCH_SIZE) ==="
make -C "$SRC" clean > /dev/null 2>&1 || true
make -C "$SRC" SGX_MODE="$SGX_MODE" MAX_BATCH_SIZE="$BATCH_SIZE"

cat <<EOF

=== Built: $SRC/sgx_app ===

Run it FROM ITS OWN DIRECTORY -- it loads its signed enclave from the current
working directory:

  cd "$SRC" && ./sgx_app <query.sql> <input_dir> <output.csv>

Queries and data from this artifact can be passed by absolute path, e.g.:

  cd "$SRC" && ./sgx_app "$(pwd)/input/queries/higgs_tw4_w4.sql" \\
      "$(pwd)/input/plaintext/higgs_1d" /tmp/out.csv

Its result is written as ciphertext (save_encrypted_csv), so verification checks
schema and cardinality only -- see the ENCRYPTED verdict in
tests/e2e_sqlite_compare.py.
EOF
