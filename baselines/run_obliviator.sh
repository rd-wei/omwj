#!/bin/bash
# Driver harness for the Obliviator baseline: runs a batch of queries through
# the multi-way pipeline wrapper and verifies each result against SQLite.
#
# The wrapper chains Obliviator's binary equi-joins into a multi-way pipeline
# (Obliviator itself is binary-equality only).  It re-signs the enclave per
# configured size and sets the host I/O buffer per scale -- do not hand-edit
# enclave/parallel.conf and expect it to survive a run.
#
# Config file format, one test per line:
#
#   <query_file>,<data_dir>,<enclave_size>
#   test_data/queries/tpch_tm1.sql,test_data/data_0_01,4GB
#
# Enclave sizes: 64MB 128MB 256MB 512MB 640MB 768MB 896MB 1GB 2GB 4GB 8GB 16GB.
# Reported comparison numbers use 4GB (identical to ours).  The runner caches by
# query+scale WITHIN a run, so two enclave sizes for the same query must go in
# SEPARATE config files / runs.
#
# Host I/O buffer (OBLIVIATOR_MAX_BUF) is set by the runner per scale:
# 16 MiB @ 0.001, 128 MiB @ 0.01, 256 MiB @ 0.1.  The EDL copies the whole
# buffer into the enclave on every ECALL, so it is both a floor on the required
# enclave heap and the dominant per-step wall cost.
#
# Env:
#   OBLIVIATOR_SRC   path to the Obliviator artifact checkout   [required]
#
# Usage:  OBLIVIATOR_SRC=... baselines/run_obliviator.sh <config-file> [extra args...]
set -eu
cd "$(dirname "$0")/.."

if [ $# -lt 1 ]; then
    echo "usage: OBLIVIATOR_SRC=<path> $0 <config-file> [--no-resume] [--clean] [--no-verify]"
    exit 2
fi

if [ -z "${OBLIVIATOR_SRC:-}" ]; then
    echo "ERROR: set OBLIVIATOR_SRC (see baselines/build_obliviator.sh)"
    exit 2
fi

# The wrapper + runner live alongside the artifact checkout.
RUNNER_DIR="$(dirname "$OBLIVIATOR_SRC")"
[ -f "$RUNNER_DIR/run_tests.py" ] || {
    echo "ERROR: run_tests.py not found in $RUNNER_DIR"
    echo "       OBLIVIATOR_SRC should point at <wrapper-dir>/Parallel-join-ae"
    exit 1
}

CONFIG="$1"; shift
# Config paths inside the file are relative to the runner directory.
case "$CONFIG" in
    /*) ;;
    *)  CONFIG="$(pwd)/$CONFIG" ;;
esac

echo "=== Obliviator pipeline: $(basename "$CONFIG") ==="
echo "Hardware mode, single-threaded, daemon mode (enclave reused across steps)."
cd "$RUNNER_DIR"
exec python3 run_tests.py "$CONFIG" "$@"
