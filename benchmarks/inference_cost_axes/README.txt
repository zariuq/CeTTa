Inference-cost axes: synthetic rows that separate the principal per-inference costs.
A_single_clause
            one-clause prepared counting loop: arithmetic/control floor
A_head_disjoint
            two clauses with distinguishable heads: generic clause selection without a
            retained same-head fallback at every recursive level
A_dispatch  two identical heads, one productive recursive clause, and one guarded-empty
            fallback: clause selection plus a retained doomed alternative per level
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
bench_match_decision_prefix_observation.c [DEPTH] [ITERATIONS]
            standalone selector adversary: 64 ordered patterns expose 16 independently
            discriminating leaves beneath a configurable shared expression prefix.
            Conjunctive
            selection must return one exact source occurrence.  This isolates query-
            prefix observation from matching, body, bag, and evaluator-transition
            costs when comparing proposed selector realizations.
Run each on cetta --lang petta and SWI-PeTTa; report wall, RSS, per-iteration cost.
Optimizations are steered here and validated on the OBC/set.mm rows.
