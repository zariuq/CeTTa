PLN incremental source-library learning: frozen-profile tranche
==============================================================

Run from the CeTTa repository root:

  make BUILD=core ENABLE_LIB_PROLOG=0 CETTA_TEST_ISOLATED=1 demo-ic-pln-mvp

Or use a built native runtime directly:

  bash experiments/ic_pln_mvp/run.sh path/to/cetta

This opt-in experiment leaves the installed libraries, C runtime and PLN source
unchanged. It creates a new retained capsule under runtime/ic-pln-mvp/run.*/.
Learning, reconstruction and execution use native CeTTa. The shell driver uses
cmp, sha256sum, patch, AWK, od and xxd; it needs no Python checker.

Result and scope
----------------
On the pinned real lib/petta/lib_pln.metta:

                       original    first feature    second feature
  Stored-source nodes     2207          2186              2180
  Archive bytes          59537         59092             59059
  Retained helpers           0             1                 2
  Uses of new helper         -             4                 2

The first helper is the shared similarity-to-inheritance computation. The
second shares the guarded deduction-strength expression in Truth_Deduction
and simpleDeductionStrength. Their different threshold and unit literals remain
parameters, not an algebraic normalization. Search generates the templates and
occurrence sites: four ranked seed pairs initially, then one on the resulting
source. These small regions were selected during the diagnostic review; this is
not unrestricted discovery, an unbiased search-quality benchmark or held-out
generalization. First-stage statistics count two improving incumbent proposals,
but only one helper is retained at that stage.

Cost(atom)=1 and cost(expression)=1+sum(child costs), including every stored
definition, parameter, call and thunk. Both accepted steps shrink the whole
stored program. Names have no spelling penalty. Archive byte counts do NOT
include the separately retained profile/runtime capsule; they are not a binary
MDL claim about the entire distribution. The second helper saves six nodes:
two 32-node subtrees become two 8-node calls plus one 42-node definition.

The fixed gate checks two features, both original reconstructions, independent
AWK node counts, 33 finite public-call observations, a separate arithmetic fault,
pause/resume at both stages, 17 existing cost controls, the controlled rule-head
suite, four pinned-call checks, malformed archive refusal, and fresh-process
replay using the retained profile. Check result.txt, not just process startup.

Frozen profile, not a production codec extension
-----------------------------------------------
profile.patch is applied ONLY to copied libraries. It separates the primitive
reservation set from the structural call inventory and adds the existing /safe
call consistently to traversal, generation and decoding. profile.metta checks
the exact untyped two-argument definition before each search:

  (= (/safe $A $B) (if (> $B 0.0) (/ $A $B) (empty)))

Both zero and negative denominators yield no result. The launcher pins the
entire source by SHA-256. Each search and execution starts in a fresh process.
No ambient user equations/types or reflective observations are part of the
advertised execution contract. Do not reuse this patch as a general whitelist.

The capsule retains profile/lib, profile/cetta, glue, PROFILE.txt and checksums
alongside the artifacts. Move or preserve the WHOLE capsule. To decode again:

  bash experiments/ic_pln_mvp/replay.sh path/to/capsule
  bash experiments/ic_pln_mvp/check_capsule.sh path/to/capsule

The second command checks refusal of altered private copies of the metadata and
archive, without modifying the retained result. The capsule also contains its
own replay.sh. It verifies the profile and archive
checksums before using the matching decoder in a fresh process. This is a
reproducibility envelope, not a signed security boundary or a versioned profile
inside LC2/LM1. Raw A2.sic alone is not portable to the stock decoder. Binary
replay requires a compatible platform; source rebuild uses this repository.

Known semantic limits, with runnable counterexamples
----------------------------------------------------
  bash experiments/ic_pln_mvp/check_boundaries.sh path/to/cetta

Exit zero means the two LIMITS reproduce, not that arbitrary source rewriting
is sound. In the ambient-type case, (: audit-f (-> Number Atom)) exposes the
original arithmetic expression versus the helper call as returned syntax.
In the reflective case, an untouched (match &self ...) observer sees body
size 4 become 2. Both rewrites reconstruct exactly and shrink structurally.
Therefore exact reconstruction is separate from general execution equivalence.
The general adapter's ambient target-type/name checks remain an integration
issue; source reflection needs an explicit observation contract. Do not install
learned definitions into a shared live application automatically.

Extension points after this tranche
----------------------------------
1. Profile persistence and admission: make primitive reservation, authored call
   arity/definition/demand, environment identity and observation explicit in
   preparation, artifacts, decoding and resumption. Reject/reprepare on changes.
   Extend the ambient target-type and helper-name checks before widening use.
2. Exact plan-before-pack: after selecting disjoint sites, compute
     next = current + definition + sum(actual_call - old_subtree).
   Keep final reconstruction and packed checking authoritative. Materialization
   currently receives the stage original, not the incumbent; do not accidentally
   prune against a stale or guessed incumbent cost. Reuse prepared catalogs.
3. Optional candidate deduplication and broader calls: retain sort/role,
   repeated-hole relationships, readiness and full frozen context in the key.
   Preserve first representatives and proposal order in differential replay.

These are future integration tasks, not conditions for pausing this experiment.
The stock demo-ic-pln target is intentionally unchanged and still documents its
narrower candidate language. Do not relabel its old one-feature gate a success.
ILP composition and proof-trace learning remain separate applications.

Owners and provenance
---------------------
Search: lib_ic_source_seeds, lib_ic_search, lib_ic_templates.
Source admission/rewrite: lib_source_context, lib_source_rule_heads,
lib_ic_context_library. Reconstruction: lib_ic_library_stack and codecs.
Cost: lib_ic_library_cost; final acceptance: lib_ic_packed.
All these are ordinary MeTTa libraries; no corpus names entered C dispatch.

Input SHA-256:
  6b980321bbe9b49e5b12e2fcee0479ab1d5c7550c2104a67b0722a5b473da4ee
Working review archive SHA-256:
  b705188c3807c6f8fdaee8ac32684a9b3981bc0184e7327c88e393c28545ec2c
The /safe profile, observation panel and counterexamples come from the returned
2026-10-30 working review. The capsule launcher, native boundary runner and
replay envelope reproduce that work locally without adopting its production
recommendations as new completion gates. Corresponding technical write-up:
Mettapedia papers/IncrementalCompression.tex (and its built PDF).

No new inference rule, shortest-feature theorem, numerical-suite parity,
whole-program reflection equivalence or execution speedup is claimed.
