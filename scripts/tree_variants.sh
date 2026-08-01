#!/bin/bash
# Run a query under every rooted orientation of its join tree and report the
# spread, so our own numbers can be quoted under the same blind-order model we
# apply to a baseline that ships no planner.
#
# Our bottom-up/top-down passes fix how a tree is *evaluated*, so we have none
# of a binary-join engine's parenthesisation freedom -- but the tree can be
# rooted at any table, and a node's children can be visited in either order.
# `sgx_join <q> <d> <o> list` prints how many such variants exist.
#
# Variant 0 is the historical tree (root = first table in FROM), so the first
# row of the output reproduces the published number.
#
# Usage: scripts/tree_variants.sh <query.sql> <encrypted_dir> [reps]

set -u
cd "$(dirname "$0")/.."
. scripts/run_env.sh 2>/dev/null || true
. scripts/measure_lib.sh 2>/dev/null || true    # provides ej_verify()

QUERY="${1:?usage: tree_variants.sh <query.sql> <data_dir> [reps]}"
DATA="${2:?}"
REPS="${3:-1}"
COOLDOWN="${COOLDOWN:-0}"
# first | all | off.  `first` checks one rep per variant, which is sufficient:
# every rep of a variant produces the same output, so `all` only repeats work.
# EVERY variant is checked -- that is what makes the orientations trustworthy
# individually rather than merely consistent with each other.
#
# `off` now leaves NO content check at all (the cross-variant digest that used
# to cover that case has been removed in favour of verifying every orientation),
# so use it only when you knowingly want timings without correctness.
VERIFY="${VERIFY:-first}"
# Cap the number of orientations executed.  Unset = all.  Deep Higgs trees reach
# 128 orientations and btree8 @ 3d costs ~34 min each, so full enumeration is not
# always affordable; when capped we take a SEEDED sample so the subset is
# reproducible, and label the output a sample so it is not mistaken for a census.
MAX_VARIANTS="${MAX_VARIANTS:-0}"
SAMPLE_SEED="${SAMPLE_SEED:-0}"

OUT="${EJ_RUN_DIR:-output/runs}/results"
LOG="${EJ_LOG_DIR:-output/runs/logs}"
mkdir -p "$OUT" "$LOG"
STEM=$(basename "$QUERY" .sql)
TAG="${STEM}_$(basename "$DATA")"

# Ground truth lives beside the encrypted data: input/encrypted/X -> input/plaintext/X.
# Works for the sibling checkout's SF 0.1 tree as well.
PLAIN=$(printf '%s' "$DATA" | python3 -c "import sys; print(sys.stdin.read().replace('/encrypted/','/plaintext/'))")
if [ ! -d "$PLAIN" ]; then
    echo "WARNING: no plaintext dir at $PLAIN -- correctness will not be checked" >&2
    VERIFY=off
fi

N=$(./sgx_join "$QUERY" "$DATA" /dev/null list | grep -oP 'TREE_VARIANTS: \K[0-9]+')

# Which orientations to run: all of them, or a seeded sample.
VARIANTS=$(python3 -c "
import random, sys
n, cap, seed = $N, $MAX_VARIANTS, $SAMPLE_SEED
if cap and cap < n:
    # always include variant 0 -- it is the historical tree every published
    # number used, so the sample stays anchored to something comparable
    rest = sorted(random.Random(seed).sample(range(1, n), cap - 1))
    print(' '.join(str(v) for v in [0] + rest))
else:
    print(' '.join(str(v) for v in range(n)))
")
NRUN=$(printf '%s\n' $VARIANTS | wc -l)

echo "query   : $STEM      data: $DATA"
if [ "$NRUN" -lt "$N" ]; then
    echo "variants: SAMPLED $NRUN of $N (seed $SAMPLE_SEED): $VARIANTS"
else
    echo "variants: $N (all)      reps: $REPS      cooldown: ${COOLDOWN}s"
fi
echo "verify  : $VERIFY${PLAIN:+   (vs $PLAIN)}"
echo
printf "%-4s %-12s %9s %9s %9s %9s %9s %6s\n" "var" "root" "rows" "wall" "init" "exec" "ecall" "check"

DATAFILE="$OUT/${TAG}_variants.tsv"
: > "$DATAFILE"
ANYFAIL=0

for v in $VARIANTS; do
    for r in $(seq 1 "$REPS"); do
        [ "$COOLDOWN" -gt 0 ] && [ "$v$r" != "01" ] && sleep "$COOLDOWN"
        RES="$OUT/${TAG}_v${v}_r${r}.csv"
        L="$LOG/${TAG}_v${v}_r${r}.log"
        T0=$(date +%s.%N)
        ./sgx_join "$QUERY" "$DATA" "$RES" "$v" > "$L" 2>&1
        RC=$?
        T1=$(date +%s.%N)

        WALL=$(python3 -c "print('%.2f'%($T1-$T0))")
        INIT=$(grep -oP 'ENCLAVE_INIT:\s*\K[0-9.]+' "$L" | head -1)
        ECALL=$(grep -oP 'ECALL_TOTAL:\s*\K[0-9.]+' "$L" | head -1)
        ROOT=$(grep -oP 'TREE_VARIANT:.*root=\K[^)]+' "$L" | head -1)
        ROWS=$(( $(wc -l < "$RES" 2>/dev/null || echo 1) - 1 ))
        EXEC=$(python3 -c "
i='${INIT:-}'
print('%.2f'%(${WALL}-float(i)) if i else 'n/a')")

        # Correctness is checked AFTER the timing is taken, so it never enters
        # the measured region.
        CHECK="-"
        if [ "$RC" -eq 0 ] && { [ "$VERIFY" = "all" ] || { [ "$VERIFY" = "first" ] && [ "$r" -eq 1 ]; }; }; then
            EJ_VERIFY=FAIL
            ej_verify "$STEM" "$PLAIN" "$RES"
            CHECK="$EJ_VERIFY"
            [ "$CHECK" = "PASS" ] || ANYFAIL=1
        fi

        printf "%-4s %-12s %9s %9s %9s %9s %9s %6s\n" \
               "$v" "${ROOT:-?}" "$ROWS" "$WALL" "${INIT:-n/a}" "$EXEC" "${ECALL:-n/a}" "$CHECK"
        [ "$RC" -eq 0 ] && printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
            "$v" "$ROWS" "$WALL" "${INIT:-0}" "$EXEC" "${ECALL:-0}" "$CHECK" >> "$DATAFILE"
    done
done

# Every orientation is checked against SQLite directly (see VERIFY above), so
# there is no separate cross-variant agreement check: agreeing with ground truth
# implies agreeing with each other, and it is the stronger statement.  A digest
# comparison used to stand in here for the rungs where SQLite was switched off;
# verifying every orientation supersedes it.
echo
python3 - "$DATAFILE" <<'PY'
import statistics as st, sys
rows = [l.split('\t') for l in open(sys.argv[1]) if l.strip()]
if not rows:
    print("no successful runs"); raise SystemExit(1)
counts = {r[1] for r in rows}
print("row counts across variants:", counts,
      "(identical)" if len(counts) == 1 else "*** DIFFER -- a variant is wrong ***")
# .strip() matters: the verdict is the last tab-separated field, so it still
# carries the line's trailing newline.  Without it "PASS\n" != "PASS" and every
# checked run is counted as a failure -- the summary read "0 PASS, 12 FAILED"
# for a set of runs that had all passed.
checks = [r[6].strip() for r in rows if len(r) > 6 and r[6].strip() != "-"]
if checks:
    bad = [i for i, c in enumerate(checks) if c != "PASS"]
    print("SQLite verification: %d checked, %d PASS%s"
          % (len(checks), checks.count("PASS"),
             "" if not bad else "   *** %d FAILED ***" % len(bad)))
else:
    print("SQLite verification: none run (VERIFY=off or no plaintext dir)")
print()
print("%-7s %9s %9s %9s %9s %9s" % ("metric", "v0", "mean", "sd", "min", "max"))
for name, col in (("wall", 2), ("init", 3), ("exec", 4), ("ecall", 5)):
    xs = [float(r[col]) for r in rows if r[col] not in ("n/a", "")]
    if not xs:
        continue
    v0 = float(rows[0][col])
    sd = st.stdev(xs) if len(xs) > 1 else 0.0
    print("%-7s %9.2f %9.2f %9.2f %9.2f %9.2f" %
          (name, v0, st.mean(xs), sd, min(xs), max(xs)))
    if name in ("exec", "ecall") and v0:
        print("        v0 is %+.1f%% vs the blind mean; spread %.2fx" %
              (100 * (v0 / st.mean(xs) - 1), max(xs) / min(xs)))
PY

exit "$ANYFAIL"
