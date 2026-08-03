#ifndef CETTA_PETTA_SPECIALIZER_H
#define CETTA_PETTA_SPECIALIZER_H

#include "atom.h"
#include "petta_program.h"
#include "space.h"

typedef enum {
    PETTA_SPECIALIZE_UNCHANGED = 0,
    PETTA_SPECIALIZE_REWRITTEN,
    PETTA_SPECIALIZE_INVALIDATED,
    PETTA_SPECIALIZE_CAPACITY,
} PettaSpecializeResult;

typedef struct PettaSpecializerPatternNode
    PettaSpecializerPatternNode;

/*
 * Derive the specialization selected by a concrete higher-order call before
 * a search machine pins any space cursor.  Successful specializations are
 * represented by ordinary equations and type declarations in `space`, while
 * `*out_call` names the derived head.  An unchanged result leaves `call`
 * authoritative.
 */
PettaSpecializeResult petta_specializer_prepare_call(
    Space *space, PettaProgram *program,
    Arena *persistent_arena, Arena *result_arena,
    Atom *call, Atom **out_call);

/*
 * Equation and function-type mutations invalidate every specialization
 * derived from the mutated source head, recursively.  Other atom mutations
 * cannot change specialization meaning.
 */
void petta_specializer_note_mutation(Space *space, Atom *atom);

/*
 * A generated equation keeps a parallel, non-semantic pattern map recording
 * application nodes introduced by substituting higher-order parameters.
 * Those nodes match structurally; authored callable applications in the same
 * head remain relational.  The map is derived metadata and disappears with
 * its specialization record.
 */
const PettaSpecializerPatternNode *
petta_specializer_pattern_root(Space *space, Atom *equation);
const PettaPlanNode *
petta_specializer_equation_plan(Space *space, Atom *equation);
bool petta_specializer_pattern_is_structural(
    const PettaSpecializerPatternNode *node);
const PettaSpecializerPatternNode *
petta_specializer_pattern_child(
    const PettaSpecializerPatternNode *node,
    CettaExprIndex index);

/* Release thread-local derivation metadata at evaluator-session teardown. */
void petta_specializer_reset_thread(void);

#endif /* CETTA_PETTA_SPECIALIZER_H */
