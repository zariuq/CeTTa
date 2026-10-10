# Native memory collections

## Local quick start

The native service and generated-data test need GCC, Make, Python 3,
`pkg-config` and GMP development headers. No Rust build, model download,
Chroma, SQLite or Python embedding library is required for this test.
On Debian/Ubuntu, install `build-essential pkg-config python3 libgmp-dev`.

```bash
git clone --branch feature/our-cetta-unified https://github.com/godelclaw/CeTTa.git
cd CeTTa
make BUILD=core ENABLE_LIB_PROLOG=0 ENABLE_JSON_GSLT=0 \
  ENABLE_MANAGED_SYMBOLS=1 test-memory-collection
make BUILD=core ENABLE_LIB_PROLOG=0 ENABLE_JSON_GSLT=0 \
  ENABLE_MANAGED_SYMBOLS=1 install-memory-service MEMORY_SERVICE_INSTALL_ARGS=--enable
python3 examples/memory/native_collection.py
```

The last two commands install/start a **user** service on Linux/systemd and run
a two-record demonstration in a temporary named space. Without systemd, omit
`--enable`, start the installed `cetta-space-service` with your own private data
directory and socket, and pass `--socket` to the example. All paths are computed
from the current user or supplied arguments. No private configuration or
personal memory export is shipped. The generated-data test creates and removes
its own temporary storage; its small receipt stays under `runtime/`.

For an existing Chroma-backed application, follow the optional
[collection bridge guide](https://github.com/godelclaw/petta_lib_chromadb).
Run offline replay and shadow qualification before selecting native mode.

## Architecture

`lib/petta/lib_memory_collection.metta` implements collection policy in MeTTa:
profile validation, metadata matching, ID lookup, mutation plans, episode-link
updates, index invalidation and cosine retrieval. The C layer supplies owned
tensor values, bulk numeric kernels, binary transport and durable named spaces.
The Python adapter translates the existing collection interface into values;
it does not implement a second ranking or filtering policy.

```
application / embedding provider
             |
NativeCollection: binary transport + revision checks
             |
    MeTTa collection program <--> disposable packed matrix
             |
     conditional service batch
             |
   named native space + journal/checkpoint
```

The authoritative records are `(Memory id revision profile tensor document
metadata)`. Metadata is an expression of string-key/value pairs. A separate
`MemoryProfile` record binds a space to its embedding profile. The profile
includes model identity/version when supplied, dimensions and cosine metric.
Missing historical model provenance must remain explicit during migration.

The matrix is derived from one captured source revision. Updates and deletions
invalidate it; another writer's revision causes the worker to rebuild. Neither
recovery nor rebuilding invokes an embedding model: the records retain the
embedding bytes. Duplicate IDs in a reconstructed source are an error.

## Interface

`tools/memory_collection.py` exposes `NativeCollection` with `add`, `upsert`,
`get`, `query`, `count`, `update` (metadata patch) and `delete`.
It supports IDs, scalar metadata equality, `$eq`, `$and` and `$or`, including
time labels. Episode links are ordinary list-valued metadata. A `None` value
in a metadata patch removes that field. Unsupported filters fail explicitly.
This is the collection subset exercised by the memory bridge, not every Chroma
API. Operations requiring documents/metadata supply them explicitly.

Queries use exact cosine search and return nearest-first cosine distances.
Embeddings are finite, nonzero `f64` vectors of the declared dimension.
Candidates tied at the boundary retain source occurrence order. That order
can change after updates; callers must not infer an ID-based tie rule.
The packed CPU kernel scans a matrix in one native call; MeTTa handles filters
and score/record association. `query-reference` retains scalar MeTTa composition
for comparison. ANN search, arbitrary range/text filters and relevance quality
are separate capabilities.

`Tensor.from_buffer` in `tools/space_service_client.py` copies supported
contiguous Python buffers into CTN1 bytes. Supported buffer formats are `q`
(i64), `d` (f64) and `B` (u8), on little-endian hosts. Unsupported layouts and
dtypes require explicit conversion. `Tensor.vector` copies a Python sequence
directly into a packed f64 buffer. Neither path creates an Atom per component.
This is safe copying, not DLPack, a writable borrow or autograd interchange.

`tensor:stack`, `tensor:row-norms` and `tensor:cosine-rows` are reusable bulk
operations. Stack checks homogeneous vector shapes. Cosine rows consumes the
norms belonging to that immutable matrix; callers are responsible for pairing
them correctly. Row norms and query norm must be finite and positive.

## Processes and durability

Build `make BUILD=core space-service value-worker`. The output names include
the selected build configuration. `cetta-value-worker MODULE ENTRY` runs an
operator-selected MeTTa program over correlated CSP1/CAV1 stdin/stdout messages.
Its requests carry values, not generated source strings. It has its parent's
authority and is **not a sandbox for untrusted programs**.

The separate space service still advertises `no-eval`. It accepts stored values
and conditional batches, never worker code. Native tensor clients require the
`tensor-ctn1-native` capability. This collection currently uses native spaces;
PathMap/MORK tensor ingress is not claimed by it.

Mutations use the captured revision. A lost response is reconciled through the
same request's retained receipt; absence remains uncertainty. Mutations are
never blindly resubmitted against a refreshed revision. Read-only snapshot
construction may restart after a lost connection or cursor. A dead query worker
is reconstructed. Successful durable writes remain successful even if updating
the disposable local view fails.

`scripts/install_memory_service.py --server SERVER --worker WORKER --enable`
installs a content-addressed release and the user `cetta-memory.service` unit.
The unit starts a dedicated private data directory and socket, restarts after
failure, and is enabled for the user's systemd session. Boot before login
requires that the host separately enable lingering for that user. Inspect with
`systemctl --user status cetta-memory.service` and `journalctl --user -u
cetta-memory.service`. Installation does not stop an already running owner;
restart that service at a suitable application boundary when changing binaries.
Use repeated `--dependent-service NAME.service` options to give agent user
services a `Wants`/`After` dependency on the owner. This orders startup without
stopping an agent merely because storage is temporarily restarting.

Service quotas are configurable (`--max-live-bytes`, `--max-query-bytes`,
`--max-snapshot-bytes`). A quota failure is an error, not an empty query result.
Clients bound batch and page sizes; the matrix remains memory-resident. Journal
compaction runs at the configured growth threshold and can also be requested.
Deletion removes current records;
older journals, exports, application logs and backups need their own retention
policy. No secure-erasure guarantee is made.

## Qualification and migration

Run `make BUILD=core test-memory-collection`. The generated 257-by-16 fixture
uses an independent scalar cosine reference. It exercises owned buffer ingress,
exact embedding round trips, malformed inputs, filters, updates, episode links,
deletion, stale writes, worker loss, owner restart and checkpoint reopening.
Inputs are generated from a seed and temporary stores are removed. The JSON
receipt records failure before any success claim.

The optional `petta_lib_chromadb` bridge supplies consistent private export,
NumPy/Chroma replay, bounded background shadow comparisons, native activation
and verified rollback. All bridge writers must share its lock; raw Chroma
writers must be quiescent for migration. Shadow mode keeps Chroma authoritative;
native mode retains Chroma as a migration fallback. Qualification should include
real collection geometry, selective filters and restart, not only this small
generated fixture. Document-vector perturbations test numerical retrieval and
do not measure semantic relevance on actual user questions.

MeTTapedia's `Mettapedia.Algebra.MemoryCollection` proves 20 laws of the keyed,
revision-bound model, including stale-write refusal, deletion after recovery,
view invalidation and shadow noninterference. Faithful native lookup, validation,
durable I/O and numerical ranking remain implementation obligations. Runtime
tests and mutation controls check those obligations separately.
