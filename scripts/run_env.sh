#!/bin/bash
# Shared output locations for every measurement / correctness run.
#
# Nothing a run produces belongs in /tmp: it is cleared on reboot and by system
# cleaners, which silently destroys long campaigns (a multi-hour uncapped run
# lost this way is not recoverable -- the numbers simply do not exist any more).
# Everything lands under one directory instead, so a campaign can be archived,
# inspected after the fact, or deleted as a unit.
#
#   $EJ_RUN_DIR/logs/      driver + per-query stdout, build logs
#   $EJ_RUN_DIR/results/   result CSVs and timing scratch files
#
# The default lives under output/, which .gitignore already excludes, so results
# never end up in a commit.  Override EJ_RUN_DIR to send a campaign elsewhere
# (e.g. a scratch volume with more space for multi-GB result CSVs).
#
# Source this AFTER cd-ing to the repo root:
#     cd "$(dirname "$0")/.."
#     . scripts/run_env.sh

EJ_RUN_DIR="${EJ_RUN_DIR:-output/runs}"
EJ_LOG_DIR="${EJ_LOG_DIR:-$EJ_RUN_DIR/logs}"
EJ_RESULT_DIR="${EJ_RESULT_DIR:-$EJ_RUN_DIR/results}"

mkdir -p "$EJ_LOG_DIR" "$EJ_RESULT_DIR" || {
    echo "run_env: cannot create $EJ_RUN_DIR" >&2
    exit 1
}
