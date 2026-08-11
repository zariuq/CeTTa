#ifndef CETTA_PETTA_PROGRAM_H
#define CETTA_PETTA_PROGRAM_H

#include "atom.h"
#include "space.h"
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

typedef struct PettaPlanNode {
    PettaPlanRole role;
    PettaPlanExecution execution;
    bool contains_length_call;
    bool contains_call;
    CettaExprLen child_count;
    const struct PettaPlanNode *children;
} PettaPlanNode;

typedef struct {
    Atom *equation;
    const PettaPlanNode *rhs_plan;
    /* Stable only within the captured Space revision.  The executor treats
     * this as evidence provenance, never as a replacement for the equation
     * or its authoritative matcher. */
    SpaceEquationOccurrenceId occurrence;
} PettaClauseCandidate;

/*
 * A revision-pinned view of one head's declaration-ordered candidates.
 * `items` may borrow a PettaProgram cache; `owned_items` is non-NULL exactly
 * when the lease owns the same array.  A borrowed view is valid only until
 * the program or its Space catalog is mutated.  Executors must materialize
 * every candidate they retain before running a clause, because the clause may
 * itself change the Space and invalidate the cache.
 */
typedef struct {
    const PettaClauseCandidate *items;
    size_t len;
    PettaClauseCandidate *owned_items;
} PettaClauseSnapshotLease;

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
} PettaClauseSnapshotStats;

typedef struct PettaProgram PettaProgram;
typedef struct PettaDeclarationBlock PettaDeclarationBlock;

typedef enum {
    PETTA_RELATION_SAFETY_UNSAFE = 0,
    PETTA_RELATION_SAFETY_STATIC,
    PETTA_RELATION_SAFETY_GUARDED_DYNAMIC,
} PettaRelationSafety;

typedef enum {
    PETTA_RESOLVED_CALL_UNSAFE = 0,
    PETTA_RESOLVED_CALL_MACHINE_LOCAL,
    PETTA_RESOLVED_CALL_RELATION,
} PettaResolvedCallClass;

PettaProgram *petta_program_new(void);
void petta_program_free(PettaProgram *program);
/* Optional language-owned catalog for analyses.  Ordinary PeTTa leaves this
 * disabled and therefore allocates no annotation or inferred-fact state. */
bool petta_program_enable_analysis(PettaProgram *program);
bool petta_program_analysis_enabled(const PettaProgram *program);
bool petta_program_is_equation(Atom *atom);

/* True when PeTTa's relational machine, rather than an ordinary user
 * equation or inert constructor, owns the head's execution semantics. */
bool petta_program_head_is_intrinsic(SymbolId head);

/*
 * PeTTa parses a document before executing its directives.  Registering the
 * heads of its top-level equation forms here makes those names callable from
 * earlier imports without installing any clause before source order reaches
 * it.  Equations constructed later by effects are deliberately absent.
 */
bool petta_program_predeclare_equation(
    PettaProgram *program, Atom *atom);

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
const PettaPlanNode *petta_program_declaration_block_plan_at(
    const PettaDeclarationBlock *block, int index);

/*
 * An equation is compiled against the currently live program plus its own
 * head, permitting direct recursion without making later source definitions
 * visible early.  The live registry changes only when note_add succeeds.
 */
const PettaPlanNode *petta_program_plan_dynamic_add(
    PettaProgram *program, Atom *atom);

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
bool petta_program_clause_snapshot(
    PettaProgram *program, Space *space, SymbolId head,
    PettaClauseCandidate **candidates, size_t *candidate_count);

/* Borrow a revision-pinned candidate view when the private catalog can own
 * it, otherwise return an owned lease.  This is the zero-copy selection
 * boundary used by the relational machine; ordinary callers should continue
 * to use `petta_program_clause_snapshot` when they need an independent array.
 */
bool petta_program_clause_snapshot_lease_profiled(
    PettaProgram *program, Space *space, SymbolId head,
    PettaClauseSnapshotLease *lease,
    PettaClauseSnapshotStats *stats);
void petta_program_clause_snapshot_lease_release(
    PettaClauseSnapshotLease *lease);

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
bool petta_program_inferred_signature_lookup(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom **signature_out);
bool petta_program_inferred_signatures_lookup(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom ***signatures_out, size_t *count_out);
void petta_program_inferred_signatures_reset(
    PettaProgram *program, Space *space);
bool petta_program_inferred_signature_put(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity, Atom *signature);
void petta_program_inferred_signature_remove_head(
    PettaProgram *program, Space *space, SymbolId head);
void petta_program_inferred_signatures_rebase(
    PettaProgram *program, Space *space);

/* Explicit type declarations remain visible to the checker across PeTTa
 * module boundaries even when the runtime import surface exports equations
 * without copying transitive annotation atoms into the caller's Space.
 * Returned types are fresh copies owned by `arena`; the caller owns only the
 * pointer array. */
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

bool petta_program_clause_snapshot_profiled(
    PettaProgram *program, Space *space, SymbolId head,
    PettaClauseCandidate **candidates, size_t *candidate_count,
    PettaClauseSnapshotStats *stats);

/*
 * Prove that every currently reachable clause body for a named relation is
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

#endif /* CETTA_PETTA_PROGRAM_H */
