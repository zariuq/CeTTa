#ifndef CETTA_PRIME_LAMBDA_PI_H
#define CETTA_PRIME_LAMBDA_PI_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"

typedef enum {
    CETTA_PRIME_LP_NOT_SCOPED = 0,
    CETTA_PRIME_LP_ESTABLISHED,
    CETTA_PRIME_LP_REFUTED,
    CETTA_PRIME_LP_INCOMPLETE
} CettaPrimeLambdaPiStatus;

typedef struct {
    bool limited;
    uint64_t remaining;
    uint64_t spent;
} CettaPrimeLambdaPiBudget;

typedef struct {
    CettaPrimeLambdaPiStatus status;
    Atom *type;
    const char *reason;
} CettaPrimeLambdaPiResult;

void cetta_prime_lambda_pi_budget_init(
    CettaPrimeLambdaPiBudget *budget, bool limited, uint64_t steps);

bool cetta_prime_lambda_pi_unwrap_scoped(
    Atom *scoped, Atom **context_out, Atom **term_out);

CettaPrimeLambdaPiResult cetta_prime_lambda_pi_synth(
    Arena *arena, Atom *scoped, CettaPrimeLambdaPiBudget *budget);

CettaPrimeLambdaPiResult cetta_prime_lambda_pi_check(
    Arena *arena, Atom *scoped, Atom *expected,
    CettaPrimeLambdaPiBudget *budget);

CettaPrimeLambdaPiResult cetta_prime_lambda_pi_convert(
    Arena *arena, Atom *left_scoped, Atom *right_scoped,
    CettaPrimeLambdaPiBudget *budget);

#endif /* CETTA_PRIME_LAMBDA_PI_H */
