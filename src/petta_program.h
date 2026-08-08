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

typedef struct PettaPlanNode {
    PettaPlanRole role;
    bool contains_length_call;
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
 * Physical work performed while reconciling the declaration-ordered PeTTa
 * catalog with the live Space occurrence stream.  These are diagnostic
 * counters only: the live Space remains semantic authority.
 */
typedef struct {
    uint64_t snapshots;
    uint64_t cache_hits;
    uint64_t live_occurrences_scanned;
    uint64_t declaration_records_examined;
    uint64_t structural_equality_checks;
    uint64_t alpha_equality_checks;
    uint64_t candidates_emitted;
} PettaClauseSnapshotStats;

typedef struct PettaProgram PettaProgram;
typedef struct PettaDeclarationBlock PettaDeclarationBlock;

PettaProgram *petta_program_new(void);
void petta_program_free(PettaProgram *program);
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

static inline const PettaPlanNode *petta_plan_child(
    const PettaPlanNode *plan, CettaExprIndex index) {
    return plan && index < plan->child_count
        ? &plan->children[index] : NULL;
}

#endif /* CETTA_PETTA_PROGRAM_H */
