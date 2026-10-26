# Named atom spaces

A CeTTa process can own several named spaces and serve other CeTTa processes
or an independent Python client. The service stores and matches data; it does
not evaluate submitted programs. It uses a private Unix stream socket and a
SQLite-free journal with checkpoints.

The initial profile serializes storage operations on one owner while accepting
multiple nonblocking connections. Its storage choices are:

| Backend | Duplicates | Representation | Matching |
| --- | --- | --- | --- |
| `native` | preserved | CeTTa term universe | CeTTa native index |
| `native-ordered` | preserved, insertion order retained | CeTTa ordered space | CeTTa native index |
| `pathmap` | preserved | counted PathMap portable keys | maintained native index |
| `mork` | unique support | raw MORK portable keys | maintained native index |

Portable keys preserve symbol/string/number distinctions, arbitrary bytes,
binary64 bits and variable sharing. A bounded-arity byte rope avoids the
compact MORK symbol limit. This profile does not reconstruct values from MM2
text or expose MORK execution, ACT attachment or Rust query acceleration. A
checkpoint recreates the chosen backend and its matching projection from
live values. Backend count disagreement stops publication.

## Build and run

```sh
make -j1 BUILD=core ENABLE_DURABLE=0 ENABLE_HTTP=0 ENABLE_JSON_GSLT=0 \
  ENABLE_LIB_PROLOG=0 ENABLE_MANAGED_SYMBOLS=1 ENABLE_GMP=1 \
  CETTA_TEST_ISOLATED=1 space-service all
```

Use the configuration-named binaries printed by Make. A core service can load
the separately built current MORK bridge through
`CETTA_MORK_SPACE_BRIDGE_LIB=/absolute/path/libcetta_space_bridge.so`; a
`BUILD=mork` service links it directly. Native storage needs no Rust library.
An unavailable requested backend returns an error and never selects native
storage silently. RPC clients need no Rust linkage.
`space:verify` / Python's `Space.verify()` compares Rust's complete stored key
multiset with the query projection, including counted duplicates. This explicit
diagnostic is limited to 65,536 rows and 8 MiB of live portable data.

```sh
mkdir -p "$HOME/.local/state/cetta-space"
chmod 700 "$HOME/.local/state/cetta-space"
SERVICE --socket "$XDG_RUNTIME_DIR/cetta-space.sock" \
  --data "$HOME/.local/state/cetta-space"
python3 tools/space_service_client.py --socket "$XDG_RUNTIME_DIR/cetta-space.sock" demo
CETTA_SPACE_SOCKET="$XDG_RUNTIME_DIR/cetta-space.sock" \
  CETTA --lang petta examples/space-service/client.metta
```

`SERVICE` and `CETTA` stand for those binaries. The private directory must
exist under an existing parent. A second writer is refused. The socket is
owner-only (`0600`) and the data directory is `0700`; this profile is for
clients running under that owner, not network or multi-user authentication.

The optional installer copies a versioned release, verifies its files and
atomically selects it. It bundles the space and system client modules and
does not replace a general `cetta` executable.

```sh
python3 scripts/install_space_service.py --server SERVICE --client CETTA \
  --bridge /absolute/path/libcetta_space_bridge.so --enable
systemctl --user status cetta-space.service
```

The unit starts at user-manager startup. For unattended startup after reboot,
the account's user manager must start at boot; on systemd systems this is
usually configured with `loginctl enable-linger USER`. The installer does not
change that system-wide account setting. Its environment file is
`~/.config/cetta-space/service.env`. Restart the service after editing it.
The previous versioned releases remain available for rollback. Select only a
release that understands the current journal format; binary rollback alone
does not reverse a storage-format change.

## API

Import `space_service` to use `space:list`, `space:open`, `space:describe`, `space:verify`, `space:add`,
`space:remove`, `space:batch`, `space:query`, `space:count`, `space:fetch`,
`space:close` and `space:receipt`. The general `space:call` takes a socket,
request data and a timeout in milliseconds. Replies remain data.

```metta
!(import! &self space_service)
!(space:open "/absolute/socket/path" "notes" native)
; The returned descriptor is:
; (space SERVICE_UUID ID NAME BACKEND bag-or-set REVISION COUNT)
; Pass that descriptor to the helpers; refresh it after a successful write.
```

Python's `Client.open()` returns a `Space` exposing `add`, `remove`, `batch`,
`query`, `fetch`, `close_query`, `match` and `match_count`. `Space.batch()`
updates its captured revision after success. Both clients expose stale writes;
neither silently retries a mutation using a newer revision.
`Client.list_spaces()` lists current descriptors. The installed `cetta-space`
command accepts `hello`, `list` and `demo` with a `--socket` argument.

Every request is `(request ID OPERATION ARGUMENTS...)`, with a nonempty
ASCII identifier without spaces, at most 128 bytes. Names use the same limit.
Every response is `(reply ID ok VALUE)` or `(reply ID error REASON)`.

| Operation | Arguments after operation |
| --- | --- |
| `hello` | none |
| `list` | service UUID |
| `open` | service UUID, name, backend symbol, expected catalog revision |
| `describe` | service UUID, space ID |
| `verify` | service UUID, space ID |
| `batch` | service UUID, space ID, expected revision, `(ops (add VALUE) (remove VALUE) ...)` |
| `drop` | service UUID, space ID, expected space revision, expected catalog revision |
| `count` | service UUID, space ID, pattern |
| `query` | service UUID, space ID, pattern, template, maximum rows, lease milliseconds |
| `fetch` | boot UUID, cursor ID, offset, maximum page rows |
| `close` | boot UUID, cursor ID |
| `receipt` | service UUID, original request ID |
| `compact` | service UUID |

An existing name opens only with its existing backend. New names require a
current catalog revision. Dropping and recreating a name assigns a new ID.
All accepted batches advance the space revision, including no-ops. `remove`
removes one alpha-equivalent occurrence if present; absent values are no-ops.
The advertised `native-ordered` profile removes the first matching occurrence
and preserves the relative order of every survivor, through recovery and
checkpointing. Other bag profiles leave ordering after removal unspecified.
Patterns use ordinary CeTTa unification and the
template is substituted without evaluation. Open facts are freshened per
match; variables are scoped within each transmitted atom packet.

Queries materialize bounded, independently owned result rows. A snapshot
records its source and revision. Repeating a page's offset returns the same
page while its lease lasts, even after writes or compaction. Exhaustion is a
successful empty page; expired, closed, invalid or over-limit queries return
errors. Cursors are not resumed after restart. Page byte/structure limits can
require a smaller page size. A row-limit failure never claims completeness.

An acknowledgement follows synchronized journal storage and completed backend
publication. A full corrupt record prevents startup. An incomplete final
journal record is discarded; an incomplete checkpoint is refused. Checkpoint
replacement synchronizes its file and directory. These durability claims
assume the filesystem and device honor synchronization.

Successful mutation receipts are retained for up to 256 requests and 16 MiB.
The journal and checkpoint retain the original request and reply bytes;
numeric parsing cannot rewrite a retained request identity.
Exact retries return their retained answer; a changed request with a retained
ID is refused. After receipt retirement, the original expected revision still
prevents reapplication. `unknown-receipt` is uncertainty, not proof of failure.
The native client returns `space:uncertain` if a reply is lost after sending,
or `space:unavailable` if no request was sent. The Python client raises
`Uncertain` with the original request. Inspect receipts before retrying.

The authored MeTTa helpers in `(library space_transaction)` support conservative
transactions in the PeTTa profile:

```metta
!(import! &self (library space_transaction))
; Bind an already opened descriptor to $store before calling these helpers.
(space:observe $socket $store
  ((scope "pending" (Pending $id $value) (Pending $id $value)))
  ((Pending $id $value) (Receipt $id $value)) 4096 60000)
; Successful result: (SpaceObserved HANDLE READS WRITES RESULTS)
(space:commit $socket $observation $request-id
  ((remove (Pending "job-1" "ready"))
   (add (Receipt "job-1" "finished"))))
```

Every read must have the descriptor's revision. An empty query still depends
on that revision. Commit validates declared write scopes before sending one
conditional atomic batch. An intervening mutation makes the observation stale,
including when it changes a previously empty range. Scope matching neither
executes stored values nor binds variables in later operations. Snapshots are
closed after consumption or a reported read failure. No write is retried by
these helpers. Applications include their durable idempotency evidence in the
same batch as the state transition.

`space:begin` and `space:publish` provide that receipt discipline. Begin takes
an application operation key and fingerprint in addition to the read/write
scopes. It returns `SpaceTransaction`, or the previously committed result for
that same key and fingerprint. Publish commits the supplied operations and a
`SpaceReceiptV1` record together. Reusing the key with another fingerprint is
refused. Application operations cannot rewrite reserved receipt records.
An uncertain publish stays uncertain until a fresh begin observes its durable
receipt; neither helper sends or repeats an external effect.

The snapshot row allowance is explicit and excessive results return an error;
it is not a truncation policy. These initial helpers materialize each declared
read and use conservative whole-store conflicts. They are suitable for bounded
record queries, not a streaming interface to an entire archive. Ordered removal
currently uses the existing linear sequence-preserving operation.

The default quotas are 32 spaces, one million facts and 64 MiB of live portable
values per space, 64 snapshots with 32 MiB total row data, a 1 MiB frame,
65,536 atom nodes, 64 levels and 4,096 variables per packet. Queries have a
five-second computation budget. These are admission limits, not a hard bound
on process RSS. `--help` lists adjustable store/journal/query settings.
Checkpointing bounds journal growth relative to its last checkpoint; it also
rebuilds ownership to release removed native terms and old Rust roots.

## Framing and checks

CSP1 is an application protocol: `CSP1`, a big-endian 32-bit payload length,
then one CAV1 value. CAV1 uses little-endian lengths and numeric bits: `S`
symbol bytes, `T` string bytes, `I` signed 64-bit integer, `F` binary64 bits,
`Y`/`N` boolean, `J` decimal bigint, `Q` exact rational, `E` expression,
`V` variable index. First variable occurrences introduce consecutive indices;
later occurrences reuse them. Decoding creates fresh identities. Heap handles,
capabilities, mutable states, closures and private runtime tags are rejected.

`tests/test_space_service.py SERVICE CETTA` checks independent clients,
backend multiplicity, wide/binary syntax, snapshot ownership, lost replies,
conditional writes, corruption, recovery, quotas and both CeTTa dialects.
The generic transition model is
[`Mettapedia/Machines/SpaceService.lean`](https://github.com/godelclaw/MeTTapedia/blob/main/lean/mettapedia/Mettapedia/Machines/SpaceService.lean).
Its laws state protocol obligations; they do not certify physical C/Rust or
filesystem behavior. Native conformance checks establish the tested bridge.

`test-native-journal` fault-injects framed writes, recovery and checkpoint
publication. With a built MeTTapedia checkout supplied as
`NATIVE_STORE_LEAN_PROJECT`, `test-native-store-transactions` runs identical
ordered traces through the real MeTTa helpers, an in-memory SQLite oracle and
`NativeStoreTransactions.lean`. Its receipt records the program, model and
binary hashes alongside every observed state.

The PeTTa `(library rel)` module supplies ordered-bag selection, projection,
joins, existence, distinctness, stable ordering, paging and sequential grouped
or running folds. Predicates use `RelTrue`, `RelFalse` and `RelUnknown`;
comparisons and NULL policy are explicit caller adapters. Callbacks must be
pure, deterministic and terminating. Zero or multiple callback answers are
reported, never converted into a partial successful bag. `test-rel` compares
complete results against SQLite and sequential reference folds.
