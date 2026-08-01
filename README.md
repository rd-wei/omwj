# Oblivious multi-way joins — reproducibility artifact

Compares three oblivious join engines on TPC-H multi-way joins, and two of them
on real-data multi-way **band** joins.

| engine | what it is | source |
|---|---|---|
| **single-ecall** | this repo — one ecall runs the whole oblivious pipeline in-enclave | included |
| **batching** | same algorithm, host-orchestrated, enclave executes fixed-size batches | included — `baselines/batching/`, built by `baselines/build_batching.sh` |
| **OBLIVIATOR** | third-party oblivious **binary equi-join** operator (USENIX Security '25) | downloaded from Zenodo + patched by `baselines/build_obliviator.sh` |

## What runs where

| workload | engines | why |
|---|---|---|
| **TM** (TPC-H TM1/TM2/TM3) | all **three** | equality joins, which all three can express |
| **TB / BM** (TPC-H band joins: TB1, TB2, BM1) | **two** | band predicates — see below |
| **Higgs** (band joins, k = 3…9) | **two** | OBLIVIATOR implements binary *equality* joins only, so these queries cannot be written for it at all |

| tier | engines | what you get |
|---|---|---|
| **SIM** — any x86, no SGX device | single-ecall, batching | correctness, tuple-exact against SQLite |
| **HW** — real SGX (e.g. Azure DCsv3) | all three | correctness **and** timings |

OBLIVIATOR is absent from SIM because it has no simulation path upstream:
`oe_create_parallel_enclave` is called with `flags=0`, so it requires real SGX.
That is a property of the published artifact, not of this harness.

## Quick start

```bash
docker build -t omwj-sim .
docker run --platform linux/amd64 --rm omwj-sim          # correctness suite
docker run --platform linux/amd64 --rm -e QUICK=1 omwj-sim   # ~30 s smoke test
```

The default build is the **two-engine SIM image** (our single-ecall engine and the
batching baseline). OBLIVIATOR is not in it: upstream has no simulation path, so it
needs real SGX. Add it with `--build-arg WITH_OBLIVIATOR=1`, which also pulls in
OpenEnclave — note that Microsoft ships `open-enclave` for Ubuntu 20.04 but not for
the 22.04 base this image pins, so that layer currently fails to install.

On SGX hardware, for timings:

```bash
docker build --build-arg SGX_MODE=HW --build-arg VECTORIZE=on -t omwj-hw .
docker run --device /dev/sgx_enclave --device /dev/sgx_provision \
           -v /var/run/aesmd:/var/run/aesmd --rm -e FULL=1 omwj-hw
```

Without Docker, on a machine with the SGX SDK installed:

```bash
make SGX_MODE=HW VECTORIZE=on SGX_DEBUG=0 \
     Enclave_Config=enclave/trusted/configs/heap_4g.xml
scripts/run_all.sh                 # correctness
scripts/run_tm_3engine.sh          # TM across three engines
scripts/run_higgs_2engine.sh       # Higgs across two
```

## Layout

```
app/ common/ enclave/   single-ecall engine
input/                  data — TPC-H (SF 0.001, 0.01) and Higgs (1d/2d/3d)
baselines/
  batching/             the batching engine (ours) -- source, no data
  build_batching.sh     build it at MAX_BATCH_SIZE=8000
  build_obliviator.sh   download from Zenodo, verify sha256, patch, build
  obliviator/patches/   the only OBLIVIATOR-derived content shipped: our diffs
  obliviator/*.py       our harness that drives OBLIVIATOR multi-way
scripts/                build, correctness and measurement drivers
tests/                  SQLite reference comparison
```

## Correctness

Every result, from all three engines, is verified **tuple-exact** (multiset,
column-aware) against SQLite running the same SQL on the same plaintext data:

```bash
python3 tests/e2e_sqlite_compare.py input/queries/tpch_tm1.sql \
        input/plaintext/data_0_001 /tmp/out.csv
```

`scripts/run_all.sh` does this for every query in the suite and exits non-zero
if any check fails.

Both oblivious engines write their result as **ciphertext** — the host is
untrusted, so the join output is AES-CTR encrypted before it leaves the enclave
and each row carries the `nonce` needed to decrypt it. The comparator detects
that trailing column and round-trips the rows back through the enclave
(`decrypt_result`, using a 64 MB-heap signing of the same enclave so it costs
~0.2 s rather than ~27 s) before comparing. Encrypting the output therefore
costs nothing in verification strength.

If `decrypt_result` is missing the check **fails loudly** (`NODEC`) rather than
falling back to a weaker schema-and-count comparison — an unverified result must
never look like a passing one.

## Reproducing

`REPRODUCE.md` has the full guide, including the parameters that silently change
results (they are easy to get wrong and none of them error out when you do).
