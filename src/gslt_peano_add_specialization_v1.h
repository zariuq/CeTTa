#ifndef CETTA_GSLT_PEANO_ADD_SPECIALIZATION_V1_H
#define CETTA_GSLT_PEANO_ADD_SPECIALIZATION_V1_H

#include "atom.h"

#include <stdbool.h>
#include <stdint.h>

/* A vocabulary-neutral view of one finite-Horn rule after compilation. */
typedef struct {
    const Atom *head;
    Atom *const *body;
    uint32_t body_len;
    uint32_t step_index;
} CettaGsltHornRuleViewV1;

/*
 * An admitted two-rule fold:
 *
 *   relation(zero, right, right).
 *   relation(succ(left), right, succ(result)) :-
 *       relation(left, right, result).
 *
 * The symbols and source-step indices are observations of the compiled
 * finite-Horn program.  The runtime does not assign a meaning to their names.
 */
typedef struct {
    SymbolId relation;
    SymbolId zero;
    SymbolId successor;
    uint32_t zero_step_index;
    uint32_t successor_step_index;
    bool admitted;
} CettaGsltPeanoAddPlanV1;

typedef enum {
    CETTA_GSLT_PEANO_ADD_OK_V1 = 0,
    CETTA_GSLT_PEANO_ADD_DEFER_V1 = 1,
    CETTA_GSLT_PEANO_ADD_RESOURCE_V1 = 2,
    CETTA_GSLT_PEANO_ADD_INVALID_V1 = 3,
} CettaGsltPeanoAddResultV1;

void cetta_gslt_peano_add_plan_init_v1(CettaGsltPeanoAddPlanV1 *plan);

/* Admit exactly the two recursive rules above, in either source order. */
bool cetta_gslt_peano_add_plan_recognize_v1(
    const CettaGsltHornRuleViewV1 *first,
    const CettaGsltHornRuleViewV1 *second,
    CettaGsltPeanoAddPlanV1 *plan_out);

/*
 * Recover the source machine's exact successful-search cost.  The original
 * two-rule interpreter tries clauses in canonical step order, so the losing
 * base/recursive alternative remains part of the observable rule-attempt
 * budget even when the compiled fold does not execute it physically.
 */
void cetta_gslt_peano_add_source_cost_v1(
    const CettaGsltPeanoAddPlanV1 *plan,
    uint32_t successor_count,
    uint64_t *rule_attempts_out,
    uint64_t *rule_matches_out);

/* Largest numeral admitted by an exact remaining source-attempt budget. */
bool cetta_gslt_peano_add_successor_budget_v1(
    const CettaGsltPeanoAddPlanV1 *plan,
    uint64_t available_rule_attempts,
    uint32_t *maximum_successors_out);

/*
 * Evaluate the admitted fold when left is a closed zero/successor numeral and
 * right is ground.  Other inputs defer to the ordinary rule machine.  The
 * successor limit makes resource refusal explicit and cannot become a false
 * negative because DEFER preserves the original execution path.
 */
CettaGsltPeanoAddResultV1 cetta_gslt_peano_add_evaluate_v1(
    Arena *arena,
    const CettaGsltPeanoAddPlanV1 *plan,
    const Atom *left,
    Atom *right,
    uint32_t maximum_successors,
    uint32_t *successor_count_out,
    Atom **result_out);

#endif
