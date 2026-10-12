# Native named mailboxes

Activities register a receive, wait without model calls, and recover results
after either the sender or the native storage owner restarts. Logical channels
have configured directory or Codex App Server bindings. Several channels share
one physical native named space.

## Build and qualification

The mailbox path needs GCC, Make, Python 3, `pkg-config`, and GMP development
headers. It has no SQLite, Rust, model-provider, or Python-embedding dependency.

```bash
make BUILD=core ENABLE_DURABLE=0 ENABLE_HTTP=0 ENABLE_JSON_GSLT=0 \
  ENABLE_LIB_PROLOG=0 ENABLE_MANAGED_SYMBOLS=1 test-native-mailbox
make BUILD=core ENABLE_DURABLE=0 ENABLE_HTTP=0 ENABLE_JSON_GSLT=0 \
  ENABLE_LIB_PROLOG=0 ENABLE_MANAGED_SYMBOLS=1 test-space-path-index
```

The tests exercise owner crashes, lost commit replies, concurrent receivers,
cancellations, duplicates, notification races, and directory recovery. Most
Codex tests use a protocol fixture. Set `CETTA_CODEX_TRANSPORT` to an installed
`codex_room.py` to additionally exercise that transport over a local Unix
WebSocket fixture. No tests start model or tool work or contact a live thread.

`tests/support/qualify_native_mailbox.py` measures waiting checks and
send-and-resume against retained terminal history. Pass `--server`, `--worker`,
`--completed`, and `--output`. `--profile-queries` also records candidates
examined by the matcher. History imports are schema-consistent fixtures,
not a live-agent soak.

## Local use

Start the built service with a private socket and data directory:

```bash
runtime/cetta-space-service-core.prime-need-heap-index.prime-eval-stack.managed-symbols \
  --socket "$XDG_RUNTIME_DIR/mailbox-space.sock" --data ./mailbox-data
```

In another terminal, set the client arguments and register a receive:

```bash
mailbox_args=(--socket "$XDG_RUNTIME_DIR/mailbox-space.sock" --space agent-mailboxes \
  --worker runtime/cetta-value-worker-core.prime-need-heap-index.prime-eval-stack.managed-symbols)
python3 tools/mailbox.py "${mailbox_args[@]}" wait review-wait \
  --channel codex-review --correlation request-1 --sender codex \
  --owner review-activity --generation 1
```

The caller waits on its connection; it does not block the native service or
other activities. A producer can deliver while it waits:

```bash
python3 tools/mailbox.py "${mailbox_args[@]}" send reply-1 \
  --channel codex-review --correlation request-1 --sender codex --body 'review result'
python3 tools/mailbox.py "${mailbox_args[@]}" read review-wait
```

The result remains ready until its consumer has durably recorded any
subsequent action and explicitly settles it:

```bash
python3 tools/mailbox.py "${mailbox_args[@]}" settle review-wait \
  --owner review-activity --generation 1
```

Re-registering the same wait after a consumer restart returns its existing
registration. Reading a ready result does not consume another message.
`cancel` requires the same owner and generation as `settle`; a completed
receive wins a concurrent cancellation. Later messages on a cancelled route
remain inert and can be recorded as late by `read`.

## Atomic boundary and semantics

`lib/petta/lib_mailbox.metta` produces transition plans from owned values.
The host observes scoped, indexed records at one native space revision and
conditionally commits the complete plan against that revision. A receive
removes its stored wait and one pending occurrence and inserts a ready record
containing the registered continuation and message identity in the same
durable commit. Message bodies remain in the delivery ledger.

The selection key is `(channel, correlation, expected sender)`. A wait has an
identity and an activity owner/generation; each owner/generation and route is
registered once. Equal message identities with equal contents are duplicates;
changed contents are refused. Receives select the first matching pending
occurrence in source order. Different logical channels remain separate
routes. Unrelated writes in the same physical space can cause a bounded retry.

This is a single-channel selective receive with stored continuations, inspired
by [RSpace's stored data and waiting continuations](https://rholang.github.io/docs/rspace-intro/).
It does not implement multi-channel joins or execute a rho process. The pure
`rhocalc` reducer is unchanged. `MailboxResume` is a closed, inert data
constructor; workers cannot supply executable code.

Ready records provide recoverable observation, not exactly-once external
effects. Consumers need action identities and durable action receipts before
settlement. Other stores' cognitive state, tools, timers, and outboxes do not
participate in this native mailbox commit. The RPC layer reports lost mutation
replies as uncertain. Identity-keyed mailbox operations reopen the same durable
owner and inspect current application state once before deciding again. They
never resend the previous batch or replay an external action. A second transport
failure remains a refusal. Recovery never creates replacement storage for a
dropped space or a changed service identity. Idle connections that have already
reached EOF are replaced before another packet is sent.

The disposable value evaluator is replaced after death or a transport deadline.
Only its pure plan is recomputed, from freshly validated storage observations;
an unfinished reply cannot contaminate another operation.

`watch` atomically compares a captured revision and registers a deferred reply.
A write in the read/subscription gap wakes immediately. Each watch occupies a
leased connection, with a configurable watcher budget reserving ordinary RPC
capacity. An unchanged lease expiry requires another state check. This is a
revision notification, not a replayable change feed. Journal and checkpoint
recovery retain the native owner's existing contract.

## Directory bindings

Run `python3 tools/mailbox_bridge.py --config bindings.json`. Configuration
contains `socket`, `space`, `worker`, optional `max_file_bytes`, and a `bindings`
array. Each binding has `name`, `sender`, and `directory`:

```json
{"socket":"SOCKET_PATH","space":"agent-mailboxes",
 "worker":"./runtime/cetta-value-worker-core.prime-need-heap-index.prime-eval-stack.managed-symbols",
 "bindings":[{"name":"codex-review","sender":"codex","directory":"./mailboxes/review"}]}
```

Supply the actual socket path. One Linux inotify descriptor watches all
configured directories. A published `.json` file contains exactly `id`,
`correlation`, `sender`, and text `body`; sender must match the binding.
`mailbox_bridge.deliver` publishes by flushing a temporary file, renaming it,
and flushing the directory. Use immutable, unique filenames and atomic
publication. A running watcher also handles ordinary copies on close;
startup and overflow rescans cannot determine whether a file is still open.

The bridge durably admits a message before moving its source into `received/`.
Malformed files and refused symlinks move into `rejected/`, retaining their
contents without scheduling endless retries. Each file failure is contained,
including an excessively nested JSON envelope. Lost
acknowledgements reconcile by exact message identity. Startup, overflow, and
replaced directories trigger rescans. Transient owner outages retry held
arrivals without another file event. Directories are trusted capabilities:
sender labels do not authenticate someone who can write that directory.

Published envelopes grant read access to their inherited group. For writers
under other accounts, use a setgid directory whose group includes the bridge.

## Codex bindings

Run `python3 tools/mailbox_codex.py --config codex-bindings.json`. Configuration
contains `socket`, `space`, `worker`, `transport_module` (the installed App
Server helper path), `app_server_socket`, and bindings with `thread`, `turn`,
`channel`, and `sender`. Thread and turn IDs are explicit operator
configuration. The bridge starts no turns and supplies no model, workspace,
or policy overrides.

The configured App Server must own the room being followed. Another server's
active writer can refuse `thread/resume`; reading that thread's history does
not establish a live subscription to it.

It uses `thread/resume` to subscribe, reconciles `thread/read`, then waits for
`turn/completed` notifications, following the
[App Server protocol](https://learn.chatgpt.com/docs/app-server).
An idle bridge makes no transcript polls. A known completion remains pending
through reconnection until a full transcript is available. Partial summaries
are never acknowledged as complete results. Reconnection also reconciles
completions missed while disconnected.

The admitted JSON is a versioned `CodexTurnResult`: fixed thread/turn identity,
terminal status, non-commentary agent messages, and any reported error.
Failed and interrupted turns remain explicit terminal data. Duplicate
completion delivery is harmless after consumption and settlement.
Qualification uses the installed transport against a local wire fixture and
the checkpointed agent consumer. A live Codex operation still needs its own
acceptance check.

## Persistent receivers and consumers

`Mailbox.listen(channel, sender, tag)` stores a persistent receiver.
`accept(ticket, message_id, channel, sender, tag, owner, generation,
capacity=...)` checks that receiver and the exact message occurrence, then
consumes the pending message and saves its registration, owner, delivery
identity and ready continuation in one conditional native batch. The receiver
remains. Several messages can share one conversation and even the same body;
their occurrence identities distinguish them. Ticket hash collisions refuse
against the retained full identity. The configurable active-wait capacity is
checked against the same revision as the commit.

`register(..., intent=(MailboxCodexWatch 1 thread turn))` records the receive
and inert subscription intent together. The turn must equal the receive's
correlation. A repeated ticket with another thread is a conflict. External
subscription happens after commitment; the stored intent recovers it.

A consumer must retain ready state until its own durable checkpoint or action
receipt. That receipt authorizes `settle`; reading alone does not. Independent
stores require this recoverable handoff, rather than a claim that both commits
are atomic. `Mailbox(..., expected=(store_uuid, space_id))` reopens an existing
identity without creating a replacement. Pin that identity across process
restarts as well as reconnects.

`tools/mailbox_post.py --config bindings.json inbox --body-file note.txt`
publishes an envelope through the configured directory, using a fresh identity
and fsync/rename. Supply `--id` only to retry the same occurrence. This is a
local delivery operation; `published` is not yet an agent checkpoint receipt.

## Indexing and retention

Native queries use the shared path index for rigid string coordinates,
including portable service records without packed term headers. Hash
collisions remain candidates and are resolved by the exact matcher. Numeric
promotion, variables, duplicate occurrences, source order, and opaque terms
retain conservative matching rules.

`query-profile` and `query-first-profile` wrap the normal view in
`(profile VIEW (query-work EXAMINED INDEX_COUNT SOURCE_ROWS KEYED_ROWS
UNCERTAIN_ROWS))`. `EXAMINED` counts candidates passed to matching; other
counters describe all retained path indexes, not just the selected access
path. `query-first` returns an explicit `selection`, not an exhaustive snapshot.

Scoped reads examine few candidates despite unrelated completed history in
the benchmark. Index construction, mutations, and checkpoints still need
measurement at larger workloads. Whole-space revisions are not per-key
validation or a general database transaction API.

Message bodies, registrations, route ownership, and terminal receipts are
retained. There is no automatic pruning, existing Telegram-journal archive,
or migration of other SQLite databases. A retention policy must preserve each
record's only copy and deduplication over its declared replay window before
removing live history. No ACT format or root-publication changes are included.
