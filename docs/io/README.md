# Native HTTP owner and durable recording boundary

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
SQLite journal. It checks independent progress, copied binary data, duplicate
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
The cognitive worker can independently use PeTTa. Cross-process data does not
grant the worker durable-store or transport authority.

The recording hooks are a mechanism, not a production durable dispatcher.
Credential references, the speculative-effect gate, durable timers, Telegram
policy, the service/worker protocol and crash recovery of that protocol belong
to the host layer. Ordinary `io:submit` does not gain durability from this change.
