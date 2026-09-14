#ifndef CETTA_PREPARED_PURE_MACHINE_H
#define CETTA_PREPARED_PURE_MACHINE_H

#include "match_decision.h"
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
typedef Atom *(*CettaPreparedPureConstructValue)(
    Arena *arena, Atom **elements, CettaExprLen length);
typedef bool (*CettaPreparedPureOpaqueValue)(const Atom *value);
typedef bool (*CettaPreparedPureInterruptPollFn)(void *context);
/* Map a dialect-owned syntax head to a semantic register instruction.
 * The generated common-head table is consulted first; this hook exists for
 * spellings whose meaning differs between compositions. */
typedef bool (*CettaPreparedPureRegisterViewFn)(
    SymbolId head, CettaExprLen arity,
    CettaGsltRegisterResultKind *result_kind,
    CettaGsltRegisterInstruction *instruction);
typedef enum {
    CETTA_PREPARED_PURE_OBSERVE_IS_EXPRESSION = 0,
} CettaPreparedPureValueObservation;
typedef enum {
    CETTA_PREPARED_PURE_EXPRESSION_DEFAULT = 0,
    CETTA_PREPARED_PURE_EXPRESSION_PROJECT,
    CETTA_PREPARED_PURE_EXPRESSION_OBSERVE,
    CETTA_PREPARED_PURE_EXPRESSION_DECLINE,
    CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY,
    CETTA_PREPARED_PURE_EXPRESSION_ZERO,
} CettaPreparedPureExpressionViewState;
typedef struct {
    Atom *projected;
    CettaPreparedPureValueObservation observation;
} CettaPreparedPureExpressionView;
/* Classify dialect-owned expression forms before the generic constructor
 * fallback.  A projection names the source child which is the form's value;
 * an observation names a pure outer-value discriminator whose operand is
 * data rather than an evaluation position.  Decline prevents an unhandled
 * form from falling through as inert syntax after shared prepared
 * instructions have had their chance.  Canonical-only rejects the form at
 * both compilation and dynamic execution boundaries.  Zero identifies a
 * dialect-owned no-result form; it is usable only as an explicitly guarded
 * clause branch and never becomes an ordinary prepared value. */
typedef CettaPreparedPureExpressionViewState
(*CettaPreparedPureExpressionViewFn)(
    const Atom *expression, CettaPreparedPureExpressionView *view);
typedef enum {
    CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE = 0,
    CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH,
    CETTA_PREPARED_PURE_PATTERN_VIEW_DECOMPOSE,
} CettaPreparedPurePatternViewState;
typedef struct {
    Atom *const *pattern_children;
    Atom *const *value_children;
    CettaExprLen child_count;
} CettaPreparedPurePatternView;
typedef CettaPreparedPurePatternViewState
(*CettaPreparedPurePatternViewFn)(
    const Atom *pattern, const Atom *value,
    CettaPreparedPurePatternView *view);

CettaPreparedPureProgram *cetta_prepared_pure_program_compile(
    Space *space, Atom *expression,
    VarId accumulator_var, VarId item_var,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics);

/* Compile a closed, deterministic, effect-free expression into the same
 * explicit-stack machine used by prepared folds.  Callable arguments are
 * retained as machine suspensions and forced according to call_mode. */
CettaPreparedPureProgram *cetta_prepared_pure_program_compile_closed(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics);

bool cetta_prepared_pure_program_is_current(
    const CettaPreparedPureProgram *program);

/* Rebind the runtime arguments of a previously compiled closed entry call.
 * The head and arity must match the compiled entry descriptor. */
bool cetta_prepared_pure_program_rebind_closed_entry_call(
    CettaPreparedPureProgram *program, Atom *expression);

/* Release every invocation-owned argument reference before a reusable entry
 * program is parked.  Compiled code and its revision pin remain intact. */
void cetta_prepared_pure_program_clear_closed_entry_call(
    CettaPreparedPureProgram *program);

bool cetta_prepared_pure_program_execute(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item, Atom **result_out);

bool cetta_prepared_pure_program_execute_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out);

/* Execute using a caller-owned scratch arena.  A nonzero nursery budget
 * enables copying collection at the machine's generated-root loop boundary;
 * zero preserves the same execution without collection.  A collected result
 * can reside in the program's invocation survivor arena, so publish it only
 * after copying or materializing it; see release_closed_execution below. */
bool cetta_prepared_pure_program_execute_closed(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    Atom **result_out);

bool cetta_prepared_pure_program_execute_closed_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out);

/* Resume a closed expression through the revision-pinned code and semantic
 * views of an existing Need program.  This is the producer-side dual of the
 * prepared fold API: callers may retain a small continuation expression and
 * request only its next weak-head value, without rebuilding the original
 * entry result.  The expression remains caller-owned.  The result is borrowed
 * from either the scratch or invocation survivor arena and must be consumed
 * before the next execution, explicit release, or program destruction. */
bool cetta_prepared_pure_program_execute_closed_expression_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *expression, size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out);

/* True when the program's dialect-aware callable classifier finds no
 * suspended computation anywhere in value.  Producer consumers use this to
 * preserve eager element evaluation while allowing the collection tail to
 * remain suspended. */
bool cetta_prepared_pure_program_value_is_fully_evaluated(
    CettaPreparedPureProgram *program, Atom *value);

/* A collected closed execution can return a result owned by the program's
 * invocation survivor arena.  The result remains valid until the next
 * execution, program destruction, or this explicit release.  A caller which
 * publishes the result into another arena must copy or materialize it first. */
void cetta_prepared_pure_program_release_closed_execution(
    CettaPreparedPureProgram *program);

void cetta_prepared_pure_program_free(
    CettaPreparedPureProgram *program);

#endif /* CETTA_PREPARED_PURE_MACHINE_H */
