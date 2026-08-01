#!/usr/bin/env python3
"""
Reduce OBLIVIATOR's mispairing defect to the smallest binary join that shows it,
and reproduce it on the **unmodified** upstream artifact.

Background
----------
TM3 @ 0.01 returns wrong rows for 4 of its 14 join plans.  All four contain
`nation JOIN <subtree spanning >=2 tables>` as a non-root node, and that node is
itself a single binary join -- 25 left rows against a 5,929-row intermediate,
producing exactly 5,929 pairs of which one is wrong: the right row with key 24
(the maximum) is paired with the left row for key 23.

This script strips that join to its skeleton.  Payloads are replaced by short
row ids, so the same input runs on the stock artifact (`DATA_LENGTH=14`) as well
as on our widened build -- if the defect survives that, it is not a consequence
of anything we changed.

    left  : one row per distinct key  (the `nation` side)
    right : `mult[k]` rows for key k  (the intermediate side)

Usage:
    reduce_defect.py [--build ours|stock] [--case full|shrink|sweep]
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from collections import Counter
from pathlib import Path

BASE = Path(__file__).resolve().parent
BUILDS = {
    "ours": BASE / "Parallel-join-ae" / "join",
    "stock": (BASE.parent / "obliviator_clean" / "Parallel-join-ae" / "join"),
    # stock + the one-line fix in oblivious_distribute.c (count only entries
    # that carry a real value), to confirm the root cause.
    "fixed": (BASE.parent / "obliviator_fixtest" / "Parallel-join-ae" / "join"),
}


def write_input(path: Path, left_keys, right_keys) -> None:
    """Stock wire format: `n1 n2` then n1 left rows then n2 right rows,
    each `<key> <payload>`.  Payloads are row ids so they fit DATA_LENGTH=14."""
    with path.open("w") as f:
        f.write(f"{len(left_keys)} {len(right_keys)}\n")
        for i, k in enumerate(left_keys):
            f.write(f"{k} L{i}\n")
        for i, k in enumerate(right_keys):
            f.write(f"{k} R{i}\n")


def run(build: str, path: Path, session=None) -> Path:
    """One join.  `session` reuses a live enclave (our build only), which turns
    a ~90 s enclave init per probe into a one-off cost -- the difference between
    a 25-minute search and a 2-minute one.  The stock build has no daemon REPL,
    so it always spawns."""
    out = path.with_name(path.stem + "_output" + path.suffix)
    if out.exists():
        out.unlink()
    if session is not None:
        session.join(path)
    else:
        r = subprocess.run(["./host/parallel", "enclave/parallel_enc.signed",
                            "1", str(path)],
                           cwd=BUILDS[build], capture_output=True, text=True,
                           timeout=900)
        if not out.exists():
            raise RuntimeError(f"no output; stdout={r.stdout[-400:]} "
                               f"stderr={r.stderr[-400:]}")
    return out


def check(path: Path, out: Path, left_keys, right_keys):
    """(n_wrong, examples) against the join computed directly in Python."""
    lby = {}
    for i, k in enumerate(left_keys):
        lby.setdefault(k, []).append(f"L{i}")
    want = Counter()
    for i, k in enumerate(right_keys):
        for lp in lby.get(k, []):
            want[(lp, f"R{i}")] += 1

    got = Counter()
    with out.open() as f:
        for line in f:
            t = line.rstrip("\n").split(" ")
            if len(t) >= 4:
                got[(t[1], t[3])] += 1

    only_ob, only_want = got - want, want - got
    return (sum(only_ob.values()), sum(only_want.values()),
            sum(want.values()), sum(got.values()),
            list(only_ob)[:3], list(only_want)[:3])


def report(tag, build, left_keys, right_keys, work: Path, session=None):
    path = work / "reduce_input.txt"
    write_input(path, left_keys, right_keys)
    out = run(build, path, session)
    nob, nwt, exp, got, eob, ewt = check(path, out, left_keys, right_keys)
    ok = nob == 0 and nwt == 0
    print(f"  {tag:<34} |L|={len(left_keys):<4} |R|={len(right_keys):<6} "
          f"expect={exp:<7} got={got:<7} wrong={nob:<3} "
          f"{'ok' if ok else '*** DEFECT ***'}")
    if not ok:
        for a, b in zip(eob, ewt):
            print(f"      produced {a}   expected {b}")
    return ok


def load_step2(work: Path):
    """Key structure of the real failing join, payloads discarded."""
    lines = (work / "plan_step2_input.txt").read_text().splitlines()
    n1, n2 = map(int, lines[0].split())
    left = [int(l.split(" ", 1)[0]) for l in lines[1:1 + n1]]
    right = [int(l.split(" ", 1)[0]) for l in lines[1 + n1:1 + n1 + n2]]
    return left, right


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="ours", choices=list(BUILDS))
    ap.add_argument("--case", default="full",
                    choices=["full", "shrink", "sweep", "shape"])
    args = ap.parse_args()

    work = BUILDS[args.build]
    print(f"build   : {args.build}  ({work})")
    print(f"elem_t  : " + next(l for l in
          (work / "common" / "elem_t.h").read_text().splitlines()
          if "ELEM_SIZE" in l).strip())
    print()

    if args.case == "full":
        # The real join's key structure, with row-id payloads.  If this still
        # fails, payload width is irrelevant to the defect.
        left, right = load_step2(BUILDS["ours"])
        report("real key structure", args.build, left, right, work)

    elif args.case == "shrink":
        # Keep only the top of the key domain, which is where the mispairing
        # lands, and shrink the right side geometrically.
        left, right = load_step2(BUILDS["ours"])
        mult = Counter(right)
        for keep in (25, 12, 6, 3, 2):
            ks = sorted(mult)[-keep:]
            L = [k for k in sorted(set(left)) if k in set(ks)]
            R = [k for k in ks for _ in range(mult[k])]
            report(f"top-{keep} keys", args.build, L, R, work)

    elif args.case == "shape":
        # The synthetic sweep showed 3 equal-multiplicity keys never failing
        # while 2 and 4 did, yet the real top-3 case (unequal multiplicities)
        # failed.  So the trigger is the multiplicity structure, not the size.
        # Vary the key count at fixed multiplicity, then perturb one key.
        session = None
        if args.build == "ours":
            from run_obliviator_queries import ObliviatorSession
            session = ObliviatorSession().__enter__()
        try:
            print("-- equal multiplicity (4 right rows per key)")
            for nkeys in range(2, 10):
                L = list(range(nkeys))
                R = [k for k in range(nkeys) for _ in range(4)]
                report(f"{nkeys} keys, all mult 4", args.build, L, R, work,
                       session)

            print("-- 3 keys, multiplicity of the TOP key perturbed")
            for top in (1, 2, 3, 4, 5, 6, 7, 8):
                L = [0, 1, 2]
                R = [0] * 4 + [1] * 4 + [2] * top
                report(f"3 keys, mult 4/4/{top}", args.build, L, R, work,
                       session)

            print("-- 3 keys, multiplicity of the MIDDLE key perturbed")
            for mid in (1, 2, 3, 5, 6, 8):
                L = [0, 1, 2]
                R = [0] * 4 + [1] * mid + [2] * 4
                report(f"3 keys, mult 4/{mid}/4", args.build, L, R, work,
                       session)
        finally:
            if session is not None:
                session.__exit__(None, None, None)

    else:   # sweep: synthetic, to find the smallest shape that fails
        session = None
        if args.build == "ours":
            from run_obliviator_queries import ObliviatorSession
            session = ObliviatorSession().__enter__()
        try:
            for nkeys in (2, 3, 4):
                for per in (1, 2, 3, 4, 8, 16, 32, 64):
                    L = list(range(nkeys))
                    R = [k for k in range(nkeys) for _ in range(per)]
                    report(f"{nkeys} keys x {per} right rows", args.build,
                           L, R, work, session)
        finally:
            if session is not None:
                session.__exit__(None, None, None)
    return 0


if __name__ == "__main__":
    sys.exit(main())
