#!/usr/bin/env python3
"""
Build the TM head-to-head with BOTH systems under a blind join order.

Neither engine ships a planner whose choice could be justified obliviously:
Obliviator has no plan logic at all, and ours takes the tree root from the first
table in the FROM clause.  Selecting either by cardinality is a data-dependent
decision an oblivious engine cannot make without leaking, so the defensible
figure for both is the mean over all valid orders.

Ours   : every rooted orientation (root choice x child ordering), REPS each.
Theirs : every connected binary plan, 1 run each, fresh enclave per plan.

Usage: scripts/tm_blind_table.py [campaign_tsv_dir]
"""

import json
import os
import statistics as st
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# Obliviator results live beside its build tree; baselines/build_obliviator.sh
# puts that under external/ by default.
CMP = Path(os.environ.get("OBLIVIATOR_RESULTS",
                          REPO / "external" / "obliviator"))

CELLS = [("tpch_tm1", "0_001"), ("tpch_tm2", "0_001"), ("tpch_tm3", "0_001"),
         ("tpch_tm1", "0_01"),  ("tpch_tm2", "0_01"),  ("tpch_tm3", "0_01"),
         ("tpch_tm1", "0_1"),   ("tpch_tm2", "0_1")]

# Published single-order figures, for the "what changes" column.
PUBLISHED = {
    ("tpch_tm1", "0_001"): (0.49, 27.69), ("tpch_tm2", "0_001"): (0.44, 27.34),
    ("tpch_tm3", "0_001"): (0.47, 27.18), ("tpch_tm1", "0_01"):  (1.75, 28.67),
    ("tpch_tm2", "0_01"):  (0.92, 27.73), ("tpch_tm3", "0_01"):  (6.55, 33.36),
    ("tpch_tm1", "0_1"):   (49.91, 75.71),
}
PUB_SPEEDUP = {"tpch_tm1_0_001": 3.2, "tpch_tm2_0_001": 3.4, "tpch_tm3_0_001": 3.6,
               "tpch_tm1_0_01": 2.6, "tpch_tm2_0_01": 3.8, "tpch_tm3_0_01": 1.4,
               "tpch_tm1_0_1": 0.96}


def ours(results_dir: Path):
    """Per-cell stats from the tree_variants TSVs (variant, rows, wall, init, exec, ecall)."""
    out = {}
    for q, s in CELLS:
        # the TSV is named after the data dir actually used, which may be the
        # sibling checkout for SF 0.1
        cands = list(results_dir.glob(f"{q}_data_{s}_variants.tsv"))
        if not cands:
            continue
        rows = [l.split("\t") for l in cands[0].read_text().splitlines() if l.strip()]
        if not rows:
            continue
        by_var = {}
        for r in rows:
            by_var.setdefault(int(r[0]), []).append(r)
        # mean over reps within a variant, then stats across variants
        ex = [st.mean(float(x[4]) for x in v) for v in by_var.values()]
        tot = [st.mean(float(x[2]) for x in v) for v in by_var.values()]
        ini = [st.mean(float(x[3]) for x in v) for v in by_var.values()]
        ec = [st.mean(float(x[5]) for x in v) for v in by_var.values()]
        v0 = by_var.get(0)
        out[(q, s)] = dict(
            n=len(by_var), reps=len(rows) // max(len(by_var), 1),
            exec_m=st.mean(ex), exec_sd=st.stdev(ex) if len(ex) > 1 else 0.0,
            exec_v0=st.mean(float(x[4]) for x in v0) if v0 else float("nan"),
            exec_min=min(ex), exec_max=max(ex),
            tot_m=st.mean(tot), tot_sd=st.stdev(tot) if len(tot) > 1 else 0.0,
            init_m=st.mean(ini), init_sd=st.stdev(ini) if len(ini) > 1 else 0.0,
            ecall_m=st.mean(ec), ecall_sd=st.stdev(ec) if len(ec) > 1 else 0.0,
            rows={x[1] for x in rows},
            # 7th column added when auto-verification was wired in; runs
            # recorded before that have 6 columns and report "-".  Verification
            # is per VARIANT (all reps of a variant produce the same output), so
            # count distinct variants rather than rows -- otherwise a 4-variant
            # x 3-rep cell would claim "12 checked" for 4 comparisons.
            checks=[v for v in
                    {int(x[0]): x[6].strip() for x in rows
                     if len(x) > 6 and x[6].strip() != "-"}.values()],
        )
    return out


def theirs():
    srcs = [(CMP / "obliviator/query_results/results_test_config_plan_tm12_plan_average.json", "unpatched"),
            (CMP / "obliviator_fixtest/query_results/results_test_config_plan_tm3_plan_average.json", "fixed"),
            (CMP / "obliviator_fixtest/query_results/results_test_config_plan_tm1_01_plan_average.json", "fixed"),
            # TM2 @ 0.1 only runs with an enlarged output buffer (1 GB, 8 GB enclave)
            (CMP / "obliviator_fixtest/query_results/results_test_config_plan_tm2_01_bigbuf_plan_average.json", "fixed,1GB buf")]
    out = {}
    for f, tag in srcs:
        if not f.exists():
            continue
        for k, v in json.loads(f.read_text()).items():
            if k in out and out[k]["build"] == "fixed":
                continue          # prefer the fixed build wherever we have it
            pp = v["per_plan"]
            ex = [p["exec_s"] for p in pp]
            tt = [p["total_s"] for p in pp]
            ii = [p["init_s"] for p in pp]
            cc = [p["core_runtime"] for p in pp]
            out[k] = dict(build=tag, n=len(pp),
                          exec_m=st.mean(ex),
                          exec_sd=st.stdev(ex) if len(ex) > 1 else 0.0,
                          exec_min=min(ex), exec_max=max(ex),
                          tot_m=st.mean(tt), tot_sd=st.stdev(tt) if len(tt) > 1 else 0.0,
                          init_m=st.mean(ii), init_sd=st.stdev(ii) if len(ii) > 1 else 0.0,
                          core_m=st.mean(cc), core_sd=st.stdev(cc) if len(cc) > 1 else 0.0,
                          fail=len(v.get("plans_failed") or []))
    return out


def main() -> int:
    rd = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "output/runs/results"
    O, T = ours(rd), theirs()
    if not O:
        print(f"no our-side results under {rd}")
        return 1

    print("FULL BREAKDOWN — both systems blind (mean over all join orders)\n")
    print("| cell | system | orders | Total (s) | Init (s) | **Execution (s)** | *kernel* |")
    print("|---|---|---|---|---|---|---|")
    for q, s in CELLS:
        if (q, s) not in O:
            continue
        o, t = O[(q, s)], T.get(f"{q.replace('tpch_','')}_{s}")
        if not t:
            continue
        name = f"{q.replace('tpch_','').upper()} @ {s.replace('_','.')}"
        print(f"| {name} | **ours** | {o['n']} | {o['tot_m']:.2f} ± {o['tot_sd']:.2f} | "
              f"{o['init_m']:.2f} ± {o['init_sd']:.2f} | "
              f"**{o['exec_m']:.2f} ± {o['exec_sd']:.2f}** | "
              f"*{o['ecall_m']:.2f} ECALL* |")
        print(f"| | Obliviator | {t['n']} | {t['tot_m']:.2f} ± {t['tot_sd']:.2f} | "
              f"{t['init_m']:.2f} ± {t['init_sd']:.2f} | "
              f"{t['exec_m']:.2f} ± {t['exec_sd']:.2f} | "
              f"*{t['core_m']:.2f} Core* |")

    print("\n\nSPEEDUPS (ours ÷ theirs)\n")
    print("| cell | our orders | ours Exec | their plans | their Exec | **Exec speedup** | Total speedup | published |")
    print("|---|---|---|---|---|---|---|---|")
    for q, s in CELLS:
        if (q, s) not in O:
            continue
        o, t = O[(q, s)], T.get(f"{q.replace('tpch_','')}_{s}")
        if not t:
            continue
        name = f"{q.replace('tpch_','').upper()} @ {s.replace('_','.')}"
        print(f"| {name} | {o['n']} | **{o['exec_m']:.2f} ± {o['exec_sd']:.2f}** | "
              f"{t['n']} | {t['exec_m']:.2f} ± {t['exec_sd']:.2f} | "
              f"**{t['exec_m']/o['exec_m']:.2f}×** | {t['tot_m']/o['tot_m']:.2f}× | "
              f"{PUB_SPEEDUP.get(q+'_'+s,0):.1f}× |")

    print("\n\nORDER SENSITIVITY — Execution max/min across orders\n")
    print("| cell | ours | theirs |")
    print("|---|---|---|")
    for q, s in CELLS:
        if (q, s) not in O:
            continue
        o = O[(q, s)]
        t = T.get(f"{q.replace('tpch_','')}_{s}")
        name = f"{q.replace('tpch_','').upper()} @ {s.replace('_','.')}"
        os_ = o["exec_max"] / o["exec_min"] if o["exec_min"] else 0
        ts_ = (t["exec_max"] / t["exec_min"]) if t and t["exec_min"] else 0
        print(f"| {name} | {os_:.2f}× | {ts_:.2f}× |")

    print("\n\nWHAT THE BLIND MODEL COSTS EACH SIDE\n")
    print("| cell | ours v0 | ours blind | Δ | ours verified | theirs |")
    print("|---|---|---|---|---|---|")
    for q, s in CELLS:
        if (q, s) not in O:
            continue
        o = O[(q, s)]
        t = T.get(f"{q.replace('tpch_','')}_{s}")
        name = f"{q.replace('tpch_','').upper()} @ {s.replace('_','.')}"
        d = 100 * (o["exec_m"] / o["exec_v0"] - 1) if o["exec_v0"] else 0
        ck = o["checks"]
        if ck:
            rc = (f"{ck.count('PASS')}/{len(ck)} PASS"
                  if ck.count("PASS") == len(ck)
                  else f"*** {len(ck) - ck.count('PASS')} of {len(ck)} FAILED ***")
        else:
            rc = "not checked" + (" (rows agree)" if len(o["rows"]) == 1 else " *** ROWS DIFFER ***")
        note = f"{t['build']}, {t['fail']} failed" if t else "?"
        print(f"| {name} | {o['exec_v0']:.2f} | {o['exec_m']:.2f} | {d:+.1f}% | {rc} | {note} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
