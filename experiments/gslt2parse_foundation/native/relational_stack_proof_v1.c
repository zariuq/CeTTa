#include "relational_stack_proof_v1.h"

#include "gslt_indexed_instruction_decoder_v1.h"
#include "gslt_indexed_value_table_v1.h"
#include "gslt_literal_hole_program_v1.h"
#include "gslt_dense_bitset_v1.h"
#include "gslt_epoch_slots_v1.h"
#include "gslt_u32_index_v1.h"
#include "gslt_u32_slice_arena_v1.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PPRELATIONAL_STACK_PROOF_V1_INITIAL_CAPACITY = 16u };

typedef struct {
    uint32_t *items;
    uint32_t len;
    uint32_t cap;
} PPProofV1Vector;

typedef struct {
    uint32_t label;
    uint32_t kind;
    uint32_t variable;
    uint32_t formula;
} PPProofV1Hypothesis;

typedef struct {
    PPProofV1Hypothesis *items;
    uint32_t len;
    uint32_t cap;
    PPProofV1Vector mandatory;
} PPProofV1Frame;

typedef struct {
    uint32_t variable;
    PPProofV1Vector image;
} PPProofV1Substitution;

typedef struct {
    PPProofV1Substitution *items;
    uint32_t len;
    uint32_t cap;
} PPProofV1Substitutions;

typedef CettaGsltLiteralHeadProgramV1 PPProofV1Template;

typedef struct {
    uint32_t stack_offset;
    uint32_t slot;
    uint32_t type_head;
} PPProofV1BindInstruction;

typedef struct {
    uint32_t stack_offset;
    PPProofV1Template pattern;
} PPProofV1MatchInstruction;

typedef struct {
    uint32_t left_slot;
    uint32_t right_slot;
} PPProofV1DenseDisjoint;

typedef struct {
    uint32_t assertion;
    PPProofV1BindInstruction *binds;
    uint32_t bind_len;
    PPProofV1MatchInstruction *matches;
    uint32_t match_len;
    uint32_t instruction_len;
    uint32_t slot_len;
    PPProofV1DenseDisjoint *disjoints;
    uint32_t disjoint_len;
    PPProofV1Template conclusion;
    uint64_t template_cell_len;
    uint64_t template_part_len;
    uint64_t template_head_len;
} PPProofV1CompiledFrame;

typedef struct {
    uint32_t label;
    bool source_ready;
    bool compiled_ready;
    PPProofV1Frame source;
    PPProofV1CompiledFrame frame;
} PPProofV1FrameCacheEntry;

typedef CettaGsltU32SliceV1 PPProofV1Formula;
typedef CettaGsltU32SliceArenaV1 PPProofV1FormulaArena;

typedef struct {
    PPProofV1Formula *items;
    uint32_t len;
    uint32_t cap;
} PPProofV1FormulaStack;

typedef struct {
    CettaGsltEpochSlotsV1 slots;
    CettaGsltDenseBitsetV1 *supports;
    uint64_t *support_epochs;
    uint32_t support_capacity;
    uint32_t support_width;
    uint64_t support_compute_len;
    uint64_t support_hit_len;
} PPProofV1DenseSubstitutions;

typedef enum {
    PPPROOF_V1_HEAP_FORMULA = 0,
    PPPROOF_V1_HEAP_RULE = 1
} PPProofV1HeapKind;

typedef CettaGsltIndexedValueV1 PPProofV1HeapEntry;
typedef CettaGsltIndexedValueTableV1 PPProofV1Heap;

typedef struct {
    uint32_t *keys;
    uint32_t key_len;
    uint32_t key_cap;
    CettaGsltDenseBitsetV1 *apartness;
    uint32_t matrix_width;
    bool ready;
} PPProofV1FiniteSupportContext;

typedef struct {
    PPRelationalStoreV1 store;
    PPRelationalStackProofV1Machine machine;
    PPRelationalStackProofV1CacheAdmission admission;
    PPProofV1FrameCacheEntry *entries;
    CettaGsltU32IndexV1 entry_index;
    uint32_t entry_len;
    uint32_t entry_cap;
    uint64_t hit_len;
    uint64_t miss_len;
    uint64_t retained_source_len;
    uint64_t source_reuse_len;
    uint64_t template_cell_len;
    uint64_t template_part_len;
    uint64_t template_head_len;
    PPProofV1FormulaArena scratch_arena;
    PPProofV1FormulaStack scratch_stack;
    PPProofV1Heap scratch_heap;
    PPProofV1DenseSubstitutions scratch_substitutions;
    PPProofV1FiniteSupportContext scratch_support;
    uint64_t scratch_acquire_len;
    uint64_t scratch_reuse_len;
    bool scratch_active;
} PPProofV1FrameCacheImpl;

static void ppproof_v1_finite_support_free(
    PPProofV1FiniteSupportContext *support);

static void ppproof_v1_set_error(char *buf, size_t size,
                                 const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppproof_v1_array_fits(uint32_t count, size_t item_size) {
    return item_size == 0u || (size_t)count <= SIZE_MAX / item_size;
}

static bool ppproof_v1_grow(void **items, uint32_t *capacity,
                            uint32_t needed, size_t item_size) {
    uint32_t next;
    void *grown;

    if (needed <= *capacity)
        return true;
    next = *capacity ? *capacity
                     : PPRELATIONAL_STACK_PROOF_V1_INITIAL_CAPACITY;
    while (next < needed) {
        if (next > UINT32_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (!ppproof_v1_array_fits(next, item_size))
        return false;
    grown = realloc(*items, (size_t)next * item_size);
    if (!grown)
        return false;
    *items = grown;
    *capacity = next;
    return true;
}

static bool ppproof_v1_vector_push(PPProofV1Vector *vector,
                                   uint32_t value) {
    if (!vector || vector->len == UINT32_MAX ||
        !ppproof_v1_grow((void **)&vector->items, &vector->cap,
                         vector->len + 1u, sizeof(*vector->items)))
        return false;
    vector->items[vector->len++] = value;
    return true;
}

static bool ppproof_v1_vector_contains(const PPProofV1Vector *vector,
                                       uint32_t value) {
    uint32_t index;

    for (index = 0u; vector && index < vector->len; index++) {
        if (vector->items[index] == value)
            return true;
    }
    return false;
}

static void ppproof_v1_vector_free(PPProofV1Vector *vector) {
    if (!vector)
        return;
    free(vector->items);
    memset(vector, 0, sizeof(*vector));
}

static void ppproof_v1_frame_free(PPProofV1Frame *frame) {
    if (!frame)
        return;
    free(frame->items);
    ppproof_v1_vector_free(&frame->mandatory);
    memset(frame, 0, sizeof(*frame));
}

static void ppproof_v1_template_free(PPProofV1Template *template) {
    cetta_gslt_literal_head_program_free_v1(template);
}

static void ppproof_v1_compiled_frame_free(
    PPProofV1CompiledFrame *frame) {
    uint32_t index;

    if (!frame)
        return;
    for (index = 0u; index < frame->match_len; index++)
        ppproof_v1_template_free(&frame->matches[index].pattern);
    ppproof_v1_template_free(&frame->conclusion);
    free(frame->binds);
    free(frame->matches);
    free(frame->disjoints);
    memset(frame, 0, sizeof(*frame));
}

static void ppproof_v1_substitutions_free(
    PPProofV1Substitutions *substitutions) {
    uint32_t index;

    if (!substitutions)
        return;
    for (index = 0u; index < substitutions->len; index++)
        ppproof_v1_vector_free(&substitutions->items[index].image);
    free(substitutions->items);
    memset(substitutions, 0, sizeof(*substitutions));
}

static void ppproof_v1_dense_substitutions_free(
    PPProofV1DenseSubstitutions *substitutions) {
    uint32_t index;

    if (!substitutions)
        return;
    for (index = 0u; index < substitutions->support_capacity; index++)
        cetta_gslt_dense_bitset_free_v1(&substitutions->supports[index]);
    free(substitutions->supports);
    free(substitutions->support_epochs);
    cetta_gslt_epoch_slots_free_v1(&substitutions->slots);
    memset(substitutions, 0, sizeof(*substitutions));
}

static bool ppproof_v1_dense_substitutions_prepare(
    PPProofV1DenseSubstitutions *substitutions, uint32_t slot_len,
    uint32_t support_width) {
    CettaGsltDenseBitsetV1 *supports = NULL;
    uint64_t *support_epochs = NULL;
    uint64_t previous_epoch;
    uint32_t support_capacity;
    uint32_t initialized = 0u;
    uint32_t index;

    if (!substitutions)
        return false;
    previous_epoch = substitutions->slots.epoch;
    if (!cetta_gslt_epoch_slots_prepare_v1(
            &substitutions->slots, slot_len,
            sizeof(PPProofV1Formula)))
        return false;
    if (support_width == 0u)
        return true;
    if (substitutions->support_width == support_width &&
        substitutions->support_capacity >= substitutions->slots.capacity) {
        if (substitutions->slots.epoch <= previous_epoch &&
            substitutions->support_capacity > 0u)
            memset(substitutions->support_epochs, 0,
                   (size_t)substitutions->support_capacity *
                       sizeof(*substitutions->support_epochs));
        return true;
    }
    support_capacity = substitutions->slots.capacity;
    if (!ppproof_v1_array_fits(support_capacity, sizeof(*supports)) ||
        !ppproof_v1_array_fits(support_capacity,
                               sizeof(*support_epochs)))
        return false;
    if (support_capacity > 0u) {
        supports = calloc(support_capacity, sizeof(*supports));
        support_epochs = calloc(
            support_capacity, sizeof(*support_epochs));
        if (!supports || !support_epochs)
            goto failed;
    }
    for (initialized = 0u; initialized < support_capacity; initialized++) {
        if (!cetta_gslt_dense_bitset_init_v1(
                &supports[initialized], support_width))
            goto failed;
    }
    for (index = 0u; index < substitutions->support_capacity; index++)
        cetta_gslt_dense_bitset_free_v1(&substitutions->supports[index]);
    free(substitutions->supports);
    free(substitutions->support_epochs);
    substitutions->supports = supports;
    substitutions->support_epochs = support_epochs;
    substitutions->support_capacity = support_capacity;
    substitutions->support_width = support_width;
    return true;

failed:
    for (index = 0u; index < initialized; index++)
        cetta_gslt_dense_bitset_free_v1(&supports[index]);
    free(supports);
    free(support_epochs);
    return false;
}

static const PPProofV1Formula *ppproof_v1_dense_substitution_get_const(
    const PPProofV1DenseSubstitutions *substitutions, uint32_t slot) {
    return substitutions
        ? cetta_gslt_epoch_slots_get_const_v1(&substitutions->slots, slot)
        : NULL;
}

static PPProofV1Formula *ppproof_v1_dense_substitution_set(
    PPProofV1DenseSubstitutions *substitutions, uint32_t slot) {
    return substitutions
        ? cetta_gslt_epoch_slots_set_v1(&substitutions->slots, slot)
        : NULL;
}

static void ppproof_v1_formula_arena_free(
    PPProofV1FormulaArena *arena) {
    cetta_gslt_u32_slice_arena_free_v1(arena);
}

static bool ppproof_v1_formula_arena_reserve(
    PPProofV1FormulaArena *arena, uint32_t additional,
    uint32_t *offset_out) {
    CettaGsltU32SliceV1 slice;

    if (!offset_out || !cetta_gslt_u32_slice_arena_reserve_v1(
                           arena, additional, &slice))
        return false;
    *offset_out = slice.offset;
    return true;
}

static const uint32_t *ppproof_v1_formula_items(
    const PPProofV1FormulaArena *arena,
    PPProofV1Formula formula) {
    return cetta_gslt_u32_slice_arena_items_v1(arena, formula);
}

static bool ppproof_v1_formula_equal(
    const PPProofV1FormulaArena *arena,
    PPProofV1Formula left, PPProofV1Formula right) {
    return cetta_gslt_u32_slice_arena_equal_v1(arena, left, right);
}

static void ppproof_v1_formula_stack_free(
    PPProofV1FormulaStack *stack) {
    if (!stack)
        return;
    free(stack->items);
    memset(stack, 0, sizeof(*stack));
}

static bool ppproof_v1_formula_stack_push(
    PPProofV1FormulaStack *stack, PPProofV1Formula formula) {
    if (!stack || formula.len == 0u || stack->len == UINT32_MAX ||
        !ppproof_v1_grow((void **)&stack->items, &stack->cap,
                         stack->len + 1u, sizeof(*stack->items)))
        return false;
    stack->items[stack->len++] = formula;
    return true;
}

static void ppproof_v1_heap_free(PPProofV1Heap *heap) {
    cetta_gslt_indexed_value_table_free_v1(heap);
}

static bool ppproof_v1_scratch_acquire(
    PPProofV1FrameCacheImpl *cache,
    PPProofV1FormulaArena *arena,
    PPProofV1FormulaStack *stack,
    PPProofV1Heap *heap,
    PPProofV1DenseSubstitutions *substitutions) {
    bool reused;

    if (!cache || !arena || !stack || !heap || !substitutions ||
        !cache->admission.scratch_reuse_admitted || cache->scratch_active)
        return false;
    reused = cache->scratch_arena.cap != 0u ||
             cache->scratch_stack.cap != 0u ||
             cache->scratch_heap.cap != 0u ||
             cache->scratch_substitutions.slots.capacity != 0u;
    *arena = cache->scratch_arena;
    *stack = cache->scratch_stack;
    *heap = cache->scratch_heap;
    *substitutions = cache->scratch_substitutions;
    memset(&cache->scratch_arena, 0, sizeof(cache->scratch_arena));
    memset(&cache->scratch_stack, 0, sizeof(cache->scratch_stack));
    memset(&cache->scratch_heap, 0, sizeof(cache->scratch_heap));
    memset(&cache->scratch_substitutions, 0,
           sizeof(cache->scratch_substitutions));
    arena->len = 0u;
    stack->len = 0u;
    heap->len = 0u;
    cache->scratch_active = true;
    if (cache->scratch_acquire_len < UINT64_MAX)
        cache->scratch_acquire_len++;
    if (reused && cache->scratch_reuse_len < UINT64_MAX)
        cache->scratch_reuse_len++;
    return true;
}

static void ppproof_v1_scratch_release(
    PPProofV1FrameCacheImpl *cache,
    PPProofV1FormulaArena *arena,
    PPProofV1FormulaStack *stack,
    PPProofV1Heap *heap,
    PPProofV1DenseSubstitutions *substitutions) {
    if (!cache || !cache->scratch_active || !arena || !stack || !heap ||
        !substitutions)
        return;
    cache->scratch_arena = *arena;
    cache->scratch_stack = *stack;
    cache->scratch_heap = *heap;
    cache->scratch_substitutions = *substitutions;
    memset(arena, 0, sizeof(*arena));
    memset(stack, 0, sizeof(*stack));
    memset(heap, 0, sizeof(*heap));
    memset(substitutions, 0, sizeof(*substitutions));
    cache->scratch_active = false;
}

static bool ppproof_v1_heap_push_formula(
    PPProofV1Heap *heap, PPProofV1Formula formula) {
    return formula.len != 0u && cetta_gslt_indexed_value_table_push_v1(
        heap, (PPProofV1HeapEntry){
            .tag = PPPROOF_V1_HEAP_FORMULA,
            .first = formula.offset,
            .second = formula.len,
        });
}

static bool ppproof_v1_heap_push_rule(
    PPProofV1Heap *heap, uint32_t rule) {
    return cetta_gslt_indexed_value_table_push_v1(
        heap, (PPProofV1HeapEntry){
            .tag = PPPROOF_V1_HEAP_RULE,
            .first = rule,
            .second = 0u,
        });
}

static bool ppproof_v1_store_valid(
    const PPRelationalStoreV1 *store) {
    return store && store->table_shape && store->table_row &&
           store->table_find && store->value_intern &&
           store->value_bytes;
}

static bool ppproof_v1_table_shape(
    const PPRelationalStoreV1 *store, uint32_t table,
    uint32_t expected_arity, uint32_t expected_key,
    uint32_t *row_len_out) {
    uint32_t arity = 0u;
    uint32_t key = 0u;
    uint32_t rows = 0u;

    if (!store->table_shape(store->context, table, &arity, &key, &rows) ||
        arity != expected_arity || key != expected_key)
        return false;
    if (row_len_out)
        *row_len_out = rows;
    return true;
}

static bool ppproof_v1_literal_valid(
    const PPRelationalStoreV1 *store, uint32_t value) {
    const uint8_t *bytes = NULL;
    uint32_t len = 0u;
    return store->value_bytes(store->context, value, &bytes, &len) &&
           bytes && len > 0u;
}

static CettaGsltIndexedInstructionPlanV1 ppproof_v1_indexed_plan(
    const PPRelationalStackProofV1Machine *machine) {
    CettaGsltIndexedInstructionPlanV1 plan = {0};

    if (!machine)
        return plan;
    plan.terminal_low = machine->terminal_low;
    plan.terminal_high = machine->terminal_high;
    plan.continuation_low = machine->continuation_low;
    plan.continuation_high = machine->continuation_high;
    plan.save_byte = machine->save_byte;
    plan.unknown_byte = machine->unknown_byte;
    plan.terminal_radix = machine->terminal_radix;
    plan.terminal_digit_bias = machine->terminal_digit_bias;
    plan.continuation_radix = machine->continuation_radix;
    plan.continuation_digit_bias = machine->continuation_digit_bias;
    return plan;
}

bool pprelational_stack_proof_v1_machine_validate(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    char *error_buf,
    size_t error_buf_size) {
    const uint32_t literals[] = {
        machine ? machine->binder_hypothesis_kind : 0u,
        machine ? machine->matching_hypothesis_kind : 0u,
        machine ? machine->rule_kind_first : 0u,
        machine ? machine->rule_kind_second : 0u,
        machine ? machine->variable_symbol_kind : 0u,
        machine ? machine->unknown_token : 0u,
    };
    const CettaGsltIndexedInstructionPlanV1 indexed_plan =
        ppproof_v1_indexed_plan(machine);
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!ppproof_v1_store_valid(store) || !machine ||
        !ppproof_v1_table_shape(store, machine->label_kind_table,
                                2u, 1u, NULL) ||
        !ppproof_v1_table_shape(store, machine->formula_table,
                                2u, 1u, NULL) ||
        !ppproof_v1_table_shape(store, machine->binder_variable_table,
                                2u, 1u, NULL) ||
        !ppproof_v1_table_shape(store, machine->mandatory_variable_table,
                                2u, 2u, NULL) ||
        !ppproof_v1_table_shape(store, machine->assertion_hypothesis_table,
                                3u, 2u, NULL) ||
        !ppproof_v1_table_shape(store, machine->assertion_disjoint_table,
                                3u, 3u, NULL) ||
        !ppproof_v1_table_shape(store, machine->active_hypothesis_table,
                                4u, 1u, NULL) ||
        !ppproof_v1_table_shape(store, machine->active_disjoint_table,
                                2u, 2u, NULL) ||
        !ppproof_v1_table_shape(store, machine->symbol_kind_table,
                                2u, 1u, NULL)) {
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "invalid relational proof table schema");
        return false;
    }
    for (index = 0u; index < sizeof(literals) / sizeof(literals[0]);
         index++) {
        if (!ppproof_v1_literal_valid(store, literals[index])) {
            ppproof_v1_set_error(error_buf, error_buf_size,
                                 "invalid relational proof literal");
            return false;
        }
    }
    if (machine->binder_hypothesis_kind ==
            machine->matching_hypothesis_kind ||
        machine->rule_kind_first == machine->rule_kind_second ||
        !cetta_gslt_indexed_instruction_plan_validate_v1(&indexed_plan) ||
        machine->unknown_policy >
            PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM) {
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "invalid relational proof decoder");
        return false;
    }
    return true;
}

static bool ppproof_v1_digest_valid(const char digest[65]) {
    uint32_t index;

    if (!digest || digest[64] != '\0')
        return false;
    for (index = 0u; index < 64u; index++) {
        if (!((digest[index] >= '0' && digest[index] <= '9') ||
              (digest[index] >= 'a' && digest[index] <= 'f')))
            return false;
    }
    return true;
}

static bool ppproof_v1_admission_matches(
    const PPRelationalStackProofV1Machine *machine,
    const PPRelationalStackProofV1CacheAdmission *admission) {
    return machine && admission &&
           admission->label_kind_table == machine->label_kind_table &&
           admission->formula_table == machine->formula_table &&
           admission->binder_variable_table ==
               machine->binder_variable_table &&
           admission->mandatory_variable_table ==
               machine->mandatory_variable_table &&
           admission->assertion_hypothesis_table ==
               machine->assertion_hypothesis_table &&
           admission->assertion_disjoint_table ==
               machine->assertion_disjoint_table &&
           admission->symbol_kind_table == machine->symbol_kind_table &&
           admission->scratch_reuse_admitted &&
           ppproof_v1_digest_valid(admission->native_type_digest) &&
           ppproof_v1_digest_valid(admission->storage_plan_digest);
}

static bool ppproof_v1_store_equal(
    const PPRelationalStoreV1 *left,
    const PPRelationalStoreV1 *right) {
    return left && right && left->context == right->context &&
           left->table_shape == right->table_shape &&
           left->table_row == right->table_row &&
           left->table_find == right->table_find &&
           left->table_prefix_next == right->table_prefix_next &&
           left->value_intern == right->value_intern &&
           left->value_bytes == right->value_bytes;
}

static bool ppproof_v1_machine_equal(
    const PPRelationalStackProofV1Machine *left,
    const PPRelationalStackProofV1Machine *right) {
    return left && right &&
           left->label_kind_table == right->label_kind_table &&
           left->formula_table == right->formula_table &&
           left->binder_variable_table == right->binder_variable_table &&
           left->mandatory_variable_table ==
               right->mandatory_variable_table &&
           left->assertion_hypothesis_table ==
               right->assertion_hypothesis_table &&
           left->assertion_disjoint_table ==
               right->assertion_disjoint_table &&
           left->active_hypothesis_table ==
               right->active_hypothesis_table &&
           left->active_disjoint_table == right->active_disjoint_table &&
           left->symbol_kind_table == right->symbol_kind_table &&
           left->binder_hypothesis_kind ==
               right->binder_hypothesis_kind &&
           left->matching_hypothesis_kind ==
               right->matching_hypothesis_kind &&
           left->rule_kind_first == right->rule_kind_first &&
           left->rule_kind_second == right->rule_kind_second &&
           left->variable_symbol_kind == right->variable_symbol_kind &&
           left->unknown_token == right->unknown_token &&
           left->terminal_low == right->terminal_low &&
           left->terminal_high == right->terminal_high &&
           left->continuation_low == right->continuation_low &&
           left->continuation_high == right->continuation_high &&
           left->save_byte == right->save_byte &&
           left->unknown_byte == right->unknown_byte &&
           left->terminal_radix == right->terminal_radix &&
           left->terminal_digit_bias == right->terminal_digit_bias &&
           left->continuation_radix == right->continuation_radix &&
           left->continuation_digit_bias ==
               right->continuation_digit_bias &&
           left->unknown_policy == right->unknown_policy;
}

bool pprelational_stack_proof_v1_cache_init(
    PPRelationalStackProofV1Cache *cache,
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    const PPRelationalStackProofV1CacheAdmission *admission,
    char *error_buf,
    size_t error_buf_size) {
    PPProofV1FrameCacheImpl *impl;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!cache || cache->implementation ||
        !pprelational_stack_proof_v1_machine_validate(
            store, machine, error_buf, error_buf_size) ||
        !ppproof_v1_admission_matches(machine, admission)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0')
            ppproof_v1_set_error(
                error_buf, error_buf_size,
                "frame cache lacks a matching generated admission");
        return false;
    }
    impl = calloc(1u, sizeof(*impl));
    if (!impl) {
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "cannot allocate frame cache");
        return false;
    }
    impl->store = *store;
    impl->machine = *machine;
    impl->admission = *admission;
    cache->implementation = impl;
    return true;
}

void pprelational_stack_proof_v1_cache_free(
    PPRelationalStackProofV1Cache *cache) {
    PPProofV1FrameCacheImpl *impl;
    uint32_t index;

    if (!cache)
        return;
    impl = cache->implementation;
    if (impl) {
        for (index = 0u; index < impl->entry_len; index++) {
            ppproof_v1_frame_free(&impl->entries[index].source);
            ppproof_v1_compiled_frame_free(
                &impl->entries[index].frame);
        }
        ppproof_v1_formula_arena_free(&impl->scratch_arena);
        ppproof_v1_formula_stack_free(&impl->scratch_stack);
        ppproof_v1_heap_free(&impl->scratch_heap);
        ppproof_v1_dense_substitutions_free(
            &impl->scratch_substitutions);
        ppproof_v1_finite_support_free(&impl->scratch_support);
        cetta_gslt_u32_index_free_v1(&impl->entry_index);
        free(impl->entries);
        free(impl);
    }
    memset(cache, 0, sizeof(*cache));
}

bool pprelational_stack_proof_v1_cache_stats(
    const PPRelationalStackProofV1Cache *cache,
    PPRelationalStackProofV1CacheStats *stats_out) {
    const PPProofV1FrameCacheImpl *impl;

    if (!cache || !(impl = cache->implementation) || !stats_out)
        return false;
    *stats_out = (PPRelationalStackProofV1CacheStats){
        .hit_len = impl->hit_len,
        .miss_len = impl->miss_len,
        .retained_source_len = impl->retained_source_len,
        .source_reuse_len = impl->source_reuse_len,
        .template_cell_len = impl->template_cell_len,
        .template_part_len = impl->template_part_len,
        .template_head_len = impl->template_head_len,
        .scratch_acquire_len = impl->scratch_acquire_len,
        .scratch_reuse_len = impl->scratch_reuse_len,
        .entry_len = impl->entry_len,
        .lookup_index_capacity = impl->entry_index.cap,
        .scratch_formula_capacity = impl->scratch_arena.cap,
        .scratch_stack_capacity = impl->scratch_stack.cap,
        .scratch_heap_capacity = impl->scratch_heap.cap,
        .scratch_substitution_capacity =
            impl->scratch_substitutions.slots.capacity,
        .scratch_support_width = impl->scratch_support.matrix_width,
        .scratch_support_slot_capacity =
            impl->scratch_substitutions.support_capacity,
        .scratch_support_compute_len =
            impl->scratch_substitutions.support_compute_len,
        .scratch_support_hit_len =
            impl->scratch_substitutions.support_hit_len,
    };
    return true;
}

static PPRelationalStackProofV1Result ppproof_v1_intern_slice(
    const PPRelationalStoreV1 *store,
    PPRelationalValueV1Slice slice, uint32_t *value_out) {
    if (!slice.bytes || slice.len == 0u)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    return store->value_intern(
               store->context, slice.bytes, slice.len, value_out)
               ? PPRELATIONAL_STACK_PROOF_V1_OK
               : PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
}

static PPRelationalStackProofV1Result ppproof_v1_formula_from_slices(
    const PPRelationalStoreV1 *store,
    const PPRelationalValueV1Slice *items, uint32_t item_len,
    PPProofV1FormulaArena *arena, PPProofV1Formula *formula_out) {
    uint32_t mark;
    uint32_t offset;
    uint32_t index;

    if (!items || item_len == 0u || !arena || !formula_out)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    mark = arena->len;
    if (!ppproof_v1_formula_arena_reserve(arena, item_len, &offset))
        return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    for (index = 0u; index < item_len; index++) {
        if (!items[index].bytes || items[index].len == 0u) {
            arena->len = mark;
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
        if (!store->value_intern(
                store->context, items[index].bytes, items[index].len,
                &arena->items[offset + index])) {
            arena->len = mark;
            return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
        }
    }
    *formula_out = (PPProofV1Formula){offset, item_len};
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result ppproof_v1_formula_decode(
    const PPRelationalStoreV1 *store, uint32_t formula,
    PPProofV1FormulaArena *arena, PPProofV1Formula *formula_out) {
    const uint8_t *bytes = NULL;
    uint32_t byte_len = 0u;
    PPRelationalValueListV1Cursor cursor;
    uint32_t mark;
    uint32_t offset;
    uint32_t index;

    if (!arena || !formula_out || !store->value_bytes(
                    store->context, formula, &bytes, &byte_len) ||
        !pprelational_value_list_v1_cursor_init(bytes, byte_len, &cursor) ||
        cursor.item_len == 0u)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    mark = arena->len;
    if (!ppproof_v1_formula_arena_reserve(
            arena, cursor.item_len, &offset))
        return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    for (index = 0u; index < cursor.item_len; index++) {
        const uint8_t *item_bytes = NULL;
        uint32_t item_len = 0u;
        if (!pprelational_value_list_v1_cursor_next(
                &cursor, &item_bytes, &item_len)) {
            arena->len = mark;
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
        if (!store->value_intern(
                store->context, item_bytes, item_len,
                &arena->items[offset + index])) {
            arena->len = mark;
            return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
        }
    }
    *formula_out = (PPProofV1Formula){offset, cursor.item_len};
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result ppproof_v1_formula_from_vector(
    PPProofV1FormulaArena *arena, const PPProofV1Vector *source,
    PPProofV1Formula *formula_out) {
    uint32_t offset;

    if (!arena || !source || source->len == 0u || !formula_out)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    if (!ppproof_v1_formula_arena_reserve(arena, source->len, &offset))
        return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    memcpy(&arena->items[offset], source->items,
           (size_t)source->len * sizeof(*source->items));
    *formula_out = (PPProofV1Formula){offset, source->len};
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static bool ppproof_v1_find_row(
    const PPRelationalStoreV1 *store, uint32_t table,
    const uint32_t *key, uint32_t key_len, uint32_t *row,
    uint32_t row_capacity) {
    return store->table_find(
        store->context, table, key, key_len, row, row_capacity);
}

typedef struct {
    uint32_t table;
    uint32_t prefix;
    uint32_t arity;
    uint32_t row_len;
    uint32_t scan_index;
    uint32_t column_mask;
    uint64_t cursor;
    bool indexed;
} PPProofV1PrefixIterator;

static bool ppproof_v1_prefix_iterator_init(
    const PPRelationalStoreV1 *store, uint32_t table,
    uint32_t expected_arity, uint32_t expected_key, uint32_t prefix,
    uint32_t column_mask,
    PPProofV1PrefixIterator *iterator) {
    uint32_t row_len = 0u;

    if (!iterator || !ppproof_v1_table_shape(
                         store, table, expected_arity, expected_key,
                         &row_len) ||
        column_mask == 0u || expected_arity > 32u ||
        (expected_arity < 32u &&
         (column_mask >> expected_arity) != 0u))
        return false;
    *iterator = (PPProofV1PrefixIterator){
        .table = table,
        .prefix = prefix,
        .arity = expected_arity,
        .row_len = row_len,
        .column_mask = column_mask,
        .cursor = UINT64_MAX,
        .indexed = store->table_prefix_next != NULL,
    };
    return true;
}

static PPRelationalStackProofV1Result ppproof_v1_prefix_iterator_next(
    const PPRelationalStoreV1 *store,
    PPProofV1PrefixIterator *iterator, uint32_t *row,
    uint32_t row_capacity, bool *found_out) {
    if (!store || !iterator || !row || !found_out ||
        row_capacity < iterator->arity)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    *found_out = false;
    if (iterator->indexed) {
        if (!store->table_prefix_next(
                store->context, iterator->table, &iterator->prefix, 1u,
                iterator->column_mask, &iterator->cursor,
                row, row_capacity, found_out))
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        return PPRELATIONAL_STACK_PROOF_V1_OK;
    }
    while (iterator->scan_index < iterator->row_len) {
        uint32_t index = iterator->scan_index++;
        if (!store->table_row(store->context, iterator->table, index,
                              row, row_capacity))
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        if (row[0] == iterator->prefix) {
            uint32_t column;
            for (column = 0u; column < iterator->arity; column++) {
                if ((iterator->column_mask &
                     (UINT32_C(1) << column)) == 0u)
                    row[column] = 0u;
            }
            *found_out = true;
            break;
        }
    }
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static bool ppproof_v1_lookup_kind(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine, uint32_t label,
    uint32_t *kind_out) {
    uint32_t row[2];
    if (!ppproof_v1_find_row(
            store, machine->label_kind_table, &label, 1u, row, 2u))
        return false;
    *kind_out = row[1];
    return true;
}

static bool ppproof_v1_lookup_formula(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine, uint32_t label,
    uint32_t *formula_out) {
    uint32_t row[2];
    if (!ppproof_v1_find_row(
            store, machine->formula_table, &label, 1u, row, 2u))
        return false;
    *formula_out = row[1];
    return true;
}

static bool ppproof_v1_lookup_binder_variable(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine, uint32_t label,
    uint32_t *variable_out) {
    uint32_t row[2];
    if (!ppproof_v1_find_row(
            store, machine->binder_variable_table,
            &label, 1u, row, 2u))
        return false;
    *variable_out = row[1];
    return true;
}

static bool ppproof_v1_hypothesis_active(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine, uint32_t label) {
    uint32_t row[4];
    return ppproof_v1_find_row(
        store, machine->active_hypothesis_table,
        &label, 1u, row, 4u);
}

static bool ppproof_v1_is_rule(
    const PPRelationalStackProofV1Machine *machine, uint32_t kind) {
    return kind == machine->rule_kind_first ||
           kind == machine->rule_kind_second;
}

static PPRelationalStackProofV1Result ppproof_v1_frame_push(
    PPProofV1Frame *frame, PPProofV1Hypothesis hypothesis) {
    if (frame->len == UINT32_MAX ||
        !ppproof_v1_grow((void **)&frame->items, &frame->cap,
                         frame->len + 1u, sizeof(*frame->items)))
        return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    frame->items[frame->len++] = hypothesis;
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result ppproof_v1_build_frame(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    uint32_t assertion, PPProofV1Frame *frame,
    char *error_buf, size_t error_buf_size) {
    uint32_t index;
    PPProofV1PrefixIterator iterator;
    PPRelationalStackProofV1Result result;

    if (!ppproof_v1_prefix_iterator_init(
            store, machine->mandatory_variable_table, 2u, 2u,
            assertion, UINT32_C(1) << 1u, &iterator))
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    for (;;) {
        uint32_t row[2];
        bool found;
        result = ppproof_v1_prefix_iterator_next(
            store, &iterator, row, 2u, &found);
        if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
            return result;
        if (!found)
            break;
        if (!ppproof_v1_vector_contains(&frame->mandatory, row[1]) &&
            !ppproof_v1_vector_push(&frame->mandatory, row[1]))
            return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    }
    if (!ppproof_v1_prefix_iterator_init(
            store, machine->assertion_hypothesis_table, 3u, 2u,
            assertion, UINT32_C(1) << 2u, &iterator))
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    for (;;) {
        uint32_t row[3];
        uint32_t kind;
        PPProofV1Hypothesis hypothesis;
        bool found;

        result = ppproof_v1_prefix_iterator_next(
            store, &iterator, row, 3u, &found);
        if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
            return result;
        if (!found)
            break;
        memset(&hypothesis, 0, sizeof(hypothesis));
        hypothesis.label = row[2];
        hypothesis.variable = UINT32_MAX;
        if (!ppproof_v1_lookup_kind(store, machine, row[2], &kind) ||
            !ppproof_v1_lookup_formula(
                store, machine, row[2], &hypothesis.formula)) {
            ppproof_v1_set_error(error_buf, error_buf_size,
                                 "assertion frame references an unknown hypothesis");
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
        hypothesis.kind = kind;
        if (kind == machine->binder_hypothesis_kind) {
            if (!ppproof_v1_lookup_binder_variable(
                    store, machine, hypothesis.label,
                    &hypothesis.variable)) {
                ppproof_v1_set_error(
                    error_buf, error_buf_size,
                    "assertion binder lacks a declared variable");
                return PPRELATIONAL_STACK_PROOF_V1_INVALID;
            }
            if (!ppproof_v1_vector_contains(
                    &frame->mandatory, hypothesis.variable))
                continue;
        } else if (kind != machine->matching_hypothesis_kind) {
            ppproof_v1_set_error(error_buf, error_buf_size,
                                 "assertion frame contains a non-hypothesis");
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
        result = ppproof_v1_frame_push(frame, hypothesis);
        if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
            return result;
    }
    for (index = 0u; index < frame->mandatory.len; index++) {
        uint32_t hypothesis_index;
        uint32_t count = 0u;
        for (hypothesis_index = 0u;
             hypothesis_index < frame->len; hypothesis_index++) {
            if (frame->items[hypothesis_index].kind ==
                    machine->binder_hypothesis_kind &&
                frame->items[hypothesis_index].variable ==
                    frame->mandatory.items[index])
                count++;
        }
        if (count != 1u) {
            ppproof_v1_set_error(error_buf, error_buf_size,
                                 "mandatory variable lacks a unique binder");
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
    }
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static bool ppproof_v1_vector_index(
    const PPProofV1Vector *vector, uint32_t value,
    uint32_t *index_out) {
    uint32_t index;

    for (index = 0u; vector && index < vector->len; index++) {
        if (vector->items[index] == value) {
            if (index_out)
                *index_out = index;
            return true;
        }
    }
    return false;
}

static PPRelationalStackProofV1Result ppproof_v1_template_compile(
    const PPRelationalStoreV1 *store, uint32_t formula,
    const PPProofV1Vector *slots, bool coalesce_literals,
    PPProofV1Template *template_out) {
    PPProofV1FormulaArena arena = {0};
    PPProofV1Formula decoded = {0};
    const uint32_t *items;
    PPProofV1Template result = {0};
    PPRelationalStackProofV1Result status;
    uint32_t index;

    if (!template_out)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    status = ppproof_v1_formula_decode(
        store, formula, &arena, &decoded);
    if (status != PPRELATIONAL_STACK_PROOF_V1_OK)
        goto done;
    items = ppproof_v1_formula_items(&arena, decoded);
    if (!items) {
        status = PPRELATIONAL_STACK_PROOF_V1_INVALID;
        goto done;
    }
    status = PPRELATIONAL_STACK_PROOF_V1_OK;
    for (index = 0u; index < decoded.len; index++) {
        uint32_t slot = 0u;
        if (ppproof_v1_vector_index(slots, items[index], &slot)) {
            if (!cetta_gslt_literal_head_program_append_hole_v1(
                    &result, slot)) {
                status = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
                goto done;
            }
        } else {
            if (index == 0u && coalesce_literals) {
                if (!cetta_gslt_literal_head_program_set_head_v1(
                        &result, items[index])) {
                    status = PPRELATIONAL_STACK_PROOF_V1_INVALID;
                    goto done;
                }
            } else if (!cetta_gslt_literal_head_program_append_literal_v1(
                           &result, items[index], coalesce_literals)) {
                status = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
                goto done;
            }
        }
    }
    if (!cetta_gslt_literal_head_program_validate_v1(&result)) {
        status = PPRELATIONAL_STACK_PROOF_V1_INVALID;
        goto done;
    }
    *template_out = result;
    memset(&result, 0, sizeof(result));

done:
    ppproof_v1_template_free(&result);
    ppproof_v1_formula_arena_free(&arena);
    return status;
}

static PPRelationalStackProofV1Result
ppproof_v1_compiled_frame_disjoints(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    uint32_t assertion, const PPProofV1Vector *slots,
    PPProofV1CompiledFrame *compiled) {
    PPProofV1PrefixIterator iterator;
    uint32_t capacity = 0u;
    PPRelationalStackProofV1Result result;

    if (!ppproof_v1_prefix_iterator_init(
            store, machine->assertion_disjoint_table, 3u, 3u,
            assertion, (UINT32_C(1) << 1u) | (UINT32_C(1) << 2u),
            &iterator))
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    for (;;) {
        uint32_t row[3];
        uint32_t left_slot;
        uint32_t right_slot;
        bool found;

        result = ppproof_v1_prefix_iterator_next(
            store, &iterator, row, 3u, &found);
        if (result != PPRELATIONAL_STACK_PROOF_V1_OK || !found)
            return result;
        if (!ppproof_v1_vector_index(slots, row[1], &left_slot) ||
            !ppproof_v1_vector_index(slots, row[2], &right_slot))
            continue;
        if (compiled->disjoint_len == UINT32_MAX ||
            !ppproof_v1_grow(
                (void **)&compiled->disjoints, &capacity,
                compiled->disjoint_len + 1u,
                sizeof(*compiled->disjoints)))
            return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
        compiled->disjoints[compiled->disjoint_len++] =
            (PPProofV1DenseDisjoint){left_slot, right_slot};
    }
}

static PPRelationalStackProofV1Result
ppproof_v1_compile_frame_from_source(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    uint32_t assertion, const PPProofV1Frame *source,
    bool coalesce_literals,
    PPProofV1CompiledFrame *compiled_out,
    char *error_buf, size_t error_buf_size) {
    PPProofV1CompiledFrame compiled = {0};
    PPProofV1FormulaArena decode_arena = {0};
    uint8_t *slot_seen = NULL;
    PPRelationalStackProofV1Result result;
    uint32_t conclusion;
    uint32_t bind_write = 0u;
    uint32_t match_write = 0u;
    uint32_t index;

    if (!source || !compiled_out)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    compiled.assertion = assertion;
    compiled.instruction_len = source->len;
    compiled.slot_len = source->mandatory.len;
    for (index = 0u; index < source->len; index++) {
        const PPProofV1Hypothesis *hypothesis = &source->items[index];
        if (hypothesis->kind == machine->binder_hypothesis_kind) {
            if (compiled.bind_len == UINT32_MAX) {
                result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
                goto done;
            }
            compiled.bind_len++;
        } else if (hypothesis->kind ==
                   machine->matching_hypothesis_kind) {
            if (compiled.match_len == UINT32_MAX) {
                result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
                goto done;
            }
            compiled.match_len++;
        } else {
            result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
            goto done;
        }
    }
    if (compiled.bind_len != compiled.slot_len ||
        !ppproof_v1_array_fits(
            compiled.bind_len, sizeof(*compiled.binds)) ||
        !ppproof_v1_array_fits(
            compiled.match_len, sizeof(*compiled.matches)) ||
        !ppproof_v1_array_fits(compiled.slot_len, sizeof(*slot_seen))) {
        result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
        goto done;
    }
    if (compiled.bind_len != 0u &&
        !(compiled.binds = calloc(
              compiled.bind_len, sizeof(*compiled.binds)))) {
        result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
        goto done;
    }
    if (compiled.match_len != 0u &&
        !(compiled.matches = calloc(
              compiled.match_len, sizeof(*compiled.matches)))) {
        result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
        goto done;
    }
    if (compiled.slot_len != 0u &&
        !(slot_seen = calloc(compiled.slot_len, sizeof(*slot_seen)))) {
        result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
        goto done;
    }
    for (index = 0u; index < source->len; index++) {
        const PPProofV1Hypothesis *hypothesis = &source->items[index];

        if (hypothesis->kind == machine->binder_hypothesis_kind) {
            PPProofV1BindInstruction *instruction =
                &compiled.binds[bind_write++];
            PPProofV1Formula formula = {0};
            const uint32_t *formula_items;

            instruction->stack_offset = index;
            if (!ppproof_v1_vector_index(
                    &source->mandatory, hypothesis->variable,
                    &instruction->slot) ||
                instruction->slot >= compiled.slot_len ||
                slot_seen[instruction->slot]) {
                result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
                goto done;
            }
            slot_seen[instruction->slot] = 1u;
            result = ppproof_v1_formula_decode(
                store, hypothesis->formula, &decode_arena, &formula);
            if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
                goto done;
            formula_items = ppproof_v1_formula_items(
                &decode_arena, formula);
            if (formula.len != 2u || !formula_items ||
                formula_items[1] != hypothesis->variable) {
                ppproof_v1_set_error(
                    error_buf, error_buf_size,
                    "proof binder lacks a first-order type shape");
                result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
                goto done;
            }
            instruction->type_head = formula_items[0];
        } else if (hypothesis->kind ==
                   machine->matching_hypothesis_kind) {
            PPProofV1MatchInstruction *instruction =
                &compiled.matches[match_write++];
            instruction->stack_offset = index;
            result = ppproof_v1_template_compile(
                store, hypothesis->formula, &source->mandatory,
                coalesce_literals,
                &instruction->pattern);
            if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
                goto done;
            compiled.template_cell_len +=
                instruction->pattern.residual.cell_len +
                (instruction->pattern.has_head ? 1u : 0u);
            compiled.template_part_len +=
                instruction->pattern.residual.part_len;
            compiled.template_head_len +=
                instruction->pattern.has_head ? 1u : 0u;
        } else {
            result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
            goto done;
        }
    }
    result = ppproof_v1_compiled_frame_disjoints(
        store, machine, assertion, &source->mandatory, &compiled);
    if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
        goto done;
    if (!ppproof_v1_lookup_formula(
            store, machine, assertion, &conclusion)) {
        result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
        goto done;
    }
    result = ppproof_v1_template_compile(
        store, conclusion, &source->mandatory, coalesce_literals,
        &compiled.conclusion);
    if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
        goto done;
    compiled.template_cell_len +=
        compiled.conclusion.residual.cell_len +
        (compiled.conclusion.has_head ? 1u : 0u);
    compiled.template_part_len += compiled.conclusion.residual.part_len;
    compiled.template_head_len +=
        compiled.conclusion.has_head ? 1u : 0u;
    *compiled_out = compiled;
    memset(&compiled, 0, sizeof(compiled));

done:
    free(slot_seen);
    ppproof_v1_formula_arena_free(&decode_arena);
    ppproof_v1_compiled_frame_free(&compiled);
    return result;
}

static PPRelationalStackProofV1Result ppproof_v1_compile_frame(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    uint32_t assertion, bool coalesce_literals,
    PPProofV1CompiledFrame *compiled_out,
    char *error_buf, size_t error_buf_size) {
    PPProofV1Frame source = {0};
    PPRelationalStackProofV1Result result = ppproof_v1_build_frame(
        store, machine, assertion, &source, error_buf, error_buf_size);

    if (result == PPRELATIONAL_STACK_PROOF_V1_OK) {
        result = ppproof_v1_compile_frame_from_source(
            store, machine, assertion, &source, coalesce_literals,
            compiled_out,
            error_buf, error_buf_size);
    }
    ppproof_v1_frame_free(&source);
    return result;
}

static PPProofV1FrameCacheEntry *ppproof_v1_cache_find(
    PPProofV1FrameCacheImpl *cache, uint32_t assertion) {
    uint32_t index;
    PPProofV1FrameCacheEntry *entry;

    if (!cache || !cetta_gslt_u32_index_find_v1(
                      &cache->entry_index, assertion, &index) ||
        index >= cache->entry_len)
        return NULL;
    entry = &cache->entries[index];
    return entry->label == assertion ? entry : NULL;
}

static PPProofV1FrameCacheEntry *ppproof_v1_cache_insert(
    PPProofV1FrameCacheImpl *cache, uint32_t assertion) {
    PPProofV1FrameCacheEntry *entry;
    uint32_t next_len;
    CettaGsltU32IndexInsertResultV1 inserted;

    if (!cache || cache->entry_len == UINT32_MAX)
        return NULL;
    next_len = cache->entry_len + 1u;
    if (!ppproof_v1_grow(
            (void **)&cache->entries, &cache->entry_cap,
            next_len, sizeof(*cache->entries)))
        return NULL;
    entry = &cache->entries[cache->entry_len];
    memset(entry, 0, sizeof(*entry));
    entry->label = assertion;
    inserted = cetta_gslt_u32_index_insert_unique_v1(
        &cache->entry_index, assertion, cache->entry_len);
    if (inserted != CETTA_GSLT_U32_INDEX_INSERTED_V1)
        return NULL;
    cache->entry_len = next_len;
    return entry;
}

static bool ppproof_v1_cache_retain_source(
    PPProofV1FrameCacheImpl *cache,
    uint32_t assertion, PPProofV1Frame *source) {
    PPProofV1FrameCacheEntry *entry;

    if (!cache || !source ||
        ppproof_v1_cache_find(cache, assertion))
        return false;
    entry = ppproof_v1_cache_insert(cache, assertion);
    if (!entry)
        return false;
    entry->source_ready = true;
    entry->source = *source;
    memset(source, 0, sizeof(*source));
    if (cache->retained_source_len < UINT64_MAX)
        cache->retained_source_len++;
    return true;
}

static PPRelationalStackProofV1Result ppproof_v1_cache_frame(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    PPProofV1FrameCacheImpl *cache,
    uint32_t assertion, const PPProofV1CompiledFrame **frame_out,
    char *error_buf, size_t error_buf_size) {
    PPProofV1FrameCacheEntry *entry;
    PPProofV1CompiledFrame compiled = {0};
    PPRelationalStackProofV1Result result;

    if (!cache || !frame_out) {
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "proof transaction lacks a bound frame cache");
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    }
    entry = ppproof_v1_cache_find(cache, assertion);
    if (entry && entry->compiled_ready) {
        if (cache->hit_len < UINT64_MAX)
            cache->hit_len++;
        *frame_out = &entry->frame;
        return PPRELATIONAL_STACK_PROOF_V1_OK;
    }
    if (entry && !entry->source_ready)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    if (cache->miss_len < UINT64_MAX)
        cache->miss_len++;
    if (entry) {
        result = ppproof_v1_compile_frame_from_source(
            store, machine, assertion, &entry->source,
            cache->admission.literal_hole_admitted, &compiled,
            error_buf, error_buf_size);
    } else {
        result = ppproof_v1_compile_frame(
            store, machine, assertion,
            cache->admission.literal_hole_admitted, &compiled,
            error_buf, error_buf_size);
    }
    if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
        return result;
    if (UINT64_MAX - cache->template_cell_len <
        compiled.template_cell_len)
        cache->template_cell_len = UINT64_MAX;
    else
        cache->template_cell_len += compiled.template_cell_len;
    if (UINT64_MAX - cache->template_part_len <
        compiled.template_part_len)
        cache->template_part_len = UINT64_MAX;
    else
        cache->template_part_len += compiled.template_part_len;
    if (UINT64_MAX - cache->template_head_len <
        compiled.template_head_len)
        cache->template_head_len = UINT64_MAX;
    else
        cache->template_head_len += compiled.template_head_len;
    if (entry) {
        ppproof_v1_frame_free(&entry->source);
        entry->source_ready = false;
        entry->compiled_ready = true;
        entry->frame = compiled;
        if (cache->source_reuse_len < UINT64_MAX)
            cache->source_reuse_len++;
        *frame_out = &entry->frame;
        return PPRELATIONAL_STACK_PROOF_V1_OK;
    }
    entry = ppproof_v1_cache_insert(cache, assertion);
    if (!entry) {
        ppproof_v1_compiled_frame_free(&compiled);
        return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    }
    entry->compiled_ready = true;
    entry->frame = compiled;
    *frame_out = &entry->frame;
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static bool ppproof_v1_cache_bound_to(
    const PPRelationalStackProofV1Cache *cache_handle,
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine) {
    const PPProofV1FrameCacheImpl *cache =
        cache_handle ? cache_handle->implementation : NULL;
    return cache && ppproof_v1_store_equal(store, &cache->store) &&
           ppproof_v1_machine_equal(machine, &cache->machine);
}

static PPProofV1Substitution *ppproof_v1_substitution_find(
    PPProofV1Substitutions *substitutions, uint32_t variable) {
    uint32_t index;
    for (index = 0u; substitutions && index < substitutions->len;
         index++) {
        if (substitutions->items[index].variable == variable)
            return &substitutions->items[index];
    }
    return NULL;
}

static PPRelationalStackProofV1Result ppproof_v1_substitution_add(
    PPProofV1Substitutions *substitutions, uint32_t variable,
    const PPProofV1Vector *image) {
    PPProofV1Substitution *added;
    uint32_t index;

    if (ppproof_v1_substitution_find(substitutions, variable))
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    if (substitutions->len == UINT32_MAX ||
        !ppproof_v1_grow((void **)&substitutions->items,
                         &substitutions->cap, substitutions->len + 1u,
                         sizeof(*substitutions->items)))
        return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    added = &substitutions->items[substitutions->len++];
    memset(added, 0, sizeof(*added));
    added->variable = variable;
    for (index = 0u; index < image->len; index++) {
        if (!ppproof_v1_vector_push(&added->image, image->items[index]))
            return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    }
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result ppproof_v1_substitute(
    const PPRelationalStoreV1 *store,
    uint32_t template_formula,
    PPProofV1Substitutions *substitutions,
    PPProofV1FormulaArena *arena,
    PPProofV1Formula *result_formula) {
    PPProofV1Formula source = {0};
    const uint32_t *source_items = NULL;
    PPProofV1Vector target = {0};
    PPRelationalStackProofV1Result result;
    uint32_t index;

    result = ppproof_v1_formula_decode(
        store, template_formula, arena, &source);
    if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
        goto done;
    source_items = ppproof_v1_formula_items(arena, source);
    if (!source_items) {
        result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
        goto done;
    }
    for (index = 0u; index < source.len; index++) {
        PPProofV1Substitution *substitution =
            ppproof_v1_substitution_find(substitutions,
                                         source_items[index]);
        uint32_t image_index;
        if (!substitution) {
            if (!ppproof_v1_vector_push(&target, source_items[index])) {
                result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
                goto done;
            }
            continue;
        }
        for (image_index = 0u; image_index < substitution->image.len;
             image_index++) {
            if (!ppproof_v1_vector_push(
                    &target, substitution->image.items[image_index])) {
                result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
                goto done;
            }
        }
    }
    result = ppproof_v1_formula_from_vector(
        arena, &target, result_formula);

done:
    ppproof_v1_vector_free(&target);
    return result;
}

static bool ppproof_v1_active_disjoint(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    uint32_t left, uint32_t right) {
    uint32_t key[2] = {left, right};
    uint32_t row[2];

    if (ppproof_v1_find_row(
            store, machine->active_disjoint_table,
            key, 2u, row, 2u))
        return true;
    key[0] = right;
    key[1] = left;
    return ppproof_v1_find_row(
        store, machine->active_disjoint_table,
        key, 2u, row, 2u);
}

static int ppproof_v1_u32_compare(
    const void *left_raw, const void *right_raw) {
    uint32_t left = *(const uint32_t *)left_raw;
    uint32_t right = *(const uint32_t *)right_raw;
    return left < right ? -1 : left > right ? 1 : 0;
}

static bool ppproof_v1_finite_support_slot(
    const PPProofV1FiniteSupportContext *support,
    uint32_t key, uint32_t *slot_out) {
    uint32_t low = 0u;
    uint32_t high;

    if (!support || !slot_out)
        return false;
    high = support->key_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (support->keys[middle] < key)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= support->key_len || support->keys[low] != key)
        return false;
    *slot_out = low;
    return true;
}

static void ppproof_v1_finite_support_free(
    PPProofV1FiniteSupportContext *support) {
    uint32_t index;

    if (!support)
        return;
    for (index = 0u; index < support->matrix_width; index++)
        cetta_gslt_dense_bitset_free_v1(&support->apartness[index]);
    free(support->apartness);
    free(support->keys);
    memset(support, 0, sizeof(*support));
}

static bool ppproof_v1_finite_support_resize(
    PPProofV1FiniteSupportContext *support, uint32_t needed) {
    CettaGsltDenseBitsetV1 *apartness = NULL;
    uint32_t width;
    uint32_t initialized = 0u;

    if (!support)
        return false;
    if (needed <= support->matrix_width)
        return true;
    width = support->matrix_width > 0u
                ? support->matrix_width
                : PPRELATIONAL_STACK_PROOF_V1_INITIAL_CAPACITY;
    while (width < needed) {
        if (width > UINT32_MAX / 2u)
            return false;
        width *= 2u;
    }
    if (!ppproof_v1_array_fits(width, sizeof(*apartness)) ||
        !(apartness = calloc(width, sizeof(*apartness))))
        return false;
    for (initialized = 0u; initialized < width; initialized++) {
        if (!cetta_gslt_dense_bitset_init_v1(
                &apartness[initialized], width))
            goto failed;
    }
    for (initialized = 0u; initialized < support->matrix_width;
         initialized++)
        cetta_gslt_dense_bitset_free_v1(&support->apartness[initialized]);
    free(support->apartness);
    support->apartness = apartness;
    support->matrix_width = width;
    return true;

failed:
    for (uint32_t index = 0u; index < initialized; index++)
        cetta_gslt_dense_bitset_free_v1(&apartness[index]);
    free(apartness);
    return false;
}

static PPRelationalStackProofV1Result ppproof_v1_finite_support_prepare(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    PPProofV1FiniteSupportContext *support) {
    uint32_t active_row_len = 0u;
    uint32_t disjoint_row_len = 0u;
    uint32_t index;

    if (!store || !machine || !support ||
        !ppproof_v1_table_shape(
            store, machine->active_hypothesis_table, 4u, 1u,
            &active_row_len) ||
        !ppproof_v1_table_shape(
            store, machine->active_disjoint_table, 2u, 2u,
            &disjoint_row_len))
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    support->ready = false;
    support->key_len = 0u;
    for (index = 0u; index < active_row_len; index++) {
        uint32_t row[4];
        uint32_t kind_row[2];
        if (!store->table_row(
                store->context, machine->active_hypothesis_table,
                index, row, 4u))
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        if (row[1] != machine->binder_hypothesis_kind)
            continue;
        if (!ppproof_v1_find_row(
                store, machine->symbol_kind_table,
                &row[2], 1u, kind_row, 2u) ||
            kind_row[1] != machine->variable_symbol_kind)
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        if (support->key_len == UINT32_MAX ||
            !ppproof_v1_grow(
                (void **)&support->keys, &support->key_cap,
                support->key_len + 1u, sizeof(*support->keys)))
            return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
        support->keys[support->key_len++] = row[2];
    }
    qsort(support->keys, support->key_len, sizeof(*support->keys),
          ppproof_v1_u32_compare);
    for (index = 1u; index < support->key_len; index++) {
        if (support->keys[index - 1u] == support->keys[index])
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    }
    if (!ppproof_v1_finite_support_resize(support, support->key_len))
        return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    for (index = 0u; index < support->key_len; index++)
        cetta_gslt_dense_bitset_clear_v1(&support->apartness[index]);
    for (index = 0u; index < disjoint_row_len; index++) {
        uint32_t row[2];
        uint32_t left_slot;
        uint32_t right_slot;
        bool changed;
        if (!store->table_row(
                store->context, machine->active_disjoint_table,
                index, row, 2u))
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        if (row[0] == row[1])
            return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
        if (!ppproof_v1_finite_support_slot(
                support, row[0], &left_slot) ||
            !ppproof_v1_finite_support_slot(
                support, row[1], &right_slot) ||
            !cetta_gslt_dense_bitset_set_v1(
                &support->apartness[left_slot], right_slot, &changed) ||
            !cetta_gslt_dense_bitset_set_v1(
                &support->apartness[right_slot], left_slot, &changed))
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    }
    support->ready = true;
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result ppproof_v1_formula_support(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    PPProofV1FiniteSupportContext *support,
    const PPProofV1FormulaArena *arena, PPProofV1Formula formula,
    CettaGsltDenseBitsetV1 *packed) {
    const uint32_t *items;
    uint32_t index;

    if (!store || !machine || !support || !support->ready || !arena ||
        !packed || packed->bit_count != support->matrix_width ||
        formula.offset > arena->len ||
        formula.len > arena->len - formula.offset)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    cetta_gslt_dense_bitset_clear_v1(packed);
    items = formula.len > 0u ? &arena->items[formula.offset] : NULL;
    for (index = 0u; index < formula.len; index++) {
        uint32_t slot;
        if (ppproof_v1_finite_support_slot(support, items[index], &slot)) {
            bool changed;
            if (!cetta_gslt_dense_bitset_set_v1(packed, slot, &changed))
                return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        } else {
            uint32_t kind_row[2];
            if (!ppproof_v1_find_row(
                    store, machine->symbol_kind_table,
                    &items[index], 1u, kind_row, 2u))
                return PPRELATIONAL_STACK_PROOF_V1_INVALID;
            if (kind_row[1] == machine->variable_symbol_kind)
                return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
        }
    }
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result
ppproof_v1_dense_substitution_support(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    PPProofV1DenseSubstitutions *substitutions, uint32_t slot,
    PPProofV1FiniteSupportContext *support,
    const PPProofV1FormulaArena *arena,
    const CettaGsltDenseBitsetV1 **packed_out) {
    const PPProofV1Formula *formula;
    PPRelationalStackProofV1Result result;

    if (!substitutions || !support || !support->ready || !packed_out ||
        slot >= substitutions->slots.width ||
        slot >= substitutions->support_capacity ||
        substitutions->support_width != support->matrix_width ||
        !substitutions->support_epochs || !substitutions->supports)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    formula = ppproof_v1_dense_substitution_get_const(
        substitutions, slot);
    if (!formula)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    if (substitutions->support_epochs[slot] ==
        substitutions->slots.epoch) {
        if (substitutions->support_hit_len < UINT64_MAX)
            substitutions->support_hit_len++;
        *packed_out = &substitutions->supports[slot];
        return PPRELATIONAL_STACK_PROOF_V1_OK;
    }
    result = ppproof_v1_formula_support(
        store, machine, support, arena, *formula,
        &substitutions->supports[slot]);
    if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
        return result;
    substitutions->support_epochs[slot] = substitutions->slots.epoch;
    if (substitutions->support_compute_len < UINT64_MAX)
        substitutions->support_compute_len++;
    *packed_out = &substitutions->supports[slot];
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result ppproof_v1_supports_apart(
    const PPProofV1FiniteSupportContext *support,
    const CettaGsltDenseBitsetV1 *left,
    const CettaGsltDenseBitsetV1 *right) {
    uint32_t left_slot;

    if (!support || !support->ready || !left || !right ||
        left->bit_count != support->matrix_width ||
        right->bit_count != support->matrix_width)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    for (left_slot = 0u; left_slot < support->key_len; left_slot++) {
        bool present;
        uint32_t word;
        if (!cetta_gslt_dense_bitset_test_v1(
                left, left_slot, &present))
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        if (!present)
            continue;
        for (word = 0u; word < right->word_len; word++) {
            if ((right->words[word] &
                 ~support->apartness[left_slot].words[word]) != 0u)
                return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
        }
    }
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result ppproof_v1_check_disjoint(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    uint32_t assertion, PPProofV1Substitutions *substitutions,
    char *error_buf, size_t error_buf_size) {
    PPProofV1PrefixIterator iterator;
    PPRelationalStackProofV1Result result;

    if (!ppproof_v1_prefix_iterator_init(
            store, machine->assertion_disjoint_table, 3u, 3u,
            assertion, (UINT32_C(1) << 1u) | (UINT32_C(1) << 2u),
            &iterator))
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    for (;;) {
        uint32_t row[3];
        PPProofV1Substitution *left;
        PPProofV1Substitution *right;
        uint32_t left_index;
        bool found;
        result = ppproof_v1_prefix_iterator_next(
            store, &iterator, row, 3u, &found);
        if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
            return result;
        if (!found)
            break;
        left = ppproof_v1_substitution_find(substitutions, row[1]);
        right = ppproof_v1_substitution_find(substitutions, row[2]);
        if (!left || !right)
            continue;
        for (left_index = 0u; left_index < left->image.len;
             left_index++) {
            uint32_t left_key = left->image.items[left_index];
            uint32_t left_kind_row[2];
            uint32_t right_index;
            if (!ppproof_v1_find_row(
                    store, machine->symbol_kind_table,
                    &left_key, 1u, left_kind_row, 2u))
                return PPRELATIONAL_STACK_PROOF_V1_INVALID;
            if (left_kind_row[1] != machine->variable_symbol_kind)
                continue;
            for (right_index = 0u; right_index < right->image.len;
                 right_index++) {
                uint32_t right_key = right->image.items[right_index];
                uint32_t right_kind_row[2];
                if (!ppproof_v1_find_row(
                        store, machine->symbol_kind_table,
                        &right_key, 1u, right_kind_row, 2u))
                    return PPRELATIONAL_STACK_PROOF_V1_INVALID;
                if (right_kind_row[1] != machine->variable_symbol_kind)
                    continue;
                if (left_key == right_key ||
                    !ppproof_v1_active_disjoint(
                        store, machine, left_key, right_key)) {
                    ppproof_v1_set_error(
                        error_buf, error_buf_size,
                        "proof substitution violates a disjointness obligation");
                    return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
                }
            }
        }
    }
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

typedef struct {
    const PPProofV1DenseSubstitutions *substitutions;
    const PPProofV1FormulaArena *arena;
} PPProofV1TemplateEnvironment;

static bool ppproof_v1_template_hole_lookup(
    void *context, uint32_t hole,
    const uint32_t **items_out, uint32_t *len_out) {
    const PPProofV1TemplateEnvironment *environment = context;
    const PPProofV1Formula *image;

    if (!environment || !environment->substitutions ||
        !environment->arena || !items_out || !len_out ||
        !(image = ppproof_v1_dense_substitution_get_const(
              environment->substitutions, hole)) ||
        image->offset > environment->arena->len ||
        image->len > environment->arena->len - image->offset)
        return false;
    *items_out = image->len != 0u
                     ? &environment->arena->items[image->offset]
                     : NULL;
    *len_out = image->len;
    return true;
}

static bool ppproof_v1_template_matches(
    const PPProofV1Template *template,
    const PPProofV1DenseSubstitutions *substitutions,
    const PPProofV1FormulaArena *arena, PPProofV1Formula actual) {
    const uint32_t *actual_items =
        ppproof_v1_formula_items(arena, actual);
    PPProofV1TemplateEnvironment environment = {
        substitutions, arena,
    };

    if (!template || !substitutions || !actual_items)
        return false;
    return cetta_gslt_literal_head_program_match_v1(
        template, ppproof_v1_template_hole_lookup, &environment,
        actual_items, actual.len);
}

static PPRelationalStackProofV1Result ppproof_v1_template_instantiate(
    const PPProofV1Template *template,
    const PPProofV1DenseSubstitutions *substitutions,
    PPProofV1FormulaArena *arena, PPProofV1Formula *formula_out) {
    uint32_t result_len = 0u;
    uint32_t offset;
    PPProofV1TemplateEnvironment environment = {
        substitutions, arena,
    };

    if (!template || !substitutions || !arena || !formula_out)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    if (!cetta_gslt_literal_head_program_measure_v1(
            template, ppproof_v1_template_hole_lookup, &environment,
            &result_len))
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    if (result_len == 0u ||
        !ppproof_v1_formula_arena_reserve(
            arena, result_len, &offset))
        return result_len == 0u
                   ? PPRELATIONAL_STACK_PROOF_V1_INVALID
                   : PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    environment.arena = arena;
    if (!cetta_gslt_literal_head_program_write_v1(
            template, ppproof_v1_template_hole_lookup, &environment,
            &arena->items[offset], result_len))
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    *formula_out = (PPProofV1Formula){offset, result_len};
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result
ppproof_v1_check_compiled_disjoint(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    const PPProofV1CompiledFrame *frame,
    PPProofV1DenseSubstitutions *substitutions,
    const PPProofV1FormulaArena *arena,
    PPProofV1FiniteSupportContext *finite_support,
    char *error_buf, size_t error_buf_size) {
    uint32_t pair_index;

    for (pair_index = 0u; pair_index < frame->disjoint_len;
         pair_index++) {
        const PPProofV1DenseDisjoint *pair =
            &frame->disjoints[pair_index];
        PPProofV1Formula left;
        PPProofV1Formula right;
        const PPProofV1Formula *left_image;
        const PPProofV1Formula *right_image;
        const uint32_t *left_items;
        const uint32_t *right_items;
        uint32_t left_index;

        left_image = ppproof_v1_dense_substitution_get_const(
            substitutions, pair->left_slot);
        right_image = ppproof_v1_dense_substitution_get_const(
            substitutions, pair->right_slot);
        if (!left_image || !right_image)
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        left = *left_image;
        right = *right_image;
        if (left.offset > arena->len || left.len > arena->len - left.offset ||
            right.offset > arena->len ||
            right.len > arena->len - right.offset)
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        if (finite_support) {
            const CettaGsltDenseBitsetV1 *left_support;
            const CettaGsltDenseBitsetV1 *right_support;
            PPRelationalStackProofV1Result packed_result;

            packed_result = ppproof_v1_dense_substitution_support(
                store, machine, substitutions, pair->left_slot,
                finite_support, arena, &left_support);
            if (packed_result == PPRELATIONAL_STACK_PROOF_V1_OK)
                packed_result = ppproof_v1_dense_substitution_support(
                    store, machine, substitutions, pair->right_slot,
                    finite_support, arena, &right_support);
            if (packed_result == PPRELATIONAL_STACK_PROOF_V1_OK)
                packed_result = ppproof_v1_supports_apart(
                    finite_support, left_support, right_support);
            if (packed_result == PPRELATIONAL_STACK_PROOF_V1_REJECTED)
                ppproof_v1_set_error(
                    error_buf, error_buf_size,
                    "proof substitution violates a disjointness obligation");
            if (packed_result != PPRELATIONAL_STACK_PROOF_V1_OK)
                return packed_result;
            continue;
        }
        left_items = left.len > 0u ? &arena->items[left.offset] : NULL;
        right_items = right.len > 0u ? &arena->items[right.offset] : NULL;
        for (left_index = 0u; left_index < left.len; left_index++) {
            uint32_t left_key = left_items[left_index];
            uint32_t left_kind_row[2];
            uint32_t right_index;
            if (!ppproof_v1_find_row(
                    store, machine->symbol_kind_table,
                    &left_key, 1u, left_kind_row, 2u))
                return PPRELATIONAL_STACK_PROOF_V1_INVALID;
            if (left_kind_row[1] != machine->variable_symbol_kind)
                continue;
            for (right_index = 0u; right_index < right.len;
                 right_index++) {
                uint32_t right_key = right_items[right_index];
                uint32_t right_kind_row[2];
                if (!ppproof_v1_find_row(
                        store, machine->symbol_kind_table,
                        &right_key, 1u, right_kind_row, 2u))
                    return PPRELATIONAL_STACK_PROOF_V1_INVALID;
                if (right_kind_row[1] != machine->variable_symbol_kind)
                    continue;
                if (left_key == right_key ||
                    !ppproof_v1_active_disjoint(
                        store, machine, left_key, right_key)) {
                    ppproof_v1_set_error(
                        error_buf, error_buf_size,
                        "proof substitution violates a disjointness obligation");
                    return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
                }
            }
        }
    }
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result ppproof_v1_apply_compiled_rule(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    const PPProofV1CompiledFrame *frame,
    PPProofV1DenseSubstitutions *substitutions,
    PPProofV1FiniteSupportContext *finite_support,
    PPProofV1FormulaArena *arena, PPProofV1FormulaStack *stack,
    char *error_buf, size_t error_buf_size) {
    PPProofV1Formula result_formula = {0};
    uint32_t base;
    uint32_t index;
    PPRelationalStackProofV1Result result =
        PPRELATIONAL_STACK_PROOF_V1_OK;

    if (!frame || !substitutions || stack->len < frame->instruction_len) {
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "proof stack underflow");
        return frame && substitutions
                   ? PPRELATIONAL_STACK_PROOF_V1_REJECTED
                   : PPRELATIONAL_STACK_PROOF_V1_INVALID;
    }
    if (!ppproof_v1_dense_substitutions_prepare(
            substitutions, frame->slot_len,
            finite_support && frame->disjoint_len > 0u
                ? finite_support->matrix_width
                : 0u))
        return PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    base = stack->len - frame->instruction_len;
    for (index = 0u; index < frame->bind_len; index++) {
        const PPProofV1BindInstruction *instruction =
            &frame->binds[index];
        PPProofV1Formula actual;
        PPProofV1Formula *image;
        const uint32_t *actual_items;
        actual = stack->items[base + instruction->stack_offset];
        actual_items = ppproof_v1_formula_items(arena, actual);
        if (!actual_items || actual.len == 0u ||
            actual_items[0] != instruction->type_head) {
            ppproof_v1_set_error(
                error_buf, error_buf_size,
                "proof binder does not match its declared type");
            return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
        }
        image = ppproof_v1_dense_substitution_set(
            substitutions, instruction->slot);
        if (!image)
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        *image = (PPProofV1Formula){actual.offset + 1u, actual.len - 1u};
    }
    for (index = 0u; index < frame->match_len; index++) {
        const PPProofV1MatchInstruction *instruction =
            &frame->matches[index];
        if (!ppproof_v1_template_matches(
                &instruction->pattern, substitutions, arena,
                stack->items[base + instruction->stack_offset])) {
            ppproof_v1_set_error(error_buf, error_buf_size,
                                 "proof hypothesis mismatch");
            return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
        }
    }
    result = ppproof_v1_check_compiled_disjoint(
        store, machine, frame, substitutions, arena, finite_support,
        error_buf, error_buf_size);
    if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
        return result;
    result = ppproof_v1_template_instantiate(
        &frame->conclusion, substitutions, arena, &result_formula);
    if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
        return result;
    stack->len = base;
    return ppproof_v1_formula_stack_push(stack, result_formula)
               ? PPRELATIONAL_STACK_PROOF_V1_OK
               : PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
}

static PPRelationalStackProofV1Result ppproof_v1_apply_rule(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    uint32_t rule, PPProofV1FormulaArena *arena,
    PPProofV1FormulaStack *stack,
    char *error_buf, size_t error_buf_size) {
    PPProofV1Frame frame = {0};
    PPProofV1Substitutions substitutions = {0};
    uint32_t conclusion = 0u;
    PPProofV1Formula result_formula = {0};
    uint32_t base;
    uint32_t index;
    PPRelationalStackProofV1Result result;

    result = ppproof_v1_build_frame(
        store, machine, rule, &frame, error_buf, error_buf_size);
    if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
        goto done;
    if (stack->len < frame.len) {
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "proof stack underflow");
        result = PPRELATIONAL_STACK_PROOF_V1_REJECTED;
        goto done;
    }
    base = stack->len - frame.len;
    for (index = 0u; index < frame.len; index++) {
        PPProofV1Hypothesis *hypothesis = &frame.items[index];
        if (hypothesis->kind == machine->binder_hypothesis_kind) {
            PPProofV1Formula expected = {0};
            PPProofV1Formula actual = stack->items[base + index];
            const uint32_t *expected_items = NULL;
            const uint32_t *actual_items = NULL;
            PPProofV1Vector image = {0};
            uint32_t image_index;
            result = ppproof_v1_formula_decode(
                store, hypothesis->formula, arena, &expected);
            if (result != PPRELATIONAL_STACK_PROOF_V1_OK) {
                ppproof_v1_vector_free(&image);
                goto done;
            }
            expected_items = ppproof_v1_formula_items(arena, expected);
            actual_items = ppproof_v1_formula_items(arena, actual);
            if (expected.len != 2u ||
                !expected_items || !actual_items ||
                expected_items[1] != hypothesis->variable ||
                actual.len == 0u ||
                expected_items[0] != actual_items[0]) {
                ppproof_v1_set_error(
                    error_buf, error_buf_size,
                    "proof binder does not match its declared type");
                result = PPRELATIONAL_STACK_PROOF_V1_REJECTED;
                ppproof_v1_vector_free(&image);
                goto done;
            }
            for (image_index = 1u; image_index < actual.len;
                 image_index++) {
                if (!ppproof_v1_vector_push(
                        &image, actual_items[image_index])) {
                    result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
                    break;
                }
            }
            if (result == PPRELATIONAL_STACK_PROOF_V1_OK)
                result = ppproof_v1_substitution_add(
                    &substitutions, hypothesis->variable, &image);
            ppproof_v1_vector_free(&image);
            if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
                goto done;
        }
    }
    for (index = 0u; index < frame.mandatory.len; index++) {
        if (!ppproof_v1_substitution_find(
                &substitutions, frame.mandatory.items[index])) {
            result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
            goto done;
        }
    }
    for (index = 0u; index < frame.len; index++) {
        PPProofV1Hypothesis *hypothesis = &frame.items[index];
        PPProofV1Formula expected_formula = {0};
        if (hypothesis->kind != machine->matching_hypothesis_kind)
            continue;
        result = ppproof_v1_substitute(
            store, hypothesis->formula, &substitutions,
            arena, &expected_formula);
        if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
            goto done;
        if (!ppproof_v1_formula_equal(
                arena, expected_formula, stack->items[base + index])) {
            ppproof_v1_set_error(error_buf, error_buf_size,
                                 "proof hypothesis mismatch");
            result = PPRELATIONAL_STACK_PROOF_V1_REJECTED;
            goto done;
        }
    }
    result = ppproof_v1_check_disjoint(
        store, machine, rule, &substitutions,
        error_buf, error_buf_size);
    if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
        goto done;
    if (!ppproof_v1_lookup_formula(store, machine, rule, &conclusion)) {
        result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
        goto done;
    }
    result = ppproof_v1_substitute(
        store, conclusion, &substitutions, arena, &result_formula);
    if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
        goto done;
    stack->len = base;
    if (!ppproof_v1_formula_stack_push(stack, result_formula))
        result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;

done:
    ppproof_v1_substitutions_free(&substitutions);
    ppproof_v1_frame_free(&frame);
    return result;
}

static PPRelationalStackProofV1Result ppproof_v1_apply_label(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    uint32_t label, PPProofV1Formula claim,
    PPProofV1FormulaArena *arena, PPProofV1FormulaStack *stack,
    PPProofV1FrameCacheImpl *cache,
    PPProofV1DenseSubstitutions *dense_substitutions,
    char *error_buf, size_t error_buf_size) {
    uint32_t kind;
    uint32_t stored_formula;
    PPProofV1Formula formula = {0};

    if (label == machine->unknown_token) {
        if (machine->unknown_policy ==
            PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM) {
            return ppproof_v1_formula_stack_push(stack, claim)
                       ? PPRELATIONAL_STACK_PROOF_V1_OK
                       : PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
        }
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "unknown proof step is rejected");
        return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
    }
    if (!ppproof_v1_lookup_kind(store, machine, label, &kind)) {
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "proof references an unknown label");
        return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
    }
    if (kind == machine->binder_hypothesis_kind ||
        kind == machine->matching_hypothesis_kind) {
        if (!ppproof_v1_hypothesis_active(store, machine, label) ||
            !ppproof_v1_lookup_formula(
                store, machine, label, &stored_formula)) {
            ppproof_v1_set_error(error_buf, error_buf_size,
                                 "proof references an inactive hypothesis");
            return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
        }
        {
            PPRelationalStackProofV1Result result =
                ppproof_v1_formula_decode(
                    store, stored_formula, arena, &formula);
            if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
                return result;
        }
        return ppproof_v1_formula_stack_push(stack, formula)
               ? PPRELATIONAL_STACK_PROOF_V1_OK
               : PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    }
    if (ppproof_v1_is_rule(machine, kind)) {
        if (cache) {
            const PPProofV1CompiledFrame *compiled;
            PPRelationalStackProofV1Result result =
                ppproof_v1_cache_frame(
                    store, machine, cache, label, &compiled,
                    error_buf, error_buf_size);
            if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
                return result;
            return ppproof_v1_apply_compiled_rule(
                store, machine, compiled, dense_substitutions,
                cache->admission.finite_support_admitted
                    ? &cache->scratch_support
                    : NULL,
                arena, stack, error_buf, error_buf_size);
        }
        return ppproof_v1_apply_rule(
            store, machine, label, arena, stack,
            error_buf, error_buf_size);
    }
    ppproof_v1_set_error(error_buf, error_buf_size,
                         "proof label has no executable kind");
    return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
}

static PPRelationalStackProofV1Result ppproof_v1_finish(
    const PPProofV1FormulaArena *arena,
    const PPProofV1FormulaStack *stack, PPProofV1Formula claim,
    char *error_buf, size_t error_buf_size) {
    if (!stack || stack->len != 1u ||
        !ppproof_v1_formula_equal(arena, stack->items[0], claim)) {
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "proof does not finish with its claim");
        return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
    }
    return PPRELATIONAL_STACK_PROOF_V1_OK;
}

static PPRelationalStackProofV1Result ppproof_v1_normal(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    PPRelationalStackProofV1Cache *cache,
    const PPRelationalStackProofV1NormalInput *input,
    char *error_buf,
    size_t error_buf_size) {
    PPProofV1FormulaArena arena = {0};
    PPProofV1FormulaStack stack = {0};
    PPProofV1Heap heap = {0};
    PPProofV1DenseSubstitutions dense_substitutions = {0};
    PPProofV1FrameCacheImpl *cache_impl = NULL;
    PPProofV1Formula claim = {0};
    uint32_t label = 0u;
    uint32_t index;
    bool incomplete = false;
    bool scratch_acquired = false;
    PPRelationalStackProofV1Result result;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!input || !input->steps || input->step_len == 0u)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    if (cache) {
        if (!ppproof_v1_cache_bound_to(cache, store, machine)) {
            ppproof_v1_set_error(
                error_buf, error_buf_size,
                "frame cache is not bound to this store and proof machine");
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
        cache_impl = cache->implementation;
    } else if (!pprelational_stack_proof_v1_machine_validate(
                   store, machine, error_buf, error_buf_size)) {
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    }
    if (cache_impl) {
        scratch_acquired = ppproof_v1_scratch_acquire(
            cache_impl, &arena, &stack, &heap,
            &dense_substitutions);
        if (!scratch_acquired) {
            ppproof_v1_set_error(
                error_buf, error_buf_size,
                "proof scratch region is unavailable or unadmitted");
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
    }
    result = cache_impl && cache_impl->admission.finite_support_admitted
                 ? ppproof_v1_finite_support_prepare(
                       store, machine, &cache_impl->scratch_support)
                 : PPRELATIONAL_STACK_PROOF_V1_OK;
    if (result == PPRELATIONAL_STACK_PROOF_V1_OK)
        result = ppproof_v1_intern_slice(store, input->label, &label);
    if (result == PPRELATIONAL_STACK_PROOF_V1_OK)
        result = ppproof_v1_formula_from_slices(
            store, input->claim, input->claim_len, &arena, &claim);
    for (index = 0u;
         result == PPRELATIONAL_STACK_PROOF_V1_OK &&
         index < input->step_len; index++) {
        uint32_t step_label = 0u;
        result = ppproof_v1_intern_slice(
            store, input->steps[index], &step_label);
        if (result == PPRELATIONAL_STACK_PROOF_V1_OK) {
            if (step_label == machine->unknown_token &&
                machine->unknown_policy ==
                    PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM)
                incomplete = true;
            result = ppproof_v1_apply_label(
                store, machine, step_label, claim, &arena, &stack,
                cache_impl, &dense_substitutions,
                error_buf, error_buf_size);
        }
    }
    if (result == PPRELATIONAL_STACK_PROOF_V1_OK)
        result = ppproof_v1_finish(
            &arena, &stack, claim, error_buf, error_buf_size);
    if (result == PPRELATIONAL_STACK_PROOF_V1_OK && incomplete)
        result = PPRELATIONAL_STACK_PROOF_V1_INCOMPLETE;
    if (scratch_acquired) {
        ppproof_v1_scratch_release(
            cache_impl, &arena, &stack, &heap,
            &dense_substitutions);
    } else {
        ppproof_v1_formula_stack_free(&stack);
        ppproof_v1_heap_free(&heap);
        ppproof_v1_dense_substitutions_free(&dense_substitutions);
        ppproof_v1_formula_arena_free(&arena);
    }
    return result;
}

PPRelationalStackProofV1Result pprelational_stack_proof_v1_normal(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    const PPRelationalStackProofV1NormalInput *input,
    char *error_buf,
    size_t error_buf_size) {
    return ppproof_v1_normal(
        store, machine, NULL, input, error_buf, error_buf_size);
}

PPRelationalStackProofV1Result pprelational_stack_proof_v1_normal_cached(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    PPRelationalStackProofV1Cache *cache,
    const PPRelationalStackProofV1NormalInput *input,
    char *error_buf,
    size_t error_buf_size) {
    return ppproof_v1_normal(
        store, machine, cache, input, error_buf, error_buf_size);
}

static PPRelationalStackProofV1Result ppproof_v1_heap_add_label(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    uint32_t label, PPProofV1FormulaArena *arena,
    PPProofV1Heap *heap,
    char *error_buf, size_t error_buf_size) {
    uint32_t kind;
    uint32_t stored_formula;
    PPProofV1Formula formula = {0};

    if (!ppproof_v1_lookup_kind(store, machine, label, &kind)) {
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "compressed header references an unknown label");
        return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
    }
    if (kind == machine->binder_hypothesis_kind ||
        kind == machine->matching_hypothesis_kind) {
        if (!ppproof_v1_hypothesis_active(store, machine, label) ||
            !ppproof_v1_lookup_formula(
                store, machine, label, &stored_formula)) {
            ppproof_v1_set_error(error_buf, error_buf_size,
                                 "compressed header references an inactive hypothesis");
            return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
        }
        {
            PPRelationalStackProofV1Result result =
                ppproof_v1_formula_decode(
                    store, stored_formula, arena, &formula);
            if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
                return result;
        }
        return ppproof_v1_heap_push_formula(heap, formula)
               ? PPRELATIONAL_STACK_PROOF_V1_OK
               : PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    }
    if (ppproof_v1_is_rule(machine, kind)) {
        return ppproof_v1_heap_push_rule(heap, label)
                   ? PPRELATIONAL_STACK_PROOF_V1_OK
                   : PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    }
    return PPRELATIONAL_STACK_PROOF_V1_REJECTED;
}

static PPRelationalStackProofV1Result ppproof_v1_apply_heap_entry(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    const PPProofV1HeapEntry *entry,
    PPProofV1FormulaArena *arena, PPProofV1FormulaStack *stack,
    PPProofV1FrameCacheImpl *cache,
    PPProofV1DenseSubstitutions *dense_substitutions,
    char *error_buf, size_t error_buf_size) {
    if (entry->tag == PPPROOF_V1_HEAP_FORMULA) {
        PPProofV1Formula formula = {
            .offset = entry->first,
            .len = entry->second,
        };
        return ppproof_v1_formula_stack_push(stack, formula)
                   ? PPRELATIONAL_STACK_PROOF_V1_OK
                   : PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    }
    if (entry->tag == PPPROOF_V1_HEAP_RULE) {
        if (cache) {
            const PPProofV1CompiledFrame *compiled;
            PPRelationalStackProofV1Result result =
                ppproof_v1_cache_frame(
                    store, machine, cache, entry->first, &compiled,
                    error_buf, error_buf_size);
            if (result != PPRELATIONAL_STACK_PROOF_V1_OK)
                return result;
            return ppproof_v1_apply_compiled_rule(
                store, machine, compiled, dense_substitutions,
                cache->admission.finite_support_admitted
                    ? &cache->scratch_support
                    : NULL,
                arena, stack, error_buf, error_buf_size);
        }
        return ppproof_v1_apply_rule(
            store, machine, entry->first, arena, stack,
            error_buf, error_buf_size);
    }
    return PPRELATIONAL_STACK_PROOF_V1_INVALID;
}

static PPRelationalStackProofV1Result ppproof_v1_compressed(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    PPRelationalStackProofV1Cache *cache,
    const PPRelationalStackProofV1CompressedInput *input,
    char *error_buf,
    size_t error_buf_size) {
    PPProofV1Frame frame = {0};
    PPProofV1FormulaArena arena = {0};
    PPProofV1Heap heap = {0};
    PPProofV1FormulaStack stack = {0};
    PPProofV1DenseSubstitutions dense_substitutions = {0};
    PPProofV1FrameCacheImpl *cache_impl = NULL;
    PPProofV1Formula claim = {0};
    CettaGsltIndexedInstructionDecoderV1 decoder = {0};
    CettaGsltIndexedInstructionPlanV1 indexed_plan;
    uint32_t label = 0u;
    uint32_t index;
    bool incomplete = false;
    bool scratch_acquired = false;
    PPRelationalStackProofV1Result result;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!input || !input->code || input->code_len == 0u)
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    if (cache) {
        if (!ppproof_v1_cache_bound_to(cache, store, machine)) {
            ppproof_v1_set_error(
                error_buf, error_buf_size,
                "frame cache is not bound to this store and proof machine");
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
        cache_impl = cache->implementation;
        if (!cache_impl->admission.indexed_values_admitted) {
            ppproof_v1_set_error(
                error_buf, error_buf_size,
                "prepared indexed-value table is not admitted");
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
    } else if (!pprelational_stack_proof_v1_machine_validate(
                   store, machine, error_buf, error_buf_size)) {
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    }
    indexed_plan = ppproof_v1_indexed_plan(machine);
    if (cetta_gslt_indexed_instruction_decoder_init_v1(
            &decoder, &indexed_plan) != CETTA_GSLT_INDEXED_DECODE_OK_V1) {
        ppproof_v1_set_error(error_buf, error_buf_size,
                             "invalid relational proof decoder");
        return PPRELATIONAL_STACK_PROOF_V1_INVALID;
    }
    if (cache_impl) {
        scratch_acquired = ppproof_v1_scratch_acquire(
            cache_impl, &arena, &stack, &heap,
            &dense_substitutions);
        if (!scratch_acquired) {
            ppproof_v1_set_error(
                error_buf, error_buf_size,
                "proof scratch region is unavailable or unadmitted");
            return PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
    }
    result = cache_impl && cache_impl->admission.finite_support_admitted
                 ? ppproof_v1_finite_support_prepare(
                       store, machine, &cache_impl->scratch_support)
                 : PPRELATIONAL_STACK_PROOF_V1_OK;
    if (result == PPRELATIONAL_STACK_PROOF_V1_OK)
        result = ppproof_v1_intern_slice(store, input->label, &label);
    if (result == PPRELATIONAL_STACK_PROOF_V1_OK)
        result = ppproof_v1_formula_from_slices(
            store, input->claim, input->claim_len, &arena, &claim);
    if (result == PPRELATIONAL_STACK_PROOF_V1_OK)
        result = ppproof_v1_build_frame(
            store, machine, label, &frame, error_buf, error_buf_size);
    for (index = 0u;
         result == PPRELATIONAL_STACK_PROOF_V1_OK &&
         index < frame.len; index++) {
        PPProofV1Formula formula = {0};
        result = ppproof_v1_formula_decode(
            store, frame.items[index].formula, &arena, &formula);
        if (result == PPRELATIONAL_STACK_PROOF_V1_OK &&
            !ppproof_v1_heap_push_formula(&heap, formula))
            result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
    }
    for (index = 0u;
         result == PPRELATIONAL_STACK_PROOF_V1_OK &&
         index < input->header_len; index++) {
        uint32_t header_label = 0u;
        result = ppproof_v1_intern_slice(
            store, input->header[index], &header_label);
        if (result == PPRELATIONAL_STACK_PROOF_V1_OK)
            result = ppproof_v1_heap_add_label(
                store, machine, header_label, &arena, &heap,
                error_buf, error_buf_size);
    }
    for (index = 0u;
         result == PPRELATIONAL_STACK_PROOF_V1_OK &&
         index < input->code_len; index++) {
        uint32_t byte_index;
        if (!input->code[index].bytes || input->code[index].len == 0u) {
            result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
            break;
        }
        for (byte_index = 0u;
             result == PPRELATIONAL_STACK_PROOF_V1_OK &&
             byte_index < input->code[index].len; byte_index++) {
            uint8_t byte = input->code[index].bytes[byte_index];
            CettaGsltIndexedInstructionEventV1 event;
            CettaGsltIndexedDecodeResultV1 decode_result =
                cetta_gslt_indexed_instruction_feed_v1(
                    &decoder, byte, &event);

            if (decode_result != CETTA_GSLT_INDEXED_DECODE_OK_V1) {
                result = PPRELATIONAL_STACK_PROOF_V1_REJECTED;
                if (decode_result == CETTA_GSLT_INDEXED_DECODE_OVERFLOW_V1)
                    ppproof_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof index overflows");
                else if (decode_result ==
                         CETTA_GSLT_INDEXED_DECODE_SAVE_INSIDE_INDEX_V1)
                    ppproof_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof save interrupts an open index");
                else if (decode_result ==
                         CETTA_GSLT_INDEXED_DECODE_INVALID_BYTE_V1)
                    ppproof_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof byte is outside the generated decoder");
                else {
                    result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
                    ppproof_v1_set_error(
                        error_buf, error_buf_size,
                        "invalid relational proof decoder state");
                }
                break;
            }
            if (event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_USE_V1) {
                if (event.index >= heap.len) {
                    result = PPRELATIONAL_STACK_PROOF_V1_REJECTED;
                    ppproof_v1_set_error(error_buf, error_buf_size,
                                         "compressed proof index is out of range");
                    break;
                }
                result = ppproof_v1_apply_heap_entry(
                    store, machine, &heap.items[event.index],
                    &arena, &stack,
                    cache_impl, &dense_substitutions,
                    error_buf, error_buf_size);
            } else if (event.kind ==
                       CETTA_GSLT_INDEXED_INSTRUCTION_SAVE_V1) {
                if (stack.len == 0u) {
                    result = PPRELATIONAL_STACK_PROOF_V1_REJECTED;
                    ppproof_v1_set_error(error_buf, error_buf_size,
                                         "compressed proof cannot save an empty stack");
                    break;
                }
                if (!ppproof_v1_heap_push_formula(
                        &heap, stack.items[stack.len - 1u]))
                    result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
            } else if (event.kind ==
                       CETTA_GSLT_INDEXED_INSTRUCTION_UNKNOWN_V1) {
                if (machine->unknown_policy ==
                    PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM) {
                    incomplete = true;
                    if (!ppproof_v1_formula_stack_push(&stack, claim))
                        result = PPRELATIONAL_STACK_PROOF_V1_RESOURCE;
                } else {
                    result = PPRELATIONAL_STACK_PROOF_V1_REJECTED;
                    ppproof_v1_set_error(error_buf, error_buf_size,
                                         "unknown proof step is rejected");
                }
            }
        }
    }
    if (result == PPRELATIONAL_STACK_PROOF_V1_OK) {
        CettaGsltIndexedDecodeResultV1 finish_result =
            cetta_gslt_indexed_instruction_finish_v1(&decoder);
        if (finish_result ==
            CETTA_GSLT_INDEXED_DECODE_OPEN_INDEX_AT_END_V1) {
            ppproof_v1_set_error(error_buf, error_buf_size,
                                 "compressed proof ends inside an index");
            result = PPRELATIONAL_STACK_PROOF_V1_REJECTED;
        } else if (finish_result != CETTA_GSLT_INDEXED_DECODE_OK_V1) {
            ppproof_v1_set_error(error_buf, error_buf_size,
                                 "invalid relational proof decoder state");
            result = PPRELATIONAL_STACK_PROOF_V1_INVALID;
        }
    }
    if (result == PPRELATIONAL_STACK_PROOF_V1_OK)
        result = ppproof_v1_finish(
            &arena, &stack, claim, error_buf, error_buf_size);
    if (result == PPRELATIONAL_STACK_PROOF_V1_OK && incomplete)
        result = PPRELATIONAL_STACK_PROOF_V1_INCOMPLETE;
    if (cache_impl &&
        (result == PPRELATIONAL_STACK_PROOF_V1_OK ||
         result == PPRELATIONAL_STACK_PROOF_V1_INCOMPLETE))
        (void)ppproof_v1_cache_retain_source(
            cache_impl, label, &frame);
    ppproof_v1_frame_free(&frame);
    if (scratch_acquired) {
        ppproof_v1_scratch_release(
            cache_impl, &arena, &stack, &heap,
            &dense_substitutions);
    } else {
        ppproof_v1_formula_stack_free(&stack);
        ppproof_v1_heap_free(&heap);
        ppproof_v1_dense_substitutions_free(&dense_substitutions);
        ppproof_v1_formula_arena_free(&arena);
    }
    return result;
}

PPRelationalStackProofV1Result pprelational_stack_proof_v1_compressed(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    const PPRelationalStackProofV1CompressedInput *input,
    char *error_buf,
    size_t error_buf_size) {
    return ppproof_v1_compressed(
        store, machine, NULL, input, error_buf, error_buf_size);
}

PPRelationalStackProofV1Result pprelational_stack_proof_v1_compressed_cached(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    PPRelationalStackProofV1Cache *cache,
    const PPRelationalStackProofV1CompressedInput *input,
    char *error_buf,
    size_t error_buf_size) {
    return ppproof_v1_compressed(
        store, machine, cache, input, error_buf, error_buf_size);
}
