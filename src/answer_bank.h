#ifndef CETTA_ANSWER_BANK_H
#define CETTA_ANSWER_BANK_H

#include "match.h"
#include "variant_instance.h"

typedef uint64_t AnswerRef;

#define CETTA_ANSWER_REF_NONE ((AnswerRef)UINT64_MAX)
#define CETTA_ANSWER_BANK_MAX_RECORDS ((CettaCount)CETTA_ANSWER_REF_NONE)

typedef struct {
    Atom *result;
    Bindings bindings;
    VariantInstance variant;
} AnswerRecord;

typedef struct {
    Arena arena;
    /* Optional syntax collection, borrowed until this bank is freed. */
    Arena *external_arena;
    AnswerRecord *items;
    CettaCount len;
    CettaCount cap;
} AnswerBank;

void answer_bank_init(AnswerBank *bank);
void answer_bank_init_in(AnswerBank *bank, Arena *syntax_owner);
void answer_bank_free(AnswerBank *bank);
bool answer_bank_add(AnswerBank *bank, Atom *result,
                     const Bindings *bindings,
                     const VariantInstance *variant,
                     AnswerRef *out_ref);
/* One copy session for all roots, bindings and variants; repeated records
 * remain distinct occurrences even when their syntax is shared. */
bool answer_bank_append(AnswerBank *bank, const AnswerBank *source);
/* Append every source record in the given order, using one copy session. */
bool answer_bank_append_order(AnswerBank *bank, const AnswerBank *source,
    const CettaIndex *order);
const AnswerRecord *answer_bank_get(const AnswerBank *bank, AnswerRef ref);

#endif /* CETTA_ANSWER_BANK_H */
