# Native named mailboxes

The mailbox policy and client are MeTTa: `lib/mailbox.metta` turns owned values
into transition plans over CeTTa's native named space service, which stores
them durably. The Python host, directory and Codex bridges and command-line
tools were removed. The client evaluates plans inside the caller's CeTTa
process; it needs neither a value worker nor a bridge daemon. The legacy
`lib/petta/lib_mailbox.metta` path imports the same implementation.

```metta
!(import! &self mailbox)
!(mailbox:send $socket "agent-mail" "unique-occurrence" "reader" "writer" "hello")
!(mailbox:next $socket "agent-mail" "reader")
; (mailbox:delivery "unique-occurrence" "writer" "hello")
!(mailbox:ack $socket "agent-mail" "reader" "unique-occurrence")
```

The recipient name is the channel. `next` first recovers an unacknowledged
ready delivery, otherwise atomically takes one pending occurrence using the
MeTTa policy. `ack` settles it; an application should do that after committing
the turn that read it. A restart before acknowledgement presents the same
occurrence again. Already settled occurrences are not presented again.
Send retries with the same identity and content are duplicates; a changed
body, sender or recipient is a conflict. Both HE and PeTTa dialects run the
native client. Runtime applications can hide occurrence and acknowledgement
details from the agent, as GödelClaw does.

`mailbox:watch` waits for the revision of an opened store to change, without
consuming anything. Capture the store before checking for pending mail and
watch that revision afterwards; arrivals in between cannot be missed.

The native client conformance test is `tests/test_native_mail_client.py`;
set `CETTA_BIN` and `CETTA_SPACE_SERVICE` to the two built executables.

## Build and qualification

```bash
make BUILD=core ENABLE_DURABLE=0 ENABLE_HTTP=0 ENABLE_JSON_GSLT=0 \
  ENABLE_LIB_PROLOG=0 ENABLE_MANAGED_SYMBOLS=1 test-space-path-index
```

## Atomic boundary and semantics

`lib/mailbox.metta` produces transition plans from owned values.
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

`watch` atomically compares a captured revision and registers a deferred reply.
A write in the read/subscription gap wakes immediately. Each watch occupies a
leased connection, with a configurable watcher budget reserving ordinary RPC
capacity. An unchanged lease expiry requires another state check. This is a
revision notification, not a replayable change feed. Journal and checkpoint
recovery retain the native owner's existing contract.

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
