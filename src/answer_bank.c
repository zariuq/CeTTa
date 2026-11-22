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
    while (next_cap < needed) {
        if (next_cap > limit / 2) { next_cap = limit; break; }
        next_cap *= 2;
    }
    if (next_cap > limit)
        next_cap = limit;
    if (next_cap < needed)
        return false;
    AnswerRecord *items = bank->items
        ? cetta_realloc(bank->items, sizeof(AnswerRecord) * (size_t)next_cap)
        : cetta_malloc(sizeof(AnswerRecord) * (size_t)next_cap);
    if (!items) return false;
    bank->items = items;
    bank->cap = next_cap;
    return true;
}

static Arena *answer_bank_arena(const AnswerBank *bank) {
    return bank->external_arena ? bank->external_arena : (Arena *)&bank->arena;
}

static void answer_bank_assert_bindings_owned(const AnswerBank *bank,
                                              const Bindings *bindings,
                                              const char *site) {
    if (!bank || !bindings)
        return;
    BindingsIterator iterator = {.bindings = bindings};
    Binding binding;
    while (bindings_iterator_next(&iterator, &binding)) {
        cetta_provenance_assert_not_transient_except(binding.name_key, site, answer_bank_arena(bank));
        cetta_provenance_assert_not_transient_except(binding.value.skeleton, site, answer_bank_arena(bank));
    }
    for (uint32_t i = 0; i < bindings->eq_len; i++) {
        cetta_provenance_assert_not_transient_except(
            bindings->constraints[i].lhs.skeleton, site, answer_bank_arena(bank));
        cetta_provenance_assert_not_transient_except(
            bindings->constraints[i].rhs.skeleton, site, answer_bank_arena(bank));
    }
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
    bank->external_arena = NULL;
}

void answer_bank_init_in(AnswerBank *bank, Arena *syntax_owner) {
    answer_bank_init(bank);
    if (bank) bank->external_arena = syntax_owner;
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
    bank->external_arena = NULL;
}

static bool answer_bank_add_copied(AnswerBank *bank, Atom *result,
                     const Bindings *bindings,
                     const VariantInstance *variant,
                     AnswerRef *out_ref, AtomDeepCopySession *copy) {
    if (out_ref)
        *out_ref = CETTA_ANSWER_REF_NONE;
    if (!bank || !result || bank->len >= CETTA_ANSWER_BANK_MAX_RECORDS ||
        !answer_bank_reserve(bank, bank->len + 1))
        return false;

    AnswerRecord staged;
    answer_record_init(&staged);
    staged.result = atom_deep_copy_session_copy(copy, result);
    if (!staged.result)
        goto fail;
    if (bindings && !bindings_clone(&staged.bindings, bindings))
        goto fail;
    if (bindings && !bindings_promote_logical_atoms_with_session(&staged.bindings, copy))
        goto fail;
    if (staged.bindings.prime_ext &&
        !bindings_promote_atoms_to_arena(&staged.bindings, answer_bank_arena(bank)))
        goto fail;
    if (variant && !variant_instance_clone(&staged.variant, variant))
        goto fail;
    if (variant && !variant_instance_promote_atoms_with_session(copy, &staged.variant))
        goto fail;
    cetta_provenance_assert_not_transient_except(staged.result,
                                                 "answer_bank.add.result",
                                                 answer_bank_arena(bank));
    answer_bank_assert_bindings_owned(bank, &staged.bindings,
                                      "answer_bank.add.bindings");

    bank->items[bank->len] = staged;
    if (out_ref)
        *out_ref = bank->len;
    bank->len++;
    return true;

fail:
    answer_record_free(&staged);
    return false;
}

bool answer_bank_add(AnswerBank *bank, Atom *result, const Bindings *bindings,
    const VariantInstance *variant, AnswerRef *out_ref) {
    if (out_ref) *out_ref = CETTA_ANSWER_REF_NONE;
    if (!bank) return false;
    AtomDeepCopySession *copy = atom_deep_copy_session_new(answer_bank_arena(bank));
    if (!copy) return false;
    bool ok = answer_bank_add_copied(bank, result, bindings, variant, out_ref, copy);
    atom_deep_copy_session_free(copy);
    return ok;
}

bool answer_bank_append_order(AnswerBank *bank, const AnswerBank *source,
    const CettaIndex *order) {
    if (!bank || !source || bank == source) return false;
    AtomDeepCopySession *copy = atom_deep_copy_session_new(answer_bank_arena(bank));
    if (!copy) return false;
    CettaCount before = bank->len;
    bool ok = true;
    for (CettaIndex i = 0; ok && i < source->len; ++i) {
        const AnswerRecord *record = answer_bank_get(source, order ? order[i] : i);
        ok = record && answer_bank_add_copied(bank, record->result, &record->bindings,
            &record->variant, NULL, copy);
    }
    if (!ok) while (bank->len > before) answer_record_free(&bank->items[--bank->len]);
    atom_deep_copy_session_free(copy);
    return ok;
}

bool answer_bank_append(AnswerBank *bank, const AnswerBank *source) {
    return answer_bank_append_order(bank, source, NULL);
}
const AnswerRecord *answer_bank_get(const AnswerBank *bank, AnswerRef ref) {
    if (!bank || ref == CETTA_ANSWER_REF_NONE || ref >= bank->len)
        return NULL;
    return &bank->items[ref];
}
