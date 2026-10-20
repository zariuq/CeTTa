#ifndef CETTA_EXACT_INTEGER_OPERATION_V1_H
#define CETTA_EXACT_INTEGER_OPERATION_V1_H

/*
 * The operation identities of the exact-integer theory.  Quotient and
 * remainder conventions are identities of their own, never read off a
 * dialect's spelling: `%` names TREM in HE and FREM in PeTTa.
 */
typedef enum {
    CETTA_EXACT_INTEGER_V1_ADD = 0,
    CETTA_EXACT_INTEGER_V1_SUB,
    CETTA_EXACT_INTEGER_V1_MUL,
    CETTA_EXACT_INTEGER_V1_TQUOT,
    CETTA_EXACT_INTEGER_V1_FQUOT,
    CETTA_EXACT_INTEGER_V1_TREM,
    CETTA_EXACT_INTEGER_V1_FREM,
    CETTA_EXACT_INTEGER_V1_OPERATION_COUNT
} CettaExactIntegerV1Operation;

#endif /* CETTA_EXACT_INTEGER_OPERATION_V1_H */
