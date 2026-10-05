# Reusable buffered LLM library

The first version implements buffered Chat Completions, OpenAI Responses and
Anthropic Messages codecs in MeTTa, with native asynchronous HTTP. HE and PeTTa
use the same authored library; PeTTa's spelling is generated. Offline fixtures
qualify the subset below. Streaming, full provider conformance and
state-of-the-art performance are not claims of this release.

The library constructs provider requests, preserves
structured responses and owns asynchronous requests through completion. Agent
loops, prompt transformations, conversation storage, tool execution and operator
budgets remain callers of the library. CeTTa supplies HTTP, credential references,
owned values and completion delivery. Provider codecs belong in MeTTa.

## Using the library

The pure codec needs `ENABLE_JSON_GSLT=1`, enabled in the default build. HTTP
submission additionally needs `ENABLE_HTTP=1`. A `BUILD=core` binary can use all
four modules without Python. Prime and SWI-PeTTa execution of this library have
not been qualified; MeTTaClaw keeps its Python transport for SWI-PeTTa.

| Import | Public operations | Purpose |
| --- | --- | --- |
| `llm` | `llm:v1:capabilities`, `encode`, `decode`, `replay`, `render` | Pure conversation codecs and ordered results |
| `llm:io` | `llm:v1:submit`, `submit-encoded`, `poll`, `wait`, `observe`, `cancel`, `headers-json` | Session-local request ownership over native I/O |
| `llm:command` | `llm:v1:encode-command` | Explicit command-text request profile |
| `llm:compat` | `llm:v1:project-chat` | Explicit lossy chat projection for existing callers |

All operation names in the table have the `llm:v1:` prefix. Protocol names are
`chat-completions`, `responses` and `anthropic`, as strings or symbols. Pure
operations return `(llm:v1:ok VALUE)` or `(llm:v1:error CODE)`; error codes contain
neither provider bodies nor native diagnostics. JSON text is accepted as a
String or a foreign-born Symbol. It is parsed as data and never interpreted as
MeTTa code.

This example constructs a request and decodes a fixture without network access:

```metta
!(import! &self llm)
!(llm:v1:capabilities responses)
!(llm:v1:encode chat-completions
  "{\"model\":\"MODEL\",\"max_tokens\":42,\"messages\":[{\"role\":\"user\",\"content\":\"Hello\"}]}")
!(llm:v1:decode anthropic
  "{\"id\":\"fixture\",\"content\":[{\"type\":\"text\",\"text\":\"Hello\"}],\"stop_reason\":\"end_turn\"}")
```

### Requests and provider decisions

`(llm:v1:encode PROTOCOL REQUEST_JSON)` returns
`(llm:v1:ok (llm:v1:encoded PATH BODY_JSON))`. The caller chooses a base endpoint;
paths are `/chat/completions`, `/responses` and `/messages`.

The common request has a nonempty `model`, a positive integer `max_tokens` and
ordered `messages`. Messages use `system`, `developer`, `user`, `assistant` and
`tool` roles. Text conversations and function tools use the familiar chat-shaped
fields `content`, `tool_calls`, `tool_call_id`, and
`tools: [{"type":"function","function":{"name":...,"parameters":...}}]`.
Tool arguments must be complete JSON objects. A `tool_choice` is `auto`, `none`,
`required`, or a named function choice. Multimodal request formats and hosted
tool declarations are outside this initial common subset.

Optional common fields are `reasoning`, `thinking`, `cache_system`,
`role_policy`, `tool_choice_policy`, `chat_profile`, `stream` and `extensions`. Unknown top-level
fields fail; provider-specific wire options belong in `extensions`, which cannot
overwrite reserved or already populated wire fields. The caller remains
responsible for the selected model's capability and accepted schema dialect.
Tool-schema validation currently checks only object shape; it is not JSON Schema
2020-12 validation or validation of arguments against the declared schema.

The defaults preserve system/developer roles in Chat Completions and Responses.
Anthropic rejects a developer role unless the caller selects
`role_policy: "merge-system-developer"`. Selecting that same policy for Responses
joins the leading instruction messages explicitly. Anthropic caches the final
system block unless `cache_system` is false; the separate command profile caches
each caller-supplied stable block to preserve its historical request layout.

Anthropic `reasoning.effort` becomes `output_config.effort`; explicit `thinking`
is retained. Forced tool choice with thinking fails unless the caller selects
`tool_choice_policy: "auto-for-thinking"`, which permits that downgrade.
Thinking and effort support still depend on the model. Responses reasoning objects retain their wire fields. Chat defaults to the
OpenAI wire profile: `max_completion_tokens` and `reasoning_effort`. Select
`chat_profile: "compatible"` for the older compatible-provider layout with
`max_tokens` and a `reasoning` object; the command profile selects that layout
explicitly. Unsupported fields in a reasoning object fail rather than vanish. `stream: true` fails rather than selecting an
unimplemented streaming route. `capabilities` reports codec support, with unknown
model capabilities and no tool execution.

The command profile accepts `model`, `max_tokens`, ordered `stable` text blocks,
`user`, optional `reasoning` and a caller-owned `cache_key`. Prompt splitting,
cache identity, memory and pacing are caller policy. It does not rewrite a loop.

### Ordered results and replay

`decode` returns the historical JSON AST (`JsonObject`, `JsonArray`, etc.). Use
`(llm:v1:render VALUE)` to produce JSON text, or `render-outcome` for a tagged
success or short failure code. The result fields are:

```text
schema: "llm-result-v1"
protocol, protocol_version, response_id?
items: ordered messages, tool calls, reasoning and opaque items
       messages contain ordered parts; each retains its native JSON value
termination: normalized, raw, details
usage: input?, output?, cache_read?, cache_write?, reasoning?, native
native: complete parsed provider response
```

Absent counters remain null. Cache counters are not summed into other counters.
Chat Completions retains all choices, with each choice's index and termination;
its top-level termination is `per-choice`. Raw reasons and future semantic items
remain visible. Recognized malformed blocks and duplicate keys fail, including
nested duplicate keys. Native payload preservation is preservation of JSON
values and embedded strings, not original wire whitespace or key ordering.

`(llm:v1:replay RESULT_JSON CHOICE_POSITION)` creates an assistant message for
the same protocol/version. Chat selects one choice by its array position;
Anthropic and Responses use position zero. Anthropic's `native_content` and
Responses' `native_items` attachments preserve grouping, opaque signatures,
encrypted reasoning and item order. Pass the returned message into the next
common request. Incomplete output or an unfinished tool item is rejected. The
source must be a canonical result produced by `decode`; callers must not forge
its native/derived fields or transfer an attachment to another provider.

`project-chat` selects one choice and intentionally flattens text and separates
tool calls from reasoning. It preserves call IDs and provider-native reasoning
payloads, but loses their original interleaving and some item metadata.
Unsupported semantic items block this projection. It is suitable for existing
mode interfaces, not lossless conversation storage. It never executes a tool.
Callers that execute tools must also reject truncated/refused output and validate
arguments against their own tool contracts.
`project-chat-value` applies the same projection to a decoder-produced historical
JSON value, avoiding serialization between pure stages. Its input must retain
the decoder's native and derived fields without caller modification.

### Owned asynchronous HTTP

```metta
!(import! &self llm:io)
; BASE ends at the provider's API root, for example https://api.openai.com/v1.
; HEADERS is held data such as ((http:header "Content-Type" "application/json")
;                             (http:credential "LLM_PROVIDER")).
; Supply REQUEST_JSON, a total timeout in milliseconds, and a response byte cap.
; Submit exactly once, retain the phase, and wait or poll that phase thereafter.
; (llm:v1:submit EPOCH CORRELATION PROTOCOL BASE HEADERS REQUEST_JSON TIMEOUT CAP)
; (llm:v1:wait PHASE WAIT_MILLISECONDS)
; (llm:v1:poll PHASE)
; (llm:v1:cancel PHASE)
```

Phases are `(llm:v1:pending EPOCH CORRELATION PROTOCOL HANDLE)`,
`(llm:v1:completed EPOCH CORRELATION RESULT)`,
`(llm:v1:failed EPOCH CORRELATION CODE CERTAINTY)` and
`(llm:v1:cancelled EPOCH CORRELATION CERTAINTY)`. A decoded provider truncation
can be a completed transport phase with a `length` result termination; transport
completion does not authorize execution.

The caller serializes use of each phase and supplies a fresh session epoch and
correlation identity. Handles cannot be persisted into another CeTTa session.
Native `io:poll HANDLE` and `io:wait HANDLE MILLISECONDS` consume only that
handle; other completed bodies remain owned by the worker and charged to its
aggregate budget. Legacy unselected I/O operations retain their behavior.
Waiting on a terminal phase returns it unchanged, without another HTTP request.
`observe` returns `(llm:v1:foreign PHASE EVENT)` for an unrelated event.
Consumption and cancellation release native request storage; guest results have
the caller's ordinary arena lifetime. Session shutdown owns abandoned cleanup.

`headers-json` accepts a JSON array of literal header specifications,
`[["http:header","NAME","VALUE"],["http:credential","ALIAS"]]`. It reifies only
those two data tags, avoiding foreign-language list conventions. Native I/O
validates the actual headers and resolves a credential alias at its authorized
endpoint. Configure aliases as described in the [I/O guide](../../docs/io/README.md).
The library neither reads credentials nor requests the name of a secret variable.

Failure certainty distinguishes `not-started`, `response-received` and
`remote-outcome-unknown`. Cancellation or timeout after submission cannot establish
that a billed remote operation never happened. There is no internal retry or
cooldown. Response headers and detailed transport-start metadata are not yet
exposed to this facade, so it does not implement Retry-After policy. Local
one-shot consumption is not exactly-once remote execution or billing.

## Offline qualification

From the CeTTa root:

```sh
make BUILD=core ENABLE_HTTP=1 ENABLE_JSON_GSLT=1 \
  test-llm-library-v1 test-llm-io-v1 test-io-runtime test-http-worker
```

For the changed paths with address, undefined-behavior and leak checks:

```sh
ASAN_OPTIONS=detect_leaks=1 make BUILD=core ENABLE_HTTP=1 ENABLE_JSON_GSLT=1 \
  ENABLE_MANAGED_SYMBOLS=1 ENABLE_SANITIZERS=1 \
  test-json-gslt-library-body test-llm-library-v1 test-llm-io-v1 test-io-runtime
```

The codecs run in HE and PeTTa, with order/call-identity/unknown-usage mutation
controls. Scripted local HTTP peers cover foreign completion retention,
terminal stability, malformed replies, quota status, bounded bodies, timeouts,
cancellation and no implicit retry. The native runtime's own controls include a
replayed-completion mutant. No accounts or external provider traffic are used.

[MeTTaClaw](https://github.com/godelclaw/mettaclaw) uses the library for its native
transport. Its fixture tests compare command request bodies across all three
protocols and both dialects, and cover scoped credentials, signed tool batches,
duplicate plans, truncation and operator admission. Process checks exercise real
Iter/Omega loops with local peers, checkpoints, restarts and cooperative exits.
Their Python path remains available. Upstream sources and their policies remain
separate from the library.

The executable [Lean boundary model](https://github.com/godelclaw/MeTTapedia/blob/main/lean/pettaclaw/LlmBoundary.lean)
proves terminal stability, at-most-one completion transition over traces, foreign
queue preservation, ordered compatible replay, call-identity preservation,
truncation/unfinished-tool rejection and selected admission laws. Host handle
uniqueness, faithful correlation, serialized ledgers and no hidden transport retry
are explicit assumptions. This is a checked model with executable fixtures, not
verification of the C implementation, JSON parser, provider or signatures.

After rejecting duplicate keys, the decoder converts the occurrence-preserving
JSON AST directly into the historical AST with `json:to-legacy`; it does not
serialize and reparse that intermediate value. Parser or conversion exhaustion
returns `protocol-resource-limit`, separately from invalid or ambiguous JSON.
Large-context performance, long-running retention and streaming allocation
bounds still need matched measurements.

## Evidence and useful precedents

The survey inspected provider documentation, protocol standards and selected
source files from nine pinned repositories. [The source catalog](survey-sources.json)
records the exact commits and file digests. Source inspection establishes design
examples, not benchmark results or a certification of the surveyed libraries.
No external implementation is vendored by this library.

| Example | Useful mechanism | Limit of the example |
| --- | --- | --- |
| [SWI-Prolog llmpl](https://www.swi-prolog.org/pack/file_details/llmpl/prolog/llm.pl?show=src) | Small deterministic request predicate; explicit HTTP status and timeout handling. | Its payload and result are a user prompt and assistant text. That facade cannot preserve general tool or reasoning conversations. |
| [Prolog streaming client](https://github.com/pvdabeel/prolog-llm) | Shared chat-compatible transport and a separate native Anthropic path. | The inspected stream processors extract text lines; service-level dynamic history and XML command dispatch should remain outside a reusable protocol kernel. |
| [OCaml Anthropic](https://github.com/tmattio/ocaml-anthropic) | Typed content and events, Eio streams, callback and final-message interfaces. | Its interface snapshot is a reference for structure, not the authority for current provider capabilities. |
| [OCaml AI SDK](https://github.com/ahrefs/ocaml-ai-sdk) | Role-constrained parts, provider metadata, typed errors, separate request and stream-idle timeouts. | Its retry-after parser omits HTTP dates, and its stream-idle retry classification does not establish remote execution or billing safety. |
| [Haskell provider bindings](https://github.com/MercuryTechnologies/claude) | Algebraic content variants with an unknown-content payload; separately typed tool, thinking and stop information. | A closed stop/event enumeration still needs an explicit forward-compatibility policy. |
| [Haskell Responses binding](https://github.com/MercuryTechnologies/openai) | Typed item lifecycles and streaming events with an unknown-event payload. | Falling back to an unknown variant must not disguise malformed data for a recognized event type. |
| [Haskell protocol router](https://github.com/junjihashimoto/louter) | Distinct protocol translators and accumulated tool-call fragments. | The inspected Anthropic stream finalizer inserts fixed usage and an end-turn reason. CeTTa must preserve unknowns and actual stop reasons instead. |
| [AI SDK provider interface](https://ai-sdk.dev/providers/community-providers/custom-providers) | Ordered content, separate generate/stream operations, capability warnings, provider metadata, raw and normalized finish reasons, optional usage counts. | Provider abstraction does not make distinct wire protocols interchangeable. |
| [Pydantic AI messages](https://pydantic.dev/docs/ai/api/pydantic-ai/messages/) | Part-based histories, tool-result provenance, signed thinking and provider-specific metadata. | Its higher-level orchestration and history rewriting are separate concerns from the protocol kernel. |

Resource ownership has especially useful precedents in
[SWI-Prolog setup_call_cleanup](https://www.swi-prolog.org/pldoc/man?predicate=setup_call_cleanup/3),
[OCaml Eio switches](https://github.com/ocaml-multicore/eio#switches) and
[Haskell ResourceT](https://github.com/snoyberg/conduit/blob/master/resourcet/README.md).
Their shared lesson is to scope acquisition and release across failure and
cancellation. CeTTa must additionally qualify cleanup across guest backtracking,
cuts, suspended choices and session shutdown. Enumerating an answer must not
silently resubmit a network request.

## Standards and fidelity

- [Open Responses](https://www.openresponses.org/specification) is a useful
  interoperability target for ordered items, content parts and semantic events.
  The inspected OpenAPI 3.1 snapshot identifies version `2026-04-24`. The first
  CeTTa release would expose an explicit subset; it must not advertise full Open
  Responses compliance merely because it uses a similar internal model.
- [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259) governs JSON on the wire.
  [JSON Schema 2020-12](https://json-schema.org/specification) supplies a named
  tool/output-schema dialect, with each provider's accepted subset stated
  separately. Valid JSON, a valid schema and a supported provider schema are
  different checks. Use occurrence-preserving [json](../../lib/json.metta)
  parsing and reject ambiguous duplicate protocol fields before extracting them.
- [WHATWG SSE](https://html.spec.whatwg.org/multipage/server-sent-events.html)
  supplies framing, UTF-8, line-ending and multiline-data rules. Borrow its frame
  parser, not browser EventSource's automatic reconnect policy for a billed POST.
  EOF does not establish successful provider completion.
- [HTTP semantics](https://www.rfc-editor.org/rfc/rfc9110.html#name-retry-after)
  supplies both numeric and date-valued Retry-After. Retry hints do not establish
  that a POST was unprocessed. Request deadlines, retry counts and cooldowns
  belong to an explicit caller policy.
- [MCP tools](https://modelcontextprotocol.io/specification/2026-07-28/server/tools)
  describe tool discovery, schemas and invocation over a separate protocol. MCP
  request IDs, provider tool-call IDs and local HTTP handles remain distinct.
  An MCP adapter is a client of the tool boundary, not a replacement LLM wire
  format. Agent-to-agent protocols likewise do not belong in the initial client.
- [OpenTelemetry GenAI conventions](https://github.com/open-telemetry/semantic-conventions-genai/blob/e07f4ebacb08f56db8c4c882d117720333fbca04/docs/gen-ai/gen-ai-spans.md)
  provide useful request, completion and usage metadata. The inspected conventions
  remain in development and mark message content as opt-in. Default telemetry
  should contain identifiers, timings, counts and error codes; prompts, tool
  arguments, responses and reasoning require explicit content capture.

Provider requirements remain authoritative:

- [OpenAI reasoning](https://developers.openai.com/api/docs/guides/reasoning)
  requires preserving response items needed for later reasoning/tool turns.
  Stateless replay must retain opaque encrypted content when returned; projecting
  only assistant text is insufficient.
- [Anthropic thinking](https://platform.claude.com/docs/en/build-with-claude/thinking)
  requires unmodified thinking blocks and their sequence within a tool-use turn.
  Effort, thinking modes and forced tool choice depend on the model and protocol
  version. Do not silently replace an unsupported requested option.
- [Gemini Generate Content thought signatures](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures)
  illustrate why attachments must retain their original part positions. This is
  a precedent from that API, not a claim that all Gemini protocols are identical
  or that a Gemini adapter is included in the first release.
- [Anthropic streaming](https://platform.claude.com/docs/en/build-with-claude/streaming)
  includes indexed block updates, signature deltas, cumulative usage and errors
  within a stream. [OpenAI streaming](https://developers.openai.com/api/docs/guides/streaming-responses)
  uses typed semantic events. HTTP 200 and receiving some text are insufficient
  success conditions for either protocol.

## Extended data model (roadmap)

The following extends the buffered API toward media, streaming and richer tool
contracts. It is a vocabulary sketch, not executable MeTTa or a released API:

```text
RequestV1(localId, provider, model, orderedInput, tools, options, extensions)
Message(role, orderedParts, nativeAttachment)
Part = Text | MediaRef | ToolCall | ToolResult | OpaqueReasoning | OpaquePart
Item = Message | ToolCall | ToolResult | OpaqueReasoning | OpaqueItem
ToolCall(callId, name, argumentBytes, parsedArguments?, status, executionOwner)
ResultV1(providerResponseId?, orderedOutput, termination, usage, extensions)
Termination(normalized, rawProviderReason?, incompleteDetails?)
Usage(input?, output?, cacheRead?, cacheWrite?, reasoning?, rawProviderUsage)
```

Keep both item order and content-part order, including provider-native grouping.
Keep system and developer roles distinct in the common model. Mapping a role to
a provider that lacks it requires an explicit adapter policy. Preserve multiple
choices/candidates where supported, rather than treating the first as the whole
response. Missing usage is unknown, not zero. Cache counts are not automatically
additive, and cumulative stream usage must not be summed as if it were deltas.

Opaque attachments carry their source protocol/version and native payload. They
can be replayed to a compatible provider without changing their contents; they
cannot be converted into executable commands or silently moved to another
provider. Unknown ancillary events can be preserved without aborting a supported
stream. Unsupported semantic items remain visible and block projections that
would otherwise discard required context. Malformed known items are errors.

The library returns tool requests and identifies provider-executed tools. It
does not run local tools. Argument fragments become actionable only after the
call completes and its declared schema is validated. Command text and the
existing chat-style result are explicit compatibility projections with defined
losses, not the canonical storage format.

```text
mode / application
       |
optional admission and retry policy
       |
MeTTa request validation + provider codec
       |
owned request -> native HTTP + host credential reference
       |
owned observation -> MeTTa provider decoder -> ordered result
```

The longer-term boundary includes capabilities, pure codecs, submission, owned
observations, cancellation and explicit stream-consumer release. The buffered
operations are listed above; no stream or separate release operation exists yet. Generic transport
events are routed by an owner; waiting for one request must not consume or lose
another request's event. A synchronous helper uses the same lifecycle and final
fold as its asynchronous counterpart.

Cancellation is a request to stop local work; after transmission it does not
prove the remote operation never happened. Distinguish submission failure,
remote rejection, malformed protocol data, truncation, local cancellation,
timeout and an unknown remote outcome. Keep request epoch, transport handle,
provider response ID and tool-call ID separate. Exactly-once local delivery is
not exactly-once remote execution or billing.

The host owns credential resolution. A guest holds an alias scoped to the
configured endpoint, not a key. HTTP helpers validate headers, verify TLS and
avoid forwarding credentials across an unapproved redirect. Secrets and raw
payloads do not belong in ordinary diagnostic text.

## Native transport and remaining gaps

[io](../../lib/io.metta), [library_io.c](../../src/library_io.c) and
[http_worker.c](../../src/http_worker.c) supply asynchronous HTTP, bounded buffered
bodies, owner-selected completion and host credential references.
`http:response` still exposes only status and body. Bounded response headers,
delivery uncertainty derived from native start facts and separate stream idle
deadlines need a versioned generic observation; the existing response shape
must remain compatible.

The write callback accumulates the whole body. It does not deliver incremental
chunks or backpressure to MeTTa. Streaming remains an unsupported capability.
[lib/petta/lib_llm.metta](../../lib/petta/lib_llm.metta) is an older Python-backed
interface, separate from the new library.

MeTTaClaw's native client now constructs wire requests through this library;
Python retains provider/model selection, prompt splitting, credential scope and
operator policy. Its imported loops receive `project-chat` or command text. The
structured host return also carries the full ordered `llm_result`, but imported
mode histories still use their existing compatibility messages. Automatic
lossless native replay in those histories is a separate integration task.

The Lean model now includes developer roles, ordered items/parts, optional usage,
compatible replay, owner-selected queues and completion traces. It does not
model streaming or verify the C transport, parser, provider or signatures.

## Bounded implementation order and acceptance

1. Buffered codecs, implemented and fixture-qualified for the three protocols:
   Chat Completions, Responses and Anthropic Messages. Introduce ordered data,
   explicit capability decisions, typed errors and legacy projections. Qualify
   held data and effects in HE and PeTTa, including Symbol text from foreign calls.
2. Buffered integration, implemented and locally qualified: native `io` with owner-scoped handles, credential
   references and versioned observations. Move request construction from Python
   to MeTTa. Preserve the existing upstream Iter/Omega prompts, transformations
   and repository defaults; managed policy keeps its current configurable values.
3. Next stage: add SSE framing and bounded chunk delivery through the same transport. Use
   distinct connect/header, idle and total deadlines, and bounded queues. Retain
   buffers through consumer leases and release them after cancellation or terminal
   consumption. Do not copy an ever-growing transcript on each delta.

Initial qualification uses fixed offline provider replies and a local scripted
HTTP server, not credentials or production conversations. Check roles and cache
boundaries; ordered text/tools/reasoning; signed and unknown native attachments;
tool IDs and batches; schema/option rejection; missing usage; all stop and error
classes; foreign/duplicate/late completions; cancellation races; bounded bodies;
and mode restart epochs. Run the relevant existing mode conformance checks.

The streaming gate adds every byte split of small UTF-8/SSE fixtures, multiline
data and line endings, fragmented tool JSON, cumulative usage, mid-stream error,
EOF before terminal, slow consumers and release after cancellation. Unknown
event controls must not let malformed recognized events pass. Measure allocations,
retained bytes, copying work and time to first event, using matched workloads.
No unsupported capability is silently simulated by a text-only result.

Selected formal obligations are:

```text
accepted round trip preserves roles, order, call IDs and opaque payloads
unrelated observations cannot complete an owned request
a terminal request cannot deliver another completion
an unfinished tool call cannot become an executable local action
missing usage cannot establish zero spend
upstream policy adds no admission restriction
```

Round-trip claims apply only to the adapter's validated capability domain. Relate
the pure MeTTa model to the executable codecs with fixtures and deliberate
order/identity/terminal mutants. State owner serialization, handle uniqueness,
buffer lifetime and external-service behavior as host obligations until they
are connected to concrete implementation evidence. Research alone establishes
neither complete correctness nor state-of-the-art performance.
