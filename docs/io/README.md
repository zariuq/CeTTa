# Native I/O providers and durable recording boundary

Processes and timers share `io:submit`, owned `io:poll` and `io:wait` with HTTP.
They work on native hosts without Python, SWI-Prolog or HTTP enabled:

```metta
(io:submit (timer:after 1000))
(io:submit (proc:request ("sh" "-c" "make test") "/workspace" inherit
                        0 "/workspace/build.log" "/workspace/build.log"))
```

Process submission returns after spawning, rather than waiting for the command
to finish. Its environment may be `inherit` or explicit name/value pairs. A
zero process timeout has no deadline. Standard input is `/dev/null`; stdout
and stderr go to the named files and remain readable after the handle is
consumed or the runtime exits. The same output path merges both streams.
The files' parent directories must already exist. Process groups allow
cancellation and deadlines to stop descendants as well as the direct child.

An independent native owner checks process deadlines and reaps owned children
even while the evaluator is busy. Startup does not wait for exec-pipe EOF.
Parent-side startup errors are submission errors; child-side cwd or exec errors
are terminal `failed` outcomes. A real exit 127 remains an ordinary exit.
Cancellation consumes the public handle immediately while the owner retains
the child until reap. The kill is requested immediately; wait for a retained
signalled handle's completion when reusing its output files requires finality.
Runtime teardown joins this owner before closing wake
descriptors; a child stuck in an uninterruptible kernel wait can delay teardown.
Normal leader exit preserves ordinary process semantics: background descendants
may continue and write output. Signals, cancellation and teardown only control
groups whose leader is still owned and unreaped; they never signal recycled IDs.
The embedding host must retain child exit status rather than configure SIGCHLD
auto-reaping; incompatible signal disposition produces a startup error.

`io:status handle` reports `(proc:running pid elapsed-ms stdout stderr)` or
`(proc:finished pid exited|signaled|timed-out|failed code stdout stderr)`.
Polling or waiting consumes a terminal outcome once. `io:signal handle number`
signals the group and leaves its completion available; `io:cancel handle`
stops the group and consumes the handle. Timer outcomes are
`(timer:fired elapsed-ms)`, with non-consuming
`(timer:status waiting|ready elapsed-ms)` status.

The evaluator's native wait polls the owners' wake pipe, using the nearest
timer deadline for its timeout. The process owner uses Linux pidfd;
when unavailable, it checks specific owned children at 20 ms intervals. There is
no per-process or timer quota inherited from HTTP. Waiting for a selected
handle preserves other providers' completions. Optional progress observation
uses `io:wait milliseconds progress-path` or
`io:wait handle milliseconds progress-path`; it does not change execution.

`io:wait-ready milliseconds` or `io:wait-ready handle milliseconds` returns
`(io:ready (id ...))` without consuming a completion, or `(io:idle)` on timeout.
Use it for shared readiness; then let each client consume its own handle.
Unselected completion order is unspecified; use owned handles for coordination.
`timer:fired` reports the timer's selected due interval, independently of how
long the evaluator waited before consuming it. Timers have no worker effects.

`fs:tail-text path bytes` seeks to the requested file tail and returns
`(fs:tail total-bytes truncated text)`, repairing malformed UTF-8. Zero bytes
requests the whole file. `fs:entries path` lists names without a page limit.
`fs:update-text` operation `replace` atomically updates a state file without
a backup, creating its parent directories. The existing `write` operation
continues to preserve a backup. `system:wall-time`, `system:format-time`,
`system:pid`, `system:pid-alive?` and `system:read-line` expose native host
utilities; `format-time` takes a format, epoch seconds and `local` or `utc`.

With `ENABLE_HTTP=1`, native requests run on one curl owner thread per I/O
runtime. The evaluator can sleep or compute without polling to drive transfers.
Only C bytes cross the thread boundary; `io:poll` and `io:wait` create arena atoms
on the evaluator thread. The browser backend continues to use browser callbacks.
The native provider requires libcurl 7.85 or newer with thread-safe global
initialization and asynchronous DNS; otherwise HTTP capability is unavailable.

The existing `io` library remains ephemeral. `io:submit` returns a transport
handle, `io:poll` consumes a completion once or returns `(io:idle)`, and
`io:wait milliseconds` waits up to that monotonic deadline and consumes a
completion. A zero-time wait polls. Blocking wait is native-only; browser code
must yield to its event loop. Ephemeral `io:cancel` abandons the completion,
which is not evidence that the peer did not receive the request.

For multiple clients sharing a runtime, `io:poll handle` and
`io:wait handle milliseconds` consume only the selected completion. Other bodies
remain owned by the worker and count toward its aggregate response-byte limit.
An unfinished handle returns `(io:idle)`; an unknown, consumed or cancelled
handle returns `(io:unknown handle)`. Handles are positive integers local to one
runtime session. The unselected operations retain their original behavior.
`io:status handle` reads `(io:progress handle phase elapsed-ms bytes quiet-ms)`
without consuming the result. `phase` is `waiting` or `ready`; body byte
activity is transport metadata and never certifies an application answer.
Consumed handles answer `io:unknown`. Progress inspection is native-only.

`io:try-submit request`, `io:try-status handle` and
`io:try-wait handle milliseconds` provide explicit error boundaries in each
supported library spelling. Failed submission or wait returns `(io:failed)`;
unavailable progress returns `(io:no-progress)`. These wrappers do not retry,
cancel, or turn diagnostic errors into response bodies.

The native request may append independent transport limits after the optional
redirect Bool:

```metta
(http:request "POST" "https://example.org/model" () "{}" 1800000 1048576 False
              (http:timeouts 30000 900000 180000))
```

The three options are connect, first body byte and idle-after-first-byte
milliseconds. Zero disables first-byte or idle limits; zero connect preserves
the legacy connect policy. The existing request timeout remains the overall
hard ceiling. An `io:wait` timeout only returns `io:idle` and leaves transport
running. Silent initial reasoning is not subjected to the post-start idle
limit. Idle is measured from the last received body byte, including heartbeat
traffic; it is not a semantic progress detector. The browser provider rejects
nonzero independent limits rather than silently ignoring them.

The [buffered LLM library](../../verification/llm/README.md) uses these operations
so waiting for one provider request does not drain another client's response.

`CettaHttpWorker` is also the transport mechanism for the durable host. Its
optional hooks execute on the owner thread, without the worker mutex:

1. `prepare(id)` returns READY only after the attempt claim is durable. DEFER
   keeps the request queued without networking; DROP produces a not-started
   cancellation observation.
2. Networking records status, bounded response bytes and a transport result.
3. `observe(result, mode)` returns ACK only after durable recording, or after
   confirming that the same fact is already recorded. RETRY retains the exact
   result and retries recording with bounded exponential delays, never HTTP.
   The default budget is five attempts per mode; hosts can set `record_attempts`.
   Exhaustion parks the job and invokes the required `recording_stalled` hook.
   This hook must report degraded health through an operator/supervisor path
   that does not depend on the failing journal or on a cognitive turn.
4. A permanently unrecordable full result may return USE_MINIMAL. The next
   observation has MINIMAL mode and the same metadata, but no body. The host
   must durably record an explicit `OutcomeUnrecordable` fact identifying the
   effect/attempt and transport outcome before returning ACK. This is neither
   remote success nor remote failure. The original body remains retained
   until that acknowledgement. Minimal recording has its own bounded retries
   and escalation; returning USE_MINIMAL again cannot create an infinite loop.

New requests do not start while an observation awaits persistence; already
active transfers continue. A parked job has no automatic recording retries.
After storage repair, `resume_recording(id)` restarts at FULL with the retained
body and grants another bounded budget without repeating HTTP. If the full
receipt still cannot be stored, the hook may request MINIMAL again. The stall
hook may call resume after repair; it runs without the worker mutex.
Retry exhaustion alone never authorizes discarding
the body or inventing a minimal outcome. If even a minimal fact cannot be
recorded, dispatch remains visibly degraded until repair or shutdown.

Hooks must perform bounded work: no application evaluation or network calls.
Database operations must be short and fail promptly under contention. Outcome
commits should validate the effect's own keys, so unrelated inbox traffic cannot
exhaust the recording budget. A hook
must not join or free its own worker. The host owns effect identities, attempt
identities, routing, retry policy, response redaction and completion delivery.
A worker handle is only a process-local transport identifier.
The worker does not enforce per-chat order. The host submits only eligible
attempts, after checking its durable lanes, dependencies and uncertainty policy.

A durable worker cannot abandon a completion or expose it through `take`.
The native cancellation enum distinguishes UNKNOWN, NOT_STARTED, REQUESTED and
TOO_LATE. NOT_STARTED prevents admission to curl, including a cancellation
racing the prepare hook or curl setup. REQUESTED means the transfer may already
have executed remotely. The recorded result, not the cancellation request,
settles the local outcome. A completed response remains a fact even if
cancellation arrives while its durable recording is pending. The host must
interpret ambiguous transport outcomes according to the effect's policy.
UNKNOWN includes a job already acknowledged and retired: consult the durable
record rather than treating it as proof that the request was never submitted.
A cancelled transfer may have status 200 and a truncated body. Status alone
does not establish Telegram success or supply a usable message ID: validate the
complete response and application receipt. `request_size_known/request_size`
record libcurl's `CURLINFO_REQUEST_SIZE` diagnostic. A zero value is not, by
itself, a supported automatic-retry rule; `started=false` remains this layer's
proof of non-send. The host may refine failure classes against a qualified
transport contract separately.

Stopping closes admission, cancels transfers, offers outstanding observations
to the recording hook one last time and joins the owner. It returns the count
of unrecorded observations. The caller must exclude concurrent API users
before freeing the worker. Persisted attempts without recorded outcomes must
recover conservatively as uncertain; orderly teardown does not prove delivery.

## Bounds and errors

Defaults are 64 outstanding jobs, 8 MiB of copied requests and job metadata,
and 32 MiB of response buffers. Results awaiting consumption or recording
continue to occupy those budgets. The C API can set other limits. Admission
reserves capacity and identity before copying a request body outside the worker
mutex. Pending reservations prevent duplicate admission and count against the
same quotas. Curl, TLS and resolver internals
use additional implementation-owned memory; these budgets are not a cap on
process RSS.

Each request additionally bounds its response, and response growth charges the
aggregate budget. Exceeding either limit fails explicitly, never reporting
truncated data as a successful response. Aggregate exhaustion is distinct from
exceeding the request's own response limit. Raw C results support binary bodies;
the existing MeTTa string adapter rejects embedded NUL bytes.

The MeTTa adapter permits at most 256 headers, 8192 bytes per header line and
URL, an 8 MiB input body, and timeout/response bounds below the signed 32-bit
limits. Inputs still must fit the aggregate admission budget.

Validation errors constructed by this adapter identify only the operation,
e.g. `(io:submit)`. Asynchronous errors identify only `(io:request id)`.
URLs, headers and request bodies are
never echoed in those error sources. Native transport diagnostics use constant
curl error descriptions; browser failures use a fixed message. Successful
response bodies are data and may themselves contain sensitive content; the
durable host must classify them before recording or exposing them.
Raw `io` still accepts caller-supplied URLs; this change does not make it a
credential vault or sanitize errors produced earlier by general evaluation.
Credential references must keep secrets out of application atoms altogether.

## Redirect policy

`http:request` accepts an optional final Bool, `follow-redirects`, defaulting to
False. For example:

```metta
(http:request "GET" "https://example.org/" () "" 30000 1024 True)
```

Native default requests return the original 3xx response. Opted-in requests
follow at most eight redirects using libcurl's redirect/method rules. Explicit
False and omission have the same meaning. Telegram's host requests must keep
redirect following disabled. The browser's Emscripten XHR backend cannot
suppress redirects: it rejects requests without explicit True before starting
network I/O. Existing browser callers must opt in, including those replacing
the no-redirect `http:get`/`http:post` convenience calls.

## Connection reuse and implicit resends

`CettaHttpRequest.idempotent` defaults to false. Such requests set both
[`CURLOPT_FRESH_CONNECT`](https://curl.se/libcurl/c/CURLOPT_FRESH_CONNECT.html)
and [`CURLOPT_FORBID_REUSE`](https://curl.se/libcurl/c/CURLOPT_FORBID_REUSE.html):
use a new connection and close it after the transfer. This avoids libcurl's
silent resend after a reused connection produces no response bytes. That retry
can duplicate a POST accepted by the server before its response was lost.
The cost is a fresh TCP/TLS connection for each attempt.

Only an explicit host declaration of repeat safety enables pooling. HTTP method
is not sufficient: Telegram permits effectful operations through GET too. A
Telegram send must keep `idempotent=false` and redirects disabled. A poll may
opt in when repeating that exact offset is safe under the durable inbox
protocol. The MeTTa `io` adapter currently retains the conservative false
default for every method; it does not infer safety from source expressions.

This closes the demonstrated stale-connection resend path; it is not an
exactly-once delivery guarantee. A started failure after remote acceptance
still requires the host's uncertain-outcome policy. Redirect opt-in is a
separate permission to make further HTTP requests. Other protocol-level retry
behaviour must be qualified before making broader transport guarantees.

## Checks and scope

`make ENABLE_HTTP=1 test-http-worker` runs a private loopback fixture with a
native CeTTa journal. It checks independent progress, copied binary data, duplicate
handles, admission and response bounds, cancellation, durable claims before
dispatch, retained completion bytes through repeated recording failures, no
HTTP replay, recovery, unrecorded shutdown outcomes and concurrent producers.
It also checks bounded recording failure, escalation, explicit resume, durable
minimal facts, blocked-job recovery and POST redirects with a remote counter.
Keep-alive tests additionally prime the pool, lose a response after accepting
the whole request, and independently count receipts and connections. Both POST
and GET effects must arrive once and report a transport failure; explicitly
idempotent requests exercise the automatic resend as a positive control.
Repair tests recover the full receipt after a parked MINIMAL observation and
re-enter resume from the stall callback to check that it runs unlocked.
The fixture counts requests independently of the worker.

`test-io-runtime` checks the evaluator adapter, wait, safe error sources and
one-time completion consumption, including a mutation that deliberately replays
completions. `test-io-library` exercises real submit/wait/poll calls in both HE
and PeTTa. The wrappers return Expression and dispatch directly so PeTTa does
not leave them as unevaluated function bodies.
`test-io-syntax`, `test-io-no-http` and `test-io-rho-bridge` cover the
library's operations. `test-io-browser` requires an Emscripten/browser toolchain.

The service orchestration profile is HE with the qualified rho/rhometta library.
The cognitive worker can independently use PeTTa. Its file and network access
follow its OS account; cross-process messages transport values.

The recording hooks are a mechanism, not a production durable dispatcher.
Credential references, durable timers, Telegram
policy, the service/worker protocol and crash recovery of that protocol belong
to the host layer. Ordinary `io:submit` does not gain durability from this change.

Native Unix notifications also use the same handles:

```metta
(io:submit (cwp:request socket code name body deadline-ms))
(io:submit (space:request socket (request id operation arguments) deadline-ms))
(io:submit (host:notice))
(io:submit (fs:changed directory (selected-file-names)))
(io:wait-ready (io:ids handle other-handle) wait-ms)
```

CWP and space requests reuse their existing wire formats. A deadline of zero
leaves the transaction open without a deadline. Failed space transactions
distinguish no request sent from a missing reply after sending; the client
does not automatically replay a write. File notifications are Linux inotify
subscriptions; omit the file-name list to observe the whole directory.
`host:notice` observes a native generation counter. Existing embedded-host
producers may call `cetta_bridge.notice()` after publishing their data; the
notifier neither evaluates MeTTa nor owns the producer's queue.

Selected readiness waits leave all results available for their owners and
ignore unrelated ready results. These primitives add no execution quotas,
command filters, automatic acknowledgements or requirements to reconsider a
scheduled command. CWP `WAIT` (code 5) takes a revision token and an ASCII
millisecond duration. An empty token obtains the current token; a subsequent
wait reports newly published work or a changed store epoch without receiving
or acknowledging a task. Previously pending tasks do not repeatedly wake it.

## Existing SQLite state

The `sqlite` library accesses existing databases without imposing the durable
space service's schema. SQL and bound parameters are caller-selected:

```metta
(sqlite:query "history.sqlite3" "SELECT id,result FROM calls WHERE id=?"
              ("call-id") 10000)
(sqlite:observe "ledger.sqlite3" "SELECT state FROM tasks WHERE id=?"
                ("task-id") 1000)
(sqlite:transaction "history.sqlite3" immediate
  ((sqlite:statement "INSERT INTO notes VALUES (?,?)" ("task-id" "finding"))
   (sqlite:statement "SELECT rowid FROM notes WHERE task=?" ("task-id"))) 10000)
```

A query returns `(sqlite:result columns rows changes last-row-id)`. A scoped
transaction returns `(sqlite:committed results)` after all statements commit;
an error rolls its unfinished transaction back. Connections and statements
close before returning to the evaluator. No callback can retain a transaction
across a model call or asynchronous wait. The busy duration is caller-selected.
There are no row quotas or SQL filters. SQL `NULL` is `sqlite:null`; BLOBs are
`(sqlite:blob bytes)` and TEXT, INTEGER and REAL retain their storage types.

`sqlite:observe` takes the arguments of `sqlite:query` and returns the same
result, without changing the database it reads. It opens an existing file
read-only and fails with `(sqlite:error io 14 "database file does not exist"
failed)` rather than creating a missing one. A statement runs only when
SQLite reports that it cannot write (`sqlite3_stmt_readonly`); writes, schema
changes, `BEGIN IMMEDIATE`, `VACUUM`, journal-mode and checkpoint pragmas fail
before they run, as do `ATTACH` and SQL holding more than one statement. The
read-only connection and `PRAGMA query_only` are further layers. A WAL
database is read at its last committed state while another process holds a
write transaction. The observer never checkpoints; like any SQLite reader it
uses the `-wal` and `-shm` files, creating them for an idle WAL database that
has none, and never writes the database file itself.

Errors carry category, SQLite code, message and outcome. A commit I/O failure
may report `unknown-outcome`; it is never automatically replayed. The library
does not rebuild tables, change rowids, stamp a schema version, force WAL or
change the existing database's journal mode.

When embedded Python and native CeTTa access the same database, build with
`ENABLE_SQLITE=1` and use the **same shared SQLite library** as Python's
`_sqlite3`. Two independent SQLite copies in one process can interfere with
file-lock ownership. `SQLITE_CFLAGS` and `SQLITE_LIBS` can select an SDK or
Python environment; otherwise the build uses pkg-config or ordinary system
linkage. `ENABLE_SQLITE=1` enables this library independently of native durability. The native SQLite parity test
checks existing rowids and types, atomic rollback, unbounded result selection,
read-only observation beside a live WAL writer, and shared linkage with an
embedded Python reader.
