#!/usr/bin/env python3
"""
Tabulate the Higgs blind-join-order campaign.

Our engine picks a join tree from the FROM clause; choosing a better one needs
cardinality statistics, and consulting those is a data-dependent decision an
oblivious engine cannot make without leaking.  So the defensible figure is the
mean over the orientations it could legitimately pick -- the same standard
scripts/tm_blind_campaign.sh applies to both engines on the TM workload.

Cells with more orientations than the cap are SAMPLED (seeded, always including
variant 0), and are marked as such: a sampled spread is an estimate, not a
population figure.

The comparison engine is batching, not Obliviator -- these are band joins, which
Obliviator cannot express.  Batching is NOT re-measured blind, so the speedup
column is ours-blind / theirs-single-order, which understates us if batching is
itself order-sensitive.

Usage: higgs_blind_table.py [results_dir]
"""

import os
import re
import statistics as st
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Published single-tree Execution (s) and batching (s), from higgs_perf.md.
PUB = {
    ("tw3", "1d"): (0.48, 3.67),    ("tw3", "2d"): (1.24, 15.81),   ("tw3", "3d"): (3.21, 34.50),
    ("tw4", "1d"): (0.52, 9.77),    ("tw4", "2d"): (5.11, 126.69),  ("tw4", "3d"): (55.61, 432.52),
    ("tw5", "1d"): (0.51, 11.17),   ("tw5", "2d"): (6.64, 154.40),  ("tw5", "3d"): (74.49, 520.70),
    ("hop1", "2d"): (0.93, None),   ("hop1", "3d"): (1.76, 19.68),
    ("hop4", "2d"): (1.20, None),   ("hop4", "3d"): (2.42, 67.05),
    ("hop8", "2d"): (1.48, None),   ("hop8", "3d"): (3.44, 143.75),
    ("btree1", "3d"): (1.73, 19.68), ("btree4", "3d"): (3.42, 93.46),
    ("btree6", "3d"): (46.07, 380.88), ("btree7", "3d"): (135.43, 786.47),
    ("btree8", "3d"): (1992.00, 2344.55),
}

# Total orientations per query (from `sgx_join ... list`), to flag sampling.
TOTAL = {"tw3": 4, "tw4": 6, "tw5": 8,
         "hop1": 2, "hop2": 4, "hop3": 6, "hop4": 8, "hop5": 10, "hop6": 12, "hop7": 14, "hop8": 16,
         "btree1": 2, "btree2": 4, "btree3": 6, "btree4": 16, "btree5": 20, "btree6": 48,
         "btree7": 56, "btree8": 128}

ORDER = (["tw3", "tw4", "tw5"]
         + [f"hop{i}" for i in range(1, 9)]
         + [f"btree{i}" for i in range(1, 9)])


def load(results: Path):
    cells = {}
    for f in results.glob("higgs_*_variants.tsv"):
        m = re.match(r"higgs_(\w+?)_w4_higgs_(?:hops_)?(\dd)_variants\.tsv", f.name)
        if not m:
            continue
        q, rung = m.group(1), m.group(2)
        by_var = {}
        for line in f.read_text().splitlines():
            if not line.strip():
                continue
            p = line.split("\t")
            by_var.setdefault(int(p[0]), []).append(p)
        if not by_var:
            continue
        ex = {v: st.mean(float(x[4]) for x in r) for v, r in by_var.items()}
        ec = {v: st.mean(float(x[5]) for x in r) for v, r in by_var.items()}
        checks = {x[6].strip() for r in by_var.values() for x in r
                  if len(x) > 6 and x[6].strip() != "-"}
        cells[(q, rung)] = dict(
            n=len(ex), total=TOTAL.get(q, len(ex)),
            v0=ex.get(0), exec_m=st.mean(ex.values()),
            exec_sd=st.stdev(ex.values()) if len(ex) > 1 else 0.0,
            lo=min(ex.values()), hi=max(ex.values()),
            ecall_m=st.mean(ec.values()),
            rows={x[1] for r in by_var.values() for x in r},
            checks=checks)
    return cells


def main() -> int:
    results = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "output/runs/results"
    C = load(results)
    if not C:
        print(f"no Higgs variant TSVs under {results}")
        return 1

    for rung in ("1d", "2d", "3d"):
        present = [q for q in ORDER if (q, rung) in C]
        if not present:
            continue
        print(f"\n### Rung {rung}\n")
        print("| query | orientations | v0 (published tree) | blind mean ± sd | "
              "spread | published | batching | speedup (blind) |")
        print("|---|---|---|---|---|---|---|---|")
        for q in present:
            c = C[(q, rung)]
            samp = f"{c['n']} of {c['total']}" + (" *(sampled)*" if c["n"] < c["total"] else "")
            pub, batch = PUB.get((q, rung), (None, None))
            spread = c["hi"] / c["lo"] if c["lo"] else 0
            sp = f"{batch / c['exec_m']:.0f}×" if batch else "—"
            print(f"| `{q}` | {samp} | {c['v0']:.2f} | **{c['exec_m']:.2f} ± {c['exec_sd']:.2f}** | "
                  f"{spread:.2f}× | {pub if pub is None else f'{pub:.2f}'} | "
                  f"{batch if batch is None else f'{batch:.1f}'} | {sp} |")

    print("\n\n### Order sensitivity and correctness\n")
    print("| cell | orientations | spread | rows agree | SQLite |")
    print("|---|---|---|---|---|")
    worst = []
    for rung in ("1d", "2d", "3d"):
        for q in ORDER:
            c = C.get((q, rung))
            if not c:
                continue
            spread = c["hi"] / c["lo"] if c["lo"] else 0
            worst.append((spread, f"{q} @ {rung}"))
            rc = "yes" if len(c["rows"]) == 1 else "*** NO ***"
            ck = ("/".join(sorted(c["checks"])) if c["checks"] else "digest only")
            print(f"| {q} @ {rung} | {c['n']} | {spread:.2f}× | {rc} | {ck} |")
    worst.sort(reverse=True)
    print(f"\nlargest spread: " + ", ".join(f"{n} {s:.2f}×" for s, n in worst[:5]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
