#!/bin/bash
# Fetch the Higgs Twitter dataset (SNAP) used to build the higgs_* band-join
# workloads.  This is a large external input -- like the TPC-H dbgen tools in
# REPRODUCE.md's "Data generation from scratch" section it is fetched on demand,
# NOT baked into the Docker image or committed to the repo.
#
# Dataset: https://snap.stanford.edu/data/higgs-twitter.html
# Citation: M. De Domenico, A. Lima, P. Mougel, M. Musolesi, "The Anatomy of a
#           Scientific Rumor," Scientific Reports 3, 2980 (2013).
#
# Usage:   scripts/fetch_higgs_data.sh            # -> ./higgs-data/
#          HIGGS_DIR=/path scripts/fetch_higgs_data.sh
set -eu
cd "$(dirname "$0")/.."

HIGGS_DIR="${HIGGS_DIR:-$(pwd)/higgs-data}"
BASE="https://snap.stanford.edu/data"
mkdir -p "$HIGGS_DIR"

# social_network: the follower graph (equality hops, no timestamps).
# activity_time:  every RT/MT/RE event with a real Unix timestamp (band attr).
# retweet/reply/mention are only needed for the deferred per-event cascade.
FILES="higgs-social_network.edgelist.gz higgs-activity_time.txt.gz"

for gz in $FILES; do
    out="$HIGGS_DIR/${gz%.gz}"
    if [ -s "$out" ]; then
        echo "  have  ${gz%.gz}"
        continue
    fi
    echo "  fetch ${gz}"
    wget -q "${BASE}/${gz}" -O "$HIGGS_DIR/$gz"
    gunzip -f "$HIGGS_DIR/$gz"
done

echo "Higgs data ready in $HIGGS_DIR:"
for gz in $FILES; do
    f="$HIGGS_DIR/${gz%.gz}"
    echo "  $(wc -l < "$f") lines  $f"
done
