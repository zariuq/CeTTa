#ifndef CETTA_PETTA_PROGRAM_H
#define CETTA_PETTA_PROGRAM_H

#include "atom.h"
#include "gslt_ground_dense_term_v1.h"
#include "nik_direct_authority.h"
#include "match.h"
#include "space.h"
#include "petta_type_policy.h"
#include "petta_semantics.h"
#include "term_universe.h"

/*
 * PeTTa compiles each source expression immediately before that expression
 * is installed or executed.  A plan records whether each source occurrence
 * was compiled as a value, inert data, a statically known call, or a dynamic
 * head dispatch.  Plans are private evaluator metadata: they never enter
 * Atom equality, ordering, printing, matching, or the user-visible Space.
 */
typedef enum {
    PETTA_PLAN_VALUE = 0,
    PETTA_PLAN_DATA,
    PETTA_PLAN_STATIC_CALL,
    PETTA_PLAN_DYNAMIC_CALL,
} PettaPlanRole;

typedef enum {
    PETTA_PLAN_EXEC_GENERIC = 0,
    PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS,
    PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS,
    PETTA_PLAN_EXEC_RELATION_SLOTS,
} PettaPlanExecution;

/* Source-derived continuation boundaries.  These tags refine the existing
 * plan tree across parent/child edges; they do not reclassify either child.
 * The anonymous-hole form records that an open producer must retain every
 * occurrence, while its always-successful anonymous match may be omitted
 * before resuming the authored body. */
typedef enum {
    PETTA_PLAN_CONTINUATION_GENERIC = 0,
    PETTA_PLAN_CONTINUATION_AFTER_ANONYMOUS_HOLE,
} PettaPlanContinuation;

/* Abstract output shape for a source occurrence. Opaque results are connected
 * by runtime goals; constructors and transparent children can constrain an
 * enclosing equation before effects run. */
typedef enum {
    PETTA_PLAN_OUTPUT_OPAQUE = 0,
    PETTA_PLAN_OUTPUT_VALUE,
    PETTA_PLAN_OUTPUT_CONSTRUCTOR,
    PETTA_PLAN_OUTPUT_CHILD,
    PETTA_PLAN_OUTPUT_QUOTED_CHILD,
    PETTA_PLAN_OUTPUT_TRUE,
} PettaPlanOutput;

_Static_assert(PETTA_PLAN_DYNAMIC_CALL <= UINT8_MAX &&
               PETTA_PLAN_EXEC_RELATION_SLOTS <= UINT8_MAX &&
               PETTA_PLAN_CONTINUATION_AFTER_ANONYMOUS_HOLE <= UINT8_MAX &&
               PETTA_PLAN_OUTPUT_TRUE <= UINT8_MAX,
               "plan classification tags must fit their compact fields");

typedef struct PettaPlanNode PettaPlanNode;

/* One qualified physical realization of a deterministic RegionPlan.  It is
 * not a language limit and not the eventual equation-plan ABI.  The compiler
 * emits this compact program only for a validated, call-free scalar region;
 * every open call and unsupported observer remains in the generic machine.
 *
 * Shape nodes pair runtime source occurrences with the already compiled
 * syntax plan.  A TermUniverse-stable authored occurrence may additionally
 * retain its exact source pointer; alpha-equivalent or transient occurrences
 * still use validated runtime shape pairing. */
typedef enum {
    PETTA_REGION_SCALAR_LOAD = 0,
    PETTA_REGION_SCALAR_APPLY,
} PettaRegionScalarInstructionKind;

typedef struct {
    const PettaPlanNode *source_plan;
    Atom *stable_source;
    size_t first_child;
    size_t child_count;
} PettaRegionScalarShapeNode;

typedef struct {
    PettaRegionScalarInstructionKind kind;
    size_t source_node;
    uint8_t argument_count;
} PettaRegionScalarInstruction;

typedef struct PettaDeterministicRegionProgram {
    const PettaPlanNode *root_plan;
    size_t source_node_count;
    size_t instruction_count;
    uint32_t operation_count;
    size_t maximum_stack;
    const PettaRegionScalarShapeNode *source_nodes;
    const PettaRegionScalarInstruction *instructions;
} PettaDeterministicRegionProgram;

/* Source-derived physical realizations of alternating Region/Hole paths.
 * Every card is tied to one exact authored occurrence and immutable plan.
 * A runtime occurrence which does not carry that identity must validate the
 * complete source shape or decline to the canonical machine.
 *
 * Boolean branching contributes one deterministic entry Region followed by
 * one selected Hole.  A binding sequence alternates producer Holes with
 * deterministic pattern-binding Regions before its final body Region.  These
 * are realizations of the common Region/Hole normal form, not new control
 * semantics or a closed inventory of possible realizations. */
typedef enum {
    PETTA_REGION_HOLE_BOOLEAN_BRANCH = 0,
    PETTA_REGION_HOLE_BINDING_SEQUENCE,
} PettaRegionHoleProgramKind;

typedef struct {
    CettaExprIndex source_child;
    const PettaPlanNode *plan;
} PettaRegionHoleBranch;

typedef struct {
    CettaExprIndex binding_index;
    CettaExprIndex pattern_child;
    CettaExprIndex producer_child;
    const PettaPlanNode *binding_plan;
    const PettaPlanNode *pattern_plan;
    const PettaPlanNode *producer_plan;
} PettaRegionHoleBinding;

typedef struct {
    const PettaDeterministicRegionProgram *entry_region;
    CettaExprIndex entry_source_child;
    size_t branch_count;
    const PettaRegionHoleBranch *branches;
} PettaRegionHoleBooleanProgram;

typedef struct {
    CettaExprIndex bindings_source_child;
    CettaExprIndex body_source_child;
    const PettaPlanNode *bindings_plan;
    const PettaPlanNode *body_plan;
    size_t binding_count;
    const PettaRegionHoleBinding *bindings;
} PettaRegionHoleBindingSequenceProgram;

typedef struct PettaRegionHoleProgram {
    const PettaPlanNode *root_plan;
    Atom *stable_source;
    PettaRegionHoleProgramKind kind;
    union {
        PettaRegionHoleBooleanProgram boolean_branch;
        PettaRegionHoleBindingSequenceProgram binding_sequence;
    } as;
} PettaRegionHoleProgram;

/* Runtime translations share plans by content (petta_plan_node_key in
 * petta_program.c), so every field other than `children` is part of that
 * key. */
struct PettaPlanNode {
    uint8_t role;
    uint8_t execution;
    CettaComputationPlan computation;
    /* Authored (Empty body), distinct from a variable pattern bound to Empty.
     * The arm occurrence keeps this fact through translation and substitution. */
    bool literal_case_default;
    uint8_t continuation;
    uint8_t output;
    /* Source positions are assigned before normalization. A zero position
     * is an unannotated adapter; its legacy canonical visit costs one. */
    uint32_t source_position;
    uint32_t source_debit;
    /* A `(let $v (collapse X) B)` of a plan made for a whole equation or
     * directive: $v occurs nowhere else in it (binding_local); and one more
     * than the number of X's own variables, those occurring nowhere else
     * (or in B too, when B spreads the list by X's template itself), when
     * each other variable of X is a parameter of the equation, which
     * occurs in X and the left side alone (value_variables; zero
     * otherwise).  An instance of X holding more variables than its own
     * took one from a call's argument. */
    bool binding_local;
    uint8_t value_variables;
    CettaExprIndex output_child;
    bool contains_cardinality_call;
    bool contains_call;
    /* This subtree contains an operator which transports authored child
     * occurrences across an evaluation stage.  Its positional plan must
     * therefore remain available until those deferred occurrences run. */
    bool contains_deferred_occurrence_transport;
    /* Source-plan evidence that this complete subtree consists of
     * plain scalar leaves and admitted scalar operators.  The operation
     * count is exact whenever the evidence is present; runtime leaf values
     * and dialect authority remain independently checked before execution. */
    bool plain_scalar_tree;
    uint32_t plain_scalar_tree_operations;
    /* Positive program-snapshot evidence that this occurrence's symbol head
     * names a Space relation.  Intrinsics and engine-known names remain
     * callable without acquiring this execution license. */
    bool relation_head_admitted;
    /* Every node of an admitted equation RHS inherits this mark.  Open
     * admission permits a finite LHS/RHS variable inventory and excludes
     * cons constraints; C0 compilation remains independently restricted to
     * RHS variables supplied by the LHS and ground-query execution. */
    bool open_template_admitted;
    /* The occurrence is dispatched at run time: a call whose head is a
     * variable or an expression, or the application `reduce` dispatches.
     * PeTTa runs its body under an implicit handler, under which an error
     * the body raises fails that path alone (DispatchErrorScope).  It is
     * derived from the syntax, so every translation of the occurrence
     * carries the handler, a specialized one included. */
    bool dispatch_handler;
    /* An admitted equation occurrence may refer directly to its finite
     * activation-frame slot.  This is derived program data: source variables
     * and the exact matcher remain semantic authority. */
    bool has_equation_variable_slot;
    uint32_t equation_variable_slot;
    /* Separate immutable residual program compiled from this exact source
     * structure.  NULL means that this backend realization declined. */
    const PettaDeterministicRegionProgram *deterministic_region;
    /* A normalized deterministic-region/observable-hole path compiled from
     * this exact source occurrence.  NULL preserves ordinary evaluation. */
    const PettaRegionHoleProgram *region_hole_program;
    CettaExprLen child_count;
    const PettaPlanNode *children;
};

/* Elaboration owns the executable word. Reading it does not reinterpret
 * source roles or grant callee/lifetime admission. An absent word retains
 * the dialect adapter's existing fresh-elaboration path. */
static inline CettaComputationPlan petta_plan_computation(
        const PettaPlanNode *plan) {
    return plan ? plan->computation : (CettaComputationPlan){0};
}

typedef struct PettaEquationTemplateC0 PettaEquationTemplateC0;
typedef struct PettaEquationTemplate PettaEquationTemplate;

typedef struct {
    Atom *lhs;
    Atom *rhs;
    CettaRuleBindingPolicy binding;
    uint32_t static_variable_count;
    /* An invalid derived fact is unknown and requires exact recomputation. */
    bool lhs_contains_cons_constraint_valid;
    bool lhs_contains_cons_constraint;
} PettaEquationActivationLayout;

typedef struct {
    Atom *equation;
    uint64_t source_root;
    const PettaPlanNode *rhs_plan;
    /* Program-owned, proof-erased derived code for the conservative C0
     * fragment.  The source equation remains semantic authority. */
    const PettaEquationTemplateC0 *equation_template_c0;
    /* Finite variable inventory shared by the LHS matcher and every RHS
     * source view.  This is the per-equation leaf of the revision-pinned
     * Space/query executable, not a semantic replacement for the equation. */
    const PettaEquationTemplate *equation_template;
    /* Revision-pinned source layout used to activate a relational equation
     * without rediscovering its sides or copying its complete syntax. */
    PettaEquationActivationLayout activation_layout;
    /* The right side has a variable the left side lacks: the body binds
     * variables of its own. */
    bool rhs_binds_variables;
    /* Stable only within the captured Space revision.  The executor treats
     * this as evidence provenance, never as a replacement for the equation
     * or its authoritative matcher. */
    SpaceEquationOccurrenceId occurrence;
} PettaEquationCandidate;

typedef enum {
    PETTA_EQUATION_TEMPLATE_C0_NOT_APPLICABLE = 0,
    PETTA_EQUATION_TEMPLATE_C0_MISMATCH,
    PETTA_EQUATION_TEMPLATE_C0_MATCH,
    PETTA_EQUATION_TEMPLATE_C0_CAPACITY,
} PettaEquationTemplateC0Status;

/* Borrow the template's sorted source-variable inventory.  All returned
 * pointers remain owned by the PettaProgram and are valid for the same
 * lifetime as the candidate carrying the template. */
bool petta_equation_template_variable_inventory(
    const PettaEquationTemplate *template,
    const VarId **source_ids_out,
    Atom *const **source_variables_out,
    uint32_t *variable_count_out);

/* Borrow the immutable schema; a binding activation retains its own reference. */
BindingsFrameSchema *petta_equation_template_frame_schema(
    const PettaEquationTemplate *template);

/* Program-owned execution syntax uses dense frame-local variable slots.
 * The candidate's equation separately retains authored occurrence identity. */
Atom *petta_equation_template_syntax(const PettaEquationTemplate *template);

/* Borrow the exact finite LHS occurrence plan compiled with this equation.
 * NULL means the source lay outside the admitted acyclic first-order tree
 * fragment; ordinary matching remains authoritative. */
const CettaOpenPatternPlan *petta_equation_template_lhs_match_plan(
    const PettaEquationTemplate *template);

/* Apply only the conservative C0 equation template: match the LHS and
 * instantiate the RHS. Selection, revisions, occurrence identity,
 * choicepoints, and rollback remain machine-owned. */
PettaEquationTemplateC0Status petta_equation_template_c0_apply(
    const PettaEquationTemplateC0 *template,
    CettaGsltGroundDenseWorkspaceV1 *workspace,
    Atom *closed_query, Arena *arena, Atom **result_out);

/* A retained declaration-ordered candidate generation. Catalog mutation may
 * retire its cache entry but cannot change or release the retained records.
 * Plans and source terms retain their existing program/Space-owner lifetime:
 * those owners must outlive execution and any captured continuation.
 * Host-produced leases may instead own an independent array, or borrow an
 * array until pin() establishes ownership. Release exactly once per lease. */
typedef struct PettaCandidateSnapshotStorage PettaCandidateSnapshotStorage;
typedef struct {
    const PettaEquationCandidate *items;
    size_t len;
    PettaEquationCandidate *owned_items;
    PettaCandidateSnapshotStorage *storage;
} PettaCandidateSnapshotLease;

/* Index selections may complete a plan locally without altering the catalog. */
typedef struct {
    uint32_t index;
    const PettaPlanNode *rhs_plan;
} PettaEquationSelectionEntry;
typedef struct PettaEquationProjection PettaEquationProjection;

/* Transfer a pinned lease and optional index vector into an immutable selected
 * observation. NULL entries denote the contiguous range [first, first+len).
 * On catalog retirement, sparse observations promote only their live records
 * when that retains less storage than sharing the retired generation. */
PettaEquationProjection *petta_program_equation_projection_take(
    PettaCandidateSnapshotLease *catalog, PettaEquationSelectionEntry *entries,
    size_t first, size_t len);
bool petta_program_equation_projection_retain(PettaEquationProjection *projection);
void petta_program_equation_projection_release(PettaEquationProjection *projection);
PettaEquationCandidate petta_program_equation_projection_get(
    const PettaEquationProjection *projection, size_t index);
size_t petta_program_equation_projection_retained_bytes(
    const PettaEquationProjection *projection);

/*
 * Physical work performed while reconciling the declaration-ordered PeTTa
 * catalog with the live Space occurrence stream.  These are diagnostic
 * counters only: the live Space remains semantic authority.
 */
typedef struct {
    uint64_t snapshots;
    uint64_t cache_hits;
    uint64_t live_occurrences_scanned;
    uint64_t declaration_records_examined;
    uint64_t pointer_identity_hits;
    uint64_t structural_equality_checks;
    uint64_t alpha_equality_checks;
    uint64_t candidates_emitted;
} PettaCandidateSnapshotStats;

/* Evaluator-neutral logical-update snapshot using the same occurrence and
 * syntax owners as program-qualified candidate leases. NONE selects all
 * heads. No executable plan is borrowed in this source-only projection. */
bool petta_rule_snapshot_lease(Space *space, SymbolId head, unsigned admitted_forms,
    PettaCandidateSnapshotLease *lease);

typedef struct PettaProgram PettaProgram;
typedef struct PettaDeclarationBlock PettaDeclarationBlock;

/* Intern a normalized computation using already established source roles.
 * Children are plans owned by this program. This neither looks up a
 * declaration nor reclassifies Atom syntax. */
const PettaPlanNode *petta_program_project_computation(
    PettaProgram *program, const PettaPlanNode *source,
    CettaComputationPlan computation, bool dynamic, bool relation_head_admitted,
    const PettaPlanNode *const *children, CettaExprLen child_count);

typedef enum {
    PETTA_RELATION_SAFETY_UNSAFE = 0,
    PETTA_RELATION_SAFETY_STATIC,
    PETTA_RELATION_SAFETY_GUARDED_DYNAMIC,
} PettaRelationSafety;

/* A deliberately small evaluator-neutral relation fragment.  The positive
 * class contains only structural equation heads, inert data constructors,
 * recursive calls within the same revision-pinned Space, and ordinary
 * declaration-order alternatives.  Language-owned intrinsics, dynamic
 * heads, effects, quotation, and specialized list constraints remain
 * unqualified. */
typedef enum {
    CETTA_RELATIONAL_EXECUTION_UNQUALIFIED = 0,
    CETTA_RELATIONAL_EXECUTION_STRUCTURAL_EQUATIONS_V1,
} CettaRelationalExecutionClass;

typedef enum {
    PETTA_RESOLVED_CALL_UNSAFE = 0,
    PETTA_RESOLVED_CALL_MACHINE_LOCAL,
    PETTA_RESOLVED_CALL_RELATION,
} PettaResolvedCallClass;

PettaProgram *petta_program_new(void);
/* Select live callability at creation, before any source is registered.
 * New calls then revalidate source translation when named relations change;
 * an already retained invocation continues with its captured plan. */
bool petta_program_enable_callability_refresh(PettaProgram *program);
/* Declaration admission is fixed for this owned program and retained by its
 * revision views. The ordinary constructor admits only base equations. */
PettaProgram *petta_program_new_with_rules(unsigned admitted_forms);
bool petta_program_admits_equation(const PettaProgram *program, Atom *atom);
void petta_program_free(PettaProgram *program);
/* Signature facts are owned by the existing program and keyed by the live
 * SpaceProgramToken. Data-only changes preserve them; equation, declaration,
 * overlay and lifetime changes invalidate them. This caches declarations,
 * never answers to a type query. The returned array is caller-owned; its
 * fresh signatures and literal modes belong to arena. Full source predicate
 * calls select exact-arity arrows; underapplications retain compatible longer
 * arrows. NULL program performs the same compilation without retaining facts. */
bool petta_program_type_calls(PettaProgram *program, Space *space, Atom *head,
                              CettaExprLen supplied, Arena *arena,
                              PettaTypeCall **calls, uint32_t *count,
                              bool *hold_body);
/* Optional language-owned catalog for analyses.  Ordinary PeTTa leaves this
 * disabled and therefore allocates no annotation or inferred-fact state. */
bool petta_program_enable_analysis(PettaProgram *program);
bool petta_program_analysis_enabled(const PettaProgram *program);
bool petta_program_is_equation(Atom *atom);
/* True exactly when adding atom can change the program-owned equation or
 * declaration catalog.  Ordinary data remains solely a Space occurrence. */
bool petta_program_atom_affects_metadata(Atom *atom);
/* Whether the stored form `id` may affect the program's metadata: a
 * three-part expression headed by a rule form or a type declaration's colon.
 * It reads the id's record without decoding the form, so a loader can store
 * a fact without building it; petta_program_atom_affects_metadata decides
 * the rest. */
bool petta_program_form_id_may_affect_metadata(const TermUniverse *universe,
                                               AtomId id);
/* Observe one already-admitted Space addition.  Data outside the program
 * projection is accepted without copying; equations and declarations are
 * retained in the program-owned catalog. */
bool petta_program_observe_addition(
    PettaProgram *program, Space *space, Arena *storage,
    Atom *atom, const PettaPlanNode *plan);

/* Build a revision-pinned occurrence catalog directly from the live Space.
 * This is the common ingress for evaluators whose document reader does not
 * already maintain PettaProgram incrementally.  A concurrent mutation makes
 * the operation decline and leaves no catalog for that Space. */
bool petta_program_synchronize_space(
    PettaProgram *program, Space *space);

/* Classify one call only after synchronize_space has established a current
 * complete catalog.  UNQUALIFIED is conservative: canonical evaluation keeps
 * authority and no continuation controller is installed. */
CettaRelationalExecutionClass
petta_program_relational_execution_class(
    PettaProgram *program, Space *space, Atom *call);

/* True when PeTTa's relational machine, rather than an ordinary user
 * equation or inert constructor, owns the head's execution semantics. */
bool petta_program_head_is_intrinsic(SymbolId head);

/*
 * PeTTa parses a document before executing its directives.  Registering the
 * heads of its top-level equation forms here makes those names callable from
 * earlier imports without installing any equation before source order reaches
 * it.  Equations constructed later by effects are deliberately absent.
 */
bool petta_program_predeclare_equation(
    PettaProgram *program, Space *space, Atom *atom);

/*
 * SWI-PeTTa's fun/1 for `head` beyond its builtins, in `space` (the
 * program's own): an equation of the space defines the head, or the parse
 * pass of a document loaded into the space declared it and no equation with
 * that head has been added to or removed from the space since.  SWI-PeTTa
 * registers a head as it parses, and retracts the registration once a
 * removal leaves the space no clause of it.
 */
bool petta_program_function_registered(
    PettaProgram *program, Space *space, SymbolId head);

/*
 * True when the parsed document or a live Space occurrence declares a named
 * equation head.  This is the source-order-independent name check used by
 * opt-in library controls such as memoize: the document is parsed before its
 * directives run, but no later equation is installed early merely to prove
 * that its function name exists.
 */
bool petta_program_head_declared(
    const PettaProgram *program, SymbolId head);

/*
 * Derive a source-occurrence plan from the currently live program.  Top-level
 * execution and module initialization call this only when prior source forms
 * have completed, so runtime definitions become visible in source order.
 */
const PettaPlanNode *petta_program_plan_current(
    PettaProgram *program, Atom *atom);

/* Derive semantic occurrence plans in the caller's storage. The caller keeps
 * the source syntax live through compilation; physical source-specific
 * workers are separate from these plans. No plan is retained by the program. */
const PettaPlanNode *petta_program_plan_current_in(
    PettaProgram *program, Arena *plans, Atom *atom);

/*
 * Translate a runtime term at an explicit forcing boundary (eval, a computed
 * hyperpose branch).  The plan records the term's code, a call-free subtree
 * being one VALUE place, and equal plans are one program object, so the
 * program keeps a plan per distinct code shape rather than per event.
 */
/* Exact context read by runtime translation. The source arena only licenses
 * weak pointer reuse until its next reset; it never grants ownership. */
typedef struct {
    const Arena *const *source_arenas;
    size_t source_arena_count;
    const void *host_identity;
    uint64_t foreign_instance;
    uint64_t foreign_revision;
    uint64_t admission_revision;
    uint64_t symbol_table_instance;
    uint64_t profile_identity;
    uint64_t features;
} PettaTranslationEnvironment;

const PettaPlanNode *petta_program_plan_transient(
    PettaProgram *program, Atom *atom,
    const PettaTranslationEnvironment *environment);

/*
 * A declaration block is the maximal run of non-executable top-level forms
 * between two source directives.  Its top-level equation heads are mutually
 * visible, matching PeTTa's forward-reference semantics, while definitions
 * created by a directive become visible only to the following block.
 */
PettaDeclarationBlock *petta_program_declaration_block_new(
    PettaProgram *program, const TermUniverse *universe,
    const AtomId *atoms, int atom_count);
void petta_program_declaration_block_free(
    PettaDeclarationBlock *block);
/* The plan of the block's index-th form: an equation's, and NULL for any
 * other form, which is stored as it is. */
const PettaPlanNode *petta_program_declaration_block_plan_at(
    const PettaDeclarationBlock *block, int index);

/*
 * An equation is compiled against the currently live program plus its own
 * head, permitting direct recursion without making later source definitions
 * visible early.  The live registry changes only when note_add succeeds.
 */
const PettaPlanNode *petta_program_plan_dynamic_add(
    PettaProgram *program, Atom *atom);
/* The plan of an equation the specializer derived, where each of `held`, a
 * value substituted for a parameter, is a value wherever it stands, as the
 * reference's specialized clause holds the selected term. */
const PettaPlanNode *petta_program_plan_derived_add(
    PettaProgram *program, Atom *atom, Atom *const *held, size_t held_len);

/* Prove that one successful match produces exactly one ordinary data answer
 * under PeTTa's translation-time occurrence plan.  Payload spelling is not
 * an answer-classification authority: an `Error` expression in a VALUE or
 * call-free DATA occurrence remains ordinary data. */
bool petta_plan_match_template_is_single_data_answer(
    const PettaPlanNode *template_plan);

/* PLAN's binding_local and value_variables for LET, a
 * `(let $v (collapse X) B)` within ROOT, an equation whose left side is
 * LHS, or a directive (LHS NULL).  When B spreads the list by X's template
 * itself, the template's variables may also occur in B. */
void petta_plan_mark_let_scope(const Atom *root, const Atom *lhs,
                               const Atom *let, PettaPlanNode *plan);

bool petta_program_note_add(
    PettaProgram *program, Space *space, Atom *atom,
    const PettaPlanNode *plan);
void petta_program_note_remove_all(
    PettaProgram *program, Space *space, Atom *atom);
void petta_program_note_remove_one(
    PettaProgram *program, Space *space, Atom *atom);

/*
 * Transaction/import workspaces preserve the compiled occurrence stream.
 * Copy and replace affect only private plans; Space remains semantic
 * authority and candidate_snapshot validates every record against it.
 */
bool petta_program_clone_space(
    PettaProgram *program, const Space *source, Space *destination);
bool petta_program_replace_space(
    PettaProgram *program, Space *destination, const Space *source);
void petta_program_forget_space(
    PettaProgram *program, const Space *space);

/*
 * Return a declaration-ordered snapshot for one call head.  The caller owns
 * the candidate array and frees it with free(3); plans remain program-owned.
 * Unregistered equations are retained with a NULL plan, preserving the live
 * Space as the differential oracle.
 */
bool petta_program_candidate_snapshot(
    PettaProgram *program, Space *space, SymbolId head,
    PettaEquationCandidate **candidates, size_t *candidate_count);

/* Return a candidate lease selected from the live Space. A cache generation is
 * retained when its complete read revision is current; a data-only append
 * can instead return an owned lease with refreshed occurrence provenance.
 * Ordinary callers should continue to use `petta_program_candidate_snapshot`
 * when they need an independent array.
 */
bool petta_program_candidate_snapshot_lease_profiled(
    PettaProgram *program, Space *space, SymbolId head,
    PettaCandidateSnapshotLease *lease,
    PettaCandidateSnapshotStats *stats);
void petta_program_candidate_snapshot_lease_release(
    PettaCandidateSnapshotLease *lease);
/* Pin a host lease before retaining it across a callback or suspension.
 * Existing retained/owned records are not copied. Borrowed host records are
 * copied once. Clone creates an independent release obligation. */
bool petta_program_candidate_snapshot_lease_pin(PettaCandidateSnapshotLease *lease);
bool petta_program_candidate_snapshot_lease_clone(
    const PettaCandidateSnapshotLease *source, PettaCandidateSnapshotLease *out);

/*
 * An immutable, revision-bound selection view of the authored equation
 * program.  Capture acquires an owned lease from the program-space authority:
 * an exact-key repeat reuses the view, while a changed revision detaches the
 * cached reference and builds a replacement.  Existing leases remain valid
 * until released with `petta_program_revision_view_free`.
 *
 * Source-owned plans and templates are explicitly erased: alpha-equivalence
 * supplies no representation map for compiled payloads.  Binding to the
 * source Space at the captured revision is the identity presentation;
 * binding to another Space checks the complete ordered equation family.
 * Both erased payloads and stale projections use the generic target-equation
 * realization; neither may refuse evaluation.
 */
typedef struct PettaProgramRevisionView PettaProgramRevisionView;

typedef struct {
    const PettaProgramRevisionView *view;
    SpaceEquationToken target;
} PettaProgramRevisionProjection;

PettaProgramRevisionView *petta_program_revision_view_capture(
    PettaProgram *program, Space *source);
void petta_program_revision_view_free(
    PettaProgramRevisionView *view);
bool petta_program_revision_view_bind(
    const PettaProgramRevisionView *view, Space *target,
    PettaProgramRevisionProjection *projection);
bool petta_program_revision_projection_current(
    const PettaProgramRevisionProjection *projection,
    const Space *target);
bool petta_program_revision_view_equation_lease(
    const PettaProgramRevisionProjection *projection,
    Space *space, SymbolId head,
    PettaCandidateSnapshotLease *lease,
    PettaCandidateSnapshotStats *stats);

/* Borrow translation payloads only when a captured projection still names
 * the identical source Space.  An alpha-equivalent target preserves equation
 * selection but supplies no representation map for source-owned plans. */
bool petta_program_revision_view_source_candidate_lease(
    const PettaProgramRevisionProjection *projection,
    Space *space, SymbolId head,
    PettaCandidateSnapshotLease *lease,
    PettaCandidateSnapshotStats *stats);

/* Return the declaration-ordered live equation catalog for one Space.  The
 * caller owns only the pointer array; equation atoms remain Space-owned. */
bool petta_program_equation_snapshot(
    PettaProgram *program, Space *space,
    Atom ***equations, size_t *equation_count);

/*
 * Inferred function signatures are checker-private derived facts.  They are
 * never inserted into the user-visible Space (and therefore never affect
 * get-type or matching), but they survive declaration blocks so later source
 * directives can use the same inferred type.  Every lookup is pinned to the
 * exact Space revision; a successful checked source transition rebases the
 * derived facts after publishing its atoms.
 */
bool petta_program_inferred_signatures_current(
    PettaProgram *program, Space *space);
bool petta_program_inferred_signatures_current_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority);
bool petta_program_inferred_signature_lookup(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom **signature_out);
bool petta_program_inferred_signature_lookup_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom **signature_out);
bool petta_program_inferred_signatures_lookup(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom ***signatures_out, size_t *count_out);
bool petta_program_inferred_signatures_lookup_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom ***signatures_out, size_t *count_out);
void petta_program_inferred_signatures_reset(
    PettaProgram *program, Space *space);
void petta_program_inferred_signatures_reset_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority);
bool petta_program_inferred_signature_put(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity, Atom *signature);
bool petta_program_inferred_signature_put_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity, Atom *signature);
void petta_program_inferred_signature_remove_head(
    PettaProgram *program, Space *space, SymbolId head);
void petta_program_inferred_signatures_rebase(
    PettaProgram *program, Space *space);
void petta_program_inferred_signatures_rebase_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority);

/* Explicit type declarations remain visible to the checker across PeTTa
 * module boundaries even when the runtime module interface exports equations
 * without copying transitive annotation atoms into the caller's Space.
 * Returned types are fresh copies owned by `arena`; the caller owns only the
 * pointer array. */
/* Alias, Newtype, Foreign and SpaceOf declarations compete for one
 * source-ordered meaning. Ordinary value and arrow declarations do not. */
bool petta_program_type_is_exclusive_kind(const Atom *type);

uint32_t petta_program_declared_types(
    PettaProgram *program, Atom *subject,
    Arena *arena, Atom ***types_out);

/* Whole-catalog view of every live type annotation, in space and
 * declaration order.  Subject-local lookup answers ordinary call typing;
 * this exists for the judgments that genuinely quantify over all
 * declarations, such as constructor exhaustiveness.  The caller owns only
 * the pointer array; the annotation atoms remain program-owned. */
bool petta_program_type_annotation_snapshot(
    PettaProgram *program, Atom ***annotations_out, size_t *count_out);

bool petta_program_candidate_snapshot_profiled(
    PettaProgram *program, Space *space, SymbolId head,
    PettaEquationCandidate **candidates, size_t *candidate_count,
    PettaCandidateSnapshotStats *stats);

/*
 * Prove that every currently reachable equation body for a named relation is
 * free of effects and dynamically selected calls.  The proof is
 * conservative: false means "run through ordinary relational search", not
 * that the relation is invalid.  Results are cached against the exact Space
 * instance and revision, so dynamic definition changes cannot make a stale
 * admission authoritative.
 */
bool petta_program_relation_table_safe(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity);

/* A guarded relation has no statically known unsafe operation, but contains
 * at least one dynamically headed application.  Such a relation may run
 * only behind a transactional caller which checks every resolved executable
 * call before executing it and can discard the whole attempt on refusal.
 * Checking every call is intentional: specialization may turn a dynamic
 * source head into a static-looking residual call without granting it new
 * authority. */
PettaRelationSafety petta_program_relation_safety(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity);
PettaResolvedCallClass petta_program_classify_resolved_call(
    PettaProgram *program, Space *space, Atom *call);

static inline const PettaPlanNode *petta_plan_child(
    const PettaPlanNode *plan, CettaExprIndex index) {
    return plan && index < plan->child_count
        ? &plan->children[index] : NULL;
}

/* Link one projection's prepared-call meaning to its authored argument.
 * The interpreter still installs dispatch/type/effect protocols separately.
 * A VALUE occurrence can contain an unbound variable. */
typedef struct {
    CettaSequenceProjectionCall call;
    const PettaPlanNode *argument;
    bool source_arguments_are_values;
} PettaSequenceCallInterpretation;

static inline PettaSequenceCallInterpretation petta_plan_sequence_call(
    const PettaPlanNode *plan, SymbolId head, CettaExprLen nargs,
    CettaSequenceProjectionPolicy policy) {
    const PettaPlanNode *argument = nargs == 1u ? petta_plan_child(plan, 1u) : NULL;
    CettaSequenceProjectionCall call = cetta_sequence_projection_call(head, nargs, policy);
    return (PettaSequenceCallInterpretation){
        .call = call,
        .argument = argument,
        .source_arguments_are_values = call.projection != CETTA_SEQUENCE_PROJECTION_NONE &&
            plan && plan->role == PETTA_PLAN_STATIC_CALL && argument &&
            (argument->role == PETTA_PLAN_VALUE ||
             (argument->role == PETTA_PLAN_DATA && !argument->contains_call)),
    };
}

#endif /* CETTA_PETTA_PROGRAM_H */
