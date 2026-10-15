# Replayable Megalodon libraries in the Prime candidate

A source library retains a Megalodon `-sexprinfo` export, including theorem
proof trees, together with an explicit initial primitive profile and requested
closed type instances. It can be loaded in a new process without Megalodon or
the original source file. A later MeTTa development can cite the loaded
theorems and consume their native proofs dependently.

## Save and use

For example, export the tactics fixture using an installed Megalodon executable:

```sh
python3 tools/megalodon_native_projection_v1.py \
  --megalodon /path/to/megalodon \
  --source tests/support/megalodon/positive_tactics_package.mg \
  --cetta ./cetta --save-library tactics-library.json --run
```

Load it later and run a separate development:

```sh
python3 tools/megalodon_native_projection_v1.py \
  --load-library tactics-library.json --cetta ./cetta --run \
  --consumer tests/support/megalodon/library_consumer.metta
```

The consumer locates the source theorem `test_assume`, proves a new theorem
using it as a retained premise, and applies its native proof to a dependent
consumer. It also computes the application with `set:native-normalize`,
obtaining the identity function on proofs of `False`, checked at `False → False`.
No proof of `False` is introduced.

Without `--run`, the tool emits the projected MeTTa program after source
admission. `--save-library` refuses to overwrite an existing file. A saved
library supplies its own source, profile and instances; those cannot silently
be overridden by load-time source-selection flags.

Use repeated `--theorem` flags to select source theorem labels. Source axioms
require explicit `--axiom` selection and remain assumptions. Polymorphic source
declarations require explicit ordered `--type` arguments such as `(SET)` or
`(AR (SET) (PROP))`. The saved format can carry several closed instances of the
same source occurrence. It does not provide first-class polymorphic proof values.

Type arguments follow the source declaration order. In Megalodon's export,
`TPVAR 0` refers to the first supplied argument; nested explicit type binders
use the opposite index convention. The importer reverses the declared parameter
interval in types, terms and proofs, without reversing object variables or proof
hypotheses. Thus a source type `A -> B` instantiated with `set, prop` becomes
`set -> prop`, not `prop -> set`. Formation alone would accept either, so tests
check the exact types and uses of two- and three-parameter source declarations.

Use repeated `--include signature.mgs` flags, in source order, when a document
imports signatures. Their exported declarations are retained before the main
document and participate in the same ordered NIK admission. Missing or wrongly
ordered dependencies fail source checking. Loading a saved library cannot
silently add an include or replace its initial profile.

## Authority and revision

The library is untrusted data, not a trusted cache. On every load:

1. The source export is parsed with matched `THM`/`PROOF`/`QED` events.
2. Every declaration through the last selected occurrence is compiled into one
   connected admission article and checked by the existing `MEGALODON-TERM`
   NIK authority. An invalid earlier declaration cannot be skipped merely
   because the selected theorem does not cite it.
3. Requested instances and dependencies are projected using the existing scoped
   HOL declarations. Definitions remain definitions, axioms remain assumptions,
   and theorems retain proof trees.
4. With `--run`, the scoped and dependent checkers validate the materialized
   proofs and dependent uses. Proof-package use rechecks the current theory;
   a saved package is not accepted merely by its shape or digest.

Source-position, prefix and type-instance keys identify provenance; they do
not certify truth. Appending later source declarations preserves earlier
instance names. Changing an earlier source declaration changes the namespace.
Retained runtime proofs remain subject to dependency withdrawal and revision
checks. The file does not serialize a live space or preserve mutable runtime
state across processes.

Native proof packages collect the declarations and ordered computation rules
reachable from the proof, its result and its assumed propositions. Collection
follows declaration types, rule patterns and right-hand sides to a fixed point;
all alternatives at a selected rule head keep their original order. An unrelated
definition therefore need not invalidate a retained package. A changed reachable
rule or withdrawn assumption still prevents reuse. Package use continues to
recompile and check the current proof and compare every package field; this
dependency boundary is not a trusted proof cache.

A later consumer may call functions declared after the proof package was made.
Its current declarations and transitively reachable rules extend the execution
context, which the dependent kernel checks along with the application and its
result. The original proof package remains unchanged. `SetNativeUseV1` and
`SetNativeNormalFormV1` are observations of this current-theory execution, not
standalone authority certificates for the consumer or a frozen copy of its
ambient theory. An undeclared consumer constant is still rejected.
The package's assumption list describes its source proof; it does not summarize
additional assumptions in the consumer's ambient theory. Package-private proof
and family constants cannot be rebound by consumer declarations.

The formal native Megalodon checker has a separate computed least syntactic
dependency closure and a theorem preserving its exact verdict under agreement
on that closure, including rejection and fuel exhaustion. Least syntactic
support is not a claim about the smallest dynamic read set. The C rule collector
is tested against the same locality requirement, not identified with that Lean
algorithm by an implementation-refinement theorem.

The status line separates the complete source inventory from the admitted
prefix and selected theorems/assumptions. Unselected declarations after that
prefix have not been checked by this load. In particular, loading selected HOTG
assumptions is not verification of their truth or complete HOTG hosting.

## Definition conversion and sharing

The conversion generator compares identical terms before unfolding definitions
and exposes logical heads only as needed. This matters when an exported alias
has the same content identity as an earlier declaration: blindly unfolding that
identity can revisit an eta expansion indefinitely. Successful comparisons emit
explicit beta, delta, eta and congruence evidence, checked by the existing NIK
authority. Exhausting the bounded search is failure, not proof of inequality.
This is not a general normalization or conversion-completeness theorem.

Applications with different heads, or with a lambda head, expose their heads
before comparing arguments. Beta reduction may discard an argument altogether;
an unsuccessful attempt to compare that argument must not take precedence over
the available computation. The generator still emits the same independently
checked reduction paths. This strategy is partial and bounded, including for
shared named heads whose arguments require difficult conversion.

Declaration, signature and known-proposition lists share immutable suffixes.
Their order, duplicate occurrences and canonical encoding are unchanged. This
avoids copying a whole source environment at each proof node; sharing does not
give a cache entry any authority.

Closed DAG replay memoizes successful argument support and signature checks
within that one invocation, keyed by the immutable argument and its binder
depth. Premises, side conditions and conclusions are still checked at every
proof node. The memo is discarded before another article or revision is
checked; incremental trace calls do not use it. Scope checking separately
shares completed subtree checks at the same depth while rejecting cycles.
Stable-term copying uses an explicit traversal stack, so article depth does
not consume one C call frame per node.

The larger qualification target is `make test-prime-nik-hotg-family-library-v1`.
It uses the actual `family_enclosing_universe.mg` development and its ordered
HOTG preamble, supplied through `MEGALODON_HOTG_FAMILY_SOURCE` and
`MEGALODON_HOTG_PREAMBLE`. It checks the whole selected prefix, consumes and
normalizes all nine family theorems, and runs the separate
`tests/support/megalodon/family_library_consumer.metta` development. That
consumer derives the identity-family specialization, computes its native
membership-proof function, retains four assumptions, rejects an altered
self-membership conclusion, and refuses further package use after one of its
actual assumptions is withdrawn. It does not identify the object-theory universe
with the host's type universe or establish the truth of the source axioms.

## Computing dependent consumers

`(set:native-use package consumer)` checks the application and retains it.
`(set:native-normalize package consumer)` additionally computes the application
and its synthesized dependent type using the existing regular conversion
engine. It independently checks the computed value against the computed type,
then returns:

```metta
(SetNativeNormalFormV1 package consumer application value source-type normal-type)
```

Both operations revalidate the package in the current theory. The original
application, displayed type, source proof and assumptions remain available;
normalization does not replace the retained proof package with an assertion
that its result is assumption-free. A withdrawn dependency prevents use.

Computing the type matters for dependent pairs: the type of a second
projection can mention `Fst(Pair(...))`, while the computed result's type
mentions the first component directly. This service starts with a successfully
synthesized application. It does not make arbitrary unannotated pairs inferable
or reconstruct typing evidence from an erased term.

This is a bounded strong-reduction service for the supported native language,
not a proof of strong normalization or the ordinary program evaluation strategy.
Synthesis, normalization and final
checking share one resource budget. Exhaustion is incomplete computation;
no partial value is returned as a successful normal form. Arbitrary declared
rules have no termination guarantee. No efficiency claim for large proof
terms follows from successful small-library tests.

## Verification and scale

`make test-prime-nik-megalodon-native-use-v1` covers fresh-process reuse,
dependent consumption, assumptions, type specialization, bad preceding proofs,
forged admission articles, wrong declaration kinds and revision boundaries.
The Lean `TheoryAdmissionSequence` construction proves the sequence-checker
equation and two-way concatenation law for closed, canonically scoped data.
It does not prove correctness of the Python parser/projector or C executable.

`SourceTypeParameters.inferTerm_source_parameters` proves that applying a
finite list of closed plain type arguments to the translated source prefix
makes the existing Mathdata term checker compute the source-ordered result.
The underlying reversal is involutive and preserves formation. Controls show
that an excess argument is rejected and that omitting reversal can preserve
scope while changing meaning. This is an exact specialization law, not a
general source-parser correctness theorem.

The Lean `HOLDependentProofExecution` construction proves typing and beta
execution of an arbitrary dependent consumer body using the actual total HOL
proof compiler, together with the substitution law for the computed result.
This is a general theorem about that beta boundary, not a verification of the
complete C normalizer. Runtime controls additionally cover dependent pair
projections, capture avoidance, malformed applications, forged packages,
withdrawn assumptions and exhaustion at every budget below completion on the
selected unit cases.

Replay currently reconstructs and checks the selected source prefix and
recreates its native dependency closure. It is suitable for testing real
libraries but has no constant-time cache guarantee. Large-library admission,
projection, proof sharing and repeated dependent use require separate scale
measurements. The small tactics fixture is not a large HOTG benchmark.

### Source inventory checkpoint (2026-09-22)

The inventory below covers the ten `.mg` documents in the source checkout's
`theory` directory. Parsing an export is separate from ordered NIK admission
and native use; none of those columns imply validity of assumed axioms.

| Source development | Export/parser result | Ordered admission and native use |
| --- | --- | --- |
| `foundations/family_enclosing_universe.mg` | 2,090 items, nine theorems | All items admitted; all nine theorems used and normalized; derived program and rejection controls pass |
| `surreals/100thms_12.mg` | 1,157 items, 998 theorems | Unqualified: evidence construction exhausted memory at zero-based item 768, `add_SNo_assoc`, before whole-article NIK replay |
| `mizar/mml/finset_1.mg` | Parsed, ten theorems | Not qualified by this inventory pass |
| `mizar/mml/subset_1.mg`, `mizar/mml/zfmisc_1.mg` | Parsed, six theorems each | Not qualified by this inventory pass |
| `mizar/mml/card_2.mg` | Parsed, one compile-marker theorem | Not substantive mathematical coverage |
| `category/CategoryNov2022.mg`, `mizar/mml/card_1.mg`, `mizar/mml/schems_1.mg` | Rejected `QEDWITHADMITS` | No admission claim |
| `logic/GoedelGod.mg` | Source exporter failed on polymorphism in its preamble's PFG conversion | No admission claim |

The large surreal-number run first exposed a source-parameter-order error and
then an unnecessarily eager structural conversion search. Both were repaired
and covered by independent replay controls; the remaining memory failure is
still open. Evidence-generation progress is not partial theorem acceptance.
