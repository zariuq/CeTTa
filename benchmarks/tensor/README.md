# Generated tensor and retrieval benchmark

This suite checks the implemented native tensor slice through CeTTa's public
API, with optional NumPy and Chroma comparisons. Its datasets are generated
from a recorded seed. It keeps no corpus, database, model weights or per-query
output files. One temporary MeTTa input is overwritten between stages and
removed afterward; a JSON receipt is the retained artifact.

The suite includes:

- Native C unit checks when run through the Make target, including ownership,
  exact bit identity, codec boundaries and independent arithmetic references.
- Existing PeTTa, HE and Prime fixtures, including actual one-/four-thread runs.
- Generated `i64`, `f64` and `u8` cases over scalar, empty and higher-rank shapes;
  construction, arithmetic, copy, reshape, functional update and codec roundtrip.
- Exact retrieval over generated records: profile/group filtering before
  top-k, distinct IDs with equal vectors, empty selections, revision updates,
  insertion, deletion and filtered deletion.
- Seven deliberately wrong result controls: wrong score, revision, profile,
  deleted ID, top-k, missing result and duplicate result.
- Repeated bulk fill/sum/dot measurements, including process startup and
  library import. These are end-to-end measurements, not isolated kernel times.

## Run

From the repository root:

```sh
make benchmark-tensor-compact BUILD=core \
  TENSOR_BENCH_ARGS="--output runtime/tensor-compact.json"
```

To use an existing tensor-enabled binary directly:

```sh
python3 benchmarks/tensor/compact.py --binary ./cetta \
  --output runtime/tensor-compact.json
```

The direct command reports the C unit gate as `not-run` unless supplied
`--unit-binary`. NumPy comparisons run when NumPy is available; request
`--numpy required` to fail if it is missing. A native-only run uses
`--numpy off --chroma off` and needs only Python's standard library.

The default workload has 32 generated records plus two deliberate ties,
16 components per record, and bulk lengths 1,000 and 100,000. Change
`--records`, `--dimension`, `--lengths`, `--repeats`, `--threads` and `--seed`
to explore other sizes. For example:

```sh
python3 benchmarks/tensor/compact.py --binary ./cetta \
  --records 128 --dimension 64 --lengths 1000 100000 1000000 \
  --output runtime/tensor-scale.json
```

Output records binary/source hashes, seed, case counts, whole-process timing,
Linux RSS measurements, largest temporary input and retained dataset bytes.
Reusing an output path replaces its receipt. A failed rerun writes `fail`, so
an older passing receipt cannot masquerade as that run's result.

## Early Chroma comparison

An installed Chroma Python client is optional. The comparison uses Chroma's
`EphemeralClient`, supplies embeddings explicitly, disables its embedding
function and anonymous telemetry, and removes the collection it created.
No embedding model is downloaded. The adapter uses the current collection
configuration API and was qualified with Chroma 1.5.9.
Its HNSW batch size is two, so the small fixtures exercise indexed data rather
than leaving every embedding in the default brute-force staging buffer.

```sh
python3 benchmarks/tensor/compact.py --binary ./cetta \
  --numpy required --chroma required --output runtime/tensor-chroma.json
```

The same records, queries and lifecycle changes pass through CeTTa and Chroma.
CeTTa is checked against an independently calculated finite dot-product
reference. Chroma hits must carry current IDs, profiles, revisions and groups,
and satisfy the same filter and score requirements. Chroma's `ip` distance is
`1 - dot`; the adapter converts it back to a score. CeTTa preserves source
occurrence order among selected hits; Chroma orders distances. Comparison
therefore uses IDs and scores rather than output order.

CeTTa's boundary tie policy selects earlier occurrences. Chroma recall permits
any equally scoring boundary occurrence and still requires all strictly better
hits. Recall is averaged only over queries with nonempty expected results;
empty queries and `k=0` cannot inflate it. The default requires recall 1.0 on
each nonempty query in these small fixtures; `--min-chroma-recall` declares that
minimum per-query threshold
for a larger ANN experiment. Recall threshold failures retain the measured
native outcome and per-query recalls in a failed receipt; they cannot become
passing comparisons. Chroma's unsupported `n_results=0` is represented
as a local empty result by the comparison adapter.

This is an early comparison adapter, not a production collection API or native
tensor/Python buffer exchange. Generated records have unique IDs. Native spaces
can contain duplicate occurrences of the same ID, while Chroma collections use
IDs as keys; an application adapter must explicitly resolve that difference.
Native remove/add updates here are sequential, not an atomic transaction.

## Capacity and growth

[capacity.py](capacity.py) measures repeated operations inside one process,
after constructing resident values and discarding the first sample. It checks
the record and score counts, validation output and exact selected hits. The
clock surrounds the operation and result binding; the answer check follows it.
Whole-process timing and peak RSS are also retained separately.

```sh
make benchmark-tensor-capacity BUILD=core \
  TENSOR_CAPACITY_ARGS="--check-growth --rebuild-control --output runtime/tensor-capacity.json"
```

The default compares 2,000 with 8,000 generated 64-component records. It times
validation alone, scoring/selection over an already retrieved view, and a full
space search separately. A repeated resident million-element dot product also
separates operation time from construction and startup. This measures the
current interpreter/native boundary, not an optimized BLAS provider.

With `--check-growth`, the validation median must grow by at most the record
ratio times `--growth-overhead` (default 1.5): a fourfold input increase allows
at most sixfold time. Timing gates are explicit qualification profiles because
machine contention can affect them. `--rebuild-control` restores the original
recursive validator only within the generated probe; with the growth gate,
the same bound must reject that control. A failed run replaces its old receipt.

For an embedding dimension closer to an agent memory workload, use
`--records 1500 6000 --dimension 1024`. Inputs contain compact `tensor:full`
construction commands rather than one source atom per component. Generated
vectors isolate capacity costs; they do not establish semantic retrieval
quality or replace the independently randomized lifecycle suite above.

## Resident collection comparison

[collection.py](collection.py) exercises the binary collection adapter in one
resident process. It generates 1,000 and 4,000 vectors of dimension 384,
imports packed buffers, and removes its temporary named spaces on completion.
No model or corpus is downloaded. The combined vector payload is about 15 MB;
the default leaves only a bounded receipt, with zero retained dataset bytes.

```sh
make BUILD=core ENABLE_LIB_PROLOG=0 ENABLE_JSON_GSLT=0 \
  ENABLE_MANAGED_SYMBOLS=1 benchmark-memory-collection \
  MEMORY_COLLECTION_BENCH_ARGS="--chroma required --check-growth"
```

Install a Chroma Python client separately for `--chroma required`. `--chroma
off` needs only the standard library. To run the selected binaries with a
different Python environment:

```sh
python3 benchmarks/tensor/collection.py --server SERVER --worker WORKER \
  --records 1000 4000 --dimension 1024 --repeats 24 \
  --chroma required --output runtime/resident-collection.json
```

The queries cover unfiltered top-k, 1%, 10%, 25% and 50% filters, single-row equality,
nested conjunction/disjunction and empty selections. A separate scalar
`math.fsum` oracle checks exact native results with boundary-tie tolerance;
eight deliberately wrong results must be rejected. Updates change a vector
and its filter metadata, deletion retires an equal-vector occurrence, and
worker loss requires reconstruction. The first query, the first query after
a write, and repeated warm queries are reported separately. Ordinary writes
must preserve the base matrix, as well as preserve exact answers.

Chroma uses the same inputs and cosine metric, with explicit HNSW search width
128, batch size 32 and one index thread. The default record counts exceed the
search width. Scores, eligibility and result cardinality are checked even when
ANN recall is imperfect. Recall is reported on nonempty selections;
`--min-chroma-recall` declares an optional per-query qualification threshold.
The default zero threshold reports recall without asserting exact ANN answers.
Native correctness always requires exact top-k within the stated tolerance.

`--check-growth` checks unfiltered and group-query medians against the record
ratio times `--growth-overhead` (default 1.5). Timing gates are optional because
contention affects them. Receipts bind sources, selected module and binaries;
changing any of them during a run fails qualification. A failed rerun replaces
its old passing receipt. No generated text or embedding is retained in it.

This profile measures local resident numerical retrieval. It does not establish
semantic relevance, persistent Chroma performance, million-record ANN capacity
or full tensor-library completion. The CI profile uses smaller generated
inputs, omits optional Chroma, and checks correctness and lifecycle without a
machine-dependent latency threshold.

`--batch-sizes 1 4 16` measures correlated query batches, checking every answer
against the same scalar oracle. `--profile-stages` adds per-stage medians and
resident allocation observations; `--resident-repeats` changes the probe
length. `--scan-tier f32` selects compact screening with f64 reranking; the
default remains `wide`. Include both dimensions and filter selectivity when
comparing tiers. The warm worker may retain previously unseen row values and
up to 128 filter masks; growth before these caches plateau is not itself
evidence of a per-query leak. `make test-memory-residency` exercises 3,000 row
queries and 3,000 unique filter queries and checks their observed plateaus.

## Native screening size sweep

[screening.py](screening.py) drives the C tensor API directly, separately from
the collection/interpreter benchmark. Generated matrices stay in RAM and are
released after each size; only an aggregate receipt is written to disk.

```sh
make BUILD=core ENABLE_LIB_PROLOG=0 ENABLE_JSON_GSLT=0 \
  ENABLE_MANAGED_SYMBOLS=1 benchmark-tensor-screening \
  TENSOR_SCREEN_ARGS="--records 1000 4000 32000 128000 --dimension 1024"
```

The sweep alternates tier order, discards the first sample, measures complete
scan/selection paths and records retained-candidate counts. Every interval
must contain the original f64 kernel result, and screened reranking must
return the identical top-k indices. A separate long-double scalar calculation
checks selected scores. The receipt binds binary/source hashes; changing an
artifact or failing a run replaces a previous pass with failure. This is not
an end-to-end million-record collection or ANN benchmark. A million rows can
be requested with `--records 1000000`, choosing the dimension explicitly.

## Measured scaling sample

The [2026-10-11 receipts](results/scaling-20261011/) contain seeded aggregate
measurements, source/binary hashes and reference checks. They retain no corpus
or query identifiers. These are shared-host observations, not latency targets
for other machines.

At 5,514 rows of 1,024 dimensions, the collection's warm unfiltered top-5
median was 2.06 ms with the default wide scan and 1.87 ms with f32 screening.
Chroma 1.5.9 measured 1.76–1.82 ms in those two runs, with `ef_search=128` and
one index thread. Each profile used twelve queries. Both native tiers passed
the independent scalar reference; Chroma's mean recall over nonempty filter
and unfiltered queries was 98.5% and 97.6%. This generated workload is not a
replay of a deployed application's records. Repeated query batches warmed
differently and are reported separately in the receipts.

The direct C scan/selection sweep isolates another tradeoff:

| Rows | Dimensions | Wide median ms | Screen/rerank median ms |
| ---: | ---: | ---: | ---: |
| 1,000 | 1,024 | 0.102 | 0.124 |
| 4,000 | 1,024 | 0.419 | 0.498 |
| 32,000 | 1,024 | 5.925 | 4.440 |
| 128,000 | 1,024 | 19.964 | 16.911 |
| 1,000,000 | 128 | 33.083 | 41.756 |

All tested screened results matched the original f64 kernel's top-k indices.
Short vectors can make the extra bounds and selection passes more expensive
than the saved matrix traffic, even at a million rows. There is no universal
row-count crossover, and the library keeps the wide tier as its default.

The collection sweep also checks 1%, 10% and 50% eligibility at 32,000 × 128.
An unfiltered native query measured 2.15 ms versus Chroma's 1.13 ms; native
filtered medians were 0.65–1.42 ms versus 11.36–25.47 ms. Chroma's mean recall
on that sample was 95.1%, with a minimum of 40%. It must not be treated as an
exact reference. Native evaluation occupied most of the measured worker wait;
the wait cannot be attributed wholly to transport overhead.

## Coverage and completion

[coverage.json](coverage.json) records the full programme's nine families and
their remaining obligations. Every receipt retains `programme_complete=false`:
passing the present slice cannot establish that unimplemented features work.

| Family | Completion qualification |
| --- | --- |
| Values and arithmetic | Shape/dtype/value cases, strides, overlap, broadcast, indexing, promotion, matrix/contraction and exceptional arithmetic |
| Ownership and control | Arena/continuation/branch lifetimes, external aliases, failure unwinding, sanitizer and compiled omission controls |
| Python interchange | NumPy buffer and DLPack roundtrips, producer deletion, no-copy refusals, generic array protocols, PyTorch gradients |
| Retrieval lifecycle | Shared collection contract, three metrics, filters, batches, profile/model versions, updates and deletion |
| Hypervectors | XOR/popcount, permutations, binding/inverses, bundling/ties, seeded distributions and task accuracy |
| Execution providers | Kernel/allocation/hash/transfer costs, graph/fusion equivalence, BLAS/GPU numerical and deterministic profiles |
| ANN quality | Recall versus latency/memory/build cost, selective filters, churn, clustered and anisotropic cases, small real-data relevance fixture |
| Service and durability | Tensor capability negotiation, backend interchange, coherent revisions, reopen/crash recovery, retention and index rebuild |
| Training and learning | PyTorch-owned training first; native custom-op gradient checks, then the separate native differentiation/training programme |

Additional profiles should reuse generated inputs and bounded receipts. A
small, versioned real-data fixture is still required for relevance testing:
synthetic exact-neighbor agreement does not establish semantic retrieval
quality. Large public corpora and GPU training datasets belong to optional
qualification, not the default development gate.

The integration profiles should include a small MLP and attention block over
generated data, a hypervector associative-memory task, filtered retrieval with
update/delete churn, and reopening/rebuilding a small persisted space. These
exercise interacting operations without downloading a trained model or keeping
a large dataset. They are completion targets; those additional profiles are
not implemented by the present runner.

The [Array API test suite](https://github.com/data-apis/array-api-tests) supplies
generated shape/dtype/value conformance checks once an appropriate native
adapter exists; its full suite does not run here yet.
[ANN-benchmarks](https://github.com/erikbern/ann-benchmarks) supplies useful
recall, latency, build-time and index-size reporting conventions. Native ANN,
provider-specific numeric guarantees and Chroma persistence remain unqualified.

Chroma references: [collection configuration](https://docs.trychroma.com/docs/collections/configure),
[updates and upserts](https://docs.trychroma.com/docs/collections/update-data),
[querying and metadata](https://docs.trychroma.com/docs/querying-collections/query-and-get).
