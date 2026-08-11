#include "proof_gslt_relational_machine_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    PPProofGSLTTokenSequenceV1 formula;
    PPProofGSLTReferenceV1 proof;
    const PPProofGSLTPatternV1 *goal;
} PPProofRelationalMachineStackEntryV1;

typedef struct {
    uint32_t floating_kind;
    uint32_t essential_kind;
    uint32_t first_rule_kind;
    uint32_t second_rule_kind;
    uint32_t variable_kind;
    uint32_t unknown_token;
} PPProofRelationalMachineSelectorsV1;

typedef struct {
    uint32_t position;
    uint32_t label;
} PPProofRelationalMachineFrameRowV1;

typedef enum {
    PPPROOF_RELATIONAL_MACHINE_HEAP_V1_LABEL = 0,
    PPPROOF_RELATIONAL_MACHINE_HEAP_V1_SAVED = 1
} PPProofRelationalMachineHeapKindV1;

typedef struct {
    PPProofRelationalMachineHeapKindV1 kind;
    uint32_t label;
    PPProofRelationalMachineStackEntryV1 saved;
} PPProofRelationalMachineHeapEntryV1;

static void ppproof_relational_machine_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_from_article(
    PPProofGSLTArticleV1Result result) {
    switch (result) {
    case PPPROOF_GSLT_ARTICLE_V1_OK:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    case PPPROOF_GSLT_ARTICLE_V1_REJECTED:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    case PPPROOF_GSLT_ARTICLE_V1_RESOURCE:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    case PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_UNSUPPORTED;
    case PPPROOF_GSLT_ARTICLE_V1_INVALID:
    default:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
}

static bool ppproof_relational_machine_v1_slice_valid(
    PPRelationalValueV1Slice slice) {
    return slice.bytes && slice.len != 0u;
}

static bool ppproof_relational_machine_v1_intern(
    const PPRelationalStoreV1 *store,
    PPRelationalValueV1Slice slice,
    uint32_t *value_out) {
    return ppproof_relational_machine_v1_slice_valid(slice) &&
           store->value_intern(
               store->context, slice.bytes, slice.len, value_out);
}

static bool ppproof_relational_machine_v1_intern_literal(
    const PPRelationalStoreV1 *store,
    PPRelationalStateLiteralV1 literal,
    uint32_t *value_out) {
    return literal.bytes && literal.len != 0u &&
           store->value_intern(
               store->context, literal.bytes, literal.len, value_out);
}

static bool ppproof_relational_machine_v1_intern_selector(
    const PPRelationalStoreV1 *store,
    PPProofGSLTRelationalSelectorV1 selector,
    uint32_t *value_out) {
    return selector.bytes && selector.len != 0u &&
           store->value_intern(
               store->context, selector.bytes, selector.len, value_out);
}

static bool ppproof_relational_machine_v1_slice_equals_literal(
    PPRelationalValueV1Slice slice,
    PPRelationalStateLiteralV1 literal) {
    return slice.len == literal.len && slice.bytes && literal.bytes &&
           memcmp(slice.bytes, literal.bytes, slice.len) == 0;
}

static bool ppproof_relational_machine_v1_formula_value(
    const PPRelationalStoreV1 *store,
    const PPRelationalValueV1Slice *items,
    uint32_t item_len,
    uint32_t *value_out) {
    uint8_t *encoded = NULL;
    uint32_t encoded_len = 0u;
    uint32_t index;
    bool result = false;

    if (!store || !items || item_len == 0u || !value_out)
        return false;
    for (index = 0u; index < item_len; index++) {
        if (!ppproof_relational_machine_v1_slice_valid(items[index]))
            return false;
    }
    if (pprelational_value_list_v1_encode_items(
            items, item_len, &encoded, &encoded_len) &&
        store->value_intern(
            store->context, encoded, encoded_len, value_out))
        result = true;
    free(encoded);
    return result;
}

static bool ppproof_relational_machine_v1_formula_equal(
    PPProofGSLTTokenSequenceV1 left,
    PPProofGSLTTokenSequenceV1 right) {
    uint32_t index;

    if (left.token_len != right.token_len ||
        (left.token_len != 0u && (!left.tokens || !right.tokens)))
        return false;
    for (index = 0u; index < left.token_len; index++) {
        if (left.tokens[index] != right.tokens[index])
            return false;
    }
    return true;
}

static PPProofGSLTNameV1 ppproof_relational_machine_v1_token_name(
    const PPProofGSLTSequenceTokenV1 *token) {
    if (!token || !token->term ||
        token->term->kind != PPPROOF_GSLT_PATTERN_V1_APPLY ||
        token->term->as.apply.argument_len != 0u)
        return (PPProofGSLTNameV1){0};
    return token->term->as.apply.constructor;
}

static void ppproof_relational_machine_v1_append_formula(
    char *buf, size_t size, const char *prefix,
    PPProofGSLTTokenSequenceV1 formula) {
    size_t used;
    uint32_t index;

    if (!buf || size == 0u || !prefix)
        return;
    used = strlen(buf);
    if (used >= size)
        return;
    (void)snprintf(buf + used, size - used, "%s[", prefix);
    for (index = 0u; index < formula.token_len && index < 32u; index++) {
        PPProofGSLTNameV1 name =
            ppproof_relational_machine_v1_token_name(
                formula.tokens ? formula.tokens[index] : NULL);
        used = strlen(buf);
        if (used >= size)
            return;
        (void)snprintf(
            buf + used, size - used, "%s%.*s",
            index == 0u ? "" : " ",
            (int)(name.len > 40u ? 40u : name.len),
            name.bytes ? (const char *)name.bytes : "?");
    }
    used = strlen(buf);
    if (used < size)
        (void)snprintf(
            buf + used, size - used, "%s]",
            formula.token_len > 32u ? " ..." : "");
}

static bool ppproof_relational_machine_v1_stack_push(
    PPProofRelationalMachineStackEntryV1 **stack,
    uint32_t *stack_len,
    uint32_t *stack_cap,
    PPProofRelationalMachineStackEntryV1 entry) {
    PPProofRelationalMachineStackEntryV1 *next;
    uint32_t next_cap;

    if (!stack || !stack_len || !stack_cap || *stack_len == UINT32_MAX)
        return false;
    if (*stack_len == *stack_cap) {
        next_cap = *stack_cap ? *stack_cap * 2u : 32u;
        if (next_cap < *stack_cap ||
            (size_t)next_cap > SIZE_MAX / sizeof(*next))
            return false;
        next = realloc(*stack, (size_t)next_cap * sizeof(*next));
        if (!next)
            return false;
        *stack = next;
        *stack_cap = next_cap;
    }
    (*stack)[(*stack_len)++] = entry;
    return true;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_push_incomplete_claim(
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTTokenSequenceV1 claim,
    PPProofRelationalMachineStackEntryV1 **stack,
    uint32_t *stack_len,
    uint32_t *stack_cap,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTReferenceV1 premise;
    PPProofGSLTSequenceProofV1 proof;
    PPProofGSLTArticleV1Result article_result;

    article_result = ppproof_gslt_relational_context_v1_add_provable_premise(
        context, claim, &premise, error_buf, error_buf_size);
    if (article_result == PPPROOF_GSLT_ARTICLE_V1_OK)
        article_result = ppproof_gslt_sequence_evidence_producer_v1_use_premise(
            producer, claim, premise, &proof, error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK)
        return ppproof_relational_machine_v1_from_article(article_result);
    if (!ppproof_relational_machine_v1_stack_push(
            stack, stack_len, stack_cap,
            (PPProofRelationalMachineStackEntryV1){
                .formula = claim,
                .proof = proof.evidence,
                .goal = proof.goal,
            }))
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
}

static bool ppproof_relational_machine_v1_heap_push(
    PPProofRelationalMachineHeapEntryV1 **heap,
    uint32_t *heap_len,
    uint32_t *heap_cap,
    PPProofRelationalMachineHeapEntryV1 entry,
    uint32_t maximum_len) {
    PPProofRelationalMachineHeapEntryV1 *next;
    uint32_t next_cap;

    if (!heap || !heap_len || !heap_cap || *heap_len >= maximum_len ||
        *heap_len == UINT32_MAX)
        return false;
    if (*heap_len == *heap_cap) {
        next_cap = *heap_cap ? *heap_cap * 2u : 32u;
        if (next_cap < *heap_cap)
            return false;
        if (next_cap > maximum_len)
            next_cap = maximum_len;
        if (next_cap <= *heap_len ||
            (size_t)next_cap > SIZE_MAX / sizeof(*next))
            return false;
        next = realloc(*heap, (size_t)next_cap * sizeof(*next));
        if (!next)
            return false;
        *heap = next;
        *heap_cap = next_cap;
    }
    (*heap)[(*heap_len)++] = entry;
    return true;
}

static bool ppproof_relational_machine_v1_grow(
    void **items, uint32_t *capacity, uint32_t required,
    uint32_t maximum_len, size_t item_size) {
    uint32_t next_cap;
    void *next;

    if (!items || !capacity || item_size == 0u ||
        required > maximum_len)
        return false;
    if (required <= *capacity)
        return true;
    next_cap = *capacity ? *capacity * 2u : 32u;
    if (next_cap < *capacity)
        return false;
    if (next_cap < required)
        next_cap = required;
    if (next_cap > maximum_len)
        next_cap = maximum_len;
    if (next_cap < required ||
        (size_t)next_cap > SIZE_MAX / item_size)
        return false;
    next = realloc(*items, (size_t)next_cap * item_size);
    if (!next)
        return false;
    *items = next;
    *capacity = next_cap;
    return true;
}

static int ppproof_relational_machine_v1_frame_row_compare(
    const void *left, const void *right) {
    const PPProofRelationalMachineFrameRowV1 *left_row = left;
    const PPProofRelationalMachineFrameRowV1 *right_row = right;
    if (left_row->position < right_row->position)
        return -1;
    if (left_row->position > right_row->position)
        return 1;
    if (left_row->label < right_row->label)
        return -1;
    return left_row->label > right_row->label ? 1 : 0;
}

static bool ppproof_relational_machine_v1_active_hypothesis(
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    uint32_t authority,
    uint32_t hypothesis) {
    uint64_t cursor = UINT64_MAX;
    uint32_t row[2];
    uint32_t matches = 0u;

    for (;;) {
        bool found = false;
        if (!store->table_prefix_next(
                store->context,
                relational_plan->tables[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_ACTIVE_HYPOTHESIS],
                &authority, 1u, UINT32_C(1) << 1u,
                &cursor, row, 2u, &found))
            return false;
        if (!found)
            return matches == 1u;
        if (row[1] == hypothesis) {
            if (matches == UINT32_MAX)
                return false;
            matches++;
        }
    }
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_preload_frame(
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofRelationalMachineSelectorsV1 *selectors,
    uint32_t authority,
    PPProofRelationalMachineHeapEntryV1 **heap,
    uint32_t *heap_len,
    uint32_t *heap_cap,
    uint32_t maximum_len,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t *mandatory = NULL;
    uint32_t *binder_counts = NULL;
    uint32_t mandatory_len = 0u;
    uint32_t mandatory_cap = 0u;
    PPProofRelationalMachineFrameRowV1 *ordered = NULL;
    uint32_t ordered_len = 0u;
    uint32_t ordered_cap = 0u;
    uint64_t cursor = UINT64_MAX;
    PPProofGSLTRelationalMachineV1Result result =
        PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    uint32_t index;

    for (;;) {
        uint32_t row[2];
        bool found = false;
        if (!store->table_prefix_next(
                store->context,
                relational_plan->tables[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE],
                &authority, 1u, UINT32_C(1) << 1u,
                &cursor, row, 2u, &found))
            goto done;
        if (!found)
            break;
        for (index = 0u; index < mandatory_len; index++) {
            if (mandatory[index] == row[1]) {
                ppproof_relational_machine_v1_set_error(
                    error_buf, error_buf_size,
                    "compressed frame repeats a mandatory variable");
                goto done;
            }
        }
        if (!ppproof_relational_machine_v1_grow(
                (void **)&mandatory, &mandatory_cap,
                mandatory_len + 1u, maximum_len,
                sizeof(*mandatory))) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
        mandatory[mandatory_len++] = row[1];
    }
    if (mandatory_len != 0u) {
        binder_counts = calloc(mandatory_len, sizeof(*binder_counts));
        if (!binder_counts) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
    }

    cursor = UINT64_MAX;
    for (;;) {
        uint32_t row[3];
        uint32_t position;
        bool found = false;
        if (!store->table_prefix_next(
                store->context,
                relational_plan->tables[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
                &authority, 1u,
                (UINT32_C(1) << 1u) | (UINT32_C(1) << 2u),
                &cursor, row, 3u, &found))
            goto done;
        if (!found)
            break;
        if (!pprelational_store_v1_value_u32_decimal(
                store, row[1], &position)) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed frame has a noncanonical ordered authority");
            goto done;
        }
        if (!ppproof_relational_machine_v1_grow(
                (void **)&ordered, &ordered_cap,
                ordered_len + 1u, maximum_len,
                sizeof(*ordered))) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
        ordered[ordered_len++] =
            (PPProofRelationalMachineFrameRowV1){position, row[2]};
    }
    if (ordered_len > 1u)
        qsort(ordered, ordered_len, sizeof(*ordered),
              ppproof_relational_machine_v1_frame_row_compare);
    for (index = 0u; index < ordered_len; index++) {
        uint32_t kind_row[2];
        bool include = false;

        if (index != 0u &&
            ordered[index - 1u].position == ordered[index].position) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed frame has duplicate ordered authority");
            goto done;
        }
        {
            uint32_t prior;
            for (prior = 0u; prior < index; prior++) {
                if (ordered[prior].label == ordered[index].label) {
                    ppproof_relational_machine_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed frame repeats an active hypothesis");
                    goto done;
                }
            }
        }
        if (!store->table_find(
                store->context,
                relational_plan->tables[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
                &ordered[index].label, 1u, kind_row, 2u))
            goto done;
        if (kind_row[1] == selectors->floating_kind) {
            uint32_t variable_row[2];
            uint32_t mandatory_index;
            if (!store->table_find(
                    store->context,
                    relational_plan->tables[
                        PPPROOF_GSLT_RELATIONAL_TABLE_V1_FLOATING_VARIABLE],
                    &ordered[index].label, 1u, variable_row, 2u))
                goto done;
            for (mandatory_index = 0u;
                 mandatory_index < mandatory_len; mandatory_index++) {
                if (mandatory[mandatory_index] == variable_row[1]) {
                    if (binder_counts[mandatory_index] == UINT32_MAX)
                        goto done;
                    binder_counts[mandatory_index]++;
                    include = true;
                    break;
                }
            }
        } else if (kind_row[1] == selectors->essential_kind) {
            include = true;
        } else {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed frame contains a non-hypothesis label");
            goto done;
        }
        if (include && !ppproof_relational_machine_v1_heap_push(
                heap, heap_len, heap_cap,
                (PPProofRelationalMachineHeapEntryV1){
                    .kind = PPPROOF_RELATIONAL_MACHINE_HEAP_V1_LABEL,
                    .label = ordered[index].label,
                },
                maximum_len)) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
    }
    for (index = 0u; index < mandatory_len; index++) {
        if (binder_counts[index] != 1u) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed frame lacks a unique mandatory binding");
            goto done;
        }
    }
    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;

done:
    free(ordered);
    free(binder_counts);
    free(mandatory);
    return result;
}

static bool ppproof_relational_machine_v1_composition_valid(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    PPProofRelationalMachineSelectorsV1 *selectors_out,
    char *error_buf,
    size_t error_buf_size) {
    const PPRelationalStateProofMachineV1 *machine;
    PPProofRelationalMachineSelectorsV1 selectors;
    uint32_t relational_floating;
    uint32_t relational_essential;
    uint32_t relational_variable;

    if (!pprelational_store_v1_valid(store) || !state_plan ||
        proof_machine_id >= state_plan->proof_machine_len ||
        !state_plan->proof_machines || !proof_plan ||
        !proof_plan->storage || !evidence_abi || !evidence_abi->storage ||
        !relational_plan || !relational_plan->storage || !selectors_out ||
        strcmp(relational_plan->proof_plan_digest,
               proof_plan->semantic_digest) != 0 ||
        strcmp(relational_plan->state_plan_digest,
               state_plan->plan_digest) != 0 ||
        !ppproof_gslt_article_v1_name_equal(
            proof_plan->owner, evidence_abi->owner) ||
        !ppproof_gslt_article_v1_name_equal(
            proof_plan->base, evidence_abi->base) ||
        !ppproof_gslt_article_v1_name_equal(
            proof_plan->owner, relational_plan->owner) ||
        !ppproof_gslt_article_v1_name_equal(
            proof_plan->base, relational_plan->base)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "relational proof composition identities do not agree");
        return false;
    }
    machine = &state_plan->proof_machines[proof_machine_id];
    if (machine->label_kind_table !=
            relational_plan->tables[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND] ||
        machine->formula_table !=
            relational_plan->tables[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA] ||
        machine->binder_variable_table !=
            relational_plan->tables[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_FLOATING_VARIABLE] ||
        machine->mandatory_variable_table !=
            relational_plan->tables[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE] ||
        machine->assertion_hypothesis_table !=
            relational_plan->tables[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS] ||
        machine->assertion_disjoint_table !=
            relational_plan->tables[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_DISJOINT] ||
        machine->symbol_kind_table !=
            relational_plan->tables[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND]) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "relational proof table roles disagree with the state plan");
        return false;
    }
    if (!ppproof_relational_machine_v1_intern_literal(
            store, machine->binder_hypothesis_kind,
            &selectors.floating_kind) ||
        !ppproof_relational_machine_v1_intern_literal(
            store, machine->matching_hypothesis_kind,
            &selectors.essential_kind) ||
        !ppproof_relational_machine_v1_intern_literal(
            store, machine->rule_kind_first,
            &selectors.first_rule_kind) ||
        !ppproof_relational_machine_v1_intern_literal(
            store, machine->rule_kind_second,
            &selectors.second_rule_kind) ||
        !ppproof_relational_machine_v1_intern_literal(
            store, machine->variable_symbol_kind,
            &selectors.variable_kind) ||
        !ppproof_relational_machine_v1_intern_literal(
            store, machine->unknown_token,
            &selectors.unknown_token) ||
        !ppproof_relational_machine_v1_intern_selector(
            store,
            relational_plan->selectors[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING],
            &relational_floating) ||
        !ppproof_relational_machine_v1_intern_selector(
            store,
            relational_plan->selectors[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL],
            &relational_essential) ||
        !ppproof_relational_machine_v1_intern_selector(
            store,
            relational_plan->selectors[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE],
            &relational_variable)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "relational proof selectors cannot be interned");
        return false;
    }
    if (selectors.floating_kind != relational_floating ||
        selectors.essential_kind != relational_essential ||
        selectors.variable_kind != relational_variable ||
        selectors.floating_kind == selectors.essential_kind ||
        selectors.first_rule_kind == selectors.second_rule_kind ||
        selectors.first_rule_kind == selectors.floating_kind ||
        selectors.first_rule_kind == selectors.essential_kind ||
        selectors.second_rule_kind == selectors.floating_kind ||
        selectors.second_rule_kind == selectors.essential_kind) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "relational proof selector roles are ambiguous");
        return false;
    }
    *selectors_out = selectors;
    return true;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_hypothesis(
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    uint32_t authority,
    uint32_t label,
    PPProofRelationalMachineStackEntryV1 *entry_out,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t formula_row[2];
    PPProofGSLTTokenSequenceV1 formula;
    PPProofGSLTReferenceV1 premise;
    PPProofGSLTSequenceProofV1 proof;
    PPProofGSLTArticleV1Result result;

    if (!ppproof_relational_machine_v1_active_hypothesis(
            store, relational_plan, authority, label)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof references a hypothesis outside its active snapshot");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    if (!store->table_find(
            store->context,
            relational_plan->tables[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA],
            &label, 1u, formula_row, 2u)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "active proof hypothesis has no formula");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
    result = ppproof_gslt_relational_context_v1_formula(
        context, formula_row[1], &formula, error_buf, error_buf_size);
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_relational_context_v1_add_provable_premise(
            context, formula, &premise, error_buf, error_buf_size);
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_sequence_evidence_producer_v1_use_premise(
            producer, formula, premise, &proof,
            error_buf, error_buf_size);
    if (result != PPPROOF_GSLT_ARTICLE_V1_OK)
        return ppproof_relational_machine_v1_from_article(result);
    *entry_out = (PPProofRelationalMachineStackEntryV1){
        .formula = formula,
        .proof = proof.evidence,
        .goal = proof.goal,
    };
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_assertion(
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    uint32_t active_apartness_table,
    uint32_t assertion,
    PPProofRelationalMachineStackEntryV1 *stack,
    uint32_t *stack_len,
    PPProofRelationalMachineStackEntryV1 *entry_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTRelationalDeclarationV1 schema;
    PPProofGSLTRelationalPreparedAssertionV1 prepared;
    PPProofGSLTRelationalActualHypothesisV1 *actuals = NULL;
    PPProofGSLTRelationalEvidenceV1 relational_evidence;
    PPProofGSLTSequenceEvidenceSourcesV1 sources;
    PPProofGSLTReferenceV1 declaration_reference;
    PPProofGSLTAssertionApplicationV1 application;
    PPProofGSLTTokenSequenceV1 formula;
    PPProofGSLTArticleV1Result result;
    PPProofGSLTRelationalMachineV1Result mapped;
    const char *failure_stage = "declaration elaboration";
    uint32_t stack_base;
    uint32_t index;

    ppproof_gslt_relational_declaration_v1_init(&schema);
    ppproof_gslt_relational_prepared_assertion_v1_init(&prepared);
    result = ppproof_gslt_relational_declaration_v1_elaborate(
        context, assertion, &schema, error_buf, error_buf_size);
    if (result != PPPROOF_GSLT_ARTICLE_V1_OK) {
        if (result == PPPROOF_GSLT_ARTICLE_V1_INVALID &&
            error_buf && error_buf_size != 0u && error_buf[0] == '\0')
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "assertion %u failed during %s", assertion,
                failure_stage);
        mapped = ppproof_relational_machine_v1_from_article(result);
        goto done;
    }
    if (schema.ordered_len > *stack_len) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof stack lacks the assertion's ordered hypotheses");
        mapped = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
        goto done;
    }
    if (schema.ordered_len != 0u) {
        if ((size_t)schema.ordered_len > SIZE_MAX / sizeof(*actuals) ||
            !(actuals = calloc(schema.ordered_len, sizeof(*actuals)))) {
            mapped = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
    }
    stack_base = *stack_len - schema.ordered_len;
    for (index = 0u; index < schema.ordered_len; index++) {
        actuals[index] = (PPProofGSLTRelationalActualHypothesisV1){
            .formula = stack[stack_base + index].formula,
            .proof = stack[stack_base + index].proof,
        };
    }
    failure_stage = "assertion preparation";
    result = ppproof_gslt_relational_prepared_assertion_v1_build(
        &schema, actuals, schema.ordered_len, &prepared,
        error_buf, error_buf_size);
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
        failure_stage = "declaration-premise reservation";
        result = ppproof_gslt_relational_context_v1_reserve_premise(
            context, &declaration_reference, error_buf, error_buf_size);
    }
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
        failure_stage = "evidence-source construction";
        result = ppproof_gslt_relational_context_v1_evidence_sources(
            context, active_apartness_table,
            &relational_evidence, &sources,
            error_buf, error_buf_size);
    }
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
        failure_stage = "assertion evidence production";
        result = ppproof_gslt_sequence_evidence_producer_v1_apply_assertion(
            producer, &prepared.declaration, declaration_reference.index,
            &sources, &application, error_buf, error_buf_size);
    }
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
        failure_stage = "declaration-premise completion";
        result = ppproof_gslt_relational_context_v1_fill_premise(
            context, declaration_reference, application.declared_goal,
            error_buf, error_buf_size);
    }
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
        failure_stage = "typed-result construction";
        result = ppproof_gslt_relational_context_v1_typed_formula(
            context, prepared.declaration.conclusion_type,
            (PPProofGSLTTokenSequenceV1){
                .tokens = application.result.tokens,
                .token_len = application.result.token_len,
            },
            &formula, error_buf, error_buf_size);
    }
    if (result != PPPROOF_GSLT_ARTICLE_V1_OK) {
        if (error_buf && error_buf_size != 0u) {
            char detail[256] = "no diagnostic";

            if (error_buf[0] != '\0')
                (void)snprintf(detail, sizeof(detail), "%s", error_buf);
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "assertion %u failed during %s "
                "(ordered hypotheses %u, stack depth %u): %s",
                assertion, failure_stage, schema.ordered_len, *stack_len,
                detail);
        }
        mapped = ppproof_relational_machine_v1_from_article(result);
        goto done;
    }
    *stack_len = stack_base;
    *entry_out = (PPProofRelationalMachineStackEntryV1){
        .formula = formula,
        .proof = application.proof.evidence,
        .goal = application.proof.goal,
    };
    mapped = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;

done:
    free(actuals);
    ppproof_gslt_relational_prepared_assertion_v1_free(&prepared);
    ppproof_gslt_relational_declaration_v1_free(&schema);
    return mapped;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_apply_label(
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    const PPProofRelationalMachineSelectorsV1 *selectors,
    uint32_t authority,
    uint32_t active_apartness_table,
    uint32_t label,
    PPProofRelationalMachineStackEntryV1 **stack,
    uint32_t *stack_len,
    uint32_t *stack_cap,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRelationalMachineStackEntryV1 entry = {0};
    uint32_t kind_row[2];
    PPProofGSLTRelationalMachineV1Result result;

    if (!store->table_find(
            store->context,
            relational_plan->tables[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
            &label, 1u, kind_row, 2u)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof references an unknown or future label");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    if (kind_row[1] == selectors->floating_kind ||
        kind_row[1] == selectors->essential_kind) {
        result = ppproof_relational_machine_v1_hypothesis(
            store, relational_plan, context, producer,
            authority, label, &entry, error_buf, error_buf_size);
    } else if (kind_row[1] == selectors->first_rule_kind ||
               kind_row[1] == selectors->second_rule_kind) {
        result = ppproof_relational_machine_v1_assertion(
            context, producer, active_apartness_table, label,
            *stack, stack_len, &entry, error_buf, error_buf_size);
    } else {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof label has no generated proof role");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK) {
        const uint8_t *label_bytes = NULL;
        uint32_t label_len = 0u;

        if (error_buf && error_buf_size != 0u && error_buf[0] != '\0' &&
            store->value_bytes(
                store->context, label, &label_bytes, &label_len) &&
            label_bytes && label_len != 0u) {
            char detail[256];

            (void)snprintf(detail, sizeof(detail), "%s", error_buf);
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "proof label %.*s failed: %s",
                (int)(label_len > 120u ? 120u : label_len),
                (const char *)label_bytes, detail);
        }
        return result;
    }
    if (!ppproof_relational_machine_v1_stack_push(
            stack, stack_len, stack_cap, entry))
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_finish_incomplete(
    PPProofGSLTRelationalContextV1 *context,
    const PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTRelationalMachineV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    const PPProofGSLTPatternV1 *premises = NULL;
    uint32_t premise_len = 0u;
    PPProofGSLTArticleV1Result article_result;

    article_result = ppproof_gslt_relational_context_v1_view(
        context, &premises, &premise_len, error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK)
        return ppproof_relational_machine_v1_from_article(article_result);
    receipt->context_premise_len = premise_len;
    receipt->article_node_len = producer->node_len;
    receipt->complete = false;
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_finish(
    const PPProofGSLTPlanV1 *proof_plan,
    PPProofGSLTRelationalContextV1 *context,
    const PPProofGSLTSequenceEvidenceProducerV1 *producer,
    const PPProofRelationalMachineStackEntryV1 *stack,
    uint32_t stack_len,
    PPRelationalValueV1Slice label,
    PPProofGSLTTokenSequenceV1 claim,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTRelationalMachineV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    const PPProofGSLTPatternV1 *premises = NULL;
    uint32_t premise_len = 0u;
    PPProofGSLTArticleV1 article;
    PPProofGSLTArticleV1Receipt article_receipt;
    PPProofGSLTArticleV1Result article_result;

    if (stack_len != 1u) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof %.*s ends with stack depth %u instead of one",
            (int)(label.len > 200u ? 200u : label.len),
            (const char *)label.bytes, stack_len);
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    if (!stack[0].goal ||
        stack[0].proof.kind != PPPROOF_GSLT_REFERENCE_V1_NODE ||
        stack[0].proof.index >= producer->node_len) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof %.*s has an invalid root reference "
            "(goal %u, kind %u, index %u, nodes %u)",
            (int)(label.len > 120u ? 120u : label.len),
            (const char *)label.bytes,
            stack[0].goal ? 1u : 0u,
            (unsigned)stack[0].proof.kind,
            stack[0].proof.index, producer->node_len);
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
    article_result = ppproof_gslt_relational_context_v1_view(
        context, &premises, &premise_len, error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK)
        return ppproof_relational_machine_v1_from_article(article_result);
    if (!ppproof_relational_machine_v1_formula_equal(
            stack[0].formula, claim)) {
        uint32_t mismatch = 0u;
        PPProofGSLTNameV1 actual = {0};
        PPProofGSLTNameV1 expected = {0};

        while (mismatch < stack[0].formula.token_len &&
               mismatch < claim.token_len &&
               stack[0].formula.tokens[mismatch] == claim.tokens[mismatch])
            mismatch++;
        if (mismatch < stack[0].formula.token_len)
            actual = ppproof_relational_machine_v1_token_name(
                stack[0].formula.tokens[mismatch]);
        if (mismatch < claim.token_len)
            expected = ppproof_relational_machine_v1_token_name(
                claim.tokens[mismatch]);
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof %.*s formula mismatch at token %u "
            "(actual %.*s, expected %.*s; lengths %u and %u)",
            (int)(label.len > 120u ? 120u : label.len),
            (const char *)label.bytes, mismatch,
            (int)(actual.len > 80u ? 80u : actual.len),
            actual.bytes ? (const char *)actual.bytes : "",
            (int)(expected.len > 80u ? 80u : expected.len),
            expected.bytes ? (const char *)expected.bytes : "",
            stack[0].formula.token_len, claim.token_len);
        ppproof_relational_machine_v1_append_formula(
            error_buf, error_buf_size, "; actual ", stack[0].formula);
        ppproof_relational_machine_v1_append_formula(
            error_buf, error_buf_size, "; expected ", claim);
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    article = (PPProofGSLTArticleV1){
        .version = 1u,
        .nodes = producer->nodes,
        .node_len = producer->node_len,
        .root_id = stack[0].proof.index,
        .target = stack[0].goal,
    };
    article_result = ppproof_gslt_article_v1_check_open(
        &proof_plan->presentation, premises, premise_len,
        &article, true, limits, &article_receipt,
        error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK) {
        if (error_buf && error_buf_size != 0u) {
            char detail[256] = "proof article check failed";
            if (error_buf[0] != '\0')
                (void)snprintf(detail, sizeof(detail), "%s", error_buf);
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "proof %.*s article check failed: %s",
                (int)(label.len > 120u ? 120u : label.len),
                (const char *)label.bytes, detail);
        }
        return ppproof_relational_machine_v1_from_article(article_result);
    }
    receipt->context_premise_len = premise_len;
    receipt->article_node_len = producer->node_len;
    receipt->article = article_receipt;
    receipt->complete = true;
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
}

PPProofGSLTRelationalMachineV1Result
ppproof_gslt_relational_machine_v1_normal(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofGSLTRelationalNormalInputV1 *input,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size) {
    const PPRelationalStateProofMachineV1 *machine;
    PPProofRelationalMachineSelectorsV1 selectors;
    PPProofGSLTRelationalContextV1 context;
    PPProofGSLTSequenceEvidenceProducerV1 producer;
    PPProofRelationalMachineStackEntryV1 *stack = NULL;
    uint32_t stack_len = 0u;
    uint32_t stack_cap = 0u;
    uint32_t authority = 0u;
    uint32_t claim_value = 0u;
    PPProofGSLTTokenSequenceV1 claim;
    PPProofGSLTRelationalMachineV1Receipt receipt = {0};
    PPProofGSLTRelationalMachineV1Result result =
        PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    PPProofGSLTArticleV1Result article_result;
    uint32_t index;
    bool incomplete = false;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (receipt_out)
        memset(receipt_out, 0, sizeof(*receipt_out));
    ppproof_gslt_relational_context_v1_init(&context);
    ppproof_gslt_sequence_evidence_producer_v1_init(&producer);
    if (!input || !receipt_out ||
        !ppproof_relational_machine_v1_slice_valid(input->label) ||
        !input->claim || input->claim_len == 0u ||
        !input->steps || input->step_len == 0u ||
        !ppproof_relational_machine_v1_composition_valid(
            store, state_plan, proof_machine_id, proof_plan,
            evidence_abi, relational_plan, &selectors,
            error_buf, error_buf_size))
        goto done;
    machine = &state_plan->proof_machines[proof_machine_id];
    if (!ppproof_relational_machine_v1_intern(
            store, input->label, &authority) ||
        !ppproof_relational_machine_v1_formula_value(
            store, input->claim, input->claim_len, &claim_value)) {
        result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        goto done;
    }
    article_result = ppproof_gslt_relational_context_v1_begin(
        &context, store, relational_plan, evidence_abi, limits,
        error_buf, error_buf_size);
    if (article_result == PPPROOF_GSLT_ARTICLE_V1_OK)
        article_result = ppproof_gslt_relational_context_v1_formula(
            &context, claim_value, &claim, error_buf, error_buf_size);
    if (article_result == PPPROOF_GSLT_ARTICLE_V1_OK)
        article_result = ppproof_gslt_sequence_evidence_producer_v1_begin(
            &producer, evidence_abi, 0u, limits,
            error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK) {
        result = ppproof_relational_machine_v1_from_article(article_result);
        goto done;
    }

    for (index = 0u; index < input->step_len; index++) {
        uint32_t label = 0u;

        if (ppproof_relational_machine_v1_slice_equals_literal(
                input->steps[index], machine->unknown_token)) {
            if (machine->unknown_policy !=
                PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM) {
                ppproof_relational_machine_v1_set_error(
                    error_buf, error_buf_size,
                    "generated proof policy rejects incomplete proofs");
                result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
            } else {
                incomplete = true;
                result = ppproof_relational_machine_v1_push_incomplete_claim(
                    &context, &producer, claim,
                    &stack, &stack_len, &stack_cap,
                    error_buf, error_buf_size);
            }
            if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
                goto done;
            continue;
        }
        if (!ppproof_relational_machine_v1_intern(
                store, input->steps[index], &label)) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
        result = ppproof_relational_machine_v1_apply_label(
            store, relational_plan, &context, &producer,
            &selectors, authority, machine->active_disjoint_table,
            label, &stack, &stack_len,
            &stack_cap, error_buf, error_buf_size);
        if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
            goto done;
    }
    receipt.proof_step_len = input->step_len;
    if (incomplete)
        result = ppproof_relational_machine_v1_finish_incomplete(
            &context, &producer, &receipt, error_buf, error_buf_size);
    else
        result = ppproof_relational_machine_v1_finish(
            proof_plan, &context, &producer, stack, stack_len, input->label,
            claim, limits, &receipt, error_buf, error_buf_size);

done:
    if (result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
        error_buf && error_buf_size != 0u && error_buf[0] == '\0') {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational normal-proof request");
    }
    if (result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK ||
        result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE)
        *receipt_out = receipt;
    free(stack);
    ppproof_gslt_sequence_evidence_producer_v1_free(&producer);
    ppproof_gslt_relational_context_v1_free(&context);
    return result;
}

static bool ppproof_relational_machine_v1_decoder_valid(
    const PPRelationalStateProofMachineV1 *machine) {
    return machine && machine->terminal_low <= machine->terminal_high &&
           machine->continuation_low <= machine->continuation_high &&
           (machine->terminal_high < machine->continuation_low ||
            machine->continuation_high < machine->terminal_low) &&
           !(machine->save_byte >= machine->terminal_low &&
             machine->save_byte <= machine->terminal_high) &&
           !(machine->save_byte >= machine->continuation_low &&
             machine->save_byte <= machine->continuation_high) &&
           !(machine->unknown_byte >= machine->terminal_low &&
             machine->unknown_byte <= machine->terminal_high) &&
           !(machine->unknown_byte >= machine->continuation_low &&
             machine->unknown_byte <= machine->continuation_high) &&
           machine->save_byte != machine->unknown_byte &&
           machine->terminal_radix != 0u &&
           machine->continuation_radix != 0u &&
           machine->unknown_policy <=
               PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM;
}

PPProofGSLTRelationalMachineV1Result
ppproof_gslt_relational_machine_v1_compressed(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofGSLTRelationalCompressedInputV1 *input,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTArticleV1Limits default_limits;
    const PPProofGSLTArticleV1Limits *effective_limits = limits;
    const PPRelationalStateProofMachineV1 *machine;
    PPProofRelationalMachineSelectorsV1 selectors;
    PPProofGSLTRelationalContextV1 context;
    PPProofGSLTSequenceEvidenceProducerV1 producer;
    PPProofRelationalMachineStackEntryV1 *stack = NULL;
    uint32_t stack_len = 0u;
    uint32_t stack_cap = 0u;
    PPProofRelationalMachineHeapEntryV1 *heap = NULL;
    uint32_t heap_len = 0u;
    uint32_t heap_cap = 0u;
    uint32_t authority = 0u;
    uint32_t claim_value = 0u;
    PPProofGSLTTokenSequenceV1 claim;
    PPProofGSLTRelationalMachineV1Receipt receipt = {0};
    PPProofGSLTRelationalMachineV1Result result =
        PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    PPProofGSLTArticleV1Result article_result;
    uint64_t accumulator = 0u;
    uint32_t decoded_step_len = 0u;
    uint32_t index;
    bool incomplete = false;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (receipt_out)
        memset(receipt_out, 0, sizeof(*receipt_out));
    ppproof_gslt_relational_context_v1_init(&context);
    ppproof_gslt_sequence_evidence_producer_v1_init(&producer);
    if (!effective_limits) {
        default_limits = ppproof_gslt_article_v1_default_limits();
        effective_limits = &default_limits;
    }
    if (!input) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof input is absent");
        goto done;
    }
    if (!receipt_out) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof receipt is absent");
        goto done;
    }
    if (!ppproof_relational_machine_v1_slice_valid(input->label)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof label is empty");
        goto done;
    }
    if (!input->claim || input->claim_len == 0u) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof claim is empty");
        goto done;
    }
    if (input->header_len != 0u && !input->header) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof header storage is absent");
        goto done;
    }
    if (!input->code || input->code_len == 0u) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof code is empty");
        goto done;
    }
    if (effective_limits->maximum_article_nodes == 0u) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof article-node limit is zero");
        goto done;
    }
    if (!ppproof_relational_machine_v1_composition_valid(
            store, state_plan, proof_machine_id, proof_plan,
            evidence_abi, relational_plan, &selectors,
            error_buf, error_buf_size))
        goto done;
    machine = &state_plan->proof_machines[proof_machine_id];
    if (!ppproof_relational_machine_v1_decoder_valid(machine)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "generated compressed-proof decoder is malformed");
        goto done;
    }
    if (!ppproof_relational_machine_v1_intern(
            store, input->label, &authority) ||
        !ppproof_relational_machine_v1_formula_value(
            store, input->claim, input->claim_len, &claim_value)) {
        result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        goto done;
    }
    article_result = ppproof_gslt_relational_context_v1_begin(
        &context, store, relational_plan, evidence_abi,
        effective_limits, error_buf, error_buf_size);
    if (article_result == PPPROOF_GSLT_ARTICLE_V1_OK)
        article_result = ppproof_gslt_relational_context_v1_formula(
            &context, claim_value, &claim, error_buf, error_buf_size);
    if (article_result == PPPROOF_GSLT_ARTICLE_V1_OK)
        article_result = ppproof_gslt_sequence_evidence_producer_v1_begin(
            &producer, evidence_abi, 0u, effective_limits,
            error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK) {
        result = ppproof_relational_machine_v1_from_article(article_result);
        goto done;
    }
    result = ppproof_relational_machine_v1_preload_frame(
        store, relational_plan, &selectors, authority,
        &heap, &heap_len, &heap_cap,
        effective_limits->maximum_article_nodes,
        error_buf, error_buf_size);
    if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK) {
        if (result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
            error_buf && error_buf_size != 0u && error_buf[0] == '\0')
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed proof frame preload failed");
        goto done;
    }
    for (index = 0u; index < input->header_len; index++) {
        uint32_t label = 0u;
        uint32_t kind_row[2];
        if (!ppproof_relational_machine_v1_intern(
                store, input->header[index], &label)) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
        if (!store->table_find(
                store->context,
                relational_plan->tables[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
                &label, 1u, kind_row, 2u)) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed header references an unknown or future label");
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
            goto done;
        }
        if ((kind_row[1] == selectors.floating_kind ||
             kind_row[1] == selectors.essential_kind) &&
            !ppproof_relational_machine_v1_active_hypothesis(
                store, relational_plan, authority, label)) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed header references an inactive hypothesis");
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
            goto done;
        }
        if (kind_row[1] != selectors.floating_kind &&
            kind_row[1] != selectors.essential_kind &&
            kind_row[1] != selectors.first_rule_kind &&
            kind_row[1] != selectors.second_rule_kind) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed header label has no generated proof role");
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
            goto done;
        }
        if (!ppproof_relational_machine_v1_heap_push(
                &heap, &heap_len, &heap_cap,
                (PPProofRelationalMachineHeapEntryV1){
                    .kind = PPPROOF_RELATIONAL_MACHINE_HEAP_V1_LABEL,
                    .label = label,
                },
                effective_limits->maximum_article_nodes)) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
    }

    for (index = 0u; index < input->code_len; index++) {
        uint32_t byte_index;
        if (!ppproof_relational_machine_v1_slice_valid(
                input->code[index])) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed proof contains an empty code word");
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
            goto done;
        }
        for (byte_index = 0u;
             byte_index < input->code[index].len; byte_index++) {
            uint8_t byte = input->code[index].bytes[byte_index];
            if (byte >= machine->terminal_low &&
                byte <= machine->terminal_high) {
                uint64_t digit =
                    (uint64_t)(byte - machine->terminal_low) +
                    machine->terminal_digit_bias;
                uint64_t heap_index;
                if (accumulator >
                    (UINT64_MAX - digit) / machine->terminal_radix) {
                    ppproof_relational_machine_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof index overflows");
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
                    goto done;
                }
                heap_index = machine->terminal_radix * accumulator + digit;
                accumulator = 0u;
                if (heap_index >= heap_len) {
                    ppproof_relational_machine_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof index is out of range");
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
                    goto done;
                }
                if (heap[heap_index].kind ==
                    PPPROOF_RELATIONAL_MACHINE_HEAP_V1_LABEL) {
                    result = ppproof_relational_machine_v1_apply_label(
                        store, relational_plan, &context, &producer,
                        &selectors, authority,
                        machine->active_disjoint_table,
                        heap[heap_index].label,
                        &stack, &stack_len, &stack_cap,
                        error_buf, error_buf_size);
                } else if (heap[heap_index].kind ==
                           PPPROOF_RELATIONAL_MACHINE_HEAP_V1_SAVED) {
                    result = ppproof_relational_machine_v1_stack_push(
                                 &stack, &stack_len, &stack_cap,
                                 heap[heap_index].saved)
                                 ? PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK
                                 : PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                } else {
                    ppproof_relational_machine_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof heap entry has an invalid kind");
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
                }
                if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK) {
                    if (result ==
                            PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
                        error_buf && error_buf_size != 0u &&
                        error_buf[0] == '\0')
                        ppproof_relational_machine_v1_set_error(
                            error_buf, error_buf_size,
                            "compressed proof label application failed "
                            "at decoded step %u (word %u, byte %u, "
                            "heap index %llu, label id %u, stack %u)",
                            decoded_step_len, index, byte_index,
                            (unsigned long long)heap_index,
                            heap[heap_index].kind ==
                                    PPPROOF_RELATIONAL_MACHINE_HEAP_V1_LABEL
                                ? heap[heap_index].label
                                : 0u,
                            stack_len);
                    goto done;
                }
                if (decoded_step_len == UINT32_MAX) {
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                    goto done;
                }
                decoded_step_len++;
            } else if (byte >= machine->continuation_low &&
                       byte <= machine->continuation_high) {
                uint64_t digit =
                    (uint64_t)(byte - machine->continuation_low) +
                    machine->continuation_digit_bias;
                if (accumulator >
                    (UINT64_MAX - digit) /
                        machine->continuation_radix) {
                    ppproof_relational_machine_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof index overflows");
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
                    goto done;
                }
                accumulator = machine->continuation_radix * accumulator +
                              digit;
            } else if (byte == machine->save_byte) {
                if (accumulator != 0u) {
                    ppproof_relational_machine_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof save interrupts an open index");
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
                    goto done;
                }
                if (stack_len == 0u) {
                    ppproof_relational_machine_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof cannot save an empty stack");
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
                    goto done;
                }
                if (!ppproof_relational_machine_v1_heap_push(
                        &heap, &heap_len, &heap_cap,
                        (PPProofRelationalMachineHeapEntryV1){
                            .kind =
                                PPPROOF_RELATIONAL_MACHINE_HEAP_V1_SAVED,
                            .saved = stack[stack_len - 1u],
                        },
                        effective_limits->maximum_article_nodes)) {
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                    goto done;
                }
            } else if (byte == machine->unknown_byte) {
                accumulator = 0u;
                if (machine->unknown_policy !=
                    PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM) {
                    ppproof_relational_machine_v1_set_error(
                        error_buf, error_buf_size,
                        "generated proof policy rejects incomplete proofs");
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
                    goto done;
                }
                result = ppproof_relational_machine_v1_push_incomplete_claim(
                    &context, &producer, claim,
                    &stack, &stack_len, &stack_cap,
                    error_buf, error_buf_size);
                if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
                    goto done;
                if (decoded_step_len == UINT32_MAX) {
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                    goto done;
                }
                decoded_step_len++;
                incomplete = true;
            } else {
                ppproof_relational_machine_v1_set_error(
                    error_buf, error_buf_size,
                    accumulator != 0u
                        ? "compressed proof byte interrupts an open index"
                        : "compressed proof byte is outside the generated decoder");
                result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
                goto done;
            }
        }
    }
    if (accumulator != 0u) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof ends inside an index");
        result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
        goto done;
    }
    receipt.proof_step_len = decoded_step_len;
    if (incomplete)
        result = ppproof_relational_machine_v1_finish_incomplete(
            &context, &producer, &receipt, error_buf, error_buf_size);
    else
        result = ppproof_relational_machine_v1_finish(
            proof_plan, &context, &producer, stack, stack_len, input->label,
            claim, effective_limits, &receipt, error_buf, error_buf_size);

done:
    if (result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
        error_buf && error_buf_size != 0u && error_buf[0] == '\0') {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational compressed proof %.*s "
            "(claim %u, header %u, code %u)",
            input && input->label.bytes
                ? (int)(input->label.len > 120u ? 120u : input->label.len)
                : 0,
            input && input->label.bytes
                ? (const char *)input->label.bytes : "",
            input ? input->claim_len : 0u,
            input ? input->header_len : 0u,
            input ? input->code_len : 0u);
    }
    if (result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK ||
        result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE)
        *receipt_out = receipt;
    free(heap);
    free(stack);
    ppproof_gslt_sequence_evidence_producer_v1_free(&producer);
    ppproof_gslt_relational_context_v1_free(&context);
    return result;
}
