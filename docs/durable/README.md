# Durable named spaces

The [native mailbox guide](../mailbox/README.md) describes single-channel
receives on the named-space service. Both services use native CeTTa spaces and
the same checksummed journal framing. This library supplies one local commit
domain across named record spaces; the mailbox service commits within one
physical space.

Build with `ENABLE_DURABLE=1`. Durable state has no SQL dependency. Access to
application-owned SQL databases is a separate `ENABLE_SQLITE=1` feature.
Applications use MeTTa data and the durable API, not storage-specific queries.

CLI administration is off by default. A dedicated maintenance invocation,
`cetta --durable-admin repair.metta`, enables literal top-level administrative
directives after `!(import! &self durable)`. Service and cognitive-worker
invocations must never pass this flag: their editable bootstrap files are
ordinary application code. Without the flag, even literal root directives
return `(durable:failure commit-boundary-required)`.

Administrative commits are repair-only, never application transitions. Their
arguments are literal data. For example,
`!(durable:open "coordination.db")` opens the store and returns its epoch and
revision. A subsequent `!(durable:commit "EPOCH-FROM-OPEN" 0 ((durable:insert
"state" "worker" (waiting 0))))` uses the actual 32-character epoch from that
reply. `!(durable:read "state")`, `!(durable:checkpoint)` and
`!(durable:close)` inspect, checkpoint and close it.

These are administrative commands, not effectful functions available to a
rewrite. Ordinary evaluation returns `(durable:failure commit-boundary-required)`
for their native implementation, even through user equations, `eval`, imports,
`superpose`, `collapse`, or deferred rho payloads. This deliberately replaces
the initial API that allowed a `case` around `read` to call `commit` directly.
Administrative results are returned as inert data without entering reduction.
An embedded caller must explicitly use the administrative C entry point or the
store API, outside evaluation.

The durable host will evaluate MeTTa/rho reactions against supplied snapshot
data, obtain proposed changes, select one transition, and commit through the
C API. No evaluator branch receives this authority. This boundary gates durable
administration; the host must additionally deny unmediated HTTP, foreign calls,
and other external effects during speculative evaluation.

`open` returns a recovered snapshot. `read` returns
`(durable:snapshot epoch revision (records ...))`; each record is
`(durable:record "space" "key" value)`. `read "*"` reads all named spaces.
Returned data is immutable and its nested expressions are not evaluated.
Records are enumerated by their last mutation's revision and batch position;
applications needing a different order must store their ordering key.

Each key identifies an occurrence, so identical values under different keys
remain distinct. Insert requires an absent key; replace and remove require an
existing key. A batch may name a key once. All preconditions must hold or the
whole batch rolls back. Success is `(durable:committed epoch new-revision)`.

The administrative commit API validates the expected epoch and global revision
without a read set. Use the scoped C API for application decisions:

1. `cetta_durable_observe` reads declared keys, prefixes and whole spaces in
   one transaction, returning an owned observation with immutable views.
2. Evaluation uses those views outside the transaction. All reads affecting
   the decision must be declared, including absence and range predicates.
3. `cetta_durable_commit_observed` validates the exact keys and revisions in
   each view under the native commit lock, then applies the batch. Every written
   key must also be covered by a declared scope. Empty views validate absence.

The host must enforce three observation invariants:

- Every durable value visible to a decision comes from this observation. Do not
  supply cached state from an earlier snapshot, including remembered routing.
- The host pairs a proposal with the exact observation it supplied. A proposal
  cannot declare, widen or replace its own read scopes.
- LLM output, clock readings and randomness enter as recorded observations.

Together with denial of durable reads during evaluation, these rules make the
host-supplied observation the complete durable read set of the decision.

Policy loaders can carry their own observed dependencies into that read set.
`cetta_durable_observe_joined` checks an earlier observation while reading new
scopes in one transaction. New views come first, followed by owned copies of
the earlier scopes. Their store epoch, complete keys and record revisions
must still agree. This includes recorded absences and complete prefix results;
an unrelated update may proceed. The returned observation remains independent
of the earlier owner and protects every combined scope at commit or watch.

When space/channel grants or the immutable program policy were derived from
durable records, pass that policy observation to
`cetta_host_begin_with_dependencies`. The host still constructs the permitted
grants and pure validation context; observing a record grants no capabilities.
A policy change can refuse either admission or later acceptance, even while
the old context is alive. Restoring its old bytes under a fresh revision also
refuses. Dependency views do not add indexed grants or `host:record` facts to
the evaluator. Planning runs against its original capture without checking the
store each quantum. A conflict requires a new policy observation, context
and decision; it cannot retarget an existing plan.

This revocation orders against acceptance. An already accepted outbox intent
or an external operation in progress needs the channel's own cancellation and
uncertain-outcome protocol. The original `cetta_host_begin` remains appropriate
for grants whose policy is fixed for the decision's lifetime.

Validation uses exact comparisons, not hashes, and needs no tombstone table.
Unrelated writes do not conflict. A removed and reinserted record has a new
revision and invalidates a positive read. An insertion followed by removal
preserves an absence observation: the predicate is true again at commit.
Prefixes compare raw key bytes with no wildcard interpretation. Observations bound their total materialized size,
including overlapping scopes. They retain no storage lock or transaction.

A conflict permits retrying a pure decision; it does not permit repeating an
external effect. In particular, persist a completed LLM response as an input
observation rather than repeating its request to rebuild a draft. After losing
a commit acknowledgment, inspect the accepted-work record before proposing
another action.

## Storage and recovery

Live values are inert string/integer records in a private native CeTTa space.
A commit checks its complete read manifest, preconditions, accounting and
admission limits, then prepares the next space projection before appending one
whole-batch journal frame and syncing it. Publication swaps in that prepared
projection without further allocation or semantic checks.

The shared CSL1 frame contains a length, a SHA-256 digest and a typed CDR1
payload. Recovery replays records as data. An incomplete final append is
truncated on reopen; a complete corrupt frame fails closed. Checkpoints stream
all live rows to a new file, sync it, atomically replace the journal, and sync
the parent directory. Pending and uncertain records are retained. Compaction
also rebuilds the private arena and intern universe, reclaiming retired values.
Existing SQL files fail the native version check; no implicit conversion occurs.

A persistent random epoch distinguishes store identities. A restored copy
retains the epoch and gains no dispatch authority. The host must keep replay
copies dispatch-disabled and separately control ownership. The service records
its handler, payload-language and continuation versions as application data;
this layer does not migrate programs.

One cooperating process holds a lifetime exclusive lock on the stable `.lock`
sidecar. The lock survives replacement of the journal during checkpointing.
Hard-linked journals are refused; an aliased checkpoint temporary file is
refused before truncation. Keep the journal, lock and `.checkpoint` temporary file in one private local
directory. Do not replace or remove the lock while the store is open. This is
not distributed fencing and cannot constrain software that bypasses the lock.
Store operations serialize across threads; close requires exclusive ownership.
No evaluation or network effect runs while the store holds its commit lock.

Every admission limit is independently optional: zero means unlimited. The
library default imposes no policy quotas. `cetta_durable_bounded_limits()` is an
explicit service profile: 1 MiB per value, 4 MiB per batch, 4096 operations,
100,000 live records, 64 MiB live data and names, 16 MiB delta history, and
65,536 journal pages of 4096 bytes. Responsive service constructors select that
profile. Applications can supply different limits. Native string, integer and
frame representation ranges remain distinct from these policy choices.

A growth batch compacts history when its selected allowance is exceeded, or
when retired data warrants reclaiming the arena. Removal-only batches may log
the names of existing occurrences beyond a reduced byte/page allowance so
reclamation can proceed without copying the live set first. Physical disk
exhaustion can still prevent a write. A configured capacity refusal happens
before append and leaves no published revision. A journal write or sync failure
returns `unknown-outcome` and poisons the handle: close and reopen, then inspect
state. A missing acknowledgment never licenses blind retry or external dispatch.

Lowered limits do not prevent open, snapshot, recovery, removal or checkpointing.
Usage reports the exceeded dimensions; growth beyond an already excessive live
byte or row count is refused. Existing oversized values remain readable. The
native projection currently clones row references and scans keys under the
owner lock; this is not an indexed-store performance claim or a wall-clock
service bound. Native arena allocation retains the runtime's process-failure
contract; recoverable allocator failure is not interpreted as an empty query.

The C API exposes bounds for the host to configure. The MeTTa library currently
uses unlimited admission. No storage lock escapes this API. Back up a stopped,
closed store, or copy one stable checkpoint inode under an explicit host backup
protocol. A copied journal does not transfer dispatch ownership.

## Stored data

The versioned CDV1 codec supports symbols, strings, booleans, signed integers,
binary64 floats, big integers and rationals, and ground expressions. Float bits
survive the round trip, including signed zero and NaN. Exact numbers retain
their exact value. Decoding rejects truncation, trailing bytes, unknown tags
and invalid exact-number text. Unknown tags and damaged headers are corruption;
a recognized newer CDV header is an unsupported version. A failed MeTTa decode
identifies the offending space and key. It never silently drops the record.

Use strings for unbounded identifiers, user text, Telegram IDs, and effect IDs.
Symbols are intended for a finite protocol vocabulary. The default build can
retain decoded symbol names in the process-wide table after arena resets.
With `ENABLE_MANAGED_SYMBOLS=1`, decoded data symbols have managed spellings,
and explicitly owned registered names can be released. Legacy raw numeric IDs
are not covered by that reclamation. This codec does not enforce a lifetime
budget on new symbols, so unbounded text still belongs in strings.

This is a closed-data fragment, not a heap image. Variables, spaces, mutable
cells, foreign values and native handles are rejected. Represent a continuation
by its name, program version and ground arguments. Expression depth is bounded
at 128 and expanded node count at 100,000. Sharing is not encoded, and cycles
fail admission. These are explicit admission limits, not claims that arbitrary
CeTTa spaces or cyclic runtime objects can be persisted.

## Checks

`make test-durable-store` checks atomic transitions, stale snapshot rejection,
checkpoint/delta reconstruction, concurrent decisions, process death at four
write boundaries, storage allowances, optional quotas and version rejection.
It also tests actual journal write/fsync failure through linker interposition,
173 partial appends, binary values beyond the bounded profile, stable checkpoint
lock identity, poisoned handles, byte-for-byte recovery, lowered limits, deletion
at the page cap, concurrent reads and scoped decisions
amid unrelated traffic and conflicting positive/negative reads.
`make test-durable-value` checks the codec. With an enabled binary,
`python3 tests/test_durable_library.py ./cetta` checks the MeTTa boundary and
recovery, refusal without administrative opt-in, and speculative denial in
PeTTa and HE, including rho payloads even during an administrative invocation.

## Speculative evaluation

The native host uses `eval_top_speculative` with an HE library context to compute
proposals. This policy is native authority: no MeTTa option, pragma, CLI repair
flag or environment variable enables effects within it. Nested choices and rho
payloads inherit the same session policy. The previous policy and context are
restored on return, including the previous fuel setting. Other language profiles remain available to an independent
cognitive worker; this service evaluation entry currently accepts HE only.

An explicit allowlist admits arithmetic, structural computation, control forms,
in-memory queries and the audited string, JSON and rho interfaces. Core
operations and native library extensions are denied by default until audited.
The checks apply at execution, so quoted effect descriptions are ordinary data.
Filesystem access, ambient process observations, raw transport, foreign calls,
module loading, mutable state/space operations and durable-store operations are
unavailable. Rejection produces `(Error speculative-evaluation EffectNotAllowed)`
without echoing arguments. Existing HE control forms retain their error
semantics: for example, `collapse` may omit a rejected branch. Every attempted
forbidden effect also increments a shared atomic counter, independent of the
result bag. `EvalOutcome.effect_denials` reports this count. If evaluation would
otherwise be complete, a denial changes completion to `effect-denied`; another
incomplete reason (such as exhausted fuel) is preserved alongside the count.
The host must reject the entire evaluation on any denial or incomplete outcome,
without committing its remaining proposals. An ordinary Error value supplied as
data is not itself a denied effect. Counts saturate instead of wrapping.
State changes are returned as proposals. Prepared execution and
the relational machine are deferred at this boundary pending separate
qualification; ordinary evaluations retain their existing optimizations.

The host must load its trusted, versioned program before this entry and provide
fresh in-memory query spaces for the exact durable observation. Never provide
external backends, foreign handles, mutable live resources or cached state from
another snapshot. This is an evaluator effect boundary, not an OS sandbox for
untrusted native code. The host must validate closed proposal values, pair them
with the original observation and commit the selected transition. Returning a
term named `telegram:send` does not confer channel authority or dispatch it.
The entry requires an explicit positive fuel argument and refuses zero or
unlimited budgets before evaluation. Nested evaluation debits the same bounded
purse. The host also supplies input/result limits and wall-clock supervision;
this entry does not implement service admission or scheduling policy.

`make ENABLE_HTTP=1 ENABLE_DURABLE=1 test-speculative-eval` exercises the native
entry against a private counted HTTP peer and a file sentinel. It checks pure
proposals, quoted data, computed heads, nested choices and rho payloads, denied
operations and policy restoration. Qualification uses HE's extended profile,
including a two-thread configuration; finite fuel selects the existing
cooperative fallback. Threaded rho/cost-trace entries remain excluded. Before
admitting them, workers must attach a context without `eval_set_library_context`
(which initializes its fuel from thread-local defaults). Python-enabled builds also load a callable
before evaluation and verify that neither a direct nor computed call executes
it. Other language entries are refused before execution. The normal HE and
PeTTa corpus paths run with the policy off.

The native host below pairs this evaluation boundary with scoped channel
grants and durable acceptance. The independently supervised Telegram service
remains integration work.


## Native Telegram credential boundary

`telegram_transport.h` supplies an immutable host-only credential reference.
The host opens a private regular descriptor without following symlinks; reading
is bounded, accepts a final newline, leaves the descriptor position unchanged,
and returns enum errors only. The reference has no token getter and never
enters a MeTTa value, durable record or serialized proposal. Rotation constructs
a replacement after outstanding users have finished.

The native adapter constructs the token-bearing Bot API URL immediately before
request admission. Production defaults to `https://api.telegram.org`. A trusted
host configuration can select another HTTPS origin and CA bundle; HTTP requires
an explicit loopback-only mock option. Proposals cannot select the origin,
redirect policy, proxy, TLS configuration or idempotence. Ambient proxies are
disabled for this adapter, redirects are refused, and certificate/hostname
verification stays enabled. The generic HTTP worker still supports native
proxy/CA settings, with their copied bytes charged to its admission budget.

Effects use the default fresh, non-reusable transport. The separate host poll
entry alone opts into reuse for `getUpdates`; an effect cannot select that
method, `setWebhook`, `deleteWebhook`, `logOut` or `close`, regardless of case.
These control update delivery or bot availability and require operator action.
The host still enforces positive method and chat-routing allowlists. Application
permissions and retry decisions remain the host and MeTTa/rho library's responsibility.

Before journaling a response or constructing any evaluator atoms, the host must
call `cetta_telegram_response_safe`. It performs a bounded linear scan for the
secret suffix after the colon, including percent/JSON ASCII-Unicode escaped
spellings. This also catches the full token. On reflection,
suppress the body and record a minimal privacy outcome: the remote operation's
success remains unknown. This is defense against accidental reflection, not
information-flow security against an adversarial server's arbitrary encoding.
Provider schema validation is still required. Private URL buffers owned by the
adapter and worker are erased; this does not promise erasure of every copy
inside curl/TLS. The service must disable core dumps (`LimitCORE=0` under systemd).

`make BUILD=core ENABLE_HTTP=1 test-telegram-transport` uses only fake credentials
and loopback peers. Its Python fixture requires `h2` and `openssl`. It qualifies
HTTP/1.1, HTTPS/1.1 and negotiated HTTP/2 against drop-after-complete-request-read,
with independent receipt counts and connection identity checks. It also checks
poll reuse, redirect refusal, poisoned ambient proxy settings, reflected-token
screening, untrusted CA and wrong-hostname failures. These tests qualify those
faults on the linked libcurl; they do not establish remote exactly-once delivery
or a universal absence of every protocol-level retry.

This native adapter is not an evaluator builtin. The production durable host
must still connect credential lookup and response screening to its registered
channel and observation-recording boundaries.

## Native decision acceptance

`durable_host.h` joins the scoped store and the speculative evaluator. It is a
native host API, not a library operation available to evaluated code. The host
opens a decision with an input occurrence, actor identity, program version,
read/write space grants and channel grants. It receives immutable views from
one snapshot. Input and actor dependencies are always included; other scopes
include negative key/prefix/space observations. Unrelated inbox arrivals or
HTTP outcomes do not invalidate the decision.

`cetta_host_evaluate` builds fresh in-memory spaces and a private registry from
the ticket's views and the trusted program. It owns the copied syntax, projected
values and evaluation outcome. The program version must match the ticket.
`cetta_host_accept` accepts only that stored outcome; no caller-supplied status
or foreign ticket's result can replace it. A failed re-evaluation clears the old
result. Program templates are immutable code/static facts, never cached decision
data; native handles and preinserted `host:record` facts are refused. The program's
library context must not contain a previous decision's registry or spaces.
Worker proposals, LLM output, clock and randomness enter through recorded inbox
data, never as a trusted expression to execute. Parse and JSON
inputs must be bounded before symbol interning; use strings for unbounded IDs
and external text. Pending ticket count and aggregate observation memory also
require bounded admission in the service.

`cetta_host_plan_open` supplies an explicit retained Prime alternative. It
captures the same fresh views in a private world and exposes its actual owned
task through `cetta_host_plan_task`. A host may admit that task to the existing
ready queue or poll it after its enclosing service selects the candidate.
Planning has no durable mutation or external I/O. A captured snapshot may
finish after live records change; `cetta_host_accept` still validates the
original scopes and grants at the atomic commit boundary.

`CettaHostPlanLimits` separates a finite per-poll controller quantum, a total
work purse, preparation inspection/storage limits and retained result count.
The purse never refills when a task is polled again. Zero total work preserves
the initial task. Controller work is implementation telemetry, separate from
source-semantic cost and whole-callback time. Refused result collection keeps
the same pending loan. `cetta_host_plan_cancel` removes ready membership and
releases the computation while keeping copied results and final paid work
inspectable; cancellation prevents later acceptance.

When the Need index is compiled in, captured computations use it by default.
The persistent index keeps older and sibling snapshots separate while avoiding
linear history scans. `CETTA_PRIME_NEED_HEAP_INDEX=0` selects the log fallback
for diagnostic comparisons. Native lookup telemetry can differ between these
implementations; source firings and controller allowances retain their meanings.
Indexed lookup alone does not bound the duration of a complete planning poll.

This retained profile accepts Prime default contexts with already linked
structural libraries. Embedded imports use the same compiled source reader
and document-scope elaboration as the CLI. Capture retains code, binding
metadata and structural library availability, without retaining loader
inventories or acquiring external services. Equation lookup uses frozen code;
`&self` and `context-space` designate the separate private data world.
Computed storage coordinates use the retained argument continuation.
Every resolved call still needs its invocation-time service admission.
Opaque capabilities and other mutable library services need their own
capture/ownership contracts and are declined without a synchronous fallback. The existing
`cetta_host_evaluate` route retains its HE extended semantics. Neither route
requires a history recorder or correctness receipt.

The durable service's existing input, commit and intent records are operational
state for recovery and idempotence. They are distinct from optional evaluator
history or proof-checking certificates; ordinary evaluation does not open a
journal merely because this service is available.

For draft supersession, key inbound occurrences by chat/update and include a
read-only prefix scope for that chat's inbox lane. New input in the same lane
invalidates the unaccepted decision; another chat's input does not. This does
not cancel an already accepted intent.

One selected result has this closed-data shape:

```metta
(host:transition
  (waiting "reply")
  ((host:put 0 "actor/value" 42))
  ((host:send 0 (sendMessage "approved-chat" "hello") (resume "reply"))))
```

A write uses a space-grant index and a string key inside its declared scope.
`host:put` inserts or replaces according to that exact observation;
`host:remove` requires presence. Duplicate write keys are rejected before a
commit attempt, including aliases through different grants. A send uses a channel-grant index, payload and
reply continuation. These indices select authority bound to the native ticket;
writing a channel name or another number grants no additional permission.
Native channel validators are pure and bounded, and enforce payload/method/chat
permissions before commitment. Their context stays immutable until the host
retires the ticket. For a fixed-policy `cetta_host_begin` ticket, changing that
policy requires retiring affected tickets. `cetta_host_begin_with_dependencies`
instead retains the policy's observed records, so later acceptance detects
their revocation without replacing the original context. Programs
cannot write reserved `host.*` spaces through state grants.

`cetta_host_accept` requires bounded, complete evaluation with no effect denials.
It bounds and encodes the selected result, validates all grants, then atomically:

- consumes the bound `host.inbox` occurrence;
- replaces `host.actors` with a versioned continuation;
- applies granted state writes;
- inserts immutable `host.outbox` intents with channel and handler versions;
- records program version, input/actor identity and selected alternative in
  `host.commits`.

No evaluation, callback or networking runs inside the storage transaction.
Unselected alternatives produce no intents. There is no dispatch operation in
this API. The thin `durable:rho` module supplies request descriptions and pure
completion-to-rho projection; the embedding host supplies completions only after
recording them durably.

Private random receipt keys and outbox prefixes reserve fresh identities using
absence scopes. This bookkeeping randomness is not exposed as program input or
used to select branches. The receipt key remains available through the native
ticket even after an unknown commit acknowledgment. A ticket is spent once
commit is attempted; recovery inspects its journal receipt. Transport handles
are not effect IDs: an immutable outbox row's `(epoch, revision, position)` is
its durable occurrence identity. Keep claims/outcomes separate so later status
changes never overwrite the acceptance revision. Checkpoints preserve those
identities and all unresolved intents.

Wire records use CDV1, with schema 1 envelopes `host:actor`, `host:commit` and
`host:intent`. This records versions, not an automatic upgrade protocol: the
service must explicitly handle incompatible waiting continuations/handlers.
Receipts support inspection; they do not constitute deterministic replay.

`make BUILD=core ENABLE_DURABLE=1 test-durable-host` exercises actual speculative
HE evaluation and rho COMM, authority rejection, hidden effect denial, selection
of one alternative, absence conflicts, unrelated traffic, atomic rollback and
process death before/after acceptance.

## Accepted intent to durable completion

`durable_dispatch.h` connects immutable accepted outbox rows to the native
Telegram transport. It owns one HTTP worker and binds channel names/versions
to immutable native credentials and bounded request marshalling callbacks.
Those callbacks enforce positive method and destination allowlists; they run
on the host thread, before admission. Application routing and retry decisions
belong in MeTTa/rho, not these callbacks.

The store's lifetime file lock excludes other processes. A native runtime
attachment also refuses a second dispatcher on the same store handle. Startup
recovery completes before the worker starts. Administration, store closure and
policy/credential replacement require stopping the owner first.

The I/O owner checks the exact accepted row (epoch, revision, position and
bytes, including channel name/version) against its authorized request and
current immutable registry. It atomically inserts `host.attempts` before any
network activity. A repeated submission never creates a second attempt.
Transport handles are ephemeral; durable effect IDs use the acceptance
`epoch/revision/position`, with a distinct `/1` first-attempt ID.

Responses pass the credential screen before encoding or journaling. The owner
uses the native CDV1 field encoder, which has no evaluator or symbol-table
access. It commits `host.outcomes` and a `host.inbox` completion occurrence in
one transaction. Outcomes retain the transport status and safe response bytes;
HTTP 200 is not itself Telegram API success. The trusted application must
validate the JSON response and classify its `ok`/result/error fields.

Schema-1 outcome shape:

```metta
(host:outcome 1 "effect-id" "attempt-id-or-empty" kind
  (started cancelled curl-code http-status request-size-known request-size
   response-too-large response-budget-exceeded allocation-failed)
  "response-body")
(host:completion 1 "effect-id" "outbox-key")
```

Kinds are `observed`, `uncertain`, `not-started`, `privacy-suppressed` and
`unrecordable`. Privacy-suppressed and unrecordable outcomes make no assertion
of API success or failure. Embedded NUL cannot enter a CDV1 string, so such a
body becomes an explicit unrecordable outcome. Response bodies are bounded to
256 KiB; there are at most 64 registered jobs, in addition to the HTTP worker's
queue/byte limits. Unbounded external IDs and response text remain strings.

Full recording failures use the worker's bounded retries and minimal-fact
fallback. If even the minimal transaction cannot commit, retained jobs park and
the required health callback escalates outside the journal. No HTTP is repeated.
Transient claim conflicts/busy results retry at most five times before
escalation and recording of a not-started result. Failed/unknown commits never
authorize network activity.

Cancellation before a claim atomically records a definitive not-started outcome
and completion. After a claim, it records a cancellation request and signals the
worker; a racing completion remains a fact. An existing outcome is never
overwritten or re-injected after its completion has been consumed.

On startup, a claim without an outcome becomes `uncertain`; unavailable
transport metadata is represented by `unknown`, not fabricated zero/false
values. Recovery itself performs no HTTP. This first-attempt mechanism has no
automatic retry: another dispatch of the same intent is refused. A later retry
policy must explicitly authorize and record new work, with its relationship to
the original intent, before sending again.

`make BUILD=core ENABLE_HTTP=1 ENABLE_DURABLE=1 test-durable-dispatch` runs a real
rho reaction through acceptance, HTTP, durable completion and a second rho
continuation. A private mock exercises HTTP/1.1, HTTPS/1.1 and negotiated HTTP/2,
lost responses, secret reflection, cancellation, oversized recording, storage
exhaustion, duplicate admission, SIGKILL after remote receipt and recovery
without resending or recreating consumed completions. The fixture uses only
invented credentials.

This is an embedded transport boundary. The independently supervised service,
worker IPC, inbox cursor, timers, ordered chat lanes and full Telegram policy
remain separate integration work.

## Received batches and the remote cursor

`durable_inbox.h` provides the native batch-commit boundary. The host opens a
window on one source's committed cursor, uses that offset for its poll, and
commits the screened, validated and classified batch through that window.
Only a successful commit returns a new offset that may acknowledge the batch
remotely. Telegram confirms updates when a later poll's offset passes their
IDs, so constructing offsets from uncommitted responses loses updates across
a crash. [Telegram getUpdates](https://core.telegram.org/bots/api#getupdates).

One transaction inserts immutable `host.received` occurrences, their
`host.inbox` references and the new `host.cursors` value. Identity is
`source/update-id`; the inbox key is `source/lane/update-id`, so a chat-prefix
read scope can invalidate stale drafts without depending on other chats.
Source and lane are bounded native-selected string components, not symbols or
credentials. A source is the stable bot identity across token rotations.

Schema-1 envelopes:

```metta
(host:received 1 "source" update-id "lane" disposition closed-payload)
(host:input 1 "source" update-id "source/update-id" "lane" disposition)
(host:cursor 1 "source" next-offset)
```

The trusted reaction declares the referenced received-record scope when it
needs the payload. Core consumption remains a separate input/state/outbox
transaction. Consuming an inbox reference does not erase its received record;
repeated delivery therefore cannot recreate consumed work. Different payload,
lane or disposition under an existing identity is an explicit corruption
result, never a silent replacement. Retention must preserve this deduplication
evidence while replay is possible; storage pressure stops admission rather
than acknowledging data that was not retained.

Gaps, repeated IDs and out-of-order batches are handled without a contiguous-ID
assumption. A distinct lower ID is retained while the cursor remains monotone.
Exact duplicate IDs within a batch collapse; conflicting duplicates reject
the whole batch. `routed`, `unsupported` and `unauthorized` dispositions all
retain the payload. Application policy decides which inputs enter cognition
or operator handling; unsupported input is not silently discarded.

A window conflicts if its source cursor changes; unrelated traffic does not
invalidate it. Commit attempts spend the window even when acknowledgment is
unknown. Recovery opens a fresh window and reconciles the original batch
against the ledger. Empty batches spend their window without writing or
advancing the cursor. Batches contain at most 100 items and at most 4 MiB of
encoded candidates, subject also to the store's configured limits and the
closed-value codec's per-value bounds.

`make BUILD=core ENABLE_DURABLE=1 test-durable-inbox` covers these rules,
capacity-failure rollback and process death before/after the batch commit.
This native API is not exposed to worker proposals. The dispatcher binds HTTP
polls to these windows as described below. Provider schema validation and
classification still require the trusted application policy; this module
alone is not a receiving service.

### Polls on the shared HTTP owner

`cetta_dispatch_poll` opens an inbox window and constructs `getUpdates` using
its committed offset. The caller cannot supply an offset or raw request body.
Native configuration supplies the credential, stable source, update types,
limit and long-poll duration. Types are bounded identifiers and always sent
explicitly; an empty list selects Telegram's documented default exclusions,
not every update type. The HTTP timeout must exceed the long-poll duration by
at least one second. These are host-only interfaces, unavailable to proposals.

The dispatcher uses its existing I/O owner for sends and receives. It refuses
a second active poll for a source, and a saved response blocks subsequent
polls until classified. The host must map each actual bot to exactly one
stable source across aliases and credential rotations. This local check does
not fence an unrelated deployment polling the same bot.

Before any provider response becomes visible to the evaluator, the owner
screens it and commits a native CDV1 record under `host.polls/source`:

```metta
(host:poll 1 "source" requested-offset kind
  (started cancelled curl-code http-status request-size-known request-size
   response-too-large response-budget-exceeded allocation-failed)
  "response-body")
```

Recording is guarded by the exact original cursor and absence of a saved
response. It never advances the cursor. The hook uses native bytes, without
interning symbols or constructing arena atoms on the I/O thread. The same
bounded recording retries, minimal-fact fallback and degraded-health callback
used for outgoing effects apply here. A suppressed or unrecordable response
makes no assertion about Telegram's update list.

`cetta_inbox_recover_poll` opens a window on both the committed cursor and saved
response. Trusted policy validates the complete provider schema, verifies the
source/offset envelope, and classifies every update before calling
`cetta_inbox_commit`. That transaction validates the exact response revision,
inserts occurrences and inbox references, advances the cursor, and removes the
saved response together. An empty classified response is also consumed
transactionally. Failed classification or commit leaves the response pending.
The next poll can therefore acknowledge only durable occurrences.

If the service dies before recording, it polls again from the committed
cursor. If it dies after recording but before classification, it processes the
saved response first. After a successful classification commit, recovery uses
the advanced cursor. This follows Telegram's acknowledgment rule: a poll with
an offset beyond an update confirms it. [Telegram getUpdates](https://core.telegram.org/bots/api#getupdates).

Malformed responses, rate-limit results, unsupported updates and uncertain
poll outcomes remain policy inputs. They are not automatically treated as an
empty update list or permission to advance. Retry schedules and backoff must
be recorded by the service policy, without requiring a cognitive turn.

`make BUILD=core ENABLE_HTTP=1 ENABLE_DURABLE=1 test-durable-poll` exercises
HTTP/1.1, HTTPS/1.1 and negotiated HTTP/2 with fake credentials. It checks
cursor binding, held responses, exact-response validation, secret reflection,
lost replies, minimal recording, rate-limit and malformed-response retention,
capacity rollback and process death on both sides of response recording.
The dispatch fixture also overlaps receiving and sending on one owner.
The poll fixture runs the trusted MeTTa intake policy below on actual recorded
HTTP responses, including across process death/reopen and durable retry
admission. The continuous event loop remains service integration work.

## Telegram intake policy

`telegram_intake.h` binds one saved poll to its speculative classification and
commit. It uses the same fresh-program projection as decision tickets. The
native caller pins a trusted HE program version containing the standard
library and `durable:telegram`; workers cannot supply the program, response,
entry expression or routing configuration. The fixed `telegram:poll` entry
receives that ticket's screened `host:poll` observation and host-owned config:

```metta
(telegram:policy 1 (allowed-chat-ids...) allow-private
  (operator-user-ids...) (wake-peer-user-ids...))
```

The policy returns a complete batch, a bounded retry hint, or a hold reason.
Only one `COMPLETE` batch result with zero denials can commit. A failed
re-evaluation clears the previous outcome. Commit still validates the exact
saved response and cursor; no external result can be substituted. A hold,
retry hint, unsupported version or incomplete evaluation leaves the response
pending. Recovery uses the separate transition below. Classification never
retries or advances an offset after a hold by itself.

The MeTTa policy preserves complete canonical JSON updates, classifies known
message types and callbacks, and retains unknown update types as unsupported.
Chat authorization and sender role are separate: an operator ID never bypasses
the chat allowlist. Inline callbacks without a chat remain unsupported. A
routed message's value is:

```metta
(telegram:input 1 "update-kind" chat-id thread-id sender-id role normalized-update)
```

The role is `operator`, `wake-peer` or `ordinary`; it is classification data,
not permission to dispatch. Consumers must check the recorded disposition
before exposing an input and recheck current authority before executing an
operator control; a historical role is not a permanent grant. Chat/thread lanes
use decimal IDs separated by a dot. IDs are parsed without float conversion
or interning provider text as MeTTa source. Unknown event names, text and keys
remain JSON strings. Unicode, including escaped NUL, stays in scalar form.

Normalization rejects duplicate JSON keys, sorts object keys, renumbers
members and removes parser source positions. Arrays retain their order and
numbers retain their exact lexemes. Consequently reordering object fields or
moving an update within a response does not defeat deduplication. A duplicate
reuses its first durable classification even after a routing configuration
change; changed normalized provider data for the same ID is corruption and
remains pending. Policy output is not assumed to be a complete validation of
every nested Bot API object: later media/control handlers validate their own
fields before use.

Current admission bounds are 256 KiB of response text, 100 updates, nesting
depth 64, 256 object members and 4096 array elements. Parser work limits,
evaluation fuel, closed-value limits and store quotas also apply; these are
independent bounds, not a promise that every document below the byte limit
will fit. A parser resource limit is a distinct hold reason. Native parser
elaboration iterates character and sibling sequences without charging them
as nesting; actual nested values and total work remain limited.

`make BUILD=core ENABLE_DURABLE=1 test-telegram-policy` exercises the real
speculative HE program and intake ticket: routing/roles, all-or-nothing
classification, malformed/duplicate controls, exact IDs, Unicode,
normalization, bounded input, symbol growth, fuel/denial refusal, policy
changes during deduplication, competing tickets and retained polls on reopen.
This is intake; media actions, conversational controls, outgoing formatting
and the independent service/worker loop remain separate integration.

### Poll recovery

`cetta_telegram_intake_evaluate_recovery` first records a host UTC clock sample
in `host.poll-clock`, paired with the exact pending response. A restart or
reevaluation reuses that observation. It then calls the fixed pure MeTTa
`telegram:recover-poll` entry with the response, configuration, prior failure
count and recorded clock. `commit_recovery` uses only that ticket's complete,
unique, denial-free outcome; normal batch acceptance is a separate entry.

The policy honors valid bounded `retry_after` delays. Transient connection,
DNS, timeout, partial/lost response and HTTP/2 stream failures, and HTTP 5xx,
use 1, 2, 4, 8, 16, 32, then 64 seconds. Continued failures keep the capped
rate and carry `telegram:degraded` after eight failures. This is specifically
repeat-safe **getUpdates at the committed offset**, not a send retry policy.
Cancellation, privacy suppression, unrecordable responses, credential failures,
conflicting pollers and malformed input remain held. Deadline overflow holds
rather than wrapping. No unrecorded random jitter enters the decision.

The transaction writes `host.poll-control`: source, offset, response revision,
consecutive failure count, clock, deadline, program version, decision and the
screened original response. A retry removes the pending response and clock in
that same transaction, without changing the cursor. A hold keeps the pending
response and records deadline -1; that response cannot be recovered repeatedly
to inflate its failure count. A successfully classified batch clears control
and clock together with the normal inbox/cursor update. The latest failure
receipt is live state; older resolved decisions are subject to journal retention.

`cetta_dispatch_poll` checks the durable deadline using a native clock sample;
`cetta_dispatch_poll_at` accepts an explicit sample from the host event loop.
The untimed inbox begin refuses scheduled retries. A backward wall-clock jump
delays eligibility; a forward jump can make the retry immediately due. These
checks do not acknowledge any new input. Other sources remain independent.
The service can use the timer wait helper for monotonic waiting and resample
wall time periodically.

The policy/ticket tests cover clock persistence, competing observations,
unchanged cursor, storage refusal, recovery after immediate process exit,
backoff/degraded state across restarts, and hidden effect-denial refusal. The
HTTP fixture checks due-time admission and exact offsets across owner restart
on HTTP/1.1, HTTPS/1.1 and HTTPS/2. The service still must drive this transition,
expose degraded/held state outside Telegram, and provide explicit repair for
held responses. A generic dispatcher or worker proposal cannot clear a hold.

## Durable timers

`durable_timer.h` implements the native `timer.after` channel, version `1`.
The accepted outbox intent is the timer registration; there is no second
registration write. `durable:timer:after` and `durable:timer:every` in
`lib/durable/timer.metta` construct requests from a supplied, recorded clock
observation. They never read an ambient clock. The native channel validator
accepts closed payloads and replies of at most 64 KiB each:

```metta
(timer:at 1 utc-deadline-ms period-ms policy grace-ms value)
```

Deadlines, periods and grace intervals are nonnegative signed 64-bit
milliseconds. Period zero means one-shot. The policy is explicit:

- `FireOnce` emits one firing representing all due occurrences and advances
  beyond the sampled time.
- `SkipMissed` records a summary of deadlines older than the grace interval.
  A deadline exactly at the grace boundary still fires. Skipped work is
  observable, not silently lost.
- `CatchUpAll` emits individual overdue occurrences, at most eight per timer
  per tick. A tick admits at most 64 events, with a caller-selected smaller
  budget and a round-robin cursor so one backlog cannot monopolize admission.

For each timer, one transaction inserts its `host.inbox` observations and
advances `host.timer-state`, or writes a terminal `host.outcomes` record and
removes the cursor. Event keys are `timer/effect-id/sequence`, where effect
identity comes from the accepted intent's epoch, revision and position.

```metta
(host:timer-event 1 "effect-id" sequence kind scheduled-ms observed-ms
  represented-count value reply)
(host:timer-state 1 "effect-id" next-deadline-ms next-sequence observed-ms)
(host:timer-outcome 1 "effect-id" kind next-sequence observed-ms)
```

Event kinds are `fired`, `skipped` and `cancelled`. A cancellation records its
own event with represented count zero and prevents later firings; any earlier
committed firing remains a fact. Cancelling an already terminal timer is a
no-op. Concurrent firing and cancellation conflict on their observed state;
neither overwrites the other. A deadline beyond the integer range ends the
periodic timer with an `exhausted` outcome instead of wrapping.

Recovery uses the committed cursor/outcome. Consuming a wake-up does not make
it eligible for delivery again. A failed event transaction cannot advance its
timer. An unknown commit acknowledgment requires recovery, not an assumed
failure. Invalid or unsupported timer records are retained and reported by a
required native fault callback; other timers may continue. Storage failures
stop admission and report the failure. The host must escalate or back off,
rather than spin on an unrecordable wake-up.

A forward wall-clock jump makes timers overdue; a backward jump postpones
firing until the stored UTC deadline is reached. Used time samples are stored
with the event and cursor. `cetta_timer_wait_ms` derives a monotonic wait from
a UTC/monotonic sample and caps it at one second, so the service can resample
wall time and notice new work. It does not perform the wait. Tick/validation
run on the host's evaluator thread, not the HTTP owner thread.

This initial implementation scans the bounded accepted outbox when ticked;
the event budget does not bound scan cost. A service may schedule the next
tick using the returned deadline and admission wake-ups. An incremental timer
index and retention of completed intents are separate optimizations, and must
preserve the journal as the authority. The independent service loop is still
integration work.

`make BUILD=core ENABLE_DURABLE=1 test-durable-timer` covers the accepted
MeTTa requests, clock jumps, overdue policies and inclusive grace boundary,
bounded catch-up and fairness, cancellation, integer exhaustion, capacity
rollback, and process exit/recovery before and after delivery.

## Local cognitive worker boundary

`durable_worker.h` provides a bounded Linux Unix-domain `SOCK_SEQPACKET`
endpoint. The native host adopts an already listening descriptor, such as a
supervisor-owned socket. It does not bind paths, supervise processes, or expose
a network listener. The configured worker identity is fixed by the host;
`SO_PEERCRED` must match its configured UID. This authenticates the operating
system account, not individual programs sharing that account. Socket directory
and service permissions must enforce the intended local access boundary.

The native host publishes an immutable observation under a stable worker/task
ID. UTF-8 observation and result bodies are nonempty, contain no NUL, and are
bounded at 64 KiB. They remain string data: the endpoint never parses them as
MeTTa source or interns external identifiers. A service must derive publication
from a committed task intent or continuation, and preserve the exact snapshot,
read dependencies and trusted program version that produced that observation.
Publication by itself does not consume an input or accept an effect.

The wire format is `CWP1`, one command byte, one ID-length byte, ID bytes, then
body bytes; each packet is one seqpacket message. IDs contain 1–64 ASCII
letters, digits, dots, hyphens or underscores. The public header defines the
command and response codes:

- `NEXT` (no ID or body) fetches the oldest pending observation or returns
  `IDLE`. Fetching is non-destructive. A restarted worker can fetch it again.
- `RESULT` (ID and body) atomically records the immutable result receipt,
  inserts its inbox occurrence, and removes the ready index entry. `STORED`
  means the service has recorded input, not accepted the suggested action.
- `RECEIPT` (ID only) returns `STORED`, `PENDING` or `UNKNOWN`. After a lost
  acknowledgment, resending the same result is idempotent; different bytes
  for that task are refused. Consuming the inbox does not erase this receipt
  or make a repeated result create another occurrence.

`host.worker-tasks` stores observation text. `host.worker-ready` holds small
markers, so enumerating work does not copy every pending observation.
`host.worker-results` and the corresponding `host.inbox` occurrence contain:

```metta
(host:worker-result 1 "worker" "task-id" task-revision "result-text")
```

The service's trusted, gated program selects actions from those inputs. Workers
cannot select read scopes, the trusted program, credentials, channel grants or
an acceptance outcome through this protocol. Multiple authorized clients may
fetch the same task; the first committed result wins. This endpoint does not
promise exclusive assignment or prevent duplicate cognitive computation.

There are at most 128 unanswered tasks per worker and 16 connected peers per
endpoint. Each pump handles at most one request per peer, with bounded packets
and one queued reply per peer. Writes are nonblocking; stalled peers expire
after 30 seconds of monotonic time. Storage errors go to the host health policy;
an unknown commit outcome never produces a success acknowledgment. The host
must escalate/back off on storage failure. Disconnecting is not cancellation.

Completed history does not consume pending-task slots. Store quotas still
bound retained data. Retention must preserve receipts for their required
deduplication lifetime, and task IDs must never be reused after pruning.
Program/snapshot pairing, retention policy and the production service loop
remain integration responsibilities. The endpoint does not establish the
service/worker restart demonstration by itself.

`make BUILD=core ENABLE_DURABLE=1 test-durable-worker` checks malformed input,
UTF-8 and packet limits, immutable task/result pairing, peer identity and
capacity, a stalled reader, quota rollback, and service exit after commitment
but before the client reads its acknowledgment. The test keeps the listener
open across service restart, then verifies receipt recovery without reinjection.

## Accepted worker requests and continuation selection

`durable_worker_host.h` joins the endpoint to the host's existing acceptance
boundary. Register `worker.request`, handler version `1`, with
`cetta_worker_validate` and an immutable authorized worker ID as its native
context. The pure `durable:worker:request` constructor proposes:

```metta
(host:send grant (worker:request 1 "worker" "observation") reply-continuation)
```

The selected host transition commits the consumed input, waiting actor
continuation and this request intent together. `cetta_worker_register` then
projects an existing accepted intent into the IPC queue. It validates the
channel, handler version, worker authorization and data bounds again. It never
evaluates a proposal or accepts an uncommitted request.

The task ID is 64 hexadecimal digits: journal epoch (32), intent revision
(16), and operation position (16). Publication atomically inserts task text,
readiness and a `host.worker-origins` record. The original immutable intent
participates in that transaction's read set. The origin records its epoch,
outbox key, revision and position. On restart, register the same accepted
intent again: existing task and origin must match exactly. A crash between
acceptance and publication leaves recoverable committed work; a refused
publication does not remove the accepted intent or waiting continuation.

Worker replies still enter as recorded input. Their durable receipt binds the
task revision; the origin joins that task to the accepted intent and its reply
continuation. `host.worker-results` is the worker completion ledger; this
channel does not create an HTTP claim or an HTTP outcome. Re-registering after
result consumption does not publish another task or inject another input.

Fresh host projections now include `(host:record-version view-index "key"
revision position)` beside each `host:record`. Both kinds of projection fact
are rejected in a supplied program space. A trusted policy can join a worker
receipt to its exact task, origin, intent and waiting continuation, and compare
state revisions captured before cognition with current observed revisions.
Version facts describe only the ticket's authorized views. They do not add
authority or make a worker's stated dependencies trustworthy.

Stale-draft handling remains explicit application policy. The native endpoint
does not decide that an old draft is suitable for current state. The trusted
reaction must check its captured dependencies and continuation, including
newer messages that arrived before the reaction began. A read-only chat-prefix
grant additionally catches arrivals between evaluation and commitment. It
cannot, by itself, detect staleness predating that snapshot. Every accepted send
remains a commitment; discarding a stale worker draft is a different transition.

`test-durable-worker-host` exercises a trusted preparation/resume program,
accepted-request recovery after process exit, publication quota rollback,
origin corruption refusal, worker/channel/version checks and exact task
pairing. Code-looking reply text remains a message string. The example policy
retires a draft after a relevant state revision changes or newer chat input
appears; an arrival after evaluation conflicts at commit, while unrelated
traffic does not. This is a coordinator integration fixture, not the complete
production Telegram service or its general conversation policy.

## Service scheduling and independent cognition

`durable_service.h` assembles the HTTP owner, poll intake/recovery, timers and
local worker endpoint into one native lifetime. The caller supplies a store,
an inherited worker listener, immutable credentials and a fixed HE extended
program/policy for each poll source. Construction recovers outstanding claims
before admission. The configuration must name each actual bot only once;
different credential objects do not establish different bot identities.

`cetta_service_step` pumps bounded worker requests, due timers, and one poll
source in round-robin order. Its wait argument bounds idle waiting to at most
100 ms; evaluation and storage can take longer. It never invokes cognition.
Polls use committed offsets and recovery deadlines. The fixed, fuel-bounded
MeTTa policy classifies recorded responses and chooses retry or hold. A held
response, invalid result or exhausted evaluation does not stop other sources
or IPC. Incomplete results are not committed. Diagnostics for each parked
response are reported once per service lifetime; durable holds survive restart.

An ordinary shutdown records cancelled polls with a distinct `shutdown` cause.
The poll policy schedules their safe retry at the committed cursor, while
preserving the cancellation metadata. It does not reinterpret cancelled sends,
privacy suppression or minimal-fact recording failures. The HTTP owner is
joined before the store or credentials may close.

Storage failures latch, stop new admission and report a host fault. Recovery
requires reopening the service/store, particularly after an unknown commit;
repeated steps do not spin on the same error. Bad timer intents are reported
separately without stopping healthy work. Diagnostics and their deduplication
cache are bounded. The caller must route host faults and the dispatcher's
separate recording-failure callback to operational supervision.

Application reactions still use host decision tickets. After a committed
reaction, `cetta_service_changed` refreshes timer scheduling. The application
chooses eligible sends using `cetta_service_submit`, handles cancellation, and
publishes accepted worker requests with `cetta_worker_register`. The loop does
not infer chat order, replay an ambiguous send, or blindly dispatch every outbox
record. Ordered lanes and general conversation policy are separate integration
work, as are the production executable, configuration and supervisor units.

`make BUILD=core ENABLE_HTTP=1 ENABLE_DURABLE=1 test-durable-service` runs actual
service processes against HTTP, HTTPS and negotiated HTTP/2 mock servers. With
no cognitive worker, polling, sends and timers continue. Workers disconnect
and reconnect; graceful and killed service processes recover retained tasks,
receipts and poll cursors. A send accepted remotely before a crash becomes
uncertain without being sent again. Other cases cover held polls, exhausted
policy fuel and a latched storage quota failure. These are synthetic process
integration tests, not a deployed agent or a complete Telegram application.

## Typed Telegram text actions

`telegram_action.h` supplies the acceptance validator and dispatch planner for
`telegram.action`, handler version `1`. Its immutable native policy contains an
explicit numeric chat list and a permission mask for send, edit and delete.
Dispatch rechecks the payload and permissions independently of acceptance.
There is no arbitrary HTTP method or JSON-body capability at this boundary.

The pure constructors in `durable:telegram_actions` propose these payloads:

```metta
(telegram:send-text 1 chat thread reply-message "text" "plain")
(telegram:edit-text 1 chat message "text" "HTML")
(telegram:delete-message 1 chat message)
```

Text formats are `plain`, `HTML` or `MarkdownV2`. Zero thread/reply-message
omits that optional send field. Edit and delete require a positive message ID;
their permission is chat-wide, so a policy limiting them to owned messages must
also check its recorded message receipts. IDs remain exact integers. Targets
must already be resolved before acceptance; neither defaults nor aliases are
looked up during dispatch.

The adapter accepts valid UTF-8 text containing 1–4096 Unicode scalars. For
formatted messages this counts the source markup too, a conservative bound;
the provider still validates entity syntax. It marshals fixed fields and an
escaped string, with no evaluator, filesystem or network calls. Quotes,
backslashes and control bytes cannot introduce extra JSON fields.

The application must choose chunk boundaries and record the resulting actions
before accepting them. Current `str:length` and `str:slice` use byte offsets;
using them directly as character counts would be incorrect. This adapter does
not yet provide Unicode chunking, media uploads, inline editing, callback
answers, default routing or delivery retry policy.

`test-telegram-actions` checks permissions, exact IDs, Unicode and text bounds,
JSON escaping, dispatch revalidation and the pure constructors under the
speculative gate. The service matrix uses this adapter for actual mock send,
edit and delete requests, including thread/reply routing and formatting.

## Telegram application reactions

`durable:telegram_agent` is the fixed `telegram-agent/1` service policy.
`cetta_telegram_agent_decide` selects its receive, worker-reply or completion
entry using a recorded input. It derives the actor and grants from recorded
routing and origin records, checks these records against the decision's fresh
snapshot, and evaluates through the speculative gate. The worker supplies JSON
data, never a program, expression, actor, read set or channel capability.

Receiving a routed input atomically accepts a worker request and saves a waiting
continuation. The request contains the canonical update as a bounded JSON
string. Non-routed input uses a separate audit actor. A reply must match the
task revision, original intent and exact observation. Its draft is retired if
the continuation has changed or new chat input is already present. A read-only
chat-prefix grant also detects arrivals between evaluation and acceptance;
unrelated chat traffic does not invalidate it.

The initial worker-result format is a JSON array of up to 64 commands:

```json
[["send", "Hello", "plain"], ["edit", 9, "Updated", "HTML"], ["delete", 9]]
```

Targets come from the saved continuation. All actions and their positions are
accepted together with the sending continuation and an application decision
record. Invalid syntax selects a recorded rejection. Native action permissions
are checked before acceptance; a disallowed batch selects a fixed rejection
branch on the same ticket. This substitution requires a COMPLETE, denial-free
first result; it never salvages an incomplete or effect-denied computation.
Each of the at most two evaluations has an explicit fuel bound. Acceptance
validates action permissions again. No prefix of a rejected batch is accepted.

Accepted work survives newer input. Completions must match the original intent
and the continuation's expected batch position. API success requires an
appropriate result, including the chat and message ID for sends/edits. Malformed
responses, cancelled/failed transport and ambiguous outcomes hold the lane;
they do not allocate another effect. Delivery records preserve the result.
Only a confirmed delivery advances the position. Missing or unknown-version
continuations are distinguished: an unknown version cannot silently restart as
an idle actor.

Transitions pass through a rho COMM before returning to the host. A busy or
held reaction may return literal `Empty`, which is not an acceptable transition;
the host keeps the input pending. The native entry does not commit or dispatch.
The application scheduler below admits only the current position's action and
retains held lanes without repeatedly evaluating them. General supervisor
controls, explicit hold repair, retries and ordering waivers remain integration
work. This policy currently covers text send/edit/delete, not full Telegram
parity or the cognitive worker's existing command vocabulary.

`test-telegram-agent` covers recovery, task pairing, whole-batch acceptance,
stale drafts before and after observation, unrelated traffic, retained accepted
work, out-of-order completions, uncertainty, API response validation, command
bounds, rejection, unknown versions and fuel/profile boundaries. Inputs and
outcomes in this policy test are synthetic durable records; network and IPC
process tests are provided by `test-durable-service` separately.

## Telegram scheduling across process restarts

`telegram_scheduler.h` connects those application reactions to the independently
running service. On its single evaluator thread it alternates bounded input and
output visits, accepts only a unique COMPLETE transition, publishes accepted
worker requests and admits eligible Telegram actions. The fixed MeTTa
`tg-agent:dispatchable` policy compares the intent with the current batch
position. Attempts and outcomes are observed alongside it; claimed or completed
actions are never resubmitted. The dispatcher independently rechecks the exact
intent, channel version, cancellation and attempt/outcome when claiming.
Construction checks that the configured bot source, worker, credential route
and action-policy object agree with the service's native registry.

Both candidate queues use the reusable native `ready_queue.h` order. Admission
appends a stable link, each visit rotates the head to the tail, and retirement
removes only that link. A duplicate admission preserves its position; a full
queue rejects admission without evicting work. Retained candidates receive a
visit within their initial queue length of visits to that queue, even when
competitors finish and new candidates arrive. The former compact-array cursor
could skip a waiter after retiring the preceding entry: `[A,B]`, visit/remove
`A`, append `C` would select `C` before `B`.

Recovery only discovers candidates; admission runs through the same visit loop.
This keeps recovered effects inside the step's visit budget. Ready links are
disposable single-owner bookkeeping, not durable IDs or commit authority.
Message interpretation, reaction selection and effect eligibility remain in
the MeTTa libraries. Removing a link never cancels transport or deletes its
durable input, intent or outcome.
Outbox recovery retains the exact `(revision, position)` of the first
untracked effect. Resuming at the revision alone would repeatedly discover the
first effect of a multi-effect commit when the ready queue has capacity one.
Worker backpressure retains a separate retry cursor and reopens discovery when
the worker-ready metadata changes.

This ordering contract requires one application-state owner: service callbacks
record outcomes but never change actors, and administrative actor repair runs
only with the service stopped. Application reactions and transport admission
are serialized on the evaluator thread. A newer input cannot replace a sending
continuation; only its expected completion advances it. These obligations are
what make the eligibility query valid until the dispatcher claims the action.
Arbitrary concurrent actor writers would require an additional claim fence.

Blocked decisions retain metadata-only watches of their exact read scopes.
The evaluation, payload copies and temporary spaces are released. Watches
compare keys and revisions in a read transaction, including absence/prefix
dependencies and the journal epoch. They grant no commit authority. Unrelated
traffic does not cause reevaluation. A changed dependency triggers a fresh
ticket and policy execution. Malformed or incomplete results are diagnosed and
parked; they are never converted into successful commitments.

Pending inputs, pending effects and retained watch bytes have explicit limits.
Resource/storage failures latch and report once; subsequent steps preserve the
failure. The caller must stop the service and recover after correction, rather
than continue admitting work. If capacity is exhausted after a batch commits,
the whole batch remains in the journal; no partial batch acknowledgment or
rollback is invented. Restart rebuilds pending output from immutable intents.
Transport queue saturation instead leaves that accepted effect pending for a
later admission attempt.

The current recovery scan is linear in retained outbox history. Inbox refresh
copies its bounded current contents when its metadata watch changes and merges
them with the pending queue. It does not repeatedly copy an unchanged inbox or
evaluate a blocked policy. Incremental persistent indexes and retention remain
operational work before long-running production qualification.
Consequently the queue bound counts candidate visits, not elapsed time. The
whole-slot latency assumptions in the event-pump model still require bounded
storage discovery and evaluation.

`make BUILD=core test-ready-queue` checks capacity, membership and service under
completion/arrival churn without building the evaluator. The executable Lean
reference and service proof are in
[MeTTapedia's ReadyQueue model](https://github.com/godelclaw/MeTTapedia/blob/262e6f38993617ba8a438432299e658d3988fe21/lean/pettaclaw/ReadyQueue.lean).
For sibling CeTTa and MeTTapedia checkouts, run from CeTTa:

```sh
python3 ../MeTTapedia/lean/pettaclaw/check_observed_strategy.py \
  --suite event-pump --output-dir ../MeTTapedia/build/event-pump
python3 tests/check_ready_queue_lean.py --mettapedia ../MeTTapedia \
  --lean-build ../MeTTapedia/build/event-pump --output-dir build/ready-queue
```

The comparator builds fresh C, rejects stale qualified Lean sources, and
compares native transitions with `ReadyQueueOracle.lean`, which executes the
proved definitions. It includes all length-four traces over its small operation
alphabet, longer seeded traces, and deliberately broken native queues. Receipts
record source hashes and replace previous success before running. This finite
comparison and sanitizer testing do not prove the C pointer implementation or
establish the complete host refinement and wall-time obligations.

`test-telegram-scheduler` runs separate native service and synthetic worker
processes against HTTP, HTTPS and negotiated HTTP/2. It kills/restarts the
worker, kills the service after acceptance but before dispatch, and kills it
after the mock accepts a later send. Accepted-unsent work resumes; ambiguous
work holds without resending or admitting the next position. Another chat
continues during a delayed send and after recovery. A capacity fault preserves
the complete batch and it resumes under adequate limits. A parked-policy
evaluation bound detects repeated work during an idle interval.

These are the integrated scheduler and process tests, not a production launcher
or the existing cognitive loop adapter. Supervisor units, operator controls,
explicit hold repair and the remaining Telegram interface are still required
for a live application.

## Standalone Telegram service

`make BUILD=core ENABLE_HTTP=1 ENABLE_DURABLE=1 telegram-service` builds
`runtime/cetta-telegram-service-<build-tag>`. This executable links the native
service and scheduler; Python is not required. It loads only the fixed
`durable:telegram_agent` program from an explicit trusted installation root,
with the HE extended profile. It accepts no program file, expression, profile
selection, durable admin capability or worker-selected policy.

Use `--help` for configuration. `--check` validates the private state directory,
credential and installed policy without opening the journal, taking ownership
of the listener, or constructing transport. `--run` explicitly starts the owner.
The root must contain the matching `lib/` tree and remain immutable and
unwritable by cognitive workers. Paths are absolute; the caller's working
directory does not participate in module lookup. Environment variables do not
supply credentials, chats, policy paths or bot identities.

Supply a credential by private regular file (`--credential-file`) or inherited
regular descriptor (`--credential-fd`). Tokens never appear in command-line
arguments. The process disables core dumps and dumpability before loading the
credential. Diagnostic lines contain only fixed component names and status
codes, never response bodies or command-line values. Production transport uses
Telegram's HTTPS origin with system trust; `--mock-origin` accepts only numeric
IPv4 loopback with an explicit port. A test CA may accompany a mock origin;
TLS verification remains enabled. Proxy environment settings remain disabled
by the native credential adapter.

The bot's public numeric token prefix determines its stable source identity;
secret rotation preserves that identity. The journal binds that source, the
worker name and the service schema on first run. An incompatible restart is
refused before recovering or dispatching work. Renaming the worker or changing
bots requires a deliberate migration/new state directory, never an automatic
reassignment of pending work. Chat permissions remain an explicit positive
list, rechecked at acceptance and dispatch. This entry currently enables text
sends only: its edit/delete permission is withheld until message-ownership
policy is integrated.

The service adopts an already listening, private filesystem Unix seqpacket
socket. Pass `--listener-fd`, or provide exactly one systemd activation descriptor
with the current `LISTEN_PID`. The socket mode must exclude group/other access;
peer credentials must match the service UID. The parent supervisor owns the
socket across process restarts. Same-UID IPC admission is not a sandbox against
a malicious process running under that UID; deployment permissions remain part
of the trust boundary. One service/journal/source must own a bot. A second
journal does not fence another process using the same external credential.

`deploy/systemd/cetta-telegram.{socket,service}` are **uninstalled user-unit
examples**. Replace the deliberately invalid chat `0`, choose the installation
root and provide the credential file before use. The socket is private and
survives service restarts. Start the service explicitly, not just its socket:
Telegram polling must begin even while cognition is absent. `LoadCredential`
passes the secret via a private file; `LimitCORE=0` accompanies the process's own
restriction. Restart backoff and rate limits belong to systemd. A configuration
failure (78) or usage error (64) is not automatically retried. SIGTERM stops the
loop and joins the transport owner before closing the journal. A forced kill
leaves durable attempts for uncertainty recovery.

The launcher checks journal usage once per second and checkpoints deltas at
half the configured default history limit. Checkpointing preserves all current
records, including unresolved work; it is not semantic retention or deletion.
The existing defaults bound live data, journal pages, batches and pending
queues. Exhaustion stops the process visibly. Retention and a persistent
pending-work index still need long-running qualification.

`test-telegram-service` runs this executable, not a fixture replacement. It
checks inert validation, malformed/duplicate options, credential/directory/
socket permissions, inherited credentials, the systemd descriptor protocol,
source/worker mismatch refusal, and worker/service recovery over HTTP/1.1,
HTTPS/1.1 and negotiated HTTP/2. No units are installed by tests. The full
cognitive adapter, operator controls, hold repair and remaining Telegram
interface are subsequent integration work; this entry is not yet a complete
replacement for an existing agent.

### Cognitive worker holds

The Telegram agent policy also accepts a bounded diagnostic worker result:
`["cognitive-hold/1", "reason"]`. The allowed reasons distinguish an unknown
model outcome, provider failure, invalid request/response/command data, a
request-size limit, unsupported commands and an adapter-version change.
Unknown reasons are rejected as malformed results. This is data in the
existing worker result protocol; it grants no routing, scope or effect authority.

The ordinary task/origin/continuation checks and stale-draft checks still run.
For a current task, one transaction consumes its result, records a held
application decision and moves only that chat to `tg-agent:worker-held`.
It creates no outgoing effects. The worker's result receipt lets it continue
with other tasks; later input in the held chat remains durable. A stale hold
cannot replace a newer continuation. Its original result remains recorded.

Worker holds and uncertain Telegram sends are distinct states. Neither is
permission for an automatic paid model repeat or an ambiguous send retry.
The optional local operator endpoint below releases a specific cognitive hold.
Deploy the worker with this matching policy: an older policy can store the diagnostic
but classify it as an invalid command batch instead of holding the chat.

### Local operator control

Build `telegram-control` for a small native client. The service enables its
separate operator endpoint only with `--operator-fd FD`, a second inherited,
private listening Unix seqpacket socket. The supervisor owns both sockets
across restarts. Passing the worker socket again, including a duplicate
file descriptor, is refused. The existing one-descriptor systemd example
leaves operator control disabled; an explicit supervisor must supply the
second descriptor to use it.

The client supports:

```text
cetta-telegram-control SOCKET status CHAT.THREAD
cetta-telegram-control SOCKET release REQUEST_ID CHAT.THREAD BATCH
cetta-telegram-control SOCKET receipt REQUEST_ID
```

`CHAT.THREAD` uses canonical decimal IDs, with `0` for no thread. Chats must be
in the service's configured action allowlist. Request and batch IDs are bounded
ASCII components. Status returns only coordination metadata: idle, waiting,
sending, send-held, worker-held with its exact batch and reason, or incompatible.
It exposes no message text, model response or credentials. Unknown actor schema
versions are incompatible, never treated as idle.

A release queues one immutable operator input and request-ledger record in a
single transaction. `recorded` means durable admission, not completed release.
The fixed MeTTa/rho policy pairs the input with that record and can release only
`worker-held` for the exact requested batch. Consumption, the actor transition
and a `released` or `refused` receipt commit together under the ordinary read
fence. There is no effect grant. Releasing a cognitive hold means retiring that
blocked decision and allowing later queued chat input to proceed; it does not
repeat the old model request or recreate its commands. The original hold and
worker result remain recorded. Sending, waiting and delivery-uncertain states
are refused without changing their meaning. This is not a send-retry control.

After a lost acknowledgment, query the same request ID or retry with exactly
the same request, lane and batch. Reusing an ID with different data conflicts;
an exact retry cannot inject another input, even after service restart. Result
receipts survive consumption. IDs must remain unique across any later retention
scheme. An unknown actor version remains pending for explicit migration.

The native pump admits eight peers, bounds packets and replies to 256 bytes,
and expires idle or stalled peers after five seconds. Connections are one
request/reply each. The endpoint and client verify same-UID credentials;
filesystem sockets exclude group/other access. As with worker IPC, same-UID
access is a deployment trust boundary, not protection against arbitrary code
running under the service UID. The worker protocol and evaluator offer no
operator operation; isolating a hostile worker additionally requires OS
permissions/namespaces that keep this socket inaccessible.

Transport and these controls run without a cognitive process. Storage errors
stop the service for recovery; they never return successful admission after an
ambiguous commit. `test-telegram-control` exercises the actual service/client
over HTTP/1.1, HTTPS/1.1 and negotiated HTTP/2 with a synthetic Bot API. It covers
private/separate listeners, malformed packets, protocol separation, stalled
peers, stale and conflicting release requests, lost replies, service restart,
and refusal to retry an uncertain send. Agent tests additionally change the
actor between evaluation and commit and reject mismatched request ledgers.

## Independent durable activities

[`durable_activity.h`](../../src/durable_activity.h) and
[`durable:activity`](../../lib/durable/activity.metta) provide an embedded
activity host using the same journal, host acceptance boundary, timers and
CWP1 workers. A native intake records an occurrence and an owner generation.
MeTTa accepts it, saves a versioned waiting continuation and emits a worker
request. The service keeps pumping other inputs and timers. A recorded worker
reply resumes only its original continuation; its body is inert string data.
The trusted handler reconstructs a rho receive and performs COMM on the
original effect channel, then accepts the resulting state and result atomically.

The coordinator has one evaluator thread. External model/tool processes may
overlap without sharing evaluator arenas or an implicit current turn. Workers
receive neither decision scopes nor channel grants. Continuations are closed,
versioned tags; workers cannot supply evaluator code. Inside authored equations,
snapshot matches use `context-space`, and `Atom` arguments keep templates raw
until their variables bind. Lexical `&self` identifies the source module.

The ready queue rotates candidate inputs, rebuilding them from the journal
after restart. Capacity applies backpressure; excess inputs remain recorded.
Blocked candidates retain metadata watches rather than payload snapshots.
The embedding loop waits on intake, worker peers and absolute timer deadlines.
The example also waits for nonblocking result-output readiness and owns only
one formatted frame; a stalled consumer cannot freeze the worker receipt path.
Disconnecting a worker does not cancel its task. Revoking a waiting generation
keeps a subsequent reply as late evidence. Cancelling a periodic timer also
retires its native resource through an idempotent terminal observation.

An embedding can instead supply a self-contained Prime policy and set
`CettaActivityConfig.planning` with a positive quantum and explicit limits.
The same candidate queue then retains each host decision and polls its owned
task once per candidate visit, continuing intake, IPC and timers between
polls. A changed input occurrence disposes its old plan. An exhausted purse
parks the retained decision behind a scope watch; unchanged observations do
not authorize another purse. If observations change, a new decision may be
captured. Full admission capacity remains backpressure: a permanently parked
candidate does not imply fairness for inputs that have not been admitted.

Build and test the runnable embedding example:

```sh
make BUILD=core ENABLE_DURABLE=1 ENABLE_HTTP=1 ENABLE_LIB_PROLOG=0 \
  ENABLE_MANAGED_SYMBOLS=1 activity-service test-durable-activity
```

[`service.c`](../../examples/durable-activities/service.c) receives a journal,
an inherited listening descriptor, a worker name and a ready capacity. Its
trusted intake uses tab-separated `work`, `notice`, `timer` and `cancel` records;
JSON request bodies must escape embedded newlines. Result output is a replayable
observation, not an external delivery receipt. The process-worker adapter and
delayed HTTP replay live on MeTTaClaw's
[`feature/durable-responsive-activities-20261012`](https://github.com/godelclaw/mettaclaw/tree/feature/durable-responsive-activities-20261012)
branch.

The example accepts optional `PRIME-POLICY [QUANTUM [WORK-LIMIT]]` arguments
after ready capacity. It reads the trusted policy once at startup as equations
and type declarations, without executing startup directives or imports. The
policy implements the same `activity:receive`, `activity:cancel`,
`activity:reply` and `activity:timer-reply` entry contracts required by its
supported inputs, using the ticket's `host:record` views through `&self`.
Native routing still supplies all scopes, actors and channel grants. The
installed `durable:activity` module remains the HE policy. Native controller
poll/work totals accompany the existing whole-step and process-memory
measurements. A larger quantum changes service turns without restarting paid
planning work.

This is an activity runtime component, not a migration of the foreground
cognitive loop. Full prompts need immutable context references when they
exceed CWP1's existing body bound. Application effect approval and Telegram
delivery still use their own established boundaries. Timer/intake discovery
currently scans journal snapshots; terminal journal retention and a sustained
production soak remain qualification work. Fuel and visit counts are not a
wall-time bound: the example reports the actual longest service step.

### Duties around an activity

The same library can retain a duty around its primary activity and a separate
repair activity. `activity:receive-at`, `activity:reply-at` and their cancellation
and timer counterparts take an explicit activity state. `activity:map-state`
changes only the state component of their proposed transition; writes and sends
still cross the original native grant checks. Existing activity entry points
use these functions with `activity:state`.

The duty state records its occurrence, original obligor, beneficiary, scope,
current holder, both activity states, cancellation, disposition and history.
The host supplies a fresh occurrence and authenticated invocation principal as
trusted arguments. A claimed sender or an authority-shaped value in input data
does not supply either. An issued duty initially belongs to its obligor.
Transfer requires the holder's `transfer` grant and the recipient's `hold`
grant, while preserving the original parties and scope.

Policy views contain `(activity:grant 1 principal scope right)` records. The
host must declare the complete policy scope and revalidate it with publication.
The recorded grant references retain each key, revision and position; they
are evidence of the decision's observation, not native execution grants.
Starting primary or repair work requires current `execute` authority and a
task owner `(activity:duty-owner occurrence purpose)`. A correlated completion
can still be persisted after cancellation or revocation. It leaves the duty
unsettled until an authorized disposition is recorded.

`activity:duty-settle` takes the principal, purpose, policy view, result view
and key, receiver-evidence view and key, contract tag, and trusted checker.
Primary adoption requires `adopt`; repair requires `compensate`. The checker
interprets the completed answer against separately captured receiver evidence.
Only `True` permits settlement. The disposition records the result, grant and
receiver-evidence versions. A contract tag or worker assertion alone cannot
settle a duty. An unavailable result, missing evidence or failed check leaves
an explicit refusal and preserves the input and duty.

The worker-host tests use an independent receiver fixture whose native commit
updates actual wallet funds, stable request receipts and debit/refund history.
They exercise primary adoption, a real compensating refund, restart after
debit, version-changing revocation, forged evidence and irreversible effects.
Receiver authenticity, correct evidence issuance and principal binding remain
host/service contracts; this fixture is not a general evidence authenticator
or a proof of eventual settlement. The complete duty scenario currently uses
the HE speculative host. Retained Prime issuance imports the same maintained
client, preserves its scope records and rejects policy revocation/regrant
before acceptance. The full Prime reply path still needs a bounded, yielding
or isolated admission face for the existing Rhometta reducer; retaining its
definition does not admit that service into a responsive slot.
