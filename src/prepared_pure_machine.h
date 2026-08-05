#ifndef CETTA_PREPARED_PURE_MACHINE_H
#define CETTA_PREPARED_PURE_MACHINE_H

#include "space.h"

/*
 * Revision-pinned machine for the deterministic, effect-free equation
 * fragment used by prepared folds.  The program compiler consumes the
 * generated control/register vocabulary and the space's equation graph;
 * execution uses positional slots and an explicit heap stack.  A program
 * outside the fragment is rejected and remains on the ordinary evaluator.
 */
typedef struct CettaPreparedPureProgram CettaPreparedPureProgram;
typedef Atom *(*CettaPreparedPureBooleanValue)(Arena *arena, bool value);

CettaPreparedPureProgram *cetta_prepared_pure_program_compile(
    Space *space, Atom *expression,
    VarId accumulator_var, VarId item_var,
    CettaPreparedPureBooleanValue boolean_value,
    bool total_structural_equality);

/* Compile a closed, deterministic, effect-free expression into the same
 * explicit-stack machine used by prepared folds.  Callable arguments are
 * retained as machine suspensions and forced according to call_mode. */
CettaPreparedPureProgram *cetta_prepared_pure_program_compile_closed(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    bool total_structural_equality);

bool cetta_prepared_pure_program_is_current(
    const CettaPreparedPureProgram *program);

/* Rebind the opaque runtime arguments of a previously compiled closed Need
 * call.  The head and arity must match the compiled entry descriptor. */
bool cetta_prepared_pure_program_rebind_closed_need_call(
    CettaPreparedPureProgram *program, Atom *expression);

/* Release every invocation-owned argument reference before a reusable Need
 * program is parked.  Compiled code and its revision pin remain intact. */
void cetta_prepared_pure_program_clear_closed_need_call(
    CettaPreparedPureProgram *program);

bool cetta_prepared_pure_program_execute(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item, Atom **result_out);

/* Execute in a caller-owned scratch arena.  A nonzero nursery budget enables
 * copying collection at the machine's generated-root loop boundary; zero
 * preserves the same execution without collection. */
bool cetta_prepared_pure_program_execute_closed(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    Atom **result_out);

void cetta_prepared_pure_program_free(
    CettaPreparedPureProgram *program);

#endif /* CETTA_PREPARED_PURE_MACHINE_H */
