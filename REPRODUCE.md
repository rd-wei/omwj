# Reproducing the results

Three engines, two workloads, two tiers. Start with the tier that matches your
machine.

| tier | machine | engines | time |
|---|---|---|---|
| **SIM** | any x86-64 | single-ecall, batching | minutes |
| **HW** | real SGX (Azure DCsv3, or a local SGX box) | all three | hours for the full campaigns |

> **Apple Silicon:** run Docker inside a full x86-64 VM (e.g. `colima start
> --profile x86 --arch x86_64`), not on the default arm64 backend under Rosetta.
> Intel's precompiled SIM libraries use AVX and `rdrand`, which SIGILL under
> Rosetta -- a limitation of the Intel SDK, not of this code. The
> `--platform linux/amd64` in the commands below is harmless and correct once you
> are inside such a VM; it is not a substitute for one.

---

## 1. SIM — correctness anywhere

```bash
docker build -t omwj-sim .
docker run --platform linux/amd64 --rm -e QUICK=1 omwj-sim   # ~30 s smoke test
docker run --platform linux/amd64 --rm omwj-sim              # full suite @ SF 0.001
docker run --platform linux/amd64 --rm -e SCALE=0_01 omwj-sim
```

Every query is checked tuple-exact against SQLite. A non-zero exit means a real
mismatch, not a timing wobble.

**OBLIVIATOR does not appear in this tier,** and the default build omits its
toolchain accordingly. `oe_create_parallel_enclave` is called with `flags=0`
upstream — there is no simulation path — so it needs real SGX. Pass
`--build-arg WITH_OBLIVIATOR=1` to add OpenEnclave for the three-engine HW tier;
be aware that Microsoft publishes `open-enclave` for Ubuntu 20.04 and not for the
22.04 base pinned here, so that layer does not currently install. `scripts/run_tm_3engine.sh` says so explicitly rather than silently
producing a two-engine table that looks like a three-engine one.

## 2. HW — timings

```bash
docker build --build-arg SGX_MODE=HW --build-arg VECTORIZE=on -t omwj-hw .
docker run --device /dev/sgx_enclave --device /dev/sgx_provision \
           -v /var/run/aesmd:/var/run/aesmd --rm -e FULL=1 omwj-hw
```

`FULL=1` runs the correctness suite and then both measurement campaigns. In SIM
it is ignored with a message: SIM measures emulation speed, not enclave
performance.

### Building the two baselines

```bash
baselines/build_batching.sh          # builds baselines/batching at MAX_BATCH_SIZE=8000
baselines/build_obliviator.sh        # downloads from Zenodo, verifies, patches, builds
```

`build_obliviator.sh` is fully automatic: it fetches
`Parallel-join-ae.zip` (DOI 10.5281/zenodo.14723872), checks its sha256, and
applies our patch series. **We redistribute none of their code** — only the
diffs in `baselines/obliviator/patches/`, each with a header explaining why it
exists. A patch that neither applies nor is already applied aborts the build
rather than producing a half-patched baseline.

---

## Parameters that silently change results

Each of these produces a *plausible but wrong* number when set wrong, and none
of them raises an error. Record them with every measurement.

| parameter | correct value | what goes wrong |
|---|---|---|
| `ELEM_SIZE` (OBLIVIATOR) | **320 B** | Its oblivious sort moves whole elements, so cost tracks element width. The stock 32 B **cannot hold a TPC-H tuple at all**; 320 B is the smallest that fits every join order evaluated here. An earlier 420 B build made its Core ~29 % slower — i.e. over-provisioning silently handicaps the baseline. |
| `OBLIVIATOR_MAX_BUF` | ≥ output size | The buffer must hold the **output** too, and the enclave does **not** bounds-check it (`ecall_scalable_oblivious_join` opens `(void)len;`). Too small → in-enclave fault → host leaves a **silent 0-byte result**, which reads as "the query cannot run". TM2 @ SF 0.1 needs 1 GB and an 8 GB enclave. |
| enclave heap | 4 GB for comparisons | `make ... Enclave_Config=enclave/trusted/configs/heap_4g.xml`. **`make` reports "up to date" if the config file is older than `enclave.signed.so`**, silently keeping the previous heap. An init time of ~112 s instead of ~27 s is the tell; `rm enclave.signed.so` forces the re-sign. |
| OBLIVIATOR `NumHeapPages` | **1048576** (4 GB) | Upstream ships 36700160 — a **140 GB** enclave. On a no-EDMM kernel the whole heap is committed at creation, so it cannot be created at all; `03-heap.patch` brings it to the same 4 GB the other engines get. `run_tests.py` also rewrites it per run, so a campaign overrides whatever the build left. |
| `MAX_BATCH_SIZE` (batching) | **8000** | A build-time `-D` flag. The header's `2000` is an `#ifndef` default, so reading the header does **not** tell you what a binary was compiled with. |

## Encrypted results

Both oblivious engines emit their join result as AES-CTR ciphertext with a
trailing `nonce` column — the host is untrusted, so the result is encrypted
before it crosses the enclave boundary. The two use the identical scheme
(same key derivation, same encrypted regions, same counter block), so one
decryptor reads both; `decrypt_result` is verified against a batching output
as well as a single-ecall one.

`is_encrypted` and `nonce` stay outside the encrypted regions by design: the
host needs the nonce to decrypt, and uses `is_encrypted` to tell data rows
from the plaintext stream markers (`field_type` is itself encrypted).

## Join order

Neither engine can choose a join order obliviously: picking a good one requires
cardinality statistics, and consulting them is a data-dependent decision that
leaks. Both are therefore reported as the **mean over the orders they could
legitimately pick**, not their best.

```bash
scripts/tm_blind_campaign.sh 3          # TM: both engines, every order
scripts/higgs_blind_campaign.sh 4 1     # Higgs: sampled 4 orientations per cell
./sgx_join <query> <data> /dev/null list   # how many orientations a query has
```

Our engine's freedom is the **join-tree orientation** (root choice × child
ordering), enumerated by `sgx_join ... list`; OBLIVIATOR's is the binary plan
shape. `scripts/tree_variants.sh` runs each orientation and verifies **every one
of them** tuple-exact against SQLite, at every rung of the ladder.

An earlier version checked only that the orientations agreed with *each other*
(a content digest) at the deeper rungs, where a SQLite comparison is expensive.
That was weaker than it looked: agreement is not correctness, since a bug
affecting every orientation identically would pass. Ground truth is now used
throughout.

## Higgs workload

Real Twitter/Higgs activity data, band predicates with fixed window **W = 4 s**,
on a day ladder (1d / 2d / 3d).

| family | shape | queries |
|---|---|---|
| `tw3/4/5` | application band joins over the follower graph | 3 |
| `hop1-8` | self-band **chain**, k = 2…9 | 8 |
| `btree1-8` | self-band **tree**, k = 2…9 | 8 |

Chain and tree at equal depth read byte-identical input, so the pair isolates
join topology. Fetch and regenerate with:

```bash
scripts/fetch_higgs_data.sh          # SNAP source data into ./higgs-data/
python3 scripts/gen_higgs_data.py    # day ladder
python3 scripts/gen_higgs_hops.py    # chain/tree families
```

## Data

TPC-H **SF 0.001 and 0.01 ship** (plaintext and encrypted). **SF 0.1 is
regenerate-only** — the TPC-H generator is licensed and not redistributable:

```bash
DBGEN_DIR=/path/to/tpch-dbgen scripts/gen_all_data.sh 0.1
```

Name the scale. With no argument the script would target all three, and it
refuses the two that ship: dbgen's string columns depend on its text pool, so
regenerated plaintext will not match the committed ciphertext for the same
scale, and the suite would fail with correct row counts and mismatched values.
Pass `FORCE=1` to override, which drops the stale ciphertext so the plaintext
fallback below applies.

Regenerated scales are plaintext-only; the drivers detect that and use the
plaintext directory for both input and ground truth.

## Script reference

Everything shipped, and what invokes it. Nothing here is dead code.

**Entry points**

| script | purpose |
|---|---|
| `scripts/run_all.sh` | Docker entrypoint. `QUICK=1` smoke → default correctness → `FULL=1` campaigns |
| `scripts/run_tm_3engine.sh` | TM across single-ecall, batching, OBLIVIATOR |
| `scripts/run_higgs_2engine.sh` | Higgs across single-ecall and batching |
| `baselines/build_batching.sh` | build the batching engine from `baselines/batching/` (ours, included) |
| `baselines/build_obliviator.sh` | download OBLIVIATOR from Zenodo, verify, patch, build |

**Correctness**

| script | purpose |
|---|---|
| `tests/e2e_sqlite_compare.py` | the tuple-exact multiset comparison everything else calls; decrypts encrypted results first |
| `decrypt_result` (built by `make`) | round-trips an encrypted result CSV back through the enclave; reads **either** engine's output |
| `scripts/run_e2e_tests.sh` | standalone correctness suite over all TPC-H test queries |
| `scripts/retro_verify.sh` | verify result CSVs already on disk, no enclave run |
| `scripts/backfill_verify.py` | fill the verdict column into older `*_variants.tsv` |

**Measurement**

| script | purpose |
|---|---|
| `scripts/tm_breakdown.sh` | single-ecall TM Total/Init/Execution, n reps |
| `scripts/higgs_perf.sh`, `scripts/higgs_perf_batch.sh` | Higgs timings, single-ecall and batching |
| `scripts/higgs_series.sh` | Higgs correctness across the whole query family |
| `scripts/tree_variants.sh` | run one query under many join-tree orientations |
| `scripts/tm_blind_campaign.sh`, `scripts/higgs_blind_campaign.sh` | blind-order campaigns |
| `scripts/tm_blind_table.py`, `scripts/higgs_blind_table.py` | tabulate those campaigns |
| `scripts/measure_lib.sh`, `scripts/run_env.sh`, `scripts/stats.py` | shared helpers |

**Data generation**

| script | purpose |
|---|---|
| `scripts/gen_all_data.sh`, `scripts/gen_tpch_data.py` | TPC-H at any scale factor |
| `scripts/fetch_higgs_data.sh` | SNAP Higgs source data |
| `scripts/gen_higgs_data.py`, `scripts/gen_higgs_hops.py`, `scripts/higgs_common.py` | Higgs day ladder, chain/tree families |
| `scripts/export_queries.py`, `scripts/input_sizes.py` | derive query listings and input sizes from the committed data |

## Known limits

| case | status |
|---|---|
| TM3 @ SF 0.1, single-ecall | does not complete within 2 h at a 16 GB heap; **no OOM observed**. Predicted peak ~7.8 GB from `(input + output) × entry_t`, a model that reproduces four measured cells within ±6 %. Not resolved either way. |
| TM3 @ SF 0.1, OBLIVIATOR | ~8.5 GB of output needs a buffer larger than the enclave we can give it. Calculated, not measured. |
| OBLIVIATOR in SIM | no simulation path upstream |
