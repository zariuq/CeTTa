#ifndef CETTA_ANSWER_BANK_H
#define CETTA_ANSWER_BANK_H

#include "match.h"
#include "variant_instance.h"

typedef uint32_t AnswerRef;

#define CETTA_ANSWER_REF_NONE ((AnswerRef)UINT32_MAX)

typedef struct {
    Atom *result;
    Bindings bindings;
    VariantInstance variant;
} AnswerRecord;

typedef struct {
    Arena arena;
    AnswerRecord *items;
    uint32_t len;
    uint32_t cap;
} AnswerBank;

void answer_bank_init(AnswerBank *bank);
void answer_bank_free(AnswerBank *bank);
bool answer_bank_add(AnswerBank *bank, Atom *result,
                     const Bindings *bindings,
                     const VariantInstance *variant,
                     AnswerRef *out_ref);
const AnswerRecord *answer_bank_get(const AnswerBank *bank, AnswerRef ref);

#endif /* CETTA_ANSWER_BANK_H */
