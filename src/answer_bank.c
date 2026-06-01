#include "answer_bank.h"
#include <stdlib.h>

static void answer_record_init(AnswerRecord *record) {
    if (!record)
        return;
    record->result = NULL;
    bindings_init(&record->bindings);
    variant_instance_init(&record->variant);
}

static void answer_record_free(AnswerRecord *record) {
    if (!record)
        return;
    bindings_free(&record->bindings);
    variant_instance_free(&record->variant);
    record->result = NULL;
}

static CettaCount answer_bank_capacity_limit(void) {
    size_t limit = SIZE_MAX / sizeof(AnswerRecord);
    if ((uint64_t)limit > CETTA_ANSWER_BANK_MAX_RECORDS)
        return CETTA_ANSWER_BANK_MAX_RECORDS;
    return (CettaCount)limit;
}

static bool answer_bank_reserve(AnswerBank *bank, CettaCount needed) {
    CettaCount next_cap;
    CettaCount limit;
    if (!bank)
        return false;
    limit = answer_bank_capacity_limit();
    if (needed > limit)
        return false;
    if (needed <= bank->cap)
        return true;
    next_cap = bank->cap ? bank->cap * 2 : 16;
    while (next_cap < needed)
        next_cap *= 2;
    if (next_cap > limit)
        next_cap = limit;
    if (next_cap < needed)
        return false;
    bank->items = bank->items
        ? cetta_realloc(bank->items, sizeof(AnswerRecord) * (size_t)next_cap)
        : cetta_malloc(sizeof(AnswerRecord) * (size_t)next_cap);
    bank->cap = next_cap;
    return true;
}

static bool answer_bank_promote_bindings(Arena *dst, Bindings *bindings) {
    if (!dst || !bindings)
        return true;
    for (uint32_t i = 0; i < bindings->len; i++) {
        Atom *promoted = atom_deep_copy(dst, bindings->entries[i].val);
        if (bindings->entries[i].val && !promoted)
            return false;
        bindings->entries[i].val = promoted;
    }
    for (uint32_t i = 0; i < bindings->eq_len; i++) {
        Atom *lhs = atom_deep_copy(dst, bindings->constraints[i].lhs);
        Atom *rhs = atom_deep_copy(dst, bindings->constraints[i].rhs);
        if ((bindings->constraints[i].lhs && !lhs) ||
            (bindings->constraints[i].rhs && !rhs)) {
            return false;
        }
        bindings->constraints[i].lhs = lhs;
        bindings->constraints[i].rhs = rhs;
    }
    bindings->lookup_cache_count = 0;
    bindings->lookup_cache_next = 0;
    return true;
}

void answer_bank_init(AnswerBank *bank) {
    if (!bank)
        return;
    arena_init(&bank->arena);
    arena_set_hashcons(&bank->arena, NULL);
    arena_set_runtime_kind(&bank->arena, CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
    bank->items = NULL;
    bank->len = 0;
    bank->cap = 0;
}

void answer_bank_free(AnswerBank *bank) {
    if (!bank)
        return;
    for (CettaIndex i = 0; i < bank->len; i++)
        answer_record_free(&bank->items[i]);
    free(bank->items);
    bank->items = NULL;
    bank->len = 0;
    bank->cap = 0;
    arena_free(&bank->arena);
}

bool answer_bank_add(AnswerBank *bank, Atom *result,
                     const Bindings *bindings,
                     const VariantInstance *variant,
                     AnswerRef *out_ref) {
    if (out_ref)
        *out_ref = CETTA_ANSWER_REF_NONE;
    if (!bank || !result || bank->len >= CETTA_ANSWER_BANK_MAX_RECORDS ||
        !answer_bank_reserve(bank, bank->len + 1))
        return false;

    AnswerRecord staged;
    answer_record_init(&staged);
    staged.result = atom_deep_copy(&bank->arena, result);
    if (!staged.result)
        goto fail;
    if (bindings && !bindings_clone(&staged.bindings, bindings))
        goto fail;
    if (bindings && !answer_bank_promote_bindings(&bank->arena, &staged.bindings))
        goto fail;
    if (variant && !variant_instance_clone(&staged.variant, variant))
        goto fail;
    if (variant && !variant_instance_promote_atoms_to_arena(&bank->arena,
                                                            &staged.variant))
        goto fail;

    bank->items[bank->len] = staged;
    if (out_ref)
        *out_ref = bank->len;
    bank->len++;
    return true;

fail:
    answer_record_free(&staged);
    return false;
}

const AnswerRecord *answer_bank_get(const AnswerBank *bank, AnswerRef ref) {
    if (!bank || ref == CETTA_ANSWER_REF_NONE || ref >= bank->len)
        return NULL;
    return &bank->items[ref];
}
