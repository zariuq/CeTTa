Inference-cost axes: synthetic rows that separate the principal per-inference costs.

Runtime unification-attempt counts admitted matcher invocations, including nested
comparisons of bound values and the linear-plan and encoded-term matchers.
Every counted invocation ends in success, repeated-variable failure,
head-failure with no net binding-row growth, or failure after binding-row growth.
The head-fail name is historical; it does not certify a constructor-only test.
An after-bind failure is not an equation-body failure. Candidate attempts and
queries use the query/selection counters separately. Counts from binaries
which omitted linear or encoded matching are not comparable denominators.

A_single_equation
            one-equation prepared counting loop: arithmetic/control floor
A_head_disjoint
            two equations with distinguishable heads: generic equation selection without a
            retained same-head fallback at every recursive level
A_dispatch  two identical heads, one productive recursive equation, and one guarded-empty
            fallback: equation selection plus a retained doomed alternative per level
B_heads     150k ground calls selecting one of three deep axiom-shaped heads: head
            matching cost on top of the A baseline
C_binding   150k calls of a 7-variable deep extraction: binding creation on top of A
D_choice    2^17 trivial-body answers: choice-point/bag machinery in isolation
E_open_body four simultaneously matchable deep heads with one productive lane and
            three ordinary failing lanes: deterministic body scaffolding separated
            by open relation calls, isolating candidate-local control around an
            authored nondeterministic boundary
F_nonlinear_refutation
            32 repeated-variable heads that survive structural indexing but are all
            rejected by exact matching: isolated pre-verification selectivity
H_scalar_tree_segment
            a candidate-local arithmetic/comparison/Boolean tree between an
            open occurrence and recursive suspension: isolates source-derived
            scalar-segment execution from relation-head and bag-width costs
J1_open_nonlinear_head
            open goal against a non-linear open head: repeated $x bound to
            independently built large spines (ladder 512, 2048, 8192). Isolates
            consistency walking of large subterms.
J2_growing_instantiation
            J1's head, with the goal growing by wrapping across recursive
            calls (ladder 512, 2048, 8192). Isolates instantiation growth.
J3_budget_head_fail
            budget-split inner relation with several open heads; most attempts
            fail at the first constructor (ladder 256, 1024, 4096). Isolates
            head-failure volume.
J4_modus_ponens_letstar
            modus-ponens shape: two recursive calls sharing bindings through
            let*. Isolates the mp/let* binding split.
K0_open_dest_interned_mp
            leftover dest `$p` against interned `(: ax T)` and interned
            `(: (mp $f $x) T)` with no recursion. J1–J4 never bind a leftover
            dest to interned `(mp $f $x)`.
K1_open_mp_letstar
            J4's let* with an *open* proof dest `(: $p T)`. Nested mp of
            size 5 exercises the Loowoz open-destination axis: let* observes
            a dest bound to interned `(mp $f $x)`.
K2_open_colon_formula
            open dest against interned colon-tagged `→` type, then mp.
K3_loowoz_open_mp
            Loowoz s19: open dest, three colon-tagged axioms, interned mp,
            occurs-check. A broken matcher returned only the occurs-check
            flag and lost all three proofs; J1–J4 and K1 do not catch it.
G1..G8_guard_admission
            if-guard empty-branch admission: else-empty hit/miss, then-empty
            hit/miss, neither-empty, second-argument budget, `none` is not
            empty, mp sibling of ax, repeated lhs variable, let-bound
            skeleton.  A false prune drops an authored value.
I1..I5_checkpoint_regions
            five independent matcher-write geometries: wide linear slots,
            nonlinear success/refutation, deep constructors, caller/rule
            aliases, and cycle rejection with rollback.  Together they test
            whether observer-delimited checkpoint coalescing transfers beyond
            any one application or head shape while preserving exact fallback.
gen_scalar_tree_family.sh MODE ITERATIONS [LAYERS]
            emits five independently scalable scalar-segment families:
            arithmetic, conjunction, mixed numeric, structural equality, and
            disjunction/numeric equality.  Each keeps the same open-occurrence
            boundary and recursive continuation while changing the local
            operator algebra, so transfer is measured without changing the
            runtime implementation or privileging a benchmark relation name.
            LAYERS grows the local expression past small backend capacities
            while leaving those semantic boundaries unchanged.
gen_support_reachability.sh WIDTH ITERATIONS [ROUNDS]
            generates a two-candidate recursive relation whose open structured
            patterns have exactly WIDTH support variables.  Sweep WIDTH to measure
            direct finite-support cycle admission and its fail-closed wide-source
            boundary independently of any application relation.  ROUNDS repeats a
            bounded-depth query to increase timing resolution without changing its
            maximum live recursive depth.
gen_match_region_hole_family.sh MODE ITERATIONS
            emits five source-independent scalar families with four simultaneously
            matchable occurrences.  It isolates composition of one exact match,
            one deterministic Region, and one preserved open Hole.  The ordinary
            and force-off routes must both emit done followed by three never rows.
gen_binding_region_hole_family.sh MODE ITERATIONS
            emits six source-independent binding-sequence families: linear,
            nested structural, dependent, repeated-variable, anonymous, and wide.
            Every recursive step alternates one or more open producer Holes with
            deterministic binding Regions before the final body Region.  The
            compiled-card and dynamic-validation routes must both emit done.
gen_rule_slot_view_family.sh MODE ITERATIONS
            emits six nonlinear pattern families with different repeated-variable
            geometries, including a high-fanout adversary.  The authoritative
            binding store receives every write; the optimized route may reuse only
            positive occurrence-local slot projections.  Default and force-off
            routes must preserve exact order, multiplicity, and the done/never
            result bag.
gen_source_write_buffer_family.sh MODE ITERATIONS
            emits seven unrelated transaction geometries for one generic
            observer-free source-write realization: shallow/deep/wide/long
            rigid failure, a repeated-variable observer flush, successful
            publication, and a pre-bound negative control.  No mode changes
            runtime admission.  Default and reference routes must both emit
            done; instrumented runs additionally require exact write-buffer
            accounting and a non-vacuous counters-alive sentinel.
bench_match_decision_prefix_observation.c [DEPTH] [ITERATIONS] [MODE] [GEOMETRY] [WIDTH]
            standalone selector adversary: 64 ordered patterns expose 16 independently
            discriminating leaves beneath a configurable shared expression prefix.
            MODE is conjunctive (default) or deep. Conjunctive selection returns
            two distinct occurrences of the same pattern; deep selection returns
            a conservative 32-occurrence superset. Each run checks every source
            reference, including duplicate provenance. This isolates query-
            prefix observation from matching, body, bag, and evaluator-transition
            costs when comparing proposed selector realizations. GEOMETRY is all
            (default), closed, shallow-open, middle-open, deep-open, unready,
            absent, or parts. WIDTH ranges from 6 to 256 (default 16). Measure
            geometries separately: combined timing can conceal an adverse case.
            These wildcard-free patterns also expose the overhead of an
            observation-skipping check when its lower bound cannot skip anything.
Run each on cetta --lang petta and SWI-PeTTa; report wall, RSS, per-iteration cost.
Optimizations are steered here and validated on the OBC/set.mm rows.

Standing binding panel
----------------------
make test-binding-standing-panel runs the PeTTa examples, G/J/K axes, Loowoz s19
twice, translated HE Loowoz s15, HE guard contracts, and nested space mutation.
standing_panel.json retains the output pins and historical performance references.
The runner records the exact binary hash, per-process wall time, and peak RSS.
Pass --check-reference to scripts/check_binding_panel.py at performance close to
enforce the recorded wall/RSS references; instruction counts are measured separately.
PeTTa examples default to the sibling PeTTa checkout; --petta-examples can override it.
