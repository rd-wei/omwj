# Measurement definitions and runtime configuration

This is the reference `scripts/measure_lib.sh` points at. It defines the four timing
metrics the paper reports, says which one is comparable across systems and why, and
records the configuration every reported number was taken under.

Nothing here describes the correctness tier — that is `REPRODUCE.md` and
`tests/e2e_sqlite_compare.py`. This file is only about timings, which require real SGX.
Simulation mode models no EPC and no enclave transitions, so no figure produced in SIM
belongs in a timing table.

---

## 1. The four metrics

The engine prints `ENCLAVE_INIT`, `ECALL_TOTAL` and `PEAK_HEAP`; the driver wraps the
process in `/usr/bin/time`. From those, `scripts/measure_lib.sh` derives:

| metric | definition |
|---|---|
| `e2e` | whole-program wall clock, including one-time enclave creation |
| `init` | enclave creation alone (`ENCLAVE_INIT`) |
| `exec` | `e2e − init` — all query work after the enclave is up: in-enclave compute **plus** host-side CSV load, marshalling and result writing |
| `ecall` | `ECALL_TOTAL` — in-enclave compute alone, excluding host-side I/O |

`PEAK_HEAP` (in-enclave allocation high-water mark) and the output row count are recorded
alongside each trial.

## 2. Which metric is comparable, and why it matters

**`exec` is the cross-system metric. `ecall` is not.**

`exec` is a whole-process wall minus enclave creation, and Obliviator's `Total − Init` is
the same construction on its side. The two are like-for-like: both include host I/O, both
exclude only the one-time enclave setup.

`ecall` is a strict *subset* of `exec` — it counts in-enclave compute and nothing else.
Reporting our `ecall` against another system's wall understates our cost, sometimes by a
large factor at small scales where host-side CSV parsing dominates. An earlier revision of
this work did exactly that and produced a speedup range roughly three times too favourable.
Do not compare across systems on `ecall`; use it only to decompose our own `exec` into
in-enclave and host-side halves.

**`init` is not query work.** Without EDMM, SGX commits the whole configured heap at load
time, so `init` tracks heap size and the enclave loader, never the data or the query. It is
a deployment parameter, paid once and amortised across queries. Report it separately;
never fold it into a per-query comparison. Note the rate is loader-specific — the Intel SGX
SDK and Open Enclave differ substantially at the same heap size — so an `init` figure from
one runtime says nothing about the other.

## 3. Machine state

Cells whose working set sits near the physical EPC (≈378 MB on the measurement machine)
are sensitive to how settled the machine is. The same query, build, data and heap measured
48.50 s in one session and 41.17 s in another — 15% apart — while each session's internal
spread stayed under 1%.

`measure_lib.sh` therefore idles `COOLDOWN` seconds (default 30) after every measured run,
letting the EPC and `ksgxd` quiesce. Report the machine state with any `init` figure, and
do not compare numbers across sessions that were not both taken in the settled state.

## 4. Repetitions and failures

Campaigns run `REPS` trials (3 for reported numbers) and summarise as mean ± stddev via
`scripts/stats.py`. Failed and timed-out trials are reported, never silently dropped; a
trial that does not complete appears as `timeout` or `ERR` rather than being excluded from
the mean. `TIMEOUT` caps each trial (default 2400 s).

**Never derive a number and print it as measured.** If `e2e` and `init` are known and
`exec` was not measured, report the two that were.

## 5. Configuration of record

### Our engine (single-ecall)

| | |
|---|---|
| enclave heap | 4 GB (`enclave/trusted/configs/heap_4g.xml`) for all reported comparisons |
| available heaps | 64 MB, 128 MB, 256 MB, 512 MB, 1 GB, 2 GB, 4 GB, 8 GB, 16 GB |
| `MAX_ATTRIBUTES` | 64, giving 336-byte entries — enough for the widest workload (the 9-table Higgs self-join at 54 columns) |
| vectorisation | `VECTORIZE=on` for HW timings; `off` in SIM, which may run under emulation without AVX2 |
| threading | single-threaded |

### Batching engine (external-memory baseline)

| | |
|---|---|
| `MAX_BATCH_SIZE` | 8000 — a **build-time** `-D` flag, not a runtime setting; changing it requires a rebuild |
| enclave | 32 MB for most experiments, 128 MB for the TB workload |

The enclave is sized independently of the data, so the dataset may exceed enclave capacity.
State the enclave size and batch size with each experiment.

### Obliviator (external baseline)

| | |
|---|---|
| enclave heap | 4 GB for reported comparisons, identical to ours |
| available sizes | 64 MB … 16 GB |
| `OBLIVIATOR_MAX_BUF` | set by the runner per scale: 16 MiB @ 0.001, 128 MiB @ 0.01, 256 MiB @ 0.1 |
| element payload | sized to the query rather than over-provisioned — its oblivious sort moves whole elements, so an oversized payload inflates its cost |
| threading | single-threaded, matching ours |

**`OBLIVIATOR_MAX_BUF` is ours, not theirs, and it is load-bearing.** The EDL copies the
whole buffer into the enclave on every ECALL, so it is both a floor on the required enclave
heap and the dominant per-step wall cost. It is also a correctness hazard: the enclave entry
point takes a buffer length and discards it, writing results back into the input buffer
unchecked, so a buffer smaller than the result faults. A fault under an undersized buffer is
our configuration error, not a limitation of the baseline — check the buffer against the
expected result size before recording any failure.

The runner caches by query+scale within a run, so two enclave sizes for the same query must
go in separate config files.

## 6. Join order

Neither system can choose a join order obliviously: the optimal plan depends on intermediate
cardinalities, which are a property of the data, so consulting them is itself a
data-dependent branch. The Obliviator artifact ships no planner, so every multi-way plan is
chosen by our wrapper.

Reported numbers therefore average **both** systems over all the orders each could
legitimately pick — every connected binary plan for Obliviator, every rooted orientation of
the join tree (each rooting paired with each order of visiting a node's children) for ours.
Applying blind selection to one side and a chosen plan to the other would favour whichever
side got the choice.
