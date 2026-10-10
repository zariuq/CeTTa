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
