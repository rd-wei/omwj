#!/usr/bin/env python3
"""
Backfill the correctness column of `*_variants.tsv` files recorded before
`tree_variants.sh` verified as it went.

Each row's result CSV is still on disk, so the tuple-exact check can be run
retroactively — no enclave run needed, and the timings are untouched. New
campaigns record the verdict inline; this exists so the older ones are not
permanently marked "not checked".

Usage: scripts/backfill_verify.py [tsv ...]      (default: all in the run dir)
"""

import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RESULTS = Path(os.environ.get("EJ_RUN_DIR", REPO / "output/runs")) / "results"


def plaintext_for(scale: str) -> Path:
    """Ground truth for a data-dir suffix.

    Scales the repo ships (0.001, 0.01) resolve locally.  SF 0.1 is
    regenerate-only -- either produce it with scripts/gen_all_data.sh or point
    EJ_PLAINTEXT_0_1 at an existing copy.
    """
    local = REPO / "input/plaintext" / f"data_{scale}"
    if local.is_dir():
        return local
    env = os.environ.get(f"EJ_PLAINTEXT_{scale.upper()}")
    return Path(env) if env else local


def verify(query_stem: str, plain: Path, csv: Path) -> str:
    r = subprocess.run(
        ["python3", str(REPO / "tests/e2e_sqlite_compare.py"),
         str(REPO / "input/queries" / f"{query_stem}.sql"), str(plain), str(csv)],
        capture_output=True, text=True, cwd=REPO)
    return "PASS" if "PASS" in (r.stdout + r.stderr) else "FAIL"


def main() -> int:
    tsvs = [Path(a) for a in sys.argv[1:]] or sorted(RESULTS.glob("*_variants.tsv"))
    rc = 0
    for tsv in tsvs:
        stem, scale = tsv.name.replace("_variants.tsv", "").split("_data_")
        plain = plaintext_for(scale)
        if not plain.is_dir():
            print(f"{tsv.name}: no plaintext dir, skipped")
            continue

        out, cache, changed = [], {}, 0
        for line in tsv.read_text().splitlines():
            if not line.strip():
                continue
            f = line.split("\t")
            if len(f) > 6 and f[6].strip() not in ("", "-"):
                out.append(line)                    # already verified
                continue
            var = f[0]
            if var not in cache:
                csv = RESULTS / f"{stem}_data_{scale}_v{var}_r1.csv"
                cache[var] = verify(stem, plain, csv) if csv.exists() else "-"
                if cache[var] == "FAIL":
                    rc = 1
            out.append("\t".join(f[:6] + [cache[var]]))
            changed += 1

        tsv.write_text("\n".join(out) + "\n")
        verdicts = sorted(set(cache.values()))
        print(f"{tsv.name}: {changed} row(s) backfilled from {len(cache)} "
              f"variant(s) -> {','.join(verdicts)}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
