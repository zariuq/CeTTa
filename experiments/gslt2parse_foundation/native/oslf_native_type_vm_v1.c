#include "oslf_native_type_vm_v1.h"

#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "gslt_ground_dense_term_v1.h"
#include "gslt_peano_add_specialization_v1.h"
#include "gslt_rigid_coordinate_dispatch_v1.h"
#include "match.h"
#include "native_sha256.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Atom *head;
    Atom **body;
    uint32_t body_len;
    uint32_t variable_count;
    bool body_variables_in_head;
    bool head_linear;
    bool ground_dense_ready;
    bool *body_ground;
    bool *body_activation_view_admitted;
    CettaGsltGroundDenseTermProgramV1 ground_dense_head;
    CettaGsltGroundDenseTermProgramV1 *ground_dense_body;
} PPOSLFNativeCompiledRuleV1;

typedef struct {
    SymbolId symbol;
    uint32_t arity;
    uint32_t exact_group;
    uint32_t external_relation;
    bool has_exact_group;
    bool has_external_relation;
    bool has_compiled_relation;
    uint32_t compiled_relation;
    CettaGsltRigidCoordinateIndexV1 rigid_index;
} PPOSLFNativeApplicationDispatchV1;

static uint64_t pposlf_native_stats_add_sat_v1(
    uint64_t left, uint64_t right) {
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

void pposlf_native_vm_stats_v1_accumulate(
    PPOSLFNativeVMStatsV1 *aggregate,
    const PPOSLFNativeVMStatsV1 *sample) {
    if (!aggregate || !sample)
        return;
#define PPOSLF_NATIVE_STATS_SUM_V1(field)                                      \
    aggregate->field = pposlf_native_stats_add_sat_v1(                        \
        aggregate->field, sample->field)
    PPOSLF_NATIVE_STATS_SUM_V1(goals_entered);
    PPOSLF_NATIVE_STATS_SUM_V1(rule_attempts);
    PPOSLF_NATIVE_STATS_SUM_V1(rule_matches);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_pattern_rule_attempts);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_pattern_rule_matches);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_match_nodes);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_rigid_subtrees_compared);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_slot_writes);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_slot_compares);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_expression_materializations);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_rigid_subtrees_reused);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_ground_body_reuses);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_workspace_growths);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_view_nodes);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_view_variable_resolutions);
    PPOSLF_NATIVE_STATS_SUM_V1(ground_dense_view_deferrals);
    PPOSLF_NATIVE_STATS_SUM_V1(positional_linear_rule_attempts);
    PPOSLF_NATIVE_STATS_SUM_V1(positional_linear_rule_matches);
    PPOSLF_NATIVE_STATS_SUM_V1(positional_linear_rule_fallbacks);
    PPOSLF_NATIVE_STATS_SUM_V1(deferred_epoch_goal_materializations);
    PPOSLF_NATIVE_STATS_SUM_V1(epoch_goal_materializations_not_admitted);
    PPOSLF_NATIVE_STATS_SUM_V1(
        epoch_goal_materializations_not_range_restricted);
    PPOSLF_NATIVE_STATS_SUM_V1(
        epoch_goal_materializations_consumer_unsafe);
    PPOSLF_NATIVE_STATS_SUM_V1(epoch_goal_materializations_stale);
    PPOSLF_NATIVE_STATS_SUM_V1(non_epoch_goal_materialization_attempts);
    PPOSLF_NATIVE_STATS_SUM_V1(activation_view_goal_admissions);
    PPOSLF_NATIVE_STATS_SUM_V1(activation_view_rule_attempts);
    PPOSLF_NATIVE_STATS_SUM_V1(activation_view_rule_matches);
    PPOSLF_NATIVE_STATS_SUM_V1(activation_view_fallback_materializations);
    PPOSLF_NATIVE_STATS_SUM_V1(structural_shape_guard_attempts);
    PPOSLF_NATIVE_STATS_SUM_V1(structural_shape_guard_rejections);
    PPOSLF_NATIVE_STATS_SUM_V1(structural_shape_guard_unknowns);
    PPOSLF_NATIVE_STATS_SUM_V1(compiled_application_dispatches);
    PPOSLF_NATIVE_STATS_SUM_V1(rigid_coordinate_dispatches);
    PPOSLF_NATIVE_STATS_SUM_V1(rigid_coordinate_rejections);
    PPOSLF_NATIVE_STATS_SUM_V1(compiled_relation_dispatches);
    PPOSLF_NATIVE_STATS_SUM_V1(compiled_relation_matches);
    PPOSLF_NATIVE_STATS_SUM_V1(compiled_relation_deferrals);
    PPOSLF_NATIVE_STATS_SUM_V1(indexed_candidate_visits);
    PPOSLF_NATIVE_STATS_SUM_V1(full_scan_candidate_visits);
    PPOSLF_NATIVE_STATS_SUM_V1(external_row_candidate_visits);
    PPOSLF_NATIVE_STATS_SUM_V1(external_row_matches);
    PPOSLF_NATIVE_STATS_SUM_V1(external_exact_key_lookups);
    PPOSLF_NATIVE_STATS_SUM_V1(external_exact_key_hits);
    PPOSLF_NATIVE_STATS_SUM_V1(external_prefix_key_lookups);
    PPOSLF_NATIVE_STATS_SUM_V1(external_prefix_key_hits);
    PPOSLF_NATIVE_STATS_SUM_V1(external_prefix_key_candidates);
    PPOSLF_NATIVE_STATS_SUM_V1(external_prefix_memo_hits);
    PPOSLF_NATIVE_STATS_SUM_V1(external_prefix_memo_misses);
    PPOSLF_NATIVE_STATS_SUM_V1(generated_continuations);
    PPOSLF_NATIVE_STATS_SUM_V1(generated_raw_tail_deterministic_continuations);
    PPOSLF_NATIVE_STATS_SUM_V1(generated_tail_deterministic_continuations);
    PPOSLF_NATIVE_STATS_SUM_V1(generated_tail_frame_reuses);
    PPOSLF_NATIVE_STATS_SUM_V1(deferred_shape_guard_candidates);
    PPOSLF_NATIVE_STATS_SUM_V1(deferred_shape_guard_attempts);
    PPOSLF_NATIVE_STATS_SUM_V1(external_continuations);
    PPOSLF_NATIVE_STATS_SUM_V1(external_tail_deterministic_continuations);
    PPOSLF_NATIVE_STATS_SUM_V1(external_tail_frame_reuses);
    PPOSLF_NATIVE_STATS_SUM_V1(deterministic_tail_collections);
    PPOSLF_NATIVE_STATS_SUM_V1(deterministic_tail_collection_failures);
    PPOSLF_NATIVE_STATS_SUM_V1(deterministic_goal_roots_scanned);
    PPOSLF_NATIVE_STATS_SUM_V1(deterministic_binding_collections);
    PPOSLF_NATIVE_STATS_SUM_V1(deterministic_binding_collection_failures);
    PPOSLF_NATIVE_STATS_SUM_V1(deterministic_binding_roots_scanned);
    PPOSLF_NATIVE_STATS_SUM_V1(deterministic_binding_items_discarded);
    PPOSLF_NATIVE_STATS_SUM_V1(deterministic_trail_entries_discarded);
    PPOSLF_NATIVE_STATS_SUM_V1(deterministic_arena_bytes_copied);
    PPOSLF_NATIVE_STATS_SUM_V1(deterministic_arena_bytes_reclaimed);
    PPOSLF_NATIVE_STATS_SUM_V1(goal_materialization_arena_bytes);
    PPOSLF_NATIVE_STATS_SUM_V1(generated_match_arena_bytes);
    PPOSLF_NATIVE_STATS_SUM_V1(body_expansion_arena_bytes);
    PPOSLF_NATIVE_STATS_SUM_V1(pending_goal_node_arena_bytes);
    PPOSLF_NATIVE_STATS_SUM_V1(rollback_arena_bytes_reclaimed);
#undef PPOSLF_NATIVE_STATS_SUM_V1
    if (aggregate->maximum_goal_depth < sample->maximum_goal_depth)
        aggregate->maximum_goal_depth = sample->maximum_goal_depth;
    if (aggregate->maximum_search_frame_depth <
        sample->maximum_search_frame_depth) {
        aggregate->maximum_search_frame_depth =
            sample->maximum_search_frame_depth;
    }
}

typedef struct {
    const PPOSLFNativeTypePlanV1 *plan;
    PPOSLFNativeCompiledRuleV1 *rules;
    uint32_t rule_len;
    PPOSLFNativeApplicationDispatchV1 *application_dispatch;
    uint32_t application_dispatch_len;
    uint32_t variable_group;
    bool has_variable_group;
    CettaGsltPeanoAddPlanV1 *compiled_relations;
    uint32_t compiled_relation_len;
    CettaGsltRigidCoordinateScratchV1 rigid_scratch;
    Arena program_arena;
    char program_digest[65];
    char empty_capability_digest[65];
} PPOSLFNativeTypeVMImplV1;

typedef struct {
    Atom *fact;
    uint8_t *canonical;
    size_t canonical_len;
    uint32_t relation;
    bool owns_canonical;
} PPOSLFNativeCapabilityRowV1;

typedef struct {
    Atom *fact;
    const uint8_t *canonical;
    size_t canonical_len;
    uint32_t relation;
    uint32_t structural_hash;
} PPOSLFNativeCapabilityExactSlotV1;

typedef struct PPOSLFNativeCapabilitySetImplV1
    PPOSLFNativeCapabilitySetImplV1;

struct PPOSLFNativeCapabilitySetImplV1 {
    PPOSLFNativeCapabilityRowV1 *rows;
    uint32_t row_len;
    uint32_t *relation_offsets;
    uint32_t external_relation_len;
    PPOSLFNativeCapabilityExactSlotV1 *exact_slots;
    size_t exact_slot_cap;
    const PPOSLFNativeCapabilitySetImplV1 *exact_base;
    Arena arena;
    char program_digest[65];
    char capability_digest[65];
    bool borrows_facts;
    bool digest_ready;
};

typedef struct {
    Atom *source;
    uint8_t *canonical;
    size_t canonical_len;
    uint32_t relation;
} PPOSLFNativePendingCapabilityRowV1;

typedef struct {
    const Atom *cursor;
    Atom **elements;
    uint32_t arity;
    uint32_t next_child;
} PPOSLFNativeQuotedApplicationFrameV1;

typedef struct PPOSLFNativePendingGoalV1 PPOSLFNativePendingGoalV1;
enum {
    PPOSLF_NATIVE_ACTIVATION_VIEW_ADMITTED_V1 = 0u,
    PPOSLF_NATIVE_ACTIVATION_VIEW_OPEN_PRODUCER_V1 = 1u,
    PPOSLF_NATIVE_ACTIVATION_VIEW_UNSAFE_CONSUMER_V1 = 2u,
};
struct PPOSLFNativePendingGoalV1 {
    Atom *goal;
    uint32_t depth;
    uint32_t epoch;
    uint32_t activation_first_entry;
    bool epoch_original;
    bool activation_view_admitted;
    uint8_t activation_view_refusal_reason;
    PPOSLFNativePendingGoalV1 *next;
};

typedef struct {
    Atom *prefix;
    uint8_t *canonical;
    size_t canonical_len;
    uint32_t structural_hash;
} PPOSLFNativePrefixMemoSlotV1;

typedef struct {
    PPOSLFNativePrefixMemoSlotV1 *slots;
    size_t len;
    size_t cap;
    Arena arena;
} PPOSLFNativePrefixMemoV1;

typedef struct {
    uint32_t prior;
    uint32_t candidate_count;
    bool full_scan;
} PPOSLFNativeFailureDebtV1;

typedef struct {
    const PPOSLFNativeTypeVMImplV1 *vm;
    PPOSLFNativeVMLimitsV1 limits;
    PPOSLFNativeVMStatsV1 stats;
    Arena scratch;
    Arena survivor;
    BindingsBuilder bindings;
    CettaGsltGroundDenseWorkspaceV1 ground_dense_workspace;
    PPOSLFNativePrefixMemoV1 prefix_memo;
    const PPOSLFNativeCapabilitySetImplV1 *capabilities;
    PPOSLFNativeVMProofEventV1 *proof_events;
    uint32_t proof_event_len;
    uint32_t proof_event_cap;
    PPOSLFNativeFailureDebtV1 *failure_debts;
    uint32_t failure_debt_len;
    uint32_t failure_debt_cap;
    size_t deterministic_collect_after;
    uint64_t binding_growth_collect_after;
} PPOSLFNativeSearchV1;

typedef enum {
    PPOSLF_NATIVE_SEARCH_PROVED_V1,
    PPOSLF_NATIVE_SEARCH_NO_PROOF_V1,
    PPOSLF_NATIVE_SEARCH_RESOURCE_V1,
} PPOSLFNativeSearchOutcomeV1;

typedef struct {
    PPOSLFNativeTermKindV1 kind;
    uint32_t exact_group;
    uint32_t variable_group;
    uint32_t external_relation;
    bool full_scan;
    bool has_exact;
    bool has_variable;
    bool has_external;
    bool used_compiled_application;
    const CettaGsltPeanoAddPlanV1 *compiled_relation;
    uint32_t compiled_relation_index;
    const CettaGsltRigidCoordinateIndexV1 *rigid_index;
} PPOSLFNativeGoalDispatchV1;

static void pposlf_native_type_vm_v1_set_error(
    char *error_buf, size_t error_buf_size, const char *format, ...) {
    va_list args;

    if (!error_buf || error_buf_size == 0u)
        return;
    va_start(args, format);
    vsnprintf(error_buf, error_buf_size, format, args);
    va_end(args);
}

static bool pposlf_native_type_vm_v1_expr_head(
    const Atom *term, const char *head, CettaExprLen arity) {
    return term && term->kind == ATOM_EXPR &&
           term->expr.len == arity + 1u &&
           atom_is_symbol(term->expr.elems[0], head);
}

/* Substitution can replace variables but cannot replace an enclosing
 * application constructor.  This local recognizer is therefore the exact
 * admission condition for dispatching an activation closure from its source
 * term while deferring all descendant substitution. */
static bool pposlf_native_type_vm_v1_rigid_application_dispatch(
    const Atom *term) {
    return term && term->kind == ATOM_EXPR && term->expr.len > 0u &&
           term->expr.elems[0] &&
           term->expr.elems[0]->kind == ATOM_SYMBOL;
}

typedef struct {
    const Atom *source;
    const Atom *pattern;
} PPOSLFNativeActivationViewPairV1;

/* Decide whether matching `source` as an activation view against `pattern`
 * can demand construction of a substituted source expression.  A pattern
 * variable captures its entire counterpart; that is allocation-free only
 * when the source counterpart is not a variable-bearing expression. */
static bool pposlf_native_type_vm_v1_activation_pair_analyze(
    const Atom *source, const Atom *pattern, bool *admitted_out) {
    PPOSLFNativeActivationViewPairV1 inline_pairs[32];
    PPOSLFNativeActivationViewPairV1 *pairs = inline_pairs;
    size_t pair_len = 0u;
    size_t pair_cap = sizeof(inline_pairs) / sizeof(inline_pairs[0]);
    bool ok = false;

    if (admitted_out)
        *admitted_out = false;
    if (!source || !pattern || !admitted_out)
        return false;
    pairs[pair_len++] = (PPOSLFNativeActivationViewPairV1){
        .source = source,
        .pattern = pattern,
    };
    while (pair_len != 0u) {
        PPOSLFNativeActivationViewPairV1 pair = pairs[--pair_len];

        if (!pair.source || !pair.pattern)
            goto done;
        if (pair.pattern->kind == ATOM_VAR) {
            if (pair.source->kind == ATOM_EXPR &&
                atom_has_vars((Atom *)pair.source)) {
                ok = true;
                goto done;
            }
            continue;
        }
        if (pair.source->kind != ATOM_EXPR ||
            pair.pattern->kind != ATOM_EXPR ||
            pair.source->expr.len != pair.pattern->expr.len)
            continue;
        if ((size_t)pair.source->expr.len > SIZE_MAX - pair_len)
            goto done;
        size_t required = pair_len + (size_t)pair.source->expr.len;
        if (required > pair_cap) {
            size_t next_cap = pair_cap;
            PPOSLFNativeActivationViewPairV1 *grown;

            while (next_cap < required) {
                if (next_cap > SIZE_MAX / 2u)
                    goto done;
                next_cap *= 2u;
            }
            if (next_cap > SIZE_MAX / sizeof(*pairs))
                goto done;
            grown = malloc(next_cap * sizeof(*pairs));
            if (!grown)
                goto done;
            memcpy(grown, pairs, pair_len * sizeof(*pairs));
            if (pairs != inline_pairs)
                free(pairs);
            pairs = grown;
            pair_cap = next_cap;
        }
        for (CettaExprIndex child = 0u;
             child < pair.source->expr.len; child++) {
            pairs[pair_len++] = (PPOSLFNativeActivationViewPairV1){
                .source = pair.source->expr.elems[child],
                .pattern = pair.pattern->expr.elems[child],
            };
        }
    }
    *admitted_out = true;
    ok = true;

done:
    if (pairs != inline_pairs)
        free(pairs);
    return ok;
}

static bool pposlf_native_type_vm_v1_activation_view_analyze(
    const PPOSLFNativeTypeVMImplV1 *impl,
    const Atom *source, bool *admitted_out) {
    const Atom *source_head;

    if (admitted_out)
        *admitted_out = false;
    if (!impl || !source || !admitted_out ||
        !pposlf_native_type_vm_v1_rigid_application_dispatch(source))
        return admitted_out != NULL;
    source_head = source->expr.elems[0];
    for (uint32_t candidate = 0u;
         candidate < impl->rule_len; candidate++) {
        const Atom *pattern = impl->rules[candidate].head;
        bool pair_admitted;

        if (!pattern)
            return false;
        if (pattern->kind == ATOM_VAR) {
            *admitted_out = false;
            return true;
        }
        if (!pposlf_native_type_vm_v1_rigid_application_dispatch(
                pattern) ||
            pattern->expr.elems[0]->sym_id != source_head->sym_id)
            continue;
        if (!pposlf_native_type_vm_v1_activation_pair_analyze(
                source, pattern, &pair_admitted))
            return false;
        if (!pair_admitted) {
            *admitted_out = false;
            return true;
        }
    }
    *admitted_out = true;
    return true;
}

static const char *pposlf_native_type_vm_v1_quoted_symbol(
    const Atom *term) {
    const Atom *symbol;
    const char *name;

    if (!pposlf_native_type_vm_v1_expr_head(term, "q-sym", 1u))
        return NULL;
    symbol = term->expr.elems[1];
    if (!symbol || symbol->kind != ATOM_SYMBOL)
        return NULL;
    name = atom_name_cstr((Atom *)symbol);
    return name && name[0] != '\0' ? name : NULL;
}

static bool pposlf_native_type_vm_v1_q_list_length(
    const Atom *list, uint32_t *length_out) {
    const Atom *cursor = list;
    uint32_t length = 0u;

    if (!length_out)
        return false;
    while (pposlf_native_type_vm_v1_expr_head(
               cursor, "q-cons", 2u)) {
        if (length == UINT32_MAX)
            return false;
        length++;
        cursor = cursor->expr.elems[2];
    }
    if (!atom_is_symbol((Atom *)cursor, "q-nil"))
        return false;
    *length_out = length;
    return true;
}

static bool pposlf_native_type_vm_v1_decode_quoted_ground(
    const PPOSLFNativeTypePlanV1 *plan,
    Arena *arena,
    const Atom *quoted,
    Atom **result_out,
    char *error_buf,
    size_t error_buf_size) {
    PPOSLFNativeQuotedApplicationFrameV1 *stack = NULL;
    size_t stack_len = 0u;
    size_t stack_cap = 0u;
    const Atom *current = quoted;
    Atom *completed = NULL;
    bool ok = false;

    if (!plan || !arena || !quoted || !result_out)
        return false;
    *result_out = NULL;
    for (;;) {
        const char *text;

        if ((text = pposlf_native_type_vm_v1_quoted_symbol(current))) {
            completed = atom_symbol(arena, text);
        } else if (pposlf_native_type_vm_v1_expr_head(
                       current, "q-int", 1u)) {
            const Atom *value = current->expr.elems[1];

            if (value && value->kind == ATOM_GROUNDED &&
                value->ground.gkind == GV_INT) {
                completed = atom_int(arena, value->ground.ival);
            } else if (value && value->kind == ATOM_GROUNDED &&
                       value->ground.gkind == GV_BIGINT &&
                       atom_bigint_cstr(value)) {
                completed = atom_bigint(arena, atom_bigint_cstr(value));
            } else {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "reflected capability contains a malformed quoted integer");
                goto done;
            }
        } else if (pposlf_native_type_vm_v1_expr_head(
                       current, "q-str", 1u)) {
            const Atom *value = current->expr.elems[1];

            if (!value || value->kind != ATOM_GROUNDED ||
                value->ground.gkind != GV_STRING || !value->ground.sval) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "reflected capability contains a malformed quoted string");
                goto done;
            }
            completed = atom_string(arena, value->ground.sval);
        } else if (pposlf_native_type_vm_v1_expr_head(
                       current, "q-var", 1u)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "reflected capability fact contains a quoted variable");
            goto done;
        } else if (pposlf_native_type_vm_v1_expr_head(
                       current, "q-app", 2u)) {
            const Atom *arguments = current->expr.elems[2];
            uint32_t arity;
            uint32_t expected_arity;
            Atom **elements;

            text = pposlf_native_type_vm_v1_quoted_symbol(
                current->expr.elems[1]);
            if (!text ||
                !pposlf_native_type_vm_v1_q_list_length(
                    arguments, &arity)) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "reflected capability contains a malformed quoted application");
                goto done;
            }
            if (!pposlf_native_type_plan_v1_head_arity(
                    plan, text, &expected_arity) ||
                expected_arity != arity) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "reflected capability application violates the generated signature");
                goto done;
            }
            if (arity == UINT32_MAX ||
                (size_t)arity + 1u >
                SIZE_MAX / sizeof(*elements)) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "reflected capability application is too large");
                goto done;
            }
            elements = calloc((size_t)arity + 1u, sizeof(*elements));
            if (!elements) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "cannot allocate a reflected capability application");
                goto done;
            }
            elements[0] = atom_symbol(arena, text);
            if (arity == 0u) {
                completed = atom_expr(arena, elements, 1u);
                free(elements);
            } else {
                PPOSLFNativeQuotedApplicationFrameV1 *frame;

                if (stack_len == stack_cap) {
                    size_t next_cap = stack_cap ? stack_cap * 2u : 32u;
                    PPOSLFNativeQuotedApplicationFrameV1 *next;

                    if (next_cap < stack_cap ||
                        next_cap > SIZE_MAX / sizeof(*next)) {
                        free(elements);
                        pposlf_native_type_vm_v1_set_error(
                            error_buf, error_buf_size,
                            "reflected capability nesting is too deep");
                        goto done;
                    }
                    next = realloc(stack, next_cap * sizeof(*next));
                    if (!next) {
                        free(elements);
                        pposlf_native_type_vm_v1_set_error(
                            error_buf, error_buf_size,
                            "cannot allocate reflected capability traversal state");
                        goto done;
                    }
                    stack = next;
                    stack_cap = next_cap;
                }
                frame = &stack[stack_len++];
                *frame = (PPOSLFNativeQuotedApplicationFrameV1){
                    .cursor = arguments,
                    .elements = elements,
                    .arity = arity,
                    .next_child = 0u,
                };
                current = arguments->expr.elems[1];
                continue;
            }
        } else {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "reflected capability contains a malformed quoted term");
            goto done;
        }

        for (;;) {
            PPOSLFNativeQuotedApplicationFrameV1 *frame;

            if (!completed) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "cannot construct a reflected capability term");
                goto done;
            }
            if (stack_len == 0u) {
                *result_out = completed;
                ok = true;
                goto done;
            }
            frame = &stack[stack_len - 1u];
            frame->elements[frame->next_child + 1u] = completed;
            frame->next_child++;
            frame->cursor = frame->cursor->expr.elems[2];
            if (frame->next_child < frame->arity) {
                current = frame->cursor->expr.elems[1];
                break;
            }
            if (!atom_is_symbol((Atom *)frame->cursor, "q-nil")) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "reflected capability argument list changed during decoding");
                goto done;
            }
            completed = atom_expr(
                arena, frame->elements,
                (CettaExprLen)frame->arity + 1u);
            free(frame->elements);
            frame->elements = NULL;
            stack_len--;
        }
    }

done:
    for (size_t index = 0u; index < stack_len; index++)
        free(stack[index].elements);
    free(stack);
    return ok;
}

static void pposlf_native_type_vm_v1_impl_free(
    PPOSLFNativeTypeVMImplV1 *impl) {
    if (!impl)
        return;
    if (impl->rules) {
        for (uint32_t rule_index = 0u;
             rule_index < impl->rule_len; rule_index++) {
            PPOSLFNativeCompiledRuleV1 *rule =
                &impl->rules[rule_index];
            if (rule->ground_dense_body) {
                for (uint32_t body = 0u;
                     body < rule->body_len; body++) {
                    cetta_gslt_ground_dense_term_program_free_v1(
                        &rule->ground_dense_body[body]);
                }
            }
            free(rule->ground_dense_body);
            free(rule->body_ground);
            free(rule->body_activation_view_admitted);
            cetta_gslt_ground_dense_term_program_free_v1(
                &rule->ground_dense_head);
        }
    }
    if (impl->application_dispatch) {
        for (uint32_t index = 0u;
             index < impl->application_dispatch_len; index++)
            cetta_gslt_rigid_coordinate_index_free_v1(
                &impl->application_dispatch[index].rigid_index);
    }
    cetta_gslt_rigid_coordinate_scratch_free_v1(&impl->rigid_scratch);
    arena_free(&impl->program_arena);
    free(impl->compiled_relations);
    free(impl->application_dispatch);
    free(impl->rules);
    free(impl);
}

static void pposlf_native_capability_set_v1_impl_free(
    PPOSLFNativeCapabilitySetImplV1 *impl) {
    if (!impl)
        return;
    if (impl->rows) {
        for (uint32_t index = 0u; index < impl->row_len; index++) {
            if (impl->rows[index].owns_canonical)
                free(impl->rows[index].canonical);
        }
    }
    arena_free(&impl->arena);
    free(impl->exact_slots);
    free(impl->relation_offsets);
    free(impl->rows);
    free(impl);
}

void pposlf_native_capability_set_v1_init(
    PPOSLFNativeCapabilitySetV1 *capabilities) {
    if (capabilities)
        capabilities->impl = NULL;
}

void pposlf_native_capability_set_v1_free(
    PPOSLFNativeCapabilitySetV1 *capabilities) {
    if (!capabilities)
        return;
    pposlf_native_capability_set_v1_impl_free(capabilities->impl);
    capabilities->impl = NULL;
}

static void pposlf_native_capability_digest_v1_begin(
    CettaNativeSha256 *sha, const char program_digest[65]) {
    static const uint8_t domain[] =
        "cetta-oslf-native-capability-set-v1\n";

    cetta_native_sha256_init(sha);
    cetta_native_sha256_update(sha, domain, sizeof(domain) - 1u);
    cetta_native_sha256_update(
        sha, (const uint8_t *)program_digest, 64u);
    cetta_native_sha256_update(sha, (const uint8_t *)"\n", 1u);
}

static void pposlf_native_capability_digest_v1_row(
    CettaNativeSha256 *sha, const uint8_t *canonical, size_t len) {
    uint8_t encoded_len[8];
    uint64_t value = (uint64_t)len;

    for (uint32_t index = 0u; index < 8u; index++) {
        encoded_len[7u - index] = (uint8_t)(value & 0xffu);
        value >>= 8u;
    }
    cetta_native_sha256_update(sha, encoded_len, sizeof(encoded_len));
    cetta_native_sha256_update(sha, canonical, len);
}

static void pposlf_native_empty_capability_digest_v1(
    const char program_digest[65], char out[65]) {
    CettaNativeSha256 sha;

    pposlf_native_capability_digest_v1_begin(&sha, program_digest);
    cetta_native_sha256_finish_hex(&sha, out);
}

static bool pposlf_native_type_vm_v1_term_uses_text(
    PPOSLFNativeTermKindV1 kind) {
    return kind == PPOSLF_NATIVE_TERM_SYMBOL_V1 ||
           kind == PPOSLF_NATIVE_TERM_INTEGER_BIG_V1 ||
           kind == PPOSLF_NATIVE_TERM_STRING_V1 ||
           kind == PPOSLF_NATIVE_TERM_APPLICATION_V1;
}

static bool pposlf_native_type_vm_v1_validate_plan(
    const PPOSLFNativeTypePlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    bool *seen = NULL;
    bool ok = false;

    if (!plan || !g_symbols ||
        (plan->head_signature_len > 0u && !plan->head_signatures) ||
        (plan->term_len > 0u && !plan->terms) ||
        (plan->term_edge_len > 0u && !plan->term_edges) ||
        (plan->body_root_len > 0u && !plan->body_roots) ||
        (plan->step_schema_len > 0u && !plan->step_schemas) ||
        (plan->head_group_len > 0u && !plan->head_groups) ||
        (plan->external_relation_len > 0u &&
         !plan->external_relations) ||
        (plan->open_head_len > 0u && !plan->open_heads) ||
        plan->head_step_index_len != plan->step_schema_len ||
        (plan->head_step_index_len > 0u && !plan->head_step_indices) ||
        (plan->step_schema_len > 0u && plan->head_group_len == 0u) ||
        plan->semantic_digest[64] != '\0') {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "native NTT VM received an incomplete admitted plan");
        return false;
    }
    for (uint32_t index = 0u; index < 64u; index++) {
        if (!isxdigit((unsigned char)plan->semantic_digest[index])) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT program digest is malformed");
            return false;
        }
    }
    for (uint32_t index = 0u; index < plan->head_signature_len; index++) {
        uint32_t arity;
        const PPOSLFNativeHeadSignatureV1 *signature =
            &plan->head_signatures[index];

        if (!signature->constructor ||
            !pposlf_native_type_plan_v1_head_arity(
                plan, signature->constructor, &arity) ||
            arity != signature->arity ||
            (index > 0u &&
             strcmp(plan->head_signatures[index - 1u].constructor,
                    signature->constructor) >= 0)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT head-signature index is inconsistent");
            return false;
        }
    }
    for (uint32_t index = 0u; index < plan->term_len; index++) {
        const PPOSLFNativeTermV1 *term = &plan->terms[index];
        bool application =
            term->kind == PPOSLF_NATIVE_TERM_APPLICATION_V1;

        if ((uint32_t)term->kind >
                (uint32_t)PPOSLF_NATIVE_TERM_APPLICATION_V1 ||
            term->edge_begin > plan->term_edge_len ||
            term->edge_len > plan->term_edge_len - term->edge_begin ||
            (!application && term->edge_len != 0u) ||
            (pposlf_native_type_vm_v1_term_uses_text(term->kind) &&
             !term->text)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT term vector is inconsistent");
            return false;
        }
        if (application) {
            uint32_t arity;
            if (!pposlf_native_type_plan_v1_head_arity(
                    plan, term->text, &arity) ||
                arity != term->edge_len) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "native NTT application violates its signature");
                return false;
            }
        }
    }
    for (uint32_t index = 0u; index < plan->term_edge_len; index++) {
        if (plan->term_edges[index] >= plan->term_len) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT term edge is out of range");
            return false;
        }
    }
    for (uint32_t index = 0u; index < plan->body_root_len; index++) {
        if (plan->body_roots[index] >= plan->term_len) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT body root is out of range");
            return false;
        }
    }
    seen = calloc(
        plan->step_schema_len ? plan->step_schema_len : 1u,
        sizeof(*seen));
    if (!seen) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot validate the native NTT head index");
        return false;
    }
    for (uint32_t group_index = 0u;
         group_index < plan->head_group_len; group_index++) {
        const PPOSLFNativeHeadGroupV1 *group =
            &plan->head_groups[group_index];
        uint32_t found;

        if ((uint32_t)group->kind >
                (uint32_t)PPOSLF_NATIVE_TERM_APPLICATION_V1 ||
            (pposlf_native_type_vm_v1_term_uses_text(group->kind) &&
             !group->text) ||
            group->step_begin > plan->head_step_index_len ||
            group->step_len >
                plan->head_step_index_len - group->step_begin ||
            !pposlf_native_type_plan_v1_head_group(
                plan, group->kind, group->text, group->integer, &found) ||
            found != group_index) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT head-group index is inconsistent");
            goto done;
        }
        for (uint32_t item = 0u; item < group->step_len; item++) {
            uint32_t step_index = plan->head_step_indices[
                group->step_begin + item];
            const PPOSLFNativeTermV1 *head;

            if (step_index >= plan->step_schema_len || seen[step_index]) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "native NTT head index duplicates or loses a rule");
                goto done;
            }
            seen[step_index] = true;
            if (plan->step_schemas[step_index].head >= plan->term_len) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "native NTT rule head is out of range");
                goto done;
            }
            head = &plan->terms[plan->step_schemas[step_index].head];
            if (head->kind != group->kind ||
                (pposlf_native_type_vm_v1_term_uses_text(head->kind) &&
                 strcmp(head->text, group->text) != 0) ||
                (head->kind == PPOSLF_NATIVE_TERM_INTEGER_I64_V1 &&
                 head->integer != group->integer)) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "native NTT head index classifies a rule incorrectly");
                goto done;
            }
        }
    }
    for (uint32_t index = 0u; index < plan->step_schema_len; index++) {
        const PPOSLFNativeStepSchemaV1 *step =
            &plan->step_schemas[index];
        if (!seen[index] || !step->owner || !step->rule ||
            step->head >= plan->term_len ||
            step->body_begin > plan->body_root_len ||
            step->body_len > plan->body_root_len - step->body_begin ||
            (index > 0u &&
             (strcmp(plan->step_schemas[index - 1u].owner,
                     step->owner) > 0 ||
              (strcmp(plan->step_schemas[index - 1u].owner,
                      step->owner) == 0 &&
               strcmp(plan->step_schemas[index - 1u].rule,
                      step->rule) >= 0)))) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT rule or head index is incomplete");
            goto done;
        }
    }
    for (uint32_t index = 0u;
         index < plan->external_relation_len; index++) {
        if (!plan->external_relations[index].owner ||
            !plan->external_relations[index].relation) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT extensional interface is incomplete");
            goto done;
        }
    }
    for (uint32_t index = 0u;
         index < plan->external_relation_len; index++) {
        const PPOSLFNativeExternalRelationV1 *external =
            &plan->external_relations[index];
        uint32_t found;
        uint32_t signature_arity;
        uint32_t body_occurrences = 0u;
        int prior_relation = 0;

        if (index > 0u)
            prior_relation = strcmp(
                plan->external_relations[index - 1u].relation,
                external->relation);
        for (uint32_t body = 0u; body < plan->body_root_len; body++) {
            const PPOSLFNativeTermV1 *term =
                &plan->terms[plan->body_roots[body]];
            if (term->kind == PPOSLF_NATIVE_TERM_APPLICATION_V1 &&
                term->text && external->relation &&
                strcmp(term->text, external->relation) == 0 &&
                term->edge_len == external->arity)
                body_occurrences++;
        }
        if (external->body_occurrences == 0u ||
            external->body_occurrences != body_occurrences ||
            !pposlf_native_type_plan_v1_head_arity(
                plan, external->relation, &signature_arity) ||
            signature_arity != external->arity ||
            !pposlf_native_type_plan_v1_external_relation(
                plan, external->relation, external->arity, &found) ||
            found != index ||
            (index > 0u &&
             (prior_relation > 0 ||
              (prior_relation == 0 &&
               plan->external_relations[index - 1u].arity >=
                   external->arity)))) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT extensional-interface index is inconsistent");
            goto done;
        }
    }
    for (uint32_t index = 0u; index < plan->open_head_len; index++) {
        const PPOSLFNativeOpenHeadV1 *open = &plan->open_heads[index];
        uint32_t defined_group;
        uint32_t external;

        if ((uint32_t)open->kind >
                (uint32_t)PPOSLF_NATIVE_TERM_APPLICATION_V1 ||
            (pposlf_native_type_vm_v1_term_uses_text(open->kind) &&
             !open->text) ||
            open->body_occurrences == 0u ||
            (open->kind != PPOSLF_NATIVE_TERM_APPLICATION_V1 &&
             open->arity != 0u) ||
            open->kind != PPOSLF_NATIVE_TERM_APPLICATION_V1 ||
            !pposlf_native_type_plan_v1_external_relation(
                plan, open->text, open->arity, &external) ||
            pposlf_native_type_plan_v1_head_group(
                plan, open->kind, open->text, open->integer,
                &defined_group)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT open-relation inventory is inconsistent");
            goto done;
        }
    }
    ok = true;

done:
    free(seen);
    return ok;
}

static bool pposlf_native_type_vm_v1_relation_is_open(
    const PPOSLFNativeTypePlanV1 *plan,
    const char *relation,
    uint32_t arity) {
    if (!plan || !relation)
        return false;
    for (uint32_t index = 0u; index < plan->open_head_len; index++) {
        const PPOSLFNativeOpenHeadV1 *open = &plan->open_heads[index];

        if (open->kind == PPOSLF_NATIVE_TERM_APPLICATION_V1 &&
            open->text && strcmp(open->text, relation) == 0 &&
            open->arity == arity)
            return true;
    }
    return false;
}

static int pposlf_native_pending_capability_row_v1_compare(
    const void *left_value, const void *right_value) {
    const PPOSLFNativePendingCapabilityRowV1 *left = left_value;
    const PPOSLFNativePendingCapabilityRowV1 *right = right_value;
    size_t common;
    int comparison;

    if (left->relation < right->relation)
        return -1;
    if (left->relation > right->relation)
        return 1;
    common = left->canonical_len < right->canonical_len
                 ? left->canonical_len : right->canonical_len;
    comparison = memcmp(left->canonical, right->canonical, common);
    if (comparison != 0)
        return comparison;
    if (left->canonical_len < right->canonical_len)
        return -1;
    if (left->canonical_len > right->canonical_len)
        return 1;
    return 0;
}

static int pposlf_native_capability_row_pending_v1_compare(
    const PPOSLFNativeCapabilityRowV1 *left,
    const PPOSLFNativePendingCapabilityRowV1 *right) {
    size_t common;
    int comparison;

    if (left->relation < right->relation)
        return -1;
    if (left->relation > right->relation)
        return 1;
    common = left->canonical_len < right->canonical_len
                 ? left->canonical_len : right->canonical_len;
    comparison = memcmp(left->canonical, right->canonical, common);
    if (comparison != 0)
        return comparison;
    if (left->canonical_len < right->canonical_len)
        return -1;
    if (left->canonical_len > right->canonical_len)
        return 1;
    return 0;
}

static size_t pposlf_native_capability_exact_slot_v1(
    uint32_t relation, uint32_t structural_hash, size_t mask) {
    uint64_t key = ((uint64_t)relation << 32u) | structural_hash;

    /* SplitMix64 finalization prevents either generated relation numbering or
     * the Atom hash's low bits from determining the open-addressing bucket. */
    key ^= key >> 30u;
    key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27u;
    key *= UINT64_C(0x94d049bb133111eb);
    key ^= key >> 31u;
    return (size_t)key & mask;
}

static bool pposlf_native_capability_exact_index_v1_capacity(
    size_t row_len, size_t *cap_out) {
    size_t cap = 8u;

    if (!cap_out)
        return false;
    while (row_len > cap / 2u) {
        if (cap > SIZE_MAX / 2u)
            return false;
        cap *= 2u;
    }
    *cap_out = cap;
    return true;
}

static bool pposlf_native_capability_exact_index_v1_insert(
    PPOSLFNativeCapabilityExactSlotV1 *slots, size_t cap,
    Atom *fact, const uint8_t *canonical, size_t canonical_len,
    uint32_t relation, uint32_t structural_hash,
    size_t *inserted_slot_out) {
    size_t slot_index;

    if (!slots || cap == 0u || (cap & (cap - 1u)) != 0u ||
        !fact || !canonical)
        return false;
    slot_index = pposlf_native_capability_exact_slot_v1(
        relation, structural_hash, cap - 1u);
    for (size_t probe = 0u; probe < cap; probe++) {
        PPOSLFNativeCapabilityExactSlotV1 *slot = &slots[slot_index];

        if (!slot->fact) {
            *slot = (PPOSLFNativeCapabilityExactSlotV1){
                .fact = fact,
                .canonical = canonical,
                .canonical_len = canonical_len,
                .relation = relation,
                .structural_hash = structural_hash,
            };
            if (inserted_slot_out)
                *inserted_slot_out = slot_index;
            return true;
        }
        if (slot->structural_hash == structural_hash &&
            slot->relation == relation && atom_eq(slot->fact, fact))
            return false;
        slot_index = (slot_index + 1u) & (cap - 1u);
    }
    return false;
}

static bool pposlf_native_capability_exact_index_v1_build(
    PPOSLFNativeCapabilitySetImplV1 *capabilities,
    bool owned_canonical_only,
    char *error_buf, size_t error_buf_size) {
    size_t row_count = 0u;
    size_t cap;

    if (!capabilities || capabilities->exact_slots ||
        capabilities->exact_slot_cap != 0u)
        return false;
    for (uint32_t row_index = 0u;
         row_index < capabilities->row_len; row_index++) {
        if (!owned_canonical_only ||
            capabilities->rows[row_index].owns_canonical)
            row_count++;
    }
    if (row_count == 0u)
        return true;
    if (!pposlf_native_capability_exact_index_v1_capacity(
            row_count, &cap) ||
        cap > SIZE_MAX / sizeof(*capabilities->exact_slots)) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "extensional capability exact index is too large");
        return false;
    }
    capabilities->exact_slots = calloc(
        cap, sizeof(*capabilities->exact_slots));
    if (!capabilities->exact_slots) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate an extensional capability exact index");
        return false;
    }
    capabilities->exact_slot_cap = cap;
    for (uint32_t row_index = 0u;
         row_index < capabilities->row_len; row_index++) {
        PPOSLFNativeCapabilityRowV1 *row =
            &capabilities->rows[row_index];
        uint32_t structural_hash;

        if (owned_canonical_only && !row->owns_canonical)
            continue;
        if (!row->fact) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "extensional capability exact index lost a row");
            return false;
        }
        structural_hash = atom_hash(row->fact);
        if (!pposlf_native_capability_exact_index_v1_insert(
                capabilities->exact_slots, cap, row->fact,
                row->canonical, row->canonical_len, row->relation,
                structural_hash, NULL)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "extensional capability exact index found a duplicate or became full");
            return false;
        }
    }
    return true;
}

static bool pposlf_native_capability_pending_rows_v1_prepare(
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    PPOSLFNativePendingCapabilityRowV1 *pending,
    char *error_buf,
    size_t error_buf_size) {
    for (uint32_t index = 0u; index < row_len; index++) {
        Atom *row = rows[index];
        Atom *head;
        const char *relation;
        size_t arity_size;
        uint32_t arity;
        uint32_t relation_index;

        if (!row || row->kind != ATOM_EXPR ||
            !cetta_expr_len_fits_size(row->expr.len) ||
            row->expr.len == 0u) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "extensional capability row must be an application");
            return false;
        }
        arity_size = (size_t)row->expr.len - 1u;
        if (arity_size > UINT32_MAX) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "extensional capability row arity is too large");
            return false;
        }
        arity = (uint32_t)arity_size;
        head = row->expr.elems[0];
        relation = head && head->kind == ATOM_SYMBOL
                       ? atom_name_cstr(head) : NULL;
        if (!relation ||
            !pposlf_native_type_plan_v1_external_relation(
                plan, relation, arity, &relation_index) ||
            !pposlf_native_type_vm_v1_relation_is_open(
                plan, relation, arity)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "extensional capability row targets no authored open relation");
            return false;
        }
        if (!fh_ground_term_v1_render(
                row, &pending[index].canonical,
                &pending[index].canonical_len,
                error_buf, error_buf_size))
            return false;
        pending[index].source = row;
        pending[index].relation = relation_index;
    }
    qsort(pending, row_len, sizeof(*pending),
          pposlf_native_pending_capability_row_v1_compare);
    for (uint32_t index = 1u; index < row_len; index++) {
        if (pposlf_native_pending_capability_row_v1_compare(
                &pending[index - 1u], &pending[index]) == 0) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "extensional capability row is duplicated");
            return false;
        }
    }
    return true;
}

static bool pposlf_native_type_vm_v1_validate_ground_term(
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *root,
    char *error_buf,
    size_t error_buf_size) {
    Atom **stack = NULL;
    size_t stack_len = 0u;
    size_t stack_cap = 0u;
    bool ok = false;

    if (!plan || !root)
        return false;
    stack_cap = 32u;
    stack = malloc(stack_cap * sizeof(*stack));
    if (!stack) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot validate an extensional capability row");
        return false;
    }
    stack[stack_len++] = root;
    while (stack_len > 0u) {
        Atom *term = stack[--stack_len];

        if (!term) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "extensional capability row contains a null term");
            goto done;
        }
        if (term->kind == ATOM_EXPR) {
            Atom *head;
            const char *constructor;
            size_t child_len;
            uint32_t arity;

            if (!cetta_expr_len_fits_size(term->expr.len) ||
                term->expr.len == 0u) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "extensional capability row contains a malformed application");
                goto done;
            }
            child_len = (size_t)term->expr.len - 1u;
            if (child_len > UINT32_MAX) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "extensional capability application arity is too large");
                goto done;
            }
            head = term->expr.elems[0];
            constructor = head && head->kind == ATOM_SYMBOL
                              ? atom_name_cstr(head) : NULL;
            if (!constructor ||
                !pposlf_native_type_plan_v1_head_arity(
                    plan, constructor, &arity) ||
                arity != (uint32_t)child_len) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "extensional capability application violates the program signature");
                goto done;
            }
            if (child_len > SIZE_MAX - stack_len ||
                stack_len + child_len > SIZE_MAX / sizeof(*stack)) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "extensional capability term is too large");
                goto done;
            }
            if (stack_len + child_len > stack_cap) {
                size_t next_cap = stack_cap;
                Atom **next;

                while (next_cap < stack_len + child_len) {
                    if (next_cap > SIZE_MAX / 2u) {
                        pposlf_native_type_vm_v1_set_error(
                            error_buf, error_buf_size,
                            "extensional capability term is too large");
                        goto done;
                    }
                    next_cap *= 2u;
                }
                next = realloc(stack, next_cap * sizeof(*next));
                if (!next) {
                    pposlf_native_type_vm_v1_set_error(
                        error_buf, error_buf_size,
                        "cannot validate an extensional capability term");
                    goto done;
                }
                stack = next;
                stack_cap = next_cap;
            }
            for (size_t child = 0u; child < child_len; child++)
                stack[stack_len++] = term->expr.elems[child + 1u];
        } else if (term->kind == ATOM_VAR ||
                   (term->kind == ATOM_GROUNDED &&
                    term->ground.gkind != GV_INT &&
                    term->ground.gkind != GV_BIGINT &&
                    term->ground.gkind != GV_STRING)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "extensional capability row is outside the ground term carrier");
            goto done;
        }
    }
    ok = true;

done:
    free(stack);
    return ok;
}

static bool pposlf_native_capability_set_v1_prepare_mode(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    bool borrow_rows,
    char *error_buf,
    size_t error_buf_size) {
    PPOSLFNativeCapabilitySetImplV1 *replacement = NULL;
    PPOSLFNativePendingCapabilityRowV1 *pending = NULL;
    CettaNativeSha256 sha;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!capabilities || (row_len > 0u && !rows) ||
        !pposlf_native_type_vm_v1_validate_plan(
            plan, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0')
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "invalid extensional capability preparation request");
        return false;
    }
    replacement = calloc(1u, sizeof(*replacement));
    pending = calloc(row_len ? row_len : 1u, sizeof(*pending));
    if (!replacement || !pending) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate an extensional capability set");
        goto done;
    }
    arena_init(&replacement->arena);
    arena_set_runtime_kind(
        &replacement->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    replacement->row_len = row_len;
    replacement->external_relation_len = plan->external_relation_len;
    replacement->borrows_facts = borrow_rows;
    memcpy(replacement->program_digest, plan->semantic_digest,
           sizeof(replacement->program_digest));
    replacement->rows = calloc(
        row_len ? row_len : 1u, sizeof(*replacement->rows));
    if ((size_t)plan->external_relation_len + 1u >
        SIZE_MAX / sizeof(*replacement->relation_offsets)) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "extensional capability relation inventory is too large");
        goto done;
    }
    replacement->relation_offsets = calloc(
        (size_t)plan->external_relation_len + 1u,
        sizeof(*replacement->relation_offsets));
    if (!replacement->rows || !replacement->relation_offsets) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate extensional capability rows");
        goto done;
    }
    if (!pposlf_native_capability_pending_rows_v1_prepare(
            plan, rows, row_len, pending,
            error_buf, error_buf_size))
        goto done;
    for (uint32_t index = 0u; index < row_len; index++)
        replacement->relation_offsets[pending[index].relation + 1u]++;
    for (size_t index = 1u;
         index < (size_t)plan->external_relation_len + 1u; index++)
        replacement->relation_offsets[index] +=
            replacement->relation_offsets[index - 1u];

    pposlf_native_capability_digest_v1_begin(
        &sha, replacement->program_digest);
    for (uint32_t index = 0u; index < row_len; index++) {
        Atom *fact = borrow_rows
            ? pending[index].source
            : atom_deep_copy(
                  &replacement->arena, pending[index].source);

        /* Canonical bytes still determine ordering and the receipt digest.
         * The row is already a parsed term in the admitted ground carrier,
         * so an owned structural copy (or an explicitly borrowed graph)
         * avoids an observationally redundant render/parse round trip while
         * retaining validation boundaries. */
        if (!fact || !pposlf_native_type_vm_v1_validate_ground_term(
                plan, fact, error_buf, error_buf_size))
            goto done;
        replacement->rows[index] = (PPOSLFNativeCapabilityRowV1){
            .fact = fact,
            .canonical = pending[index].canonical,
            .canonical_len = pending[index].canonical_len,
            .relation = pending[index].relation,
            .owns_canonical = true,
        };
        pposlf_native_capability_digest_v1_row(
            &sha, pending[index].canonical,
            pending[index].canonical_len);
        pending[index].canonical = NULL;
    }
    if (!pposlf_native_capability_exact_index_v1_build(
            replacement, false, error_buf, error_buf_size))
        goto done;
    cetta_native_sha256_finish_hex(
        &sha, replacement->capability_digest);
    replacement->digest_ready = true;
    pposlf_native_capability_set_v1_impl_free(capabilities->impl);
    capabilities->impl = replacement;
    replacement = NULL;
    ok = true;

done:
    if (pending) {
        for (uint32_t index = 0u; index < row_len; index++)
            free(pending[index].canonical);
    }
    free(pending);
    pposlf_native_capability_set_v1_impl_free(replacement);
    return ok;
}

bool pposlf_native_capability_set_v1_prepare(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size) {
    return pposlf_native_capability_set_v1_prepare_mode(
        capabilities, plan, rows, row_len, false,
        error_buf, error_buf_size);
}

bool pposlf_native_capability_set_v1_prepare_borrowed(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size) {
    return pposlf_native_capability_set_v1_prepare_mode(
        capabilities, plan, rows, row_len, true,
        error_buf, error_buf_size);
}

static bool pposlf_native_capability_set_v1_prepare_borrowed_overlay_mode(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    const PPOSLFNativeCapabilitySetV1 *base,
    Atom *const *rows,
    uint32_t row_len,
    bool commit_digest,
    char *error_buf,
    size_t error_buf_size) {
    const PPOSLFNativeCapabilitySetImplV1 *base_impl =
        base ? base->impl : NULL;
    PPOSLFNativeCapabilitySetImplV1 *replacement = NULL;
    PPOSLFNativePendingCapabilityRowV1 *pending = NULL;
    CettaNativeSha256 sha;
    uint32_t base_index = 0u;
    uint32_t pending_index = 0u;
    uint32_t output_index = 0u;
    uint32_t total_len;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!capabilities || capabilities == base || !base_impl ||
        (row_len > 0u && !rows) ||
        !pposlf_native_type_vm_v1_validate_plan(
            plan, error_buf, error_buf_size) ||
        strcmp(base_impl->program_digest, plan->semantic_digest) != 0 ||
        base_impl->external_relation_len != plan->external_relation_len ||
        base_impl->row_len > UINT32_MAX - row_len) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0')
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "invalid extensional capability overlay request");
        return false;
    }
    total_len = base_impl->row_len + row_len;
    replacement = calloc(1u, sizeof(*replacement));
    pending = calloc(row_len ? row_len : 1u, sizeof(*pending));
    if (!replacement || !pending) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate an extensional capability overlay");
        goto done;
    }
    arena_init(&replacement->arena);
    arena_set_runtime_kind(
        &replacement->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    replacement->row_len = total_len;
    replacement->external_relation_len = plan->external_relation_len;
    replacement->borrows_facts = true;
    memcpy(replacement->program_digest, plan->semantic_digest,
           sizeof(replacement->program_digest));
    replacement->rows = calloc(
        total_len ? total_len : 1u, sizeof(*replacement->rows));
    if ((size_t)plan->external_relation_len + 1u >
        SIZE_MAX / sizeof(*replacement->relation_offsets)) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "extensional capability relation inventory is too large");
        goto done;
    }
    replacement->relation_offsets = calloc(
        (size_t)plan->external_relation_len + 1u,
        sizeof(*replacement->relation_offsets));
    if (!replacement->rows || !replacement->relation_offsets) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate extensional capability overlay rows");
        goto done;
    }
    if (!pposlf_native_capability_pending_rows_v1_prepare(
            plan, rows, row_len, pending,
            error_buf, error_buf_size))
        goto done;
    while (base_index < base_impl->row_len || pending_index < row_len) {
        bool take_base;

        if (pending_index >= row_len)
            take_base = true;
        else if (base_index >= base_impl->row_len)
            take_base = false;
        else {
            int comparison =
                pposlf_native_capability_row_pending_v1_compare(
                    &base_impl->rows[base_index],
                    &pending[pending_index]);
            if (comparison == 0) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "extensional capability row is duplicated across the "
                    "stable overlay (relation=%u canonical-bytes=%zu)",
                    base_impl->rows[base_index].relation,
                    base_impl->rows[base_index].canonical_len);
                goto done;
            }
            take_base = comparison < 0;
        }
        if (take_base) {
            replacement->rows[output_index] =
                base_impl->rows[base_index++];
            replacement->rows[output_index].owns_canonical = false;
        } else {
            Atom *fact = pending[pending_index].source;

            if (!pposlf_native_type_vm_v1_validate_ground_term(
                    plan, fact, error_buf, error_buf_size))
                goto done;
            replacement->rows[output_index] =
                (PPOSLFNativeCapabilityRowV1){
                    .fact = fact,
                    .canonical = pending[pending_index].canonical,
                    .canonical_len =
                        pending[pending_index].canonical_len,
                    .relation = pending[pending_index].relation,
                    .owns_canonical = true,
                };
            pending[pending_index].canonical = NULL;
            pending_index++;
        }
        replacement->relation_offsets[
            replacement->rows[output_index].relation + 1u]++;
        output_index++;
    }
    for (size_t index = 1u;
         index < (size_t)plan->external_relation_len + 1u; index++)
        replacement->relation_offsets[index] +=
            replacement->relation_offsets[index - 1u];
    replacement->exact_base = base_impl;
    if (!pposlf_native_capability_exact_index_v1_build(
            replacement, true, error_buf, error_buf_size))
        goto done;
    if (commit_digest) {
        pposlf_native_capability_digest_v1_begin(
            &sha, replacement->program_digest);
        for (uint32_t index = 0u; index < replacement->row_len; index++)
            pposlf_native_capability_digest_v1_row(
                &sha, replacement->rows[index].canonical,
                replacement->rows[index].canonical_len);
        cetta_native_sha256_finish_hex(
            &sha, replacement->capability_digest);
        replacement->digest_ready = true;
    }
    pposlf_native_capability_set_v1_impl_free(capabilities->impl);
    capabilities->impl = replacement;
    replacement = NULL;
    ok = true;

done:
    if (pending) {
        for (uint32_t index = 0u; index < row_len; index++)
            free(pending[index].canonical);
    }
    free(pending);
    pposlf_native_capability_set_v1_impl_free(replacement);
    return ok;
}

bool pposlf_native_capability_set_v1_prepare_borrowed_overlay(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    const PPOSLFNativeCapabilitySetV1 *base,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size) {
    return pposlf_native_capability_set_v1_prepare_borrowed_overlay_mode(
        capabilities, plan, base, rows, row_len, true,
        error_buf, error_buf_size);
}

bool pposlf_native_capability_set_v1_prepare_borrowed_overlay_deferred(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    const PPOSLFNativeCapabilitySetV1 *base,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size) {
    return pposlf_native_capability_set_v1_prepare_borrowed_overlay_mode(
        capabilities, plan, base, rows, row_len, false,
        error_buf, error_buf_size);
}

bool pposlf_native_capability_set_v1_commit_digest(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    char digest_out[65]) {
    PPOSLFNativeCapabilitySetImplV1 *impl =
        capabilities ? capabilities->impl : NULL;
    CettaNativeSha256 sha;

    if (!impl || !digest_out)
        return false;
    if (!impl->digest_ready) {
        pposlf_native_capability_digest_v1_begin(
            &sha, impl->program_digest);
        for (uint32_t index = 0u; index < impl->row_len; index++)
            pposlf_native_capability_digest_v1_row(
                &sha, impl->rows[index].canonical,
                impl->rows[index].canonical_len);
        cetta_native_sha256_finish_hex(&sha, impl->capability_digest);
        impl->digest_ready = true;
    }
    memcpy(digest_out, impl->capability_digest, 65u);
    return true;
}

static bool pposlf_native_capability_exact_index_v1_materialize(
    PPOSLFNativeCapabilitySetImplV1 *next,
    PPOSLFNativeCapabilitySetImplV1 *prior,
    char *error_buf, size_t error_buf_size) {
    PPOSLFNativeCapabilityExactSlotV1 *delta_slots;
    size_t delta_cap;
    size_t delta_len = 0u;
    size_t desired_cap;

    if (!next || !prior || next->exact_base != prior ||
        !pposlf_native_capability_exact_index_v1_capacity(
            next->row_len, &desired_cap) ||
        desired_cap > SIZE_MAX / sizeof(*next->exact_slots)) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "extensional capability incremental exact index is invalid");
        return false;
    }
    delta_slots = next->exact_slots;
    delta_cap = next->exact_slot_cap;
    for (size_t index = 0u; index < delta_cap; index++) {
        if (delta_slots[index].fact)
            delta_len++;
    }
    if (prior->exact_slots && prior->exact_slot_cap >= desired_cap) {
        size_t *inserted = malloc(
            (delta_len ? delta_len : 1u) * sizeof(*inserted));
        size_t inserted_len = 0u;

        if (!inserted) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "cannot extend an extensional capability exact index");
            return false;
        }
        for (size_t index = 0u; index < delta_cap; index++) {
            const PPOSLFNativeCapabilityExactSlotV1 *entry =
                &delta_slots[index];
            size_t inserted_slot;

            if (!entry->fact)
                continue;
            if (!pposlf_native_capability_exact_index_v1_insert(
                    prior->exact_slots, prior->exact_slot_cap,
                    entry->fact, entry->canonical,
                    entry->canonical_len, entry->relation,
                    entry->structural_hash, &inserted_slot)) {
                for (size_t rollback = 0u;
                     rollback < inserted_len; rollback++)
                    prior->exact_slots[inserted[rollback]] =
                        (PPOSLFNativeCapabilityExactSlotV1){0};
                free(inserted);
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "extensional capability incremental exact index rejected an extension");
                return false;
            }
            inserted[inserted_len++] = inserted_slot;
        }
        free(inserted);
        free(delta_slots);
        next->exact_slots = prior->exact_slots;
        next->exact_slot_cap = prior->exact_slot_cap;
        next->exact_base = NULL;
        prior->exact_slots = NULL;
        prior->exact_slot_cap = 0u;
        return true;
    }
    {
        PPOSLFNativeCapabilityExactSlotV1 *combined = calloc(
            desired_cap, sizeof(*combined));

        if (!combined) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "cannot grow an extensional capability exact index");
            return false;
        }
        for (size_t index = 0u; index < prior->exact_slot_cap; index++) {
            const PPOSLFNativeCapabilityExactSlotV1 *entry =
                &prior->exact_slots[index];

            if (entry->fact &&
                !pposlf_native_capability_exact_index_v1_insert(
                    combined, desired_cap, entry->fact,
                    entry->canonical, entry->canonical_len,
                    entry->relation, entry->structural_hash, NULL)) {
                free(combined);
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "extensional capability exact index growth lost its prefix");
                return false;
            }
        }
        for (size_t index = 0u; index < delta_cap; index++) {
            const PPOSLFNativeCapabilityExactSlotV1 *entry =
                &delta_slots[index];

            if (entry->fact &&
                !pposlf_native_capability_exact_index_v1_insert(
                    combined, desired_cap, entry->fact,
                    entry->canonical, entry->canonical_len,
                    entry->relation, entry->structural_hash, NULL)) {
                free(combined);
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "extensional capability exact index growth rejected its delta");
                return false;
            }
        }
        free(delta_slots);
        next->exact_slots = combined;
        next->exact_slot_cap = desired_cap;
        next->exact_base = NULL;
    }
    return true;
}

static bool pposlf_native_capability_set_v1_append_borrowed_mode(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    bool commit_digest,
    char *error_buf,
    size_t error_buf_size) {
    PPOSLFNativeCapabilitySetImplV1 *prior =
        capabilities ? capabilities->impl : NULL;
    PPOSLFNativeCapabilitySetV1 replacement;
    PPOSLFNativeCapabilitySetImplV1 *next;
    uint32_t prior_index = 0u;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!capabilities || !plan || !prior || !prior->borrows_facts ||
        strcmp(prior->program_digest, plan->semantic_digest) != 0) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "capability append requires a matching borrowed base");
        return false;
    }
    if (row_len == 0u)
        return true;
    pposlf_native_capability_set_v1_init(&replacement);
    if (!pposlf_native_capability_set_v1_prepare_borrowed_overlay_mode(
            &replacement, plan, capabilities, rows, row_len,
            commit_digest,
            error_buf, error_buf_size))
        return false;
    next = replacement.impl;
    for (uint32_t index = 0u;
         index < next->row_len && prior_index < prior->row_len; index++) {
        if (next->rows[index].canonical ==
            prior->rows[prior_index].canonical)
            prior_index++;
    }
    if (prior_index != prior->row_len) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "capability append lost a stable canonical row");
        pposlf_native_capability_set_v1_free(&replacement);
        return false;
    }
    if (!pposlf_native_capability_exact_index_v1_materialize(
            next, prior, error_buf, error_buf_size)) {
        pposlf_native_capability_set_v1_free(&replacement);
        return false;
    }
    prior_index = 0u;
    for (uint32_t index = 0u;
         index < next->row_len && prior_index < prior->row_len; index++) {
        if (next->rows[index].canonical ==
            prior->rows[prior_index].canonical) {
            next->rows[index].owns_canonical =
                prior->rows[prior_index].owns_canonical;
            prior->rows[prior_index].owns_canonical = false;
            prior_index++;
        }
    }
    capabilities->impl = next;
    replacement.impl = NULL;
    pposlf_native_capability_set_v1_impl_free(prior);
    return true;
}

bool pposlf_native_capability_set_v1_append_borrowed(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size) {
    return pposlf_native_capability_set_v1_append_borrowed_mode(
        capabilities, plan, rows, row_len, true,
        error_buf, error_buf_size);
}

bool pposlf_native_capability_set_v1_append_borrowed_deferred(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size) {
    return pposlf_native_capability_set_v1_append_borrowed_mode(
        capabilities, plan, rows, row_len, false,
        error_buf, error_buf_size);
}

bool pposlf_native_capability_set_v1_prepare_reflected_facts(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    const char *answer_path,
    const char *expected_owner,
    char *error_buf,
    size_t error_buf_size) {
    FHAnswerStreamV1 reflection;
    Arena decoded_arena;
    Atom **rows = NULL;
    bool ok = false;

    fh_answer_stream_v1_init(&reflection);
    arena_init(&decoded_arena);
    arena_set_runtime_kind(
        &decoded_arena, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!capabilities || !plan || !answer_path || !expected_owner ||
        expected_owner[0] == '\0' ||
        !pposlf_native_type_vm_v1_validate_plan(
            plan, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0')
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "invalid reflected capability preparation request");
        goto done;
    }
    if (!fh_answer_stream_v1_read(
            &reflection, answer_path, error_buf, error_buf_size))
        goto done;
    if (reflection.len > UINT32_MAX ||
        reflection.len > SIZE_MAX / sizeof(*rows)) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "reflected capability stream is too large");
        goto done;
    }
    rows = calloc(reflection.len ? reflection.len : 1u, sizeof(*rows));
    if (!rows) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate reflected capability rows");
        goto done;
    }
    for (size_t index = 0u; index < reflection.len; index++) {
        const Atom *record = reflection.terms[index];
        const Atom *rule;
        const char *owner;
        const char *rule_name;

        if (!pposlf_native_type_vm_v1_expr_head(
                record, "source-rule", 2u)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "reflected capability stream contains a non-rule record");
            goto done;
        }
        owner = pposlf_native_type_vm_v1_quoted_symbol(
            record->expr.elems[1]);
        rule = record->expr.elems[2];
        if (!owner || strcmp(owner, expected_owner) != 0) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "reflected capability rule has the wrong source owner");
            goto done;
        }
        if (!pposlf_native_type_vm_v1_expr_head(
                rule, "q-rule", 3u)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "reflected capability contains a malformed quoted rule");
            goto done;
        }
        rule_name = pposlf_native_type_vm_v1_quoted_symbol(
            rule->expr.elems[1]);
        if (!rule_name) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "reflected capability rule has no quoted identifier");
            goto done;
        }
        if (!atom_is_symbol(rule->expr.elems[3], "q-nil")) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "reflected capability provider contains a non-fact rule");
            goto done;
        }
        if (!pposlf_native_type_vm_v1_decode_quoted_ground(
                plan, &decoded_arena, rule->expr.elems[2],
                &rows[index], error_buf, error_buf_size))
            goto done;
    }
    ok = pposlf_native_capability_set_v1_prepare(
        capabilities, plan, rows, (uint32_t)reflection.len,
        error_buf, error_buf_size);

done:
    free(rows);
    arena_free(&decoded_arena);
    fh_answer_stream_v1_free(&reflection);
    return ok;
}

void pposlf_native_type_vm_v1_init(PPOSLFNativeTypeVMV1 *vm) {
    if (vm)
        vm->impl = NULL;
}

void pposlf_native_type_vm_v1_free(PPOSLFNativeTypeVMV1 *vm) {
    if (!vm)
        return;
    pposlf_native_type_vm_v1_impl_free(vm->impl);
    vm->impl = NULL;
}

static Atom *pposlf_native_type_vm_v1_compile_term(
    PPOSLFNativeTypeVMImplV1 *impl,
    const PPOSLFNativeTypePlanV1 *plan,
    uint32_t term_index,
    Atom **compiled,
    uint8_t *state,
    char *error_buf,
    size_t error_buf_size) {
    const PPOSLFNativeTermV1 *term;
    Atom *result = NULL;

    if (term_index >= plan->term_len) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "native NTT program contains an out-of-range term");
        return NULL;
    }
    if (state[term_index] == 2u)
        return compiled[term_index];
    if (state[term_index] == 1u) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "native NTT program contains a cyclic term graph");
        return NULL;
    }
    state[term_index] = 1u;
    term = &plan->terms[term_index];
    switch (term->kind) {
    case PPOSLF_NATIVE_TERM_SYMBOL_V1:
        result = term->text
                     ? atom_symbol(&impl->program_arena, term->text)
                     : NULL;
        break;
    case PPOSLF_NATIVE_TERM_INTEGER_I64_V1:
        result = atom_int(&impl->program_arena, term->integer);
        break;
    case PPOSLF_NATIVE_TERM_INTEGER_BIG_V1:
        result = term->text
                     ? atom_bigint(&impl->program_arena, term->text)
                     : NULL;
        break;
    case PPOSLF_NATIVE_TERM_STRING_V1:
        result = term->text
                     ? atom_string(&impl->program_arena, term->text)
                     : NULL;
        break;
    case PPOSLF_NATIVE_TERM_VARIABLE_V1:
        result = atom_var_with_id(
            &impl->program_arena, "v", (VarId)term->variable + 1u);
        break;
    case PPOSLF_NATIVE_TERM_APPLICATION_V1: {
        Atom **elements;
        size_t element_len;

        if (!term->text ||
            term->edge_begin > plan->term_edge_len ||
            term->edge_len > plan->term_edge_len - term->edge_begin ||
            (size_t)term->edge_len >
                (SIZE_MAX / sizeof(*elements)) - 1u) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT application has an invalid edge span");
            return NULL;
        }
        element_len = (size_t)term->edge_len + 1u;
        elements = malloc(element_len * sizeof(*elements));
        if (!elements) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "cannot allocate a native NTT application");
            return NULL;
        }
        elements[0] = atom_symbol(&impl->program_arena, term->text);
        for (uint32_t child = 0u; child < term->edge_len; child++) {
            elements[child + 1u] = pposlf_native_type_vm_v1_compile_term(
                impl, plan, plan->term_edges[term->edge_begin + child],
                compiled, state, error_buf, error_buf_size);
            if (!elements[child + 1u]) {
                free(elements);
                return NULL;
            }
        }
        result = atom_expr(
            &impl->program_arena, elements, (CettaExprLen)element_len);
        free(elements);
        break;
    }
    default:
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "native NTT program contains an unknown term kind");
        return NULL;
    }
    if (!result) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot compile a native NTT term");
        return NULL;
    }
    compiled[term_index] = result;
    state[term_index] = 2u;
    return result;
}

static int pposlf_native_application_dispatch_v1_compare(
    const void *left_value,
    const void *right_value) {
    const PPOSLFNativeApplicationDispatchV1 *left = left_value;
    const PPOSLFNativeApplicationDispatchV1 *right = right_value;

    if (left->symbol < right->symbol)
        return -1;
    if (left->symbol > right->symbol)
        return 1;
    return 0;
}

static bool pposlf_native_type_vm_v1_build_application_dispatch(
    PPOSLFNativeTypeVMImplV1 *impl,
    const PPOSLFNativeTypePlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t variable_group = 0u;

    if (!impl || !plan || !g_symbols)
        return false;
    impl->application_dispatch_len = plan->head_signature_len;
    impl->application_dispatch = calloc(
        impl->application_dispatch_len ? impl->application_dispatch_len : 1u,
        sizeof(*impl->application_dispatch));
    if (!impl->application_dispatch) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate native NTT application dispatch");
        return false;
    }
    for (uint32_t index = 0u;
         index < impl->application_dispatch_len; index++) {
        const PPOSLFNativeHeadSignatureV1 *signature =
            &plan->head_signatures[index];
        PPOSLFNativeApplicationDispatchV1 *entry =
            &impl->application_dispatch[index];

        entry->symbol = symbol_intern_cstr(
            g_symbols, signature->constructor);
        entry->arity = signature->arity;
        entry->has_exact_group =
            pposlf_native_type_plan_v1_head_group(
                plan, PPOSLF_NATIVE_TERM_APPLICATION_V1,
                signature->constructor, 0, &entry->exact_group);
        entry->has_external_relation =
            pposlf_native_type_plan_v1_external_relation(
                plan, signature->constructor, signature->arity,
                &entry->external_relation);
        if (entry->symbol == SYMBOL_ID_NONE) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT signature contains an invalid constructor");
            return false;
        }
    }
    qsort(impl->application_dispatch,
          impl->application_dispatch_len,
          sizeof(*impl->application_dispatch),
          pposlf_native_application_dispatch_v1_compare);
    for (uint32_t index = 1u;
         index < impl->application_dispatch_len; index++) {
        if (impl->application_dispatch[index - 1u].symbol ==
            impl->application_dispatch[index].symbol) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT signature maps two constructors to one symbol");
            return false;
        }
    }
    impl->has_variable_group = pposlf_native_type_plan_v1_head_group(
        plan, PPOSLF_NATIVE_TERM_VARIABLE_V1,
        NULL, 0, &variable_group);
    impl->variable_group = variable_group;
    return true;
}

static bool pposlf_native_type_vm_v1_build_compiled_relations(
    PPOSLFNativeTypeVMImplV1 *impl,
    char *error_buf, size_t error_buf_size) {
    const PPOSLFNativeTypePlanV1 *plan;

    if (!impl || !(plan = impl->plan))
        return false;
    /* A variable-headed rule participates in every application group.  Until
     * its source-order cost is included in the certificate, retain the
     * ordinary machine rather than silently changing limit semantics. */
    if (impl->has_variable_group)
        return true;
    impl->compiled_relations = calloc(
        impl->application_dispatch_len
            ? impl->application_dispatch_len : 1u,
        sizeof(*impl->compiled_relations));
    if (!impl->compiled_relations) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate native NTT compiled-relation plans");
        return false;
    }
    for (uint32_t index = 0u;
         index < impl->application_dispatch_len; index++) {
        PPOSLFNativeApplicationDispatchV1 *entry =
            &impl->application_dispatch[index];
        const PPOSLFNativeHeadGroupV1 *group;
        uint32_t first_step;
        uint32_t second_step;
        const PPOSLFNativeCompiledRuleV1 *first;
        const PPOSLFNativeCompiledRuleV1 *second;
        CettaGsltHornRuleViewV1 first_view;
        CettaGsltHornRuleViewV1 second_view;
        CettaGsltPeanoAddPlanV1 admitted;

        if (!entry->has_exact_group || entry->arity != 3u ||
            entry->exact_group >= plan->head_group_len)
            continue;
        group = &plan->head_groups[entry->exact_group];
        if (group->step_len != 2u ||
            group->step_begin > plan->head_step_index_len ||
            group->step_len >
                plan->head_step_index_len - group->step_begin)
            continue;
        first_step = plan->head_step_indices[group->step_begin];
        second_step = plan->head_step_indices[group->step_begin + 1u];
        if (first_step >= impl->rule_len || second_step >= impl->rule_len)
            continue;
        first = &impl->rules[first_step];
        second = &impl->rules[second_step];
        first_view = (CettaGsltHornRuleViewV1){
            .head = first->head,
            .body = first->body,
            .body_len = first->body_len,
            .step_index = first_step,
        };
        second_view = (CettaGsltHornRuleViewV1){
            .head = second->head,
            .body = second->body,
            .body_len = second->body_len,
            .step_index = second_step,
        };
        if (!cetta_gslt_peano_add_plan_recognize_v1(
                &first_view, &second_view, &admitted))
            continue;
        if (admitted.relation != entry->symbol) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "compiled relation disagrees with native NTT dispatch");
            return false;
        }
        entry->has_compiled_relation = true;
        entry->compiled_relation = impl->compiled_relation_len;
        impl->compiled_relations[impl->compiled_relation_len++] = admitted;
    }
    return true;
}

typedef struct {
    const PPOSLFNativeTypeVMImplV1 *impl;
    const PPOSLFNativeHeadGroupV1 *group;
} PPOSLFNativeRigidBuildV1;

static bool pposlf_native_type_vm_v1_rigid_rule_key_at(
    void *context, uint32_t occurrence, uint32_t coordinate,
    CettaGsltRigidKeyV1 *key_out) {
    const PPOSLFNativeRigidBuildV1 *build = context;
    const PPOSLFNativeTypePlanV1 *plan;
    uint32_t step;
    Atom *head;

    if (!build || !build->impl || !(plan = build->impl->plan) ||
        !build->group || occurrence >= build->group->step_len ||
        build->group->step_begin > plan->head_step_index_len ||
        occurrence >= plan->head_step_index_len - build->group->step_begin)
        return false;
    step = plan->head_step_indices[
        build->group->step_begin + occurrence];
    if (step >= build->impl->rule_len ||
        !(head = build->impl->rules[step].head) ||
        head->kind != ATOM_EXPR || !head->expr.elems ||
        head->expr.len == 0u || coordinate >= head->expr.len - 1u)
        return false;
    return cetta_gslt_rigid_key_from_atom_v1(
        head->expr.elems[coordinate + 1u], key_out);
}

static bool pposlf_native_type_vm_v1_build_rigid_dispatch(
    PPOSLFNativeTypeVMImplV1 *impl,
    char *error_buf, size_t error_buf_size) {
    if (!impl || !impl->plan)
        return false;
    cetta_gslt_rigid_coordinate_scratch_init_v1(&impl->rigid_scratch);
    for (uint32_t index = 0u;
         index < impl->application_dispatch_len; index++) {
        PPOSLFNativeApplicationDispatchV1 *entry =
            &impl->application_dispatch[index];

        cetta_gslt_rigid_coordinate_index_init_v1(&entry->rigid_index);
        if (!entry->has_exact_group)
            continue;
        const PPOSLFNativeHeadGroupV1 *group =
            &impl->plan->head_groups[entry->exact_group];
        PPOSLFNativeRigidBuildV1 build = {
            .impl = impl,
            .group = group,
        };
        if (!cetta_gslt_rigid_coordinate_index_build_v1(
                &entry->rigid_index, &impl->rigid_scratch,
                entry->arity, group->step_len,
                pposlf_native_type_vm_v1_rigid_rule_key_at, &build)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "cannot compile native NTT rigid-coordinate dispatch");
            return false;
        }
    }
    return true;
}

static PPOSLFNativeTypeVMImplV1 *pposlf_native_type_vm_v1_build(
    const PPOSLFNativeTypePlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    PPOSLFNativeTypeVMImplV1 *impl = NULL;
    Atom **compiled = NULL;
    uint8_t *state = NULL;

    if (!pposlf_native_type_vm_v1_validate_plan(
            plan, error_buf, error_buf_size))
        return NULL;
    impl = calloc(1u, sizeof(*impl));
    if (!impl) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate the native NTT VM");
        return NULL;
    }
    arena_init(&impl->program_arena);
    arena_set_runtime_kind(
        &impl->program_arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    impl->plan = plan;
    impl->rule_len = plan->step_schema_len;
    memcpy(impl->program_digest, plan->semantic_digest,
           sizeof(impl->program_digest));
    pposlf_native_empty_capability_digest_v1(
        impl->program_digest, impl->empty_capability_digest);
    if (!pposlf_native_type_vm_v1_build_application_dispatch(
            impl, plan, error_buf, error_buf_size))
        goto fail;
    impl->rules = calloc(
        impl->rule_len ? impl->rule_len : 1u, sizeof(*impl->rules));
    compiled = calloc(
        plan->term_len ? plan->term_len : 1u, sizeof(*compiled));
    state = calloc(plan->term_len ? plan->term_len : 1u, sizeof(*state));
    if (!impl->rules || !compiled || !state) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate native NTT rule storage");
        goto fail;
    }
    for (uint32_t index = 0u; index < impl->rule_len; index++) {
        const PPOSLFNativeStepSchemaV1 *step =
            &plan->step_schemas[index];
        PPOSLFNativeCompiledRuleV1 *rule = &impl->rules[index];
        PPOSLFNativeStepFlowV1 flow;

        if (step->head >= plan->term_len ||
            step->body_begin > plan->body_root_len ||
            step->body_len > plan->body_root_len - step->body_begin ||
            !pposlf_native_type_plan_v1_step_flow(
                plan, index, &flow)) {
            pposlf_native_type_vm_v1_set_error(
                error_buf, error_buf_size,
                "native NTT rule contains an invalid term span");
            goto fail;
        }
        rule->variable_count = step->variable_count;
        rule->body_variables_in_head = flow.body_variables_in_head;
        rule->head_linear = flow.head_linear;
        cetta_gslt_ground_dense_term_program_init_v1(
            &rule->ground_dense_head);
        rule->head = pposlf_native_type_vm_v1_compile_term(
            impl, plan, step->head, compiled, state,
            error_buf, error_buf_size);
        rule->body_len = step->body_len;
        if (!rule->head)
            goto fail;
        if (rule->body_len > 0u) {
            rule->body = arena_alloc(
                &impl->program_arena,
                (size_t)rule->body_len * sizeof(*rule->body));
            rule->body_activation_view_admitted = calloc(
                rule->body_len,
                sizeof(*rule->body_activation_view_admitted));
            rule->body_ground = calloc(
                rule->body_len, sizeof(*rule->body_ground));
            if (!rule->body || !rule->body_ground ||
                !rule->body_activation_view_admitted) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "cannot allocate native NTT body analyses");
                goto fail;
            }
            for (uint32_t body = 0u; body < rule->body_len; body++) {
                uint32_t root =
                    plan->body_roots[step->body_begin + body];
                rule->body[body] = pposlf_native_type_vm_v1_compile_term(
                    impl, plan, root, compiled, state,
                    error_buf, error_buf_size);
                if (!rule->body[body])
                    goto fail;
                rule->body_ground[body] =
                    !atom_has_vars(rule->body[body]);
            }
        }
        if (rule->body_variables_in_head) {
            if (!cetta_gslt_ground_dense_term_compile_v1(
                    &rule->ground_dense_head, rule->head, 1u,
                    rule->variable_count, error_buf, error_buf_size) ||
                cetta_gslt_ground_dense_term_is_linear_v1(
                    &rule->ground_dense_head) != rule->head_linear) {
                if (!error_buf || error_buf_size == 0u ||
                    error_buf[0] == '\0') {
                    pposlf_native_type_vm_v1_set_error(
                        error_buf, error_buf_size,
                        "native NTT dense head disagrees with generated flow");
                }
                goto fail;
            }
            if (rule->body_len > 0u) {
                rule->ground_dense_body = calloc(
                    rule->body_len, sizeof(*rule->ground_dense_body));
                if (!rule->ground_dense_body) {
                    pposlf_native_type_vm_v1_set_error(
                        error_buf, error_buf_size,
                        "cannot allocate native NTT dense body programs");
                    goto fail;
                }
                for (uint32_t body = 0u;
                     body < rule->body_len; body++) {
                    cetta_gslt_ground_dense_term_program_init_v1(
                        &rule->ground_dense_body[body]);
                    if (rule->body_ground[body])
                        continue;
                    if (!cetta_gslt_ground_dense_term_compile_v1(
                            &rule->ground_dense_body[body],
                            rule->body[body], 1u,
                            rule->variable_count,
                            error_buf, error_buf_size))
                        goto fail;
                }
            }
            rule->ground_dense_ready = true;
        }
    }
    for (uint32_t rule_index = 0u;
         rule_index < impl->rule_len; rule_index++) {
        PPOSLFNativeCompiledRuleV1 *rule =
            &impl->rules[rule_index];

        for (uint32_t body = 0u; body < rule->body_len; body++) {
            if (!rule->body_variables_in_head) {
                rule->body_activation_view_admitted[body] = false;
                continue;
            }
            if (!pposlf_native_type_vm_v1_activation_view_analyze(
                    impl, rule->body[body],
                    &rule->body_activation_view_admitted[body])) {
                pposlf_native_type_vm_v1_set_error(
                    error_buf, error_buf_size,
                    "cannot analyze activation-view consumers");
                goto fail;
            }
        }
    }
    if (!pposlf_native_type_vm_v1_build_compiled_relations(
            impl, error_buf, error_buf_size))
        goto fail;
    if (!pposlf_native_type_vm_v1_build_rigid_dispatch(
            impl, error_buf, error_buf_size))
        goto fail;
    free(state);
    free(compiled);
    return impl;

fail:
    free(state);
    free(compiled);
    pposlf_native_type_vm_v1_impl_free(impl);
    return NULL;
}

bool pposlf_native_type_vm_v1_prepare(
    PPOSLFNativeTypeVMV1 *vm,
    const PPOSLFNativeTypePlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    PPOSLFNativeTypeVMImplV1 *replacement;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!vm) {
        pposlf_native_type_vm_v1_set_error(
            error_buf, error_buf_size,
            "invalid native NTT VM preparation request");
        return false;
    }
    replacement = pposlf_native_type_vm_v1_build(
        plan, error_buf, error_buf_size);
    if (!replacement)
        return false;
    pposlf_native_type_vm_v1_impl_free(vm->impl);
    vm->impl = replacement;
    return true;
}

void pposlf_native_vm_result_v1_init(PPOSLFNativeVMResultV1 *result) {
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    result->outcome = PPOSLF_NATIVE_VM_INVALID_QUERY_V1;
}

void pposlf_native_vm_result_v1_free(PPOSLFNativeVMResultV1 *result) {
    if (!result)
        return;
    free(result->proof_events);
    pposlf_native_vm_result_v1_init(result);
}

static bool pposlf_native_type_vm_v1_push_proof_event(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeVMProofEventKindV1 kind,
    uint32_t index) {
    uint32_t next_cap;
    PPOSLFNativeVMProofEventV1 *next;

    if (search->proof_event_len < search->proof_event_cap) {
        search->proof_events[search->proof_event_len++] =
            (PPOSLFNativeVMProofEventV1){
                .kind = kind,
                .index = index,
            };
        return true;
    }
    next_cap = search->proof_event_cap ? search->proof_event_cap * 2u : 16u;
    if (next_cap < search->proof_event_cap ||
        (size_t)next_cap > SIZE_MAX / sizeof(*next))
        return false;
    next = realloc(search->proof_events, (size_t)next_cap * sizeof(*next));
    if (!next)
        return false;
    search->proof_events = next;
    search->proof_event_cap = next_cap;
    search->proof_events[search->proof_event_len++] =
        (PPOSLFNativeVMProofEventV1){
            .kind = kind,
            .index = index,
        };
    return true;
}

static const PPOSLFNativeApplicationDispatchV1 *
pposlf_native_type_vm_v1_application_dispatch(
    const PPOSLFNativeTypeVMImplV1 *impl,
    SymbolId symbol) {
    uint32_t low = 0u;
    uint32_t high;

    if (!impl || symbol == SYMBOL_ID_NONE)
        return NULL;
    high = impl->application_dispatch_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        SymbolId current = impl->application_dispatch[middle].symbol;

        if (current < symbol)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= impl->application_dispatch_len ||
        impl->application_dispatch[low].symbol != symbol)
        return NULL;
    return &impl->application_dispatch[low];
}

static bool pposlf_native_type_vm_v1_goal_dispatch(
    const PPOSLFNativeTypeVMImplV1 *impl,
    Atom *goal,
    PPOSLFNativeGoalDispatchV1 *dispatch_out) {
    const PPOSLFNativeTypePlanV1 *plan;
    PPOSLFNativeTermKindV1 kind;
    const char *text = NULL;
    int64_t integer = 0;
    PPOSLFNativeGoalDispatchV1 dispatch = {0};

    if (!impl || !(plan = impl->plan) || !goal || !dispatch_out)
        return false;
    switch (goal->kind) {
    case ATOM_SYMBOL:
        kind = PPOSLF_NATIVE_TERM_SYMBOL_V1;
        text = atom_name_cstr(goal);
        break;
    case ATOM_VAR:
        kind = PPOSLF_NATIVE_TERM_VARIABLE_V1;
        break;
    case ATOM_GROUNDED:
        if (goal->ground.gkind == GV_INT) {
            kind = PPOSLF_NATIVE_TERM_INTEGER_I64_V1;
            integer = goal->ground.ival;
        } else if (goal->ground.gkind == GV_BIGINT) {
            kind = PPOSLF_NATIVE_TERM_INTEGER_BIG_V1;
            text = atom_bigint_cstr(goal);
        } else if (goal->ground.gkind == GV_STRING) {
            kind = PPOSLF_NATIVE_TERM_STRING_V1;
            text = goal->ground.sval;
        } else {
            return false;
        }
        break;
    case ATOM_EXPR: {
        Atom *head;
        const PPOSLFNativeApplicationDispatchV1 *entry;

        if (goal->expr.len == 0u || goal->expr.len - 1u > UINT32_MAX)
            return false;
        head = goal->expr.elems[0];
        if (!head || head->kind != ATOM_SYMBOL)
            return false;
        entry = pposlf_native_type_vm_v1_application_dispatch(
            impl, head->sym_id);
        if (!entry ||
            entry->arity != (uint32_t)(goal->expr.len - 1u))
            return false;
        dispatch.kind = PPOSLF_NATIVE_TERM_APPLICATION_V1;
        dispatch.has_exact = entry->has_exact_group;
        dispatch.exact_group = entry->exact_group;
        dispatch.has_external = entry->has_external_relation;
        dispatch.external_relation = entry->external_relation;
        dispatch.has_variable = impl->has_variable_group;
        dispatch.variable_group = impl->variable_group;
        if (entry->has_compiled_relation &&
            entry->compiled_relation < impl->compiled_relation_len) {
            dispatch.compiled_relation =
                &impl->compiled_relations[entry->compiled_relation];
            dispatch.compiled_relation_index =
                dispatch.compiled_relation->zero_step_index;
        }
        dispatch.rigid_index = &entry->rigid_index;
        dispatch.used_compiled_application = true;
        *dispatch_out = dispatch;
        return true;
    }
    default:
        return false;
    }
    if ((kind == PPOSLF_NATIVE_TERM_SYMBOL_V1 ||
         kind == PPOSLF_NATIVE_TERM_INTEGER_BIG_V1 ||
         kind == PPOSLF_NATIVE_TERM_STRING_V1 ||
         kind == PPOSLF_NATIVE_TERM_APPLICATION_V1) &&
        !text)
        return false;
    dispatch.kind = kind;
    dispatch.full_scan = kind == PPOSLF_NATIVE_TERM_VARIABLE_V1;
    dispatch.has_exact = !dispatch.full_scan &&
        pposlf_native_type_plan_v1_head_group(
            plan, kind, text, integer, &dispatch.exact_group);
    dispatch.has_variable = !dispatch.full_scan &&
        impl->has_variable_group;
    dispatch.variable_group = impl->variable_group;
    *dispatch_out = dispatch;
    return true;
}

static bool pposlf_native_type_vm_v1_next_candidate(
    const PPOSLFNativeTypePlanV1 *plan,
    bool full_scan,
    uint32_t exact_group,
    bool has_exact,
    uint32_t variable_group,
    bool has_variable,
    const uint32_t *rigid_positions,
    uint32_t rigid_position_count,
    bool rigid_dispatched,
    uint32_t *exact_offset,
    uint32_t *exact_source_offset,
    uint32_t *variable_offset,
    uint32_t *full_offset,
    uint32_t *rigid_rejections_out,
    uint32_t *step_out) {
    uint32_t exact_step = UINT32_MAX;
    uint32_t variable_step = UINT32_MAX;
    uint32_t selected_exact_position = UINT32_MAX;
    uint32_t selected_step = UINT32_MAX;

    if (rigid_rejections_out)
        *rigid_rejections_out = 0u;

    if (full_scan) {
        if (*full_offset >= plan->step_schema_len)
            return false;
        *step_out = (*full_offset)++;
        return true;
    }
    if (has_exact) {
        const PPOSLFNativeHeadGroupV1 *group =
            &plan->head_groups[exact_group];
        if (rigid_dispatched) {
            if (*exact_offset < rigid_position_count)
                selected_exact_position = rigid_positions[*exact_offset];
        } else if (*exact_offset < group->step_len) {
            selected_exact_position = *exact_offset;
        }
        if (selected_exact_position < group->step_len)
            exact_step = plan->head_step_indices[
                group->step_begin + selected_exact_position];
    }
    if (has_variable) {
        const PPOSLFNativeHeadGroupV1 *group =
            &plan->head_groups[variable_group];
        if (*variable_offset < group->step_len)
            variable_step = plan->head_step_indices[
                group->step_begin + *variable_offset];
    }
    selected_step = exact_step <= variable_step ? exact_step : variable_step;
    if (rigid_dispatched && has_exact) {
        const PPOSLFNativeHeadGroupV1 *group =
            &plan->head_groups[exact_group];
        while (*exact_source_offset < group->step_len) {
            uint32_t source_position = *exact_source_offset;
            uint32_t source_step = plan->head_step_indices[
                group->step_begin + source_position];

            if (source_position == selected_exact_position ||
                source_step >= selected_step)
                break;
            (*exact_source_offset)++;
            if (rigid_rejections_out &&
                *rigid_rejections_out != UINT32_MAX)
                (*rigid_rejections_out)++;
        }
    }
    if (selected_step == UINT32_MAX)
        return false;
    *step_out = selected_step;
    if (exact_step <= variable_step) {
        (*exact_offset)++;
        if (rigid_dispatched)
            *exact_source_offset = selected_exact_position + 1u;
        else
            *exact_source_offset = *exact_offset;
    } else {
        (*variable_offset)++;
    }
    return true;
}

static const PPOSLFNativeCapabilityExactSlotV1 *
pposlf_native_type_vm_v1_exact_capability_identity(
    const PPOSLFNativeCapabilitySetImplV1 *capabilities,
    uint32_t relation, Atom *ground_query, uint32_t structural_hash) {
    size_t slot_index = 0u;

    if (!capabilities || !ground_query ||
        relation >= capabilities->external_relation_len)
        return NULL;
    if (capabilities->exact_slots && capabilities->exact_slot_cap != 0u) {
        slot_index = pposlf_native_capability_exact_slot_v1(
            relation, structural_hash,
            capabilities->exact_slot_cap - 1u);
        for (size_t probe = 0u;
             probe < capabilities->exact_slot_cap; probe++) {
            const PPOSLFNativeCapabilityExactSlotV1 *slot =
                &capabilities->exact_slots[slot_index];

            if (!slot->fact)
                break;
            if (slot->structural_hash == structural_hash &&
                slot->relation == relation &&
                atom_eq(slot->fact, ground_query))
                return slot;
            slot_index = (slot_index + 1u) &
                (capabilities->exact_slot_cap - 1u);
        }
    }
    return capabilities->exact_base
        ? pposlf_native_type_vm_v1_exact_capability_identity(
              capabilities->exact_base, relation, ground_query,
              structural_hash)
        : NULL;
}

static int pposlf_native_type_vm_v1_compare_canonical(
    const uint8_t *left, size_t left_len,
    const uint8_t *right, size_t right_len) {
    size_t common = left_len < right_len ? left_len : right_len;
    int comparison = memcmp(left, right, common);

    if (comparison != 0)
        return comparison;
    if (left_len < right_len)
        return -1;
    if (left_len > right_len)
        return 1;
    return 0;
}

static bool pposlf_native_type_vm_v1_exact_capability_row(
    const PPOSLFNativeCapabilitySetImplV1 *capabilities,
    uint32_t relation, Atom *ground_query, uint32_t *row_out) {
    const PPOSLFNativeCapabilityExactSlotV1 *identity;
    uint32_t structural_hash;
    uint32_t low;
    uint32_t high;

    if (!capabilities || !ground_query || !row_out ||
        relation >= capabilities->external_relation_len)
        return false;
    structural_hash = atom_hash(ground_query);
    identity = pposlf_native_type_vm_v1_exact_capability_identity(
        capabilities, relation, ground_query, structural_hash);
    if (!identity)
        return false;
    low = capabilities->relation_offsets[relation];
    high = capabilities->relation_offsets[relation + 1u];
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPOSLFNativeCapabilityRowV1 *row =
            &capabilities->rows[middle];
        int comparison = pposlf_native_type_vm_v1_compare_canonical(
            identity->canonical, identity->canonical_len,
            row->canonical, row->canonical_len);

        if (comparison < 0)
            high = middle;
        else if (comparison > 0)
            low = middle + 1u;
        else {
            *row_out = middle;
            return true;
        }
    }
    return false;
}

static bool pposlf_native_type_vm_v1_render_application_prefix(
    const Atom *term, CettaExprLen element_len,
    uint8_t **prefix_out, size_t *prefix_len_out) {
    uint8_t **parts = NULL;
    size_t *part_lens = NULL;
    uint8_t *prefix = NULL;
    size_t prefix_len = 1u;
    size_t offset = 0u;
    bool ok = false;

    if (prefix_out)
        *prefix_out = NULL;
    if (prefix_len_out)
        *prefix_len_out = 0u;
    if (!term || term->kind != ATOM_EXPR || !prefix_out ||
        !prefix_len_out || element_len == 0u ||
        element_len >= term->expr.len ||
        !cetta_expr_len_fits_size(element_len) ||
        (size_t)element_len > SIZE_MAX / sizeof(*parts) ||
        (size_t)element_len > SIZE_MAX / sizeof(*part_lens))
        return false;
    parts = calloc((size_t)element_len, sizeof(*parts));
    part_lens = calloc((size_t)element_len, sizeof(*part_lens));
    if (!parts || !part_lens)
        goto done;
    for (CettaExprIndex index = 0u; index < element_len; index++) {
        if (!fh_ground_term_v1_render(
                term->expr.elems[index], &parts[index], &part_lens[index],
                NULL, 0u) ||
            part_lens[index] > SIZE_MAX - prefix_len -
                (index == 0u ? 0u : 1u))
            goto done;
        prefix_len += part_lens[index] + (index == 0u ? 0u : 1u);
    }
    if (prefix_len == SIZE_MAX)
        goto done;
    prefix = malloc(prefix_len + 1u);
    if (!prefix)
        goto done;
    prefix[offset++] = (uint8_t)'(';
    for (CettaExprIndex index = 0u; index < element_len; index++) {
        if (index > 0u)
            prefix[offset++] = (uint8_t)' ';
        memcpy(prefix + offset, parts[index], part_lens[index]);
        offset += part_lens[index];
    }
    prefix[offset] = 0u;
    *prefix_out = prefix;
    *prefix_len_out = prefix_len;
    prefix = NULL;
    ok = true;

done:
    if (parts) {
        for (CettaExprIndex index = 0u; index < element_len; index++)
            free(parts[index]);
    }
    free(prefix);
    free(part_lens);
    free(parts);
    return ok;
}

static void pposlf_native_prefix_memo_v1_init(
    PPOSLFNativePrefixMemoV1 *memo) {
    if (!memo)
        return;
    memset(memo, 0, sizeof(*memo));
    arena_init(&memo->arena);
    arena_set_runtime_kind(
        &memo->arena, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&memo->arena, NULL);
}

static void pposlf_native_prefix_memo_v1_free(
    PPOSLFNativePrefixMemoV1 *memo) {
    if (!memo)
        return;
    for (size_t index = 0u; index < memo->cap; index++)
        free(memo->slots[index].canonical);
    free(memo->slots);
    arena_free(&memo->arena);
    memset(memo, 0, sizeof(*memo));
}

static size_t pposlf_native_prefix_memo_v1_slot(
    uint32_t structural_hash, size_t mask) {
    return pposlf_native_capability_exact_slot_v1(
        0u, structural_hash, mask);
}

static bool pposlf_native_prefix_memo_v1_same(
    const Atom *left, const Atom *right) {
    if (!left || !right || left->kind != ATOM_EXPR ||
        right->kind != ATOM_EXPR || left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex index = 0u; index < left->expr.len; index++) {
        if (!atom_eq(left->expr.elems[index], right->expr.elems[index]))
            return false;
    }
    return true;
}

static bool pposlf_native_prefix_memo_v1_grow(
    PPOSLFNativePrefixMemoV1 *memo) {
    size_t next_cap = memo->cap ? memo->cap * 2u : 64u;
    PPOSLFNativePrefixMemoSlotV1 *next;

    if (next_cap < memo->cap ||
        next_cap > SIZE_MAX / sizeof(*next))
        return false;
    next = calloc(next_cap, sizeof(*next));
    if (!next)
        return false;
    for (size_t index = 0u; index < memo->cap; index++) {
        PPOSLFNativePrefixMemoSlotV1 entry = memo->slots[index];
        size_t slot_index;

        if (!entry.prefix)
            continue;
        slot_index = pposlf_native_prefix_memo_v1_slot(
            entry.structural_hash, next_cap - 1u);
        while (next[slot_index].prefix)
            slot_index = (slot_index + 1u) & (next_cap - 1u);
        next[slot_index] = entry;
    }
    free(memo->slots);
    memo->slots = next;
    memo->cap = next_cap;
    return true;
}

static bool pposlf_native_prefix_memo_v1_needs_grow(
    const PPOSLFNativePrefixMemoV1 *memo) {
    size_t threshold;

    if (!memo || memo->cap == 0u)
        return true;
    /* floor(7 * cap / 10), without multiplying an untrusted size first. */
    threshold = (memo->cap / 10u) * 7u +
        ((memo->cap % 10u) * 7u) / 10u;
    return memo->len >= threshold;
}

static bool pposlf_native_prefix_memo_v1_get(
    PPOSLFNativeSearchV1 *search,
    Atom *term, CettaExprLen element_len,
    const uint8_t **prefix_out, size_t *prefix_len_out) {
    PPOSLFNativePrefixMemoV1 *memo;
    Atom prefix_view;
    uint32_t structural_hash;
    size_t slot_index;
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    Atom **elements;
    Atom *prefix;

    if (prefix_out)
        *prefix_out = NULL;
    if (prefix_len_out)
        *prefix_len_out = 0u;
    if (!search || !term || term->kind != ATOM_EXPR ||
        element_len == 0u || element_len >= term->expr.len ||
        !prefix_out || !prefix_len_out)
        return false;
    memo = &search->prefix_memo;
    memset(&prefix_view, 0, sizeof(prefix_view));
    prefix_view.kind = ATOM_EXPR;
    prefix_view.expr.elems = term->expr.elems;
    prefix_view.expr.len = element_len;
    structural_hash = atom_hash(&prefix_view);
    if (memo->cap != 0u) {
        slot_index = pposlf_native_prefix_memo_v1_slot(
            structural_hash, memo->cap - 1u);
        for (size_t probe = 0u; probe < memo->cap; probe++) {
            PPOSLFNativePrefixMemoSlotV1 *slot =
                &memo->slots[slot_index];

            if (!slot->prefix)
                break;
            if (slot->structural_hash == structural_hash &&
                pposlf_native_prefix_memo_v1_same(
                    slot->prefix, &prefix_view)) {
                search->stats.external_prefix_memo_hits++;
                *prefix_out = slot->canonical;
                *prefix_len_out = slot->canonical_len;
                return true;
            }
            slot_index = (slot_index + 1u) & (memo->cap - 1u);
        }
    }
    search->stats.external_prefix_memo_misses++;
    if (!pposlf_native_type_vm_v1_render_application_prefix(
            term, element_len, &canonical, &canonical_len))
        return false;
    if (pposlf_native_prefix_memo_v1_needs_grow(memo)) {
        if (!pposlf_native_prefix_memo_v1_grow(memo)) {
            free(canonical);
            return false;
        }
    }
    elements = arena_alloc(
        &memo->arena, (size_t)element_len * sizeof(*elements));
    if (!elements) {
        free(canonical);
        return false;
    }
    for (CettaExprIndex index = 0u; index < element_len; index++) {
        elements[index] = atom_deep_copy(
            &memo->arena, term->expr.elems[index]);
        if (!elements[index]) {
            free(canonical);
            return false;
        }
    }
    prefix = atom_expr(&memo->arena, elements, element_len);
    if (!prefix) {
        free(canonical);
        return false;
    }
    slot_index = pposlf_native_prefix_memo_v1_slot(
        structural_hash, memo->cap - 1u);
    while (memo->slots[slot_index].prefix)
        slot_index = (slot_index + 1u) & (memo->cap - 1u);
    memo->slots[slot_index] = (PPOSLFNativePrefixMemoSlotV1){
        .prefix = prefix,
        .canonical = canonical,
        .canonical_len = canonical_len,
        .structural_hash = structural_hash,
    };
    memo->len++;
    *prefix_out = canonical;
    *prefix_len_out = canonical_len;
    return true;
}

static int pposlf_native_type_vm_v1_compare_row_prefix(
    const PPOSLFNativeCapabilityRowV1 *row,
    const uint8_t *prefix, size_t prefix_len) {
    size_t common;
    int comparison;

    common = row->canonical_len < prefix_len
        ? row->canonical_len : prefix_len;
    comparison = memcmp(row->canonical, prefix, common);
    if (comparison != 0)
        return comparison;
    return row->canonical_len < prefix_len ? -1 : 0;
}

static void pposlf_native_type_vm_v1_capability_prefix_range(
    const PPOSLFNativeCapabilitySetImplV1 *capabilities,
    uint32_t relation, const uint8_t *prefix, size_t prefix_len,
    uint32_t *begin_out, uint32_t *end_out) {
    uint32_t low = capabilities->relation_offsets[relation];
    uint32_t high = capabilities->relation_offsets[relation + 1u];
    uint32_t begin;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (pposlf_native_type_vm_v1_compare_row_prefix(
                &capabilities->rows[middle], prefix, prefix_len) < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    begin = low;
    high = capabilities->relation_offsets[relation + 1u];
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (pposlf_native_type_vm_v1_compare_row_prefix(
                &capabilities->rows[middle], prefix, prefix_len) <= 0)
            low = middle + 1u;
        else
            high = middle;
    }
    *begin_out = begin;
    *end_out = low;
}

typedef enum {
    PPOSLF_NATIVE_SEARCH_GENERATED_V1,
    PPOSLF_NATIVE_SEARCH_EXTERNAL_V1,
} PPOSLFNativeSearchStageV1;

typedef struct {
    PPOSLFNativePendingGoalV1 *goals;
    Atom *goal;
    ArenaMark entry_arena_mark;
    ArenaMark candidate_arena_mark;
    uint32_t entry_binding_mark;
    uint32_t candidate_binding_mark;
    uint32_t entry_proof_mark;
    uint32_t proof_mark;
    uint32_t exact_group;
    uint32_t variable_group;
    uint32_t exact_offset;
    uint32_t exact_source_offset;
    uint32_t variable_offset;
    uint32_t full_offset;
    const uint32_t *rigid_positions;
    uint32_t rigid_position_count;
    uint32_t external_relation;
    uint32_t external_offset;
    uint32_t external_end;
    uint32_t failure_debt_head;
    uint32_t entry_failure_debt_len;
    PPOSLFNativeSearchStageV1 stage;
    bool full_scan;
    bool has_exact;
    bool has_variable;
    bool entry_ready;
    bool initialized;
    bool waiting_child;
    bool saw_resource;
    bool goal_is_activation_view;
    bool rigid_dispatched;
} PPOSLFNativeSearchFrameV1;

static void pposlf_native_type_vm_v1_prepare_rigid_dispatch(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frame,
    const CettaGsltRigidCoordinateIndexV1 *index) {
    Atom *coordinate;
    Atom *resolved = NULL;
    CettaGsltRigidKeyV1 key;

    if (!search || !frame || !index || !index->admitted ||
        !frame->has_exact || !frame->goal ||
        frame->goal->kind != ATOM_EXPR || !frame->goal->expr.elems ||
        frame->goal->expr.len == 0u ||
        index->coordinate >= frame->goal->expr.len - 1u)
        return;
    coordinate = frame->goal->expr.elems[index->coordinate + 1u];
    if (coordinate && coordinate->kind == ATOM_VAR &&
        frame->goal_is_activation_view) {
        if (!bindings_resolve_epoch_view_ground(
                bindings_builder_bindings(&search->bindings),
                coordinate, frame->goals->epoch,
                frame->goals->activation_first_entry, &resolved) ||
            !resolved)
            return;
        coordinate = resolved;
    }
    if (!cetta_gslt_rigid_key_from_atom_v1(coordinate, &key) ||
        !cetta_gslt_rigid_coordinate_index_positions_v1(
            index, &key, &frame->rigid_positions,
            &frame->rigid_position_count))
        return;
    frame->rigid_dispatched = true;
    search->stats.rigid_coordinate_dispatches++;
}

/* Refine a relation-wide capability range only after a concrete goal exists.
 * Activation views intentionally postpone this work: exact canonical keys and
 * rigid prefixes are observations of the substituted value, not of its source
 * closure. */
static void pposlf_native_type_vm_v1_refine_external_range(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frame) {
    uint32_t external_relation;

    if (!search || !frame || !frame->goal ||
        frame->goal_is_activation_view || !search->capabilities ||
        frame->external_offset >= frame->external_end)
        return;
    external_relation = frame->external_relation;
    if (!atom_has_vars(frame->goal)) {
        uint32_t exact_row;

        search->stats.external_exact_key_lookups++;
        if (pposlf_native_type_vm_v1_exact_capability_row(
                search->capabilities, external_relation,
                frame->goal, &exact_row)) {
            frame->external_offset = exact_row;
            frame->external_end = exact_row + 1u;
            search->stats.external_exact_key_hits++;
        } else {
            frame->external_offset = frame->external_end;
        }
        return;
    }
    if (frame->goal->kind == ATOM_EXPR) {
        CettaExprLen prefix_elements = 1u;
        const uint8_t *prefix = NULL;
        size_t prefix_len = 0u;
        uint32_t begin;
        uint32_t end;

        while (prefix_elements < frame->goal->expr.len &&
               !atom_has_vars(
                   frame->goal->expr.elems[prefix_elements]))
            prefix_elements++;
        if (prefix_elements > 1u &&
            pposlf_native_prefix_memo_v1_get(
                search, frame->goal, prefix_elements,
                &prefix, &prefix_len)) {
            search->stats.external_prefix_key_lookups++;
            pposlf_native_type_vm_v1_capability_prefix_range(
                search->capabilities, external_relation,
                prefix, prefix_len, &begin, &end);
            frame->external_offset = begin;
            frame->external_end = end;
            search->stats.external_prefix_key_candidates +=
                (uint64_t)(end - begin);
            if (begin < end)
                search->stats.external_prefix_key_hits++;
        }
    }
}

static bool pposlf_native_type_vm_v1_materialize_activation_view(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frame) {
    size_t before;
    size_t after;
    Atom *goal;

    if (!search || !frame || !frame->goals)
        return false;
    if (!frame->goal_is_activation_view)
        return frame->goal != NULL;
    before = arena_accounted_live_bytes(&search->scratch);
    search->stats.deferred_epoch_goal_materializations++;
    search->stats.activation_view_fallback_materializations++;
    goal = bindings_apply_epoch_then_all(
        (Bindings *)bindings_builder_bindings(&search->bindings),
        &search->scratch, frame->goals->goal, frame->goals->epoch,
        frame->goals->activation_first_entry);
    after = arena_accounted_live_bytes(&search->scratch);
    if (after > before)
        search->stats.goal_materialization_arena_bytes += after - before;
    if (!goal)
        return false;
    frame->goal = goal;
    frame->goal_is_activation_view = false;
    pposlf_native_type_vm_v1_refine_external_range(search, frame);
    frame->candidate_arena_mark = arena_mark(&search->scratch);
    return true;
}

static bool pposlf_native_type_vm_v1_push_search_frame(
    PPOSLFNativeSearchFrameV1 **frames,
    uint32_t *frame_len,
    uint32_t *frame_cap,
    PPOSLFNativePendingGoalV1 *goals,
    uint32_t entry_failure_debt_len) {
    PPOSLFNativeSearchFrameV1 *grown;
    uint32_t next_cap;

    if (!frames || !frame_len || !frame_cap || !goals)
        return false;
    if (*frame_len == *frame_cap) {
        next_cap = *frame_cap ? *frame_cap * 2u : 64u;
        if (next_cap < *frame_cap ||
            (size_t)next_cap >
                SIZE_MAX / sizeof(**frames))
            return false;
        grown = realloc(*frames, (size_t)next_cap * sizeof(*grown));
        if (!grown)
            return false;
        *frames = grown;
        *frame_cap = next_cap;
    }
    (*frames)[*frame_len] = (PPOSLFNativeSearchFrameV1){
        .goals = goals,
        .failure_debt_head = UINT32_MAX,
        .entry_failure_debt_len = entry_failure_debt_len,
    };
    (*frame_len)++;
    return true;
}

/*
 * Replace a frame only after the generic candidate analysis proves that it
 * has no later alternative.  The replacement inherits the original entry
 * marks so failure rolls back the whole deterministic chain, including its
 * proof events and any resource exhaustion observed before the final branch.
 */
static void pposlf_native_type_vm_v1_reuse_tail_frame(
    PPOSLFNativeSearchFrameV1 *frame,
    PPOSLFNativePendingGoalV1 *goals) {
    ArenaMark entry_arena_mark = frame->entry_arena_mark;
    uint32_t entry_binding_mark = frame->entry_binding_mark;
    uint32_t entry_proof_mark = frame->entry_proof_mark;
    uint32_t failure_debt_head = frame->failure_debt_head;
    uint32_t entry_failure_debt_len = frame->entry_failure_debt_len;
    bool saw_resource = frame->saw_resource;

    *frame = (PPOSLFNativeSearchFrameV1){
        .goals = goals,
        .entry_arena_mark = entry_arena_mark,
        .entry_binding_mark = entry_binding_mark,
        .entry_proof_mark = entry_proof_mark,
        .failure_debt_head = failure_debt_head,
        .entry_failure_debt_len = entry_failure_debt_len,
        .entry_ready = true,
        .saw_resource = saw_resource,
    };
}

#define PPOSLF_NATIVE_DETERMINISTIC_ARENA_WINDOW_V1 \
    ((size_t)8u * 1024u * 1024u)
#define PPOSLF_NATIVE_DETERMINISTIC_BINDING_WINDOW_V1 UINT64_C(1048576)

static size_t pposlf_native_size_add_saturating_v1(
    size_t left, size_t right) {
    return left > SIZE_MAX - right ? SIZE_MAX : left + right;
}

static uint64_t pposlf_native_u64_add_saturating_v1(
    uint64_t left, uint64_t right) {
    return left > UINT64_MAX - right ? UINT64_MAX : left + right;
}

static Atom *pposlf_native_type_vm_v1_evacuate_atom(
    const Arena *source,
    AtomDeepCopySession *session,
    Atom *atom) {
    if (!atom || !arena_owns_atom(source, atom))
        return atom;
    return atom_deep_copy_session_copy(session, atom);
}

typedef struct {
    const PPOSLFNativePendingGoalV1 **slots;
    size_t len;
    size_t cap;
} PPOSLFNativePendingGoalSetV1;

static size_t pposlf_native_pending_goal_hash_v1(
    const PPOSLFNativePendingGoalV1 *goal) {
    uintptr_t value = (uintptr_t)goal;

    value >>= 3u;
    value ^= value >> 17u;
    value *= (uintptr_t)UINT64_C(0x9e3779b97f4a7c15);
    value ^= value >> 23u;
    return (size_t)value;
}

static bool pposlf_native_pending_goal_set_grow_v1(
    PPOSLFNativePendingGoalSetV1 *set) {
    const PPOSLFNativePendingGoalV1 **next;
    size_t next_cap = set->cap ? set->cap * 2u : 128u;

    if (next_cap < set->cap ||
        next_cap > SIZE_MAX / sizeof(*next))
        return false;
    next = calloc(next_cap, sizeof(*next));
    if (!next)
        return false;
    for (size_t index = 0u; index < set->cap; index++) {
        const PPOSLFNativePendingGoalV1 *item = set->slots[index];
        size_t slot;

        if (!item)
            continue;
        slot = pposlf_native_pending_goal_hash_v1(item) &
            (next_cap - 1u);
        while (next[slot])
            slot = (slot + 1u) & (next_cap - 1u);
        next[slot] = item;
    }
    free(set->slots);
    set->slots = next;
    set->cap = next_cap;
    return true;
}

/* Returns one for a new node, zero for an existing node, and minus one on
 * allocation failure. */
static int pposlf_native_pending_goal_set_insert_v1(
    PPOSLFNativePendingGoalSetV1 *set,
    const PPOSLFNativePendingGoalV1 *goal) {
    size_t slot;

    if (!set || !goal)
        return -1;
    if (set->cap == 0u ||
        set->len >= set->cap - set->cap / 4u) {
        if (!pposlf_native_pending_goal_set_grow_v1(set))
            return -1;
    }
    slot = pposlf_native_pending_goal_hash_v1(goal) &
        (set->cap - 1u);
    while (set->slots[slot]) {
        if (set->slots[slot] == goal)
            return 0;
        slot = (slot + 1u) & (set->cap - 1u);
    }
    set->slots[slot] = goal;
    set->len++;
    return 1;
}

static bool pposlf_native_pending_goal_set_slot_v1(
    const PPOSLFNativePendingGoalSetV1 *set,
    const PPOSLFNativePendingGoalV1 *goal,
    size_t *slot_out) {
    size_t slot;

    if (slot_out)
        *slot_out = 0u;
    if (!set || !goal || !slot_out || set->cap == 0u)
        return false;
    slot = pposlf_native_pending_goal_hash_v1(goal) &
        (set->cap - 1u);
    for (size_t probe = 0u; probe < set->cap; probe++) {
        if (!set->slots[slot])
            return false;
        if (set->slots[slot] == goal) {
            *slot_out = slot;
            return true;
        }
        slot = (slot + 1u) & (set->cap - 1u);
    }
    return false;
}

static bool pposlf_native_atom_root_push_v1(
    Atom ***items, size_t *len, size_t *cap, Atom *atom) {
    Atom **grown;
    size_t next_cap;

    if (!items || !len || !cap || !atom)
        return false;
    if (*len == *cap) {
        next_cap = *cap ? *cap * 2u : 128u;
        if (next_cap < *cap ||
            next_cap > SIZE_MAX / sizeof(*grown))
            return false;
        grown = realloc(*items, next_cap * sizeof(*grown));
        if (!grown)
            return false;
        *items = grown;
        *cap = next_cap;
    }
    (*items)[(*len)++] = atom;
    return true;
}

static bool pposlf_native_epoch_root_push_v1(
    BindingsEpochRoot **items, size_t *len, size_t *cap,
    Atom *atom, uint32_t epoch) {
    BindingsEpochRoot *grown;
    size_t next_cap;

    if (!items || !len || !cap || !atom)
        return false;
    if (*len == *cap) {
        next_cap = *cap ? *cap * 2u : 128u;
        if (next_cap < *cap ||
            next_cap > SIZE_MAX / sizeof(*grown))
            return false;
        grown = realloc(*items, next_cap * sizeof(*grown));
        if (!grown)
            return false;
        *items = grown;
        *cap = next_cap;
    }
    (*items)[(*len)++] = (BindingsEpochRoot){
        .atom = atom,
        .epoch = epoch,
    };
    return true;
}

static bool pposlf_native_entry_mark_push_v1(
    uint32_t **items, uint32_t ***fields,
    size_t *len, size_t *cap, uint32_t *field) {
    uint32_t *grown_items;
    uint32_t **grown_fields;
    size_t next_cap;

    if (!items || !fields || !len || !cap || !field)
        return false;
    if (*len == *cap) {
        next_cap = *cap ? *cap * 2u : 128u;
        if (next_cap < *cap ||
            next_cap > SIZE_MAX / sizeof(*grown_items) ||
            next_cap > SIZE_MAX / sizeof(*grown_fields))
            return false;
        grown_items = realloc(*items, next_cap * sizeof(*grown_items));
        if (!grown_items)
            return false;
        *items = grown_items;
        grown_fields = realloc(
            *fields, next_cap * sizeof(*grown_fields));
        if (!grown_fields)
            return false;
        *fields = grown_fields;
        *cap = next_cap;
    }
    (*items)[*len] = *field;
    (*fields)[*len] = field;
    (*len)++;
    return true;
}

/*
 * Binding liveness is derived from the same pending-goal roots that define
 * the generic finite-Horn transition.  Every live rollback checkpoint is
 * supplied to the shared compactor and rewritten transactionally; no guest
 * symbol, rule name, or fixed role inventory participates in the decision.
 */
static bool pposlf_native_type_vm_v1_compact_search_bindings(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frames,
    uint32_t frame_len) {
    PPOSLFNativePendingGoalSetV1 seen = {0};
    Atom **roots = NULL;
    size_t root_len = 0u;
    size_t root_cap = 0u;
    BindingsEpochRoot *epoch_roots = NULL;
    size_t epoch_root_len = 0u;
    size_t epoch_root_cap = 0u;
    uint32_t *marks = NULL;
    uint32_t **mark_fields = NULL;
    size_t mark_len = 0u;
    size_t mark_cap;
    uint32_t *entry_marks = NULL;
    uint32_t **entry_mark_fields = NULL;
    size_t entry_mark_len = 0u;
    size_t entry_mark_cap = 0u;
    uint64_t discarded_items = 0u;
    uint64_t discarded_trail = 0u;
    bool ok = false;

    if (!search || !frames || frame_len == 0u ||
        frame_len > SIZE_MAX / (2u * sizeof(*marks)) ||
        frame_len > SIZE_MAX / (2u * sizeof(*mark_fields)))
        return false;
    mark_cap = (size_t)frame_len * 2u;
    marks = malloc(mark_cap * sizeof(*marks));
    mark_fields = malloc(mark_cap * sizeof(*mark_fields));
    if (!marks || !mark_fields)
        goto done;

    for (uint32_t frame_index = 0u;
         frame_index < frame_len; frame_index++) {
        PPOSLFNativeSearchFrameV1 *frame = &frames[frame_index];
        PPOSLFNativePendingGoalV1 *goal;

        if (frame->entry_ready) {
            marks[mark_len] = frame->entry_binding_mark;
            mark_fields[mark_len++] = &frame->entry_binding_mark;
        }
        if (frame->waiting_child) {
            marks[mark_len] = frame->candidate_binding_mark;
            mark_fields[mark_len++] = &frame->candidate_binding_mark;
        }
        if (frame->goal && !frame->goal_is_activation_view &&
            !pposlf_native_atom_root_push_v1(
                &roots, &root_len, &root_cap, frame->goal))
            goto done;
        for (goal = frame->goals; goal; goal = goal->next) {
            int inserted =
                pposlf_native_pending_goal_set_insert_v1(&seen, goal);

            if (inserted < 0)
                goto done;
            if (inserted == 0)
                break;
            if (goal->epoch_original) {
                if (!pposlf_native_epoch_root_push_v1(
                        &epoch_roots, &epoch_root_len,
                        &epoch_root_cap, goal->goal, goal->epoch))
                    goto done;
                if (!pposlf_native_entry_mark_push_v1(
                        &entry_marks, &entry_mark_fields,
                        &entry_mark_len, &entry_mark_cap,
                        &goal->activation_first_entry))
                    goto done;
            } else if (!pposlf_native_atom_root_push_v1(
                           &roots, &root_len, &root_cap, goal->goal)) {
                goto done;
            }
        }
    }
    if (!bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
            &search->bindings, roots, root_len,
            epoch_roots, epoch_root_len,
            marks, mark_len, entry_marks, entry_mark_len,
            &discarded_items, &discarded_trail))
        goto done;
    for (size_t index = 0u; index < mark_len; index++)
        *mark_fields[index] = marks[index];
    for (size_t index = 0u; index < entry_mark_len; index++)
        *entry_mark_fields[index] = entry_marks[index];
    search->stats.deterministic_binding_collections++;
    search->stats.deterministic_binding_roots_scanned +=
        (uint64_t)root_len + epoch_root_len;
    search->stats.deterministic_binding_items_discarded +=
        discarded_items;
    search->stats.deterministic_trail_entries_discarded +=
        discarded_trail;
    {
        const Bindings *current =
            bindings_builder_bindings(&search->bindings);
        uint64_t logical_items =
            (uint64_t)current->len + current->eq_len;
        uint64_t window = logical_items >
                PPOSLF_NATIVE_DETERMINISTIC_BINDING_WINDOW_V1
            ? logical_items
            : PPOSLF_NATIVE_DETERMINISTIC_BINDING_WINDOW_V1;

        search->binding_growth_collect_after =
            pposlf_native_u64_add_saturating_v1(
                search->bindings.growth_count, window);
    }
    ok = true;

done:
    free(seen.slots);
    free(roots);
    free(epoch_roots);
    free(marks);
    free(mark_fields);
    free(entry_marks);
    free(entry_mark_fields);
    return ok;
}

static bool pposlf_native_type_vm_v1_builder_has_prime_state(
    const BindingsBuilder *builder) {
    return !builder || bindings_builder_prime_present(builder);
}

static bool pposlf_native_type_vm_v1_stage_builder_atoms(
    const BindingsBuilder *builder,
    const Arena *source,
    AtomDeepCopySession *session) {
    const Bindings *bindings;

    if (!builder || !source || !session)
        return false;
    bindings = &builder->current;
    for (uint32_t index = 0u; index < bindings->len; index++) {
        Atom *name_key = pposlf_native_type_vm_v1_evacuate_atom(
            source, session, bindings->entries[index].name_key);
        Atom *value = pposlf_native_type_vm_v1_evacuate_atom(
            source, session, bindings->entries[index].val);

        if ((bindings->entries[index].name_key && !name_key) ||
            (bindings->entries[index].val && !value))
            return false;
    }
    for (uint32_t index = 0u; index < bindings->eq_len; index++) {
        Atom *left = pposlf_native_type_vm_v1_evacuate_atom(
            source, session, bindings->constraints[index].lhs);
        Atom *right = pposlf_native_type_vm_v1_evacuate_atom(
            source, session, bindings->constraints[index].rhs);

        if ((bindings->constraints[index].lhs && !left) ||
            (bindings->constraints[index].rhs && !right))
            return false;
    }
    return true;
}

static bool pposlf_native_type_vm_v1_builder_atoms_forwarded(
    const BindingsBuilder *builder,
    const Arena *source,
    const AtomDeepCopySession *session) {
    const Bindings *bindings;

    if (!builder || !source || !session)
        return false;
    bindings = &builder->current;
    for (uint32_t index = 0u; index < bindings->len; index++) {
        if ((bindings->entries[index].name_key &&
             arena_owns_atom(source, bindings->entries[index].name_key) &&
             !atom_deep_copy_session_forwarded(
                 session, bindings->entries[index].name_key)) ||
            (bindings->entries[index].val &&
             arena_owns_atom(source, bindings->entries[index].val) &&
             !atom_deep_copy_session_forwarded(
                 session, bindings->entries[index].val)))
            return false;
    }
    for (uint32_t index = 0u; index < bindings->eq_len; index++) {
        if ((bindings->constraints[index].lhs &&
             arena_owns_atom(source, bindings->constraints[index].lhs) &&
             !atom_deep_copy_session_forwarded(
                 session, bindings->constraints[index].lhs)) ||
            (bindings->constraints[index].rhs &&
             arena_owns_atom(source, bindings->constraints[index].rhs) &&
             !atom_deep_copy_session_forwarded(
                 session, bindings->constraints[index].rhs)))
            return false;
    }
    return true;
}

static void pposlf_native_type_vm_v1_commit_builder_atoms(
    BindingsBuilder *builder,
    const Arena *source,
    const AtomDeepCopySession *session) {
    Bindings *bindings = &builder->current;

    for (uint32_t index = 0u; index < bindings->len; index++) {
        if (bindings->entries[index].name_key &&
            arena_owns_atom(source, bindings->entries[index].name_key)) {
            bindings->entries[index].name_key =
                atom_deep_copy_session_forwarded(
                    session, bindings->entries[index].name_key);
        }
        if (bindings->entries[index].val &&
            arena_owns_atom(source, bindings->entries[index].val)) {
            bindings->entries[index].val =
                atom_deep_copy_session_forwarded(
                    session, bindings->entries[index].val);
        }
    }
    for (uint32_t index = 0u; index < bindings->eq_len; index++) {
        if (bindings->constraints[index].lhs &&
            arena_owns_atom(source, bindings->constraints[index].lhs)) {
            bindings->constraints[index].lhs =
                atom_deep_copy_session_forwarded(
                    session, bindings->constraints[index].lhs);
        }
        if (bindings->constraints[index].rhs &&
            arena_owns_atom(source, bindings->constraints[index].rhs)) {
            bindings->constraints[index].rhs =
                atom_deep_copy_session_forwarded(
                    session, bindings->constraints[index].rhs);
        }
    }
    bindings_invalidate_after_key_rewrite(bindings);
}

/*
 * A generated deterministic-tail transition is a safe point: no C local
 * carries a guest value across it.  The complete live atom graph is named by
 * the generic search state itself -- materialized frame goals, shared pending
 * continuations, and the rollback-aware binding builder.  Dense ground
 * substitutions are discarded before every safe point and retain no logical
 * roots.  Proof receipts
 * carry only generated-step or capability-row indices, while the prefix memo
 * owns a separate arena.
 *
 * Copy only nursery-owned roots into the survivor arena, then move the common
 * rollback floor to the empty nursery.  Older survivors are reused by
 * identity, so the work is proportional to newly surviving data rather than
 * the full search history.  Retaining survivors is conservative: a rollback
 * may keep garbage, but cannot lose a live value.  The operation is
 * transactional; any missing root, allocation failure, or unsupported carried
 * state leaves the original search untouched.
 */
static bool pposlf_native_type_vm_v1_collect_deterministic_tail(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frames,
    uint32_t frame_len) {
    PPOSLFNativePendingGoalSetV1 pending = {0};
    PPOSLFNativePendingGoalV1 **pending_copies = NULL;
    PPOSLFNativePendingGoalV1 **frame_pending_copies = NULL;
    Atom **frame_goal_copies = NULL;
    ArenaMark survivor_entry_mark;
    ArenaMark scratch_survivor_mark;
    AtomDeepCopySession *session = NULL;
    size_t before_arena_bytes;
    size_t survivor_bytes_before;
    size_t survivor_bytes_after;
    size_t copied_bytes;
    uint64_t root_count = 0u;
    bool survivor_entry_ready = false;
    bool ok = false;

    if (!search || !frames || frame_len == 0u ||
        frame_len > SIZE_MAX / sizeof(*frame_pending_copies) ||
        frame_len > SIZE_MAX / sizeof(*frame_goal_copies))
        return false;
    if (pposlf_native_type_vm_v1_builder_has_prime_state(
            &search->bindings))
        return false;

    frame_pending_copies = calloc(
        frame_len, sizeof(*frame_pending_copies));
    frame_goal_copies = calloc(frame_len, sizeof(*frame_goal_copies));
    if (!frame_pending_copies || !frame_goal_copies)
        goto done;

    for (uint32_t frame_index = 0u;
         frame_index < frame_len; frame_index++) {
        PPOSLFNativePendingGoalV1 *goal;

        for (goal = frames[frame_index].goals; goal; goal = goal->next) {
            if (!arena_owns_ptr(&search->scratch, goal)) {
                if (goal->next &&
                    arena_owns_ptr(&search->scratch, goal->next))
                    goto done;
                break;
            }
            int inserted =
                pposlf_native_pending_goal_set_insert_v1(&pending, goal);

            if (inserted < 0)
                goto done;
            if (inserted == 0)
                break;
        }
    }
    if (pending.cap > SIZE_MAX / sizeof(*pending_copies))
        goto done;
    pending_copies = pending.cap
        ? calloc(pending.cap, sizeof(*pending_copies)) : NULL;
    if (pending.cap && !pending_copies)
        goto done;

    survivor_entry_mark = arena_mark(&search->survivor);
    survivor_entry_ready = true;
    survivor_bytes_before =
        arena_accounted_live_bytes(&search->survivor);
    session = atom_deep_copy_session_new(&search->survivor);
    if (!session)
        goto done;

    for (size_t slot = 0u; slot < pending.cap; slot++) {
        const PPOSLFNativePendingGoalV1 *source = pending.slots[slot];
        PPOSLFNativePendingGoalV1 *copy;
        Atom *goal;

        if (!source)
            continue;
        copy = arena_alloc(&search->survivor, sizeof(*copy));
        goal = pposlf_native_type_vm_v1_evacuate_atom(
            &search->scratch, session, source->goal);
        if (!copy || (source->goal && !goal))
            goto done;
        *copy = *source;
        copy->goal = goal;
        copy->next = NULL;
        pending_copies[slot] = copy;
    }
    for (size_t slot = 0u; slot < pending.cap; slot++) {
        const PPOSLFNativePendingGoalV1 *source = pending.slots[slot];
        size_t next_slot;

        if (!source || !source->next)
            continue;
        if (arena_owns_ptr(&search->scratch, source->next)) {
            if (!pposlf_native_pending_goal_set_slot_v1(
                    &pending, source->next, &next_slot) ||
                !pending_copies[next_slot])
                goto done;
            pending_copies[slot]->next = pending_copies[next_slot];
        } else {
            pending_copies[slot]->next = source->next;
        }
    }
    for (uint32_t frame_index = 0u;
         frame_index < frame_len; frame_index++) {
        size_t pending_slot;

        if (frames[frame_index].goals &&
            arena_owns_ptr(
                &search->scratch, frames[frame_index].goals)) {
            if (!pposlf_native_pending_goal_set_slot_v1(
                    &pending, frames[frame_index].goals,
                    &pending_slot) ||
                !pending_copies[pending_slot])
                goto done;
            frame_pending_copies[frame_index] =
                pending_copies[pending_slot];
        } else {
            frame_pending_copies[frame_index] =
                frames[frame_index].goals;
        }
        frame_goal_copies[frame_index] =
            pposlf_native_type_vm_v1_evacuate_atom(
                &search->scratch, session, frames[frame_index].goal);
        if (frames[frame_index].goal &&
            !frame_goal_copies[frame_index])
            goto done;
        if (frames[frame_index].goal)
            root_count++;
    }
    if (!pposlf_native_type_vm_v1_stage_builder_atoms(
            &search->bindings, &search->scratch, session) ||
        !pposlf_native_type_vm_v1_builder_atoms_forwarded(
            &search->bindings, &search->scratch, session))
        goto done;

    before_arena_bytes = arena_accounted_live_bytes(&search->scratch);
    survivor_bytes_after =
        arena_accounted_live_bytes(&search->survivor);
    if (survivor_bytes_after < survivor_bytes_before)
        goto done;
    copied_bytes = survivor_bytes_after - survivor_bytes_before;

    pposlf_native_type_vm_v1_commit_builder_atoms(
        &search->bindings, &search->scratch, session);

    arena_free(&search->scratch);
    arena_init(&search->scratch);
    arena_set_runtime_kind(
        &search->scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&search->scratch, NULL);
    scratch_survivor_mark = arena_mark(&search->scratch);
    for (uint32_t frame_index = 0u;
         frame_index < frame_len; frame_index++) {
        frames[frame_index].goals = frame_pending_copies[frame_index];
        frames[frame_index].goal = frame_goal_copies[frame_index];
        frames[frame_index].entry_arena_mark = scratch_survivor_mark;
        frames[frame_index].candidate_arena_mark = scratch_survivor_mark;
    }
    atom_deep_copy_session_free(session);
    session = NULL;

    search->stats.deterministic_tail_collections++;
    search->stats.deterministic_goal_roots_scanned +=
        root_count + pending.len;
    search->stats.deterministic_arena_bytes_copied += copied_bytes;
    if (before_arena_bytes > copied_bytes) {
        search->stats.deterministic_arena_bytes_reclaimed +=
            before_arena_bytes - copied_bytes;
    }
    {
        size_t copied_window = copied_bytes >
                PPOSLF_NATIVE_DETERMINISTIC_ARENA_WINDOW_V1
            ? copied_bytes
            : PPOSLF_NATIVE_DETERMINISTIC_ARENA_WINDOW_V1;

        search->deterministic_collect_after =
            pposlf_native_size_add_saturating_v1(
                copied_bytes, copied_window);
    }
    ok = true;

done:
    atom_deep_copy_session_free(session);
    if (!ok && survivor_entry_ready)
        arena_reset(&search->survivor, survivor_entry_mark);
    free(pending.slots);
    free(pending_copies);
    free(frame_pending_copies);
    free(frame_goal_copies);
    return ok;
}

static void pposlf_native_type_vm_v1_maybe_collect_deterministic_tail(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frames,
    uint32_t frame_len) {
    if (!search || !frames || frame_len == 0u)
        return;
    if (search->bindings.growth_count >=
            search->binding_growth_collect_after) {
        if (!pposlf_native_type_vm_v1_compact_search_bindings(
                search, frames, frame_len)) {
            search->stats.deterministic_binding_collection_failures++;
            search->binding_growth_collect_after =
                pposlf_native_u64_add_saturating_v1(
                    search->bindings.growth_count,
                    PPOSLF_NATIVE_DETERMINISTIC_BINDING_WINDOW_V1);
        }
    }
    if (frame_len == 1u &&
        arena_accounted_live_bytes(&search->scratch) >=
            search->deterministic_collect_after &&
        !pposlf_native_type_vm_v1_collect_deterministic_tail(
            search, frames, frame_len)) {
        search->stats.deterministic_tail_collection_failures++;
        search->deterministic_collect_after =
            pposlf_native_size_add_saturating_v1(
                arena_accounted_live_bytes(&search->scratch),
                PPOSLF_NATIVE_DETERMINISTIC_ARENA_WINDOW_V1);
    }
}

static bool pposlf_native_type_vm_v1_push_failure_debt(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frame,
    uint32_t candidate_count) {
    PPOSLFNativeFailureDebtV1 *grown;
    uint32_t next_cap;

    if (!search || !frame || candidate_count == 0u)
        return candidate_count == 0u;
    if (search->failure_debt_len == search->failure_debt_cap) {
        next_cap = search->failure_debt_cap
            ? search->failure_debt_cap * 2u : 64u;
        if (next_cap < search->failure_debt_cap ||
            (size_t)next_cap > SIZE_MAX / sizeof(*grown))
            return false;
        grown = realloc(
            search->failure_debts, (size_t)next_cap * sizeof(*grown));
        if (!grown)
            return false;
        search->failure_debts = grown;
        search->failure_debt_cap = next_cap;
    }
    search->failure_debts[search->failure_debt_len] =
        (PPOSLFNativeFailureDebtV1){
            .prior = frame->failure_debt_head,
            .candidate_count = candidate_count,
            .full_scan = frame->full_scan,
        };
    frame->failure_debt_head = search->failure_debt_len++;
    search->stats.deferred_shape_guard_candidates += candidate_count;
    return true;
}

static bool pposlf_native_type_vm_v1_charge_rigid_rejections(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frame,
    uint32_t candidate_count) {
    uint64_t available;
    uint64_t charged;

    if (!search || !frame)
        return false;
    if (candidate_count == 0u)
        return true;
    available = search->stats.rule_attempts <
            search->limits.maximum_rule_attempts
        ? search->limits.maximum_rule_attempts -
              search->stats.rule_attempts
        : 0u;
    charged = candidate_count < available ? candidate_count : available;
    search->stats.rule_attempts += charged;
    search->stats.structural_shape_guard_attempts += charged;
    search->stats.structural_shape_guard_rejections += charged;
    search->stats.rigid_coordinate_rejections += charged;
    search->stats.indexed_candidate_visits += charged;
    if (charged != candidate_count) {
        frame->saw_resource = true;
        return false;
    }
    return true;
}

static void pposlf_native_type_vm_v1_charge_failure_debt(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frame) {
    uint32_t debt;

    if (!search || !frame)
        return;
    debt = frame->failure_debt_head;
    while (debt != UINT32_MAX) {
        const PPOSLFNativeFailureDebtV1 *item;
        uint64_t available;
        uint64_t charged;

        if (debt >= search->failure_debt_len) {
            frame->saw_resource = true;
            break;
        }
        item = &search->failure_debts[debt];
        available = search->stats.rule_attempts <
                search->limits.maximum_rule_attempts
            ? search->limits.maximum_rule_attempts -
                  search->stats.rule_attempts
            : 0u;
        charged = item->candidate_count < available
            ? item->candidate_count : available;
        search->stats.rule_attempts += charged;
        search->stats.structural_shape_guard_attempts += charged;
        search->stats.structural_shape_guard_rejections += charged;
        search->stats.deferred_shape_guard_attempts += charged;
        if (item->full_scan)
            search->stats.full_scan_candidate_visits += charged;
        else
            search->stats.indexed_candidate_visits += charged;
        if (charged != item->candidate_count) {
            frame->saw_resource = true;
            break;
        }
        debt = item->prior;
    }
    frame->failure_debt_head = UINT32_MAX;
}

static void pposlf_native_type_vm_v1_rollback_search_branch(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frame) {
    size_t before_arena_bytes =
        arena_accounted_live_bytes(&search->scratch);

    bindings_builder_rollback(
        &search->bindings, frame->candidate_binding_mark);
    search->proof_event_len = frame->proof_mark;
    arena_reset(&search->scratch, frame->candidate_arena_mark);
    {
        size_t after_arena_bytes =
            arena_accounted_live_bytes(&search->scratch);

        if (before_arena_bytes > after_arena_bytes) {
            search->stats.rollback_arena_bytes_reclaimed +=
                before_arena_bytes - after_arena_bytes;
        }
    }
    frame->waiting_child = false;
}

typedef enum {
    PPOSLF_NATIVE_SHAPE_INCOMPATIBLE_V1,
    PPOSLF_NATIVE_SHAPE_COMPATIBLE_V1,
    PPOSLF_NATIVE_SHAPE_UNKNOWN_V1,
} PPOSLFNativeShapeCompatibilityV1;

typedef struct {
    const Atom *left;
    const Atom *right;
} PPOSLFNativeShapePairV1;

typedef struct {
    PPOSLFNativeShapePairV1 *items;
    size_t len;
    size_t cap;
    PPOSLFNativeShapePairV1 inline_items[32];
} PPOSLFNativeShapeWorklistV1;

static void pposlf_native_shape_worklist_v1_init(
    PPOSLFNativeShapeWorklistV1 *worklist) {
    worklist->items = worklist->inline_items;
    worklist->len = 0u;
    worklist->cap = sizeof(worklist->inline_items) /
        sizeof(worklist->inline_items[0]);
}

static void pposlf_native_shape_worklist_v1_free(
    PPOSLFNativeShapeWorklistV1 *worklist) {
    if (worklist->items != worklist->inline_items)
        free(worklist->items);
}

static bool pposlf_native_shape_worklist_v1_push(
    PPOSLFNativeShapeWorklistV1 *worklist,
    const Atom *left, const Atom *right) {
    PPOSLFNativeShapePairV1 *grown;
    size_t next_cap;

    if (worklist->len == worklist->cap) {
        next_cap = worklist->cap * 2u;
        if (next_cap <= worklist->cap ||
            next_cap > SIZE_MAX / sizeof(*grown))
            return false;
        grown = malloc(next_cap * sizeof(*grown));
        if (!grown)
            return false;
        memcpy(grown, worklist->items,
               worklist->len * sizeof(*grown));
        if (worklist->items != worklist->inline_items)
            free(worklist->items);
        worklist->items = grown;
        worklist->cap = next_cap;
    }
    worklist->items[worklist->len++] =
        (PPOSLFNativeShapePairV1){left, right};
    return true;
}

/*
 * A first-order rule head and goal can unify only if every pair of rigid
 * nodes has the same constructor shape.  Variables remain unknown: repeated
 * variables and existing bindings are deliberately left to the canonical
 * matcher.  Allocation failure also returns UNKNOWN, so this guard may only
 * remove work that the matcher would certainly reject.
 */
static PPOSLFNativeShapeCompatibilityV1
pposlf_native_type_vm_v1_structural_shape_compatibility(
    const Atom *left, const Atom *right) {
    PPOSLFNativeShapeWorklistV1 worklist;
    PPOSLFNativeShapeCompatibilityV1 result =
        PPOSLF_NATIVE_SHAPE_COMPATIBLE_V1;

    pposlf_native_shape_worklist_v1_init(&worklist);
    if (!pposlf_native_shape_worklist_v1_push(
            &worklist, left, right)) {
        result = PPOSLF_NATIVE_SHAPE_UNKNOWN_V1;
        goto done;
    }
    while (worklist.len > 0u) {
        PPOSLFNativeShapePairV1 pair =
            worklist.items[--worklist.len];

        left = pair.left;
        right = pair.right;
        if (!left || !right) {
            result = PPOSLF_NATIVE_SHAPE_UNKNOWN_V1;
            goto done;
        }
        if (left->kind == ATOM_VAR || right->kind == ATOM_VAR)
            continue;
        if (left->kind != right->kind) {
            result = PPOSLF_NATIVE_SHAPE_INCOMPATIBLE_V1;
            goto done;
        }
        switch (left->kind) {
        case ATOM_SYMBOL:
            if (left->sym_id != right->sym_id)
                result = PPOSLF_NATIVE_SHAPE_INCOMPATIBLE_V1;
            break;
        case ATOM_GROUNDED:
            if (!atom_eq((Atom *)left, (Atom *)right))
                result = PPOSLF_NATIVE_SHAPE_INCOMPATIBLE_V1;
            break;
        case ATOM_EXPR:
            if (left->expr.len != right->expr.len) {
                result = PPOSLF_NATIVE_SHAPE_INCOMPATIBLE_V1;
                break;
            }
            for (CettaExprIndex index = left->expr.len;
                 index > 0u; index--) {
                CettaExprIndex child = index - 1u;
                if (!pposlf_native_shape_worklist_v1_push(
                        &worklist, left->expr.elems[child],
                        right->expr.elems[child])) {
                    result = PPOSLF_NATIVE_SHAPE_UNKNOWN_V1;
                    break;
                }
            }
            break;
        case ATOM_VAR:
            break;
        }
        if (result != PPOSLF_NATIVE_SHAPE_COMPATIBLE_V1)
            goto done;
    }

done:
    pposlf_native_shape_worklist_v1_free(&worklist);
    return result;
}

typedef struct {
    const Bindings *bindings;
    uint32_t epoch;
    uint32_t first_entry;
} PPOSLFNativeDenseActivationResolverV1;

static CettaGsltGroundDenseStatusV1
pposlf_native_type_vm_v1_resolve_dense_activation_variable(
        void *context, Atom *source_variable, Atom **target_out) {
    const PPOSLFNativeDenseActivationResolverV1 *resolver = context;

    if (!resolver || !source_variable || !target_out)
        return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
    if (!bindings_resolve_epoch_view_ground(
            resolver->bindings, source_variable, resolver->epoch,
            resolver->first_entry, target_out))
        return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
    return *target_out
        ? CETTA_GSLT_GROUND_DENSE_OK_V1
        : CETTA_GSLT_GROUND_DENSE_DEFER_V1;
}

static void pposlf_native_type_vm_v1_add_ground_dense_stats(
    PPOSLFNativeSearchV1 *search,
    const CettaGsltGroundDenseStatsV1 *stats);

/*
 * A continuation is tail-deterministic when no later generated rule can
 * unify with the current goal and no later extensional row is available.
 * The first case is read directly from the compiled candidate cursors.  The
 * second is refined only by the generic rigid-shape theorem above: UNKNOWN
 * and COMPATIBLE candidates remain alternatives.  Thus a new guest obtains
 * this classification solely from its generated plan and term shapes.
 */
static bool pposlf_native_type_vm_v1_generated_tail_deterministic(
    PPOSLFNativeSearchV1 *search,
    const PPOSLFNativeSearchFrameV1 *frame,
    bool *raw_tail_out,
    uint32_t *rejected_candidate_count_out) {
    const PPOSLFNativeTypeVMImplV1 *vm;
    const PPOSLFNativeTypePlanV1 *plan;
    uint32_t exact_offset;
    uint32_t exact_source_offset;
    uint32_t variable_offset;
    uint32_t full_offset;
    uint32_t candidate;
    bool saw_candidate = false;

    if (raw_tail_out)
        *raw_tail_out = false;
    if (rejected_candidate_count_out)
        *rejected_candidate_count_out = 0u;
    if (!search || !(vm = search->vm) || !(plan = vm->plan) || !frame ||
        frame->external_offset < frame->external_end)
        return false;
    exact_offset = frame->exact_offset;
    exact_source_offset = frame->exact_source_offset;
    variable_offset = frame->variable_offset;
    full_offset = frame->full_offset;
    for (;;) {
        PPOSLFNativeShapeCompatibilityV1 compatibility;
        uint32_t rigid_rejections = 0u;
        bool has_candidate = pposlf_native_type_vm_v1_next_candidate(
            plan, frame->full_scan,
            frame->exact_group, frame->has_exact,
            frame->variable_group, frame->has_variable,
            frame->rigid_positions, frame->rigid_position_count,
            frame->rigid_dispatched,
            &exact_offset, &exact_source_offset,
            &variable_offset, &full_offset,
            &rigid_rejections, &candidate);

        if (rigid_rejections > 0u) {
            saw_candidate = true;
            if (rejected_candidate_count_out) {
                uint32_t available = UINT32_MAX -
                    *rejected_candidate_count_out;
                *rejected_candidate_count_out +=
                    rigid_rejections < available
                        ? rigid_rejections : available;
            }
        }
        if (!has_candidate)
            break;

        saw_candidate = true;
        if (candidate >= vm->rule_len)
            return false;
        if (frame->goal_is_activation_view &&
            vm->rules[candidate].ground_dense_ready) {
            PPOSLFNativeDenseActivationResolverV1 resolver = {
                .bindings = bindings_builder_bindings(&search->bindings),
                .epoch = frame->goals->epoch,
                .first_entry = frame->goals->activation_first_entry,
            };
            CettaGsltGroundDenseStatsV1 stats = {0};
            CettaGsltGroundDenseStatusV1 status =
                cetta_gslt_ground_dense_term_match_view_v1(
                    &search->ground_dense_workspace,
                    &vm->rules[candidate].ground_dense_head,
                    frame->goals->goal,
                    pposlf_native_type_vm_v1_resolve_dense_activation_variable,
                    &resolver, &stats);

            pposlf_native_type_vm_v1_add_ground_dense_stats(search, &stats);
            cetta_gslt_ground_dense_workspace_discard_match_v1(
                &search->ground_dense_workspace);
            if (status == CETTA_GSLT_GROUND_DENSE_MISMATCH_V1) {
                if (rejected_candidate_count_out)
                    (*rejected_candidate_count_out)++;
                continue;
            }
            return false;
        }
        compatibility =
            pposlf_native_type_vm_v1_structural_shape_compatibility(
                vm->rules[candidate].head, frame->goal);
        if (compatibility != PPOSLF_NATIVE_SHAPE_INCOMPATIBLE_V1)
            return false;
        if (rejected_candidate_count_out)
            (*rejected_candidate_count_out)++;
    }
    if (raw_tail_out)
        *raw_tail_out = !saw_candidate;
    return true;
}

static void pposlf_native_type_vm_v1_add_ground_dense_stats(
    PPOSLFNativeSearchV1 *search,
    const CettaGsltGroundDenseStatsV1 *stats) {
    if (!search || !stats)
        return;
    search->stats.ground_dense_match_nodes += stats->match_nodes;
    search->stats.ground_dense_rigid_subtrees_compared +=
        stats->rigid_subtrees_compared;
    search->stats.ground_dense_slot_writes += stats->slot_writes;
    search->stats.ground_dense_slot_compares += stats->slot_compares;
    search->stats.ground_dense_expression_materializations +=
        stats->expression_materializations;
    search->stats.ground_dense_rigid_subtrees_reused +=
        stats->rigid_subtrees_reused;
    search->stats.ground_dense_workspace_growths +=
        stats->workspace_growths;
    search->stats.ground_dense_view_nodes += stats->view_nodes;
    search->stats.ground_dense_view_variable_resolutions +=
        stats->view_variable_resolutions;
    search->stats.ground_dense_view_deferrals +=
        stats->view_deferrals;
}

typedef enum {
    PPOSLF_NATIVE_COMPILED_RELATION_DEFER_V1,
    PPOSLF_NATIVE_COMPILED_RELATION_MATCH_V1,
    PPOSLF_NATIVE_COMPILED_RELATION_RESOURCE_V1,
} PPOSLFNativeCompiledRelationResultV1;

static PPOSLFNativeCompiledRelationResultV1
pposlf_native_type_vm_v1_try_compiled_relation(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativeSearchFrameV1 *frame,
    const CettaGsltPeanoAddPlanV1 *plan,
    uint32_t plan_index) {
    uint64_t available;
    uint32_t maximum_successors;
    uint32_t successor_count = 0u;
    uint64_t logical_rule_attempts;
    uint64_t logical_rule_matches;
    uint64_t logical_maximum_goal_depth;
    Atom *result = NULL;
    size_t binding_mark;
    uint32_t proof_mark;
    ArenaMark arena_mark_value;
    CettaGsltPeanoAddResultV1 evaluated;

    if (!search || !frame || !plan || !plan->admitted ||
        frame->goal_is_activation_view || !frame->goal ||
        frame->goal->kind != ATOM_EXPR || !frame->goal->expr.elems ||
        frame->goal->expr.len != 4u)
        return PPOSLF_NATIVE_COMPILED_RELATION_DEFER_V1;
    search->stats.compiled_relation_dispatches++;
    available = search->stats.rule_attempts <
            search->limits.maximum_rule_attempts
        ? search->limits.maximum_rule_attempts -
              search->stats.rule_attempts
        : 0u;
    if (!cetta_gslt_peano_add_successor_budget_v1(
            plan, available, &maximum_successors)) {
        search->stats.compiled_relation_deferrals++;
        return PPOSLF_NATIVE_COMPILED_RELATION_DEFER_V1;
    }
    binding_mark = bindings_builder_save(&search->bindings);
    proof_mark = search->proof_event_len;
    arena_mark_value = arena_mark(&search->scratch);
    evaluated = cetta_gslt_peano_add_evaluate_v1(
        &search->scratch, plan,
        frame->goal->expr.elems[1], frame->goal->expr.elems[2],
        maximum_successors, &successor_count, &result);
    if (evaluated != CETTA_GSLT_PEANO_ADD_OK_V1) {
        bindings_builder_rollback(&search->bindings, binding_mark);
        arena_reset(&search->scratch, arena_mark_value);
        search->proof_event_len = proof_mark;
        search->stats.compiled_relation_deferrals++;
        return PPOSLF_NATIVE_COMPILED_RELATION_DEFER_V1;
    }
    logical_maximum_goal_depth =
        (uint64_t)frame->goals->depth + successor_count;
    if (logical_maximum_goal_depth >
            search->limits.maximum_goal_depth) {
        bindings_builder_rollback(&search->bindings, binding_mark);
        arena_reset(&search->scratch, arena_mark_value);
        search->proof_event_len = proof_mark;
        search->stats.compiled_relation_deferrals++;
        return PPOSLF_NATIVE_COMPILED_RELATION_DEFER_V1;
    }
    cetta_gslt_peano_add_source_cost_v1(
        plan, successor_count,
        &logical_rule_attempts, &logical_rule_matches);
    if (logical_rule_attempts > available || !result ||
        !match_atoms_builder(
            frame->goal->expr.elems[3], result, &search->bindings) ||
        bindings_has_loop(
            bindings_builder_bindings(&search->bindings))) {
        bindings_builder_rollback(&search->bindings, binding_mark);
        arena_reset(&search->scratch, arena_mark_value);
        search->proof_event_len = proof_mark;
        search->stats.compiled_relation_deferrals++;
        return PPOSLF_NATIVE_COMPILED_RELATION_DEFER_V1;
    }
    if (!pposlf_native_type_vm_v1_push_proof_event(
            search, PPOSLF_NATIVE_VM_PROOF_COMPILED_RELATION_V1,
            plan_index)) {
        bindings_builder_rollback(&search->bindings, binding_mark);
        arena_reset(&search->scratch, arena_mark_value);
        search->proof_event_len = proof_mark;
        return PPOSLF_NATIVE_COMPILED_RELATION_RESOURCE_V1;
    }
    search->stats.rule_attempts += logical_rule_attempts;
    search->stats.rule_matches += logical_rule_matches;
    search->stats.indexed_candidate_visits += logical_rule_attempts;
    search->stats.goals_entered = pposlf_native_stats_add_sat_v1(
        search->stats.goals_entered, successor_count);
    if (search->stats.maximum_goal_depth <
            logical_maximum_goal_depth)
        search->stats.maximum_goal_depth =
            (uint32_t)logical_maximum_goal_depth;
    search->stats.compiled_relation_matches++;
    return PPOSLF_NATIVE_COMPILED_RELATION_MATCH_V1;
}

static PPOSLFNativeSearchOutcomeV1 pposlf_native_type_vm_v1_search(
    PPOSLFNativeSearchV1 *search,
    PPOSLFNativePendingGoalV1 *goals) {
    const PPOSLFNativeTypePlanV1 *plan = search->vm->plan;
    PPOSLFNativeSearchFrameV1 *frames = NULL;
    uint32_t frame_len = 0u;
    uint32_t frame_cap = 0u;
    PPOSLFNativeSearchOutcomeV1 outcome =
        PPOSLF_NATIVE_SEARCH_NO_PROOF_V1;
    bool returning = false;

    if (!goals)
        return PPOSLF_NATIVE_SEARCH_PROVED_V1;
    if (!pposlf_native_type_vm_v1_push_search_frame(
            &frames, &frame_len, &frame_cap, goals,
            search->failure_debt_len))
        return PPOSLF_NATIVE_SEARCH_RESOURCE_V1;
    search->stats.maximum_search_frame_depth = frame_len;

    while (frame_len > 0u) {
        PPOSLFNativeSearchFrameV1 *frame = &frames[frame_len - 1u];

        if (returning) {
            if (outcome == PPOSLF_NATIVE_SEARCH_PROVED_V1)
                break;
            if (!frame->waiting_child) {
                outcome = PPOSLF_NATIVE_SEARCH_RESOURCE_V1;
                break;
            }
            if (outcome == PPOSLF_NATIVE_SEARCH_RESOURCE_V1)
                frame->saw_resource = true;
            pposlf_native_type_vm_v1_rollback_search_branch(
                search, frame);
            returning = false;
            continue;
        }

        if (!frame->initialized) {
            PPOSLFNativeGoalDispatchV1 dispatch;
            size_t materialize_before;
            size_t materialize_after;

            search->stats.goals_entered++;
            if (frame->goals->depth >
                search->stats.maximum_goal_depth)
                search->stats.maximum_goal_depth =
                    frame->goals->depth;
            if (frame->goals->depth >
                search->limits.maximum_goal_depth) {
                outcome = PPOSLF_NATIVE_SEARCH_RESOURCE_V1;
                goto finish_frame;
            }
            if (!frame->entry_ready) {
                frame->entry_arena_mark = arena_mark(&search->scratch);
                frame->entry_binding_mark =
                    bindings_builder_save(&search->bindings);
                frame->entry_proof_mark = search->proof_event_len;
                frame->entry_ready = true;
            }
            materialize_before =
                arena_accounted_live_bytes(&search->scratch);
            if (frame->goals->epoch_original &&
                frame->goals->activation_view_admitted &&
                frame->goals->activation_first_entry <=
                    bindings_builder_bindings(
                        &search->bindings)->len) {
                frame->goal = frame->goals->goal;
                frame->goal_is_activation_view = true;
                search->stats.activation_view_goal_admissions++;
            } else if (frame->goals->epoch_original) {
                search->stats.deferred_epoch_goal_materializations++;
                if (frame->goals->activation_view_admitted) {
                    search->stats.epoch_goal_materializations_stale++;
                } else {
                    search->stats
                        .epoch_goal_materializations_not_admitted++;
                    if (frame->goals->activation_view_refusal_reason ==
                        PPOSLF_NATIVE_ACTIVATION_VIEW_OPEN_PRODUCER_V1) {
                        search->stats
                            .epoch_goal_materializations_not_range_restricted++;
                    } else if (frame->goals->activation_view_refusal_reason ==
                        PPOSLF_NATIVE_ACTIVATION_VIEW_UNSAFE_CONSUMER_V1) {
                        search->stats
                            .epoch_goal_materializations_consumer_unsafe++;
                    }
                }
                frame->goal = bindings_apply_epoch_then_all(
                    (Bindings *)bindings_builder_bindings(
                        &search->bindings),
                    &search->scratch, frame->goals->goal,
                    frame->goals->epoch,
                    frame->goals->activation_first_entry);
            } else {
                const Bindings *bindings =
                    bindings_builder_bindings(&search->bindings);

                if (bindings && bindings->len > 0u &&
                    frame->goals->goal &&
                    atom_has_vars(frame->goals->goal)) {
                    search->stats
                        .non_epoch_goal_materialization_attempts++;
                }
                frame->goal = bindings_apply_if_vars(
                    bindings,
                    &search->scratch, frame->goals->goal);
            }
            materialize_after =
                arena_accounted_live_bytes(&search->scratch);
            if (materialize_after > materialize_before) {
                search->stats.goal_materialization_arena_bytes +=
                    materialize_after - materialize_before;
            }
            if (!frame->goal ||
                !pposlf_native_type_vm_v1_goal_dispatch(
                    search->vm, frame->goal, &dispatch)) {
                outcome = PPOSLF_NATIVE_SEARCH_NO_PROOF_V1;
                goto finish_frame;
            }
            if (dispatch.used_compiled_application)
                search->stats.compiled_application_dispatches++;
            frame->full_scan = dispatch.full_scan;
            frame->has_exact = dispatch.has_exact;
            frame->exact_group = dispatch.exact_group;
            frame->has_variable = dispatch.has_variable;
            frame->variable_group = dispatch.variable_group;
            pposlf_native_type_vm_v1_prepare_rigid_dispatch(
                search, frame, dispatch.rigid_index);
            if (search->capabilities && dispatch.has_external &&
                dispatch.external_relation <
                    search->capabilities->external_relation_len) {
                uint32_t external_relation = dispatch.external_relation;

                frame->external_relation = dispatch.external_relation;
                frame->external_offset =
                    search->capabilities->relation_offsets[
                        external_relation];
                frame->external_end =
                    search->capabilities->relation_offsets[
                        external_relation + 1u];
                pposlf_native_type_vm_v1_refine_external_range(
                    search, frame);
            }
            frame->candidate_arena_mark =
                arena_mark(&search->scratch);
            frame->stage = PPOSLF_NATIVE_SEARCH_GENERATED_V1;
            frame->initialized = true;
            if (dispatch.compiled_relation) {
                PPOSLFNativeCompiledRelationResultV1 compiled_result;

                frame->candidate_binding_mark =
                    bindings_builder_save(&search->bindings);
                frame->proof_mark = search->proof_event_len;
                compiled_result =
                    pposlf_native_type_vm_v1_try_compiled_relation(
                        search, frame, dispatch.compiled_relation,
                        dispatch.compiled_relation_index);
                if (compiled_result ==
                    PPOSLF_NATIVE_COMPILED_RELATION_MATCH_V1) {
                    if (!frame->goals->next) {
                        outcome = PPOSLF_NATIVE_SEARCH_PROVED_V1;
                        break;
                    }
                    frame->waiting_child = true;
                    if (!pposlf_native_type_vm_v1_push_search_frame(
                            &frames, &frame_len, &frame_cap,
                            frame->goals->next,
                            search->failure_debt_len)) {
                        frame = &frames[frame_len - 1u];
                        pposlf_native_type_vm_v1_rollback_search_branch(
                            search, frame);
                        frame->saw_resource = true;
                    }
                    if (frame_len >
                        search->stats.maximum_search_frame_depth)
                        search->stats.maximum_search_frame_depth = frame_len;
                    continue;
                }
                if (compiled_result ==
                    PPOSLF_NATIVE_COMPILED_RELATION_RESOURCE_V1)
                    frame->saw_resource = true;
            }
        }

        if (frame->stage == PPOSLF_NATIVE_SEARCH_GENERATED_V1) {
            const PPOSLFNativeCompiledRuleV1 *rule;
            PPOSLFNativePendingGoalV1 *expanded;
            uint32_t candidate;
            uint32_t epoch = 0u;
            uint32_t activation_first_entry = 0u;
            bool goal_ground = false;
            bool ground_pattern = false;
            bool expansion_failed = false;
            bool tail_deterministic = false;
            uint32_t rejected_tail_candidates = 0u;
            uint32_t rigid_rejections = 0u;
            CettaGsltGroundDenseStatusV1 ground_dense_status =
                CETTA_GSLT_GROUND_DENSE_INVALID_V1;
            CettaGsltGroundDenseStatsV1 ground_dense_stats = {0};
            PPOSLFNativeShapeCompatibilityV1 shape_compatibility;
            size_t match_before;
            size_t match_after;
            size_t expansion_before;
            size_t expansion_after;

            bool has_candidate = pposlf_native_type_vm_v1_next_candidate(
                    plan, frame->full_scan,
                    frame->exact_group, frame->has_exact,
                    frame->variable_group, frame->has_variable,
                    frame->rigid_positions,
                    frame->rigid_position_count,
                    frame->rigid_dispatched,
                    &frame->exact_offset, &frame->exact_source_offset,
                    &frame->variable_offset, &frame->full_offset,
                    &rigid_rejections, &candidate);
            if (!pposlf_native_type_vm_v1_charge_rigid_rejections(
                    search, frame, rigid_rejections)) {
                frame->stage = PPOSLF_NATIVE_SEARCH_EXTERNAL_V1;
                continue;
            }
            if (!has_candidate) {
                frame->stage = PPOSLF_NATIVE_SEARCH_EXTERNAL_V1;
                continue;
            }
            if (search->stats.rule_attempts >=
                search->limits.maximum_rule_attempts ||
                candidate >= search->vm->rule_len) {
                frame->saw_resource = true;
                frame->stage = PPOSLF_NATIVE_SEARCH_EXTERNAL_V1;
                continue;
            }
            if (frame->full_scan)
                search->stats.full_scan_candidate_visits++;
            else
                search->stats.indexed_candidate_visits++;
            search->stats.rule_attempts++;
            rule = &search->vm->rules[candidate];
            search->stats.structural_shape_guard_attempts++;
            shape_compatibility =
                pposlf_native_type_vm_v1_structural_shape_compatibility(
                    rule->head, frame->goal);
            if (shape_compatibility ==
                PPOSLF_NATIVE_SHAPE_INCOMPATIBLE_V1) {
                search->stats.structural_shape_guard_rejections++;
                continue;
            }
            if (shape_compatibility == PPOSLF_NATIVE_SHAPE_UNKNOWN_V1)
                search->stats.structural_shape_guard_unknowns++;
            frame->candidate_binding_mark =
                bindings_builder_save(&search->bindings);
            activation_first_entry =
                bindings_builder_bindings(&search->bindings)->len;
            frame->proof_mark = search->proof_event_len;
            match_before = arena_accounted_live_bytes(&search->scratch);
            goal_ground = !frame->goal_is_activation_view &&
                !atom_has_vars(frame->goal);
            if (frame->goal_is_activation_view)
                search->stats.activation_view_rule_attempts++;
            if (rule->ground_dense_ready &&
                (goal_ground || frame->goal_is_activation_view)) {
                search->stats.ground_pattern_rule_attempts++;
                if (frame->goal_is_activation_view) {
                    PPOSLFNativeDenseActivationResolverV1 resolver = {
                        .bindings = bindings_builder_bindings(
                            &search->bindings),
                        .epoch = frame->goals->epoch,
                        .first_entry =
                            frame->goals->activation_first_entry,
                    };

                    ground_dense_status =
                        cetta_gslt_ground_dense_term_match_view_v1(
                        &search->ground_dense_workspace,
                        &rule->ground_dense_head, frame->goals->goal,
                        pposlf_native_type_vm_v1_resolve_dense_activation_variable,
                        &resolver, &ground_dense_stats);
                } else {
                    ground_dense_status =
                        cetta_gslt_ground_dense_term_match_v1(
                            &search->ground_dense_workspace,
                            &rule->ground_dense_head, frame->goal,
                            &ground_dense_stats);
                }
                pposlf_native_type_vm_v1_add_ground_dense_stats(
                    search, &ground_dense_stats);
                if (ground_dense_status ==
                    CETTA_GSLT_GROUND_DENSE_MISMATCH_V1) {
                    pposlf_native_type_vm_v1_rollback_search_branch(
                        search, frame);
                    continue;
                }
                if (ground_dense_status ==
                    CETTA_GSLT_GROUND_DENSE_OK_V1) {
                    ground_pattern = true;
                    search->stats.ground_pattern_rule_matches++;
                    if (frame->goal_is_activation_view)
                        search->stats.activation_view_rule_matches++;
                } else if (ground_dense_status !=
                           CETTA_GSLT_GROUND_DENSE_DEFER_V1) {
                    cetta_gslt_ground_dense_workspace_discard_match_v1(
                        &search->ground_dense_workspace);
                    pposlf_native_type_vm_v1_rollback_search_branch(
                        search, frame);
                    frame->saw_resource = true;
                    frame->stage = PPOSLF_NATIVE_SEARCH_EXTERNAL_V1;
                    continue;
                }
            }
            if (!ground_pattern) {
                bool matched = false;
                if (!fresh_var_suffix_try(&epoch)) {
                    pposlf_native_type_vm_v1_rollback_search_branch(
                        search, frame);
                    frame->saw_resource = true;
                    frame->stage = PPOSLF_NATIVE_SEARCH_EXTERNAL_V1;
                    continue;
                }
                if (rule->head_linear && goal_ground) {
                    search->stats.positional_linear_rule_attempts++;
                    matched = match_atoms_epoch_positional_linear_builder(
                        frame->goal, rule->head, &search->bindings,
                        &search->scratch, epoch);
                    if (matched) {
                        search->stats.positional_linear_rule_matches++;
                    } else {
                        search->stats.positional_linear_rule_fallbacks++;
                        bindings_builder_rollback(
                            &search->bindings,
                            frame->candidate_binding_mark);
                        arena_reset(
                            &search->scratch,
                            frame->candidate_arena_mark);
                    }
                }
                if (!matched && frame->goal_is_activation_view) {
                    matched = match_atoms_epoch_view_builder(
                        frame->goals->goal,
                        frame->goals->epoch,
                        frame->goals->activation_first_entry,
                        rule->head, &search->bindings,
                        &search->scratch, epoch);
                    if (matched)
                        search->stats.activation_view_rule_matches++;
                } else if (!matched) {
                    matched = match_atoms_epoch_builder(
                        frame->goal, rule->head, &search->bindings,
                        &search->scratch, epoch);
                }
                if (!matched ||
                    bindings_has_loop(
                        bindings_builder_bindings(&search->bindings))) {
                    pposlf_native_type_vm_v1_rollback_search_branch(
                        search, frame);
                    continue;
                }
            }
            match_after = arena_accounted_live_bytes(&search->scratch);
            if (match_after > match_before) {
                search->stats.generated_match_arena_bytes +=
                    match_after - match_before;
            }
            search->stats.rule_matches++;
            if (!pposlf_native_type_vm_v1_push_proof_event(
                    search,
                    PPOSLF_NATIVE_VM_PROOF_GENERATED_STEP_V1,
                    candidate)) {
                if (ground_pattern)
                    cetta_gslt_ground_dense_workspace_discard_match_v1(
                        &search->ground_dense_workspace);
                pposlf_native_type_vm_v1_rollback_search_branch(
                    search, frame);
                frame->saw_resource = true;
                frame->stage = PPOSLF_NATIVE_SEARCH_EXTERNAL_V1;
                continue;
            }
            if (rule->body_len > 0u &&
                frame->goals->depth == UINT32_MAX) {
                if (ground_pattern)
                    cetta_gslt_ground_dense_workspace_discard_match_v1(
                        &search->ground_dense_workspace);
                pposlf_native_type_vm_v1_rollback_search_branch(
                    search, frame);
                frame->saw_resource = true;
                frame->stage = PPOSLF_NATIVE_SEARCH_EXTERNAL_V1;
                continue;
            }
            expansion_before =
                arena_accounted_live_bytes(&search->scratch);
            expanded = frame->goals->next;
            for (uint32_t body = rule->body_len;
                 body > 0u; body--) {
                size_t node_before =
                    arena_accounted_live_bytes(&search->scratch);
                PPOSLFNativePendingGoalV1 *item = arena_alloc(
                    &search->scratch, sizeof(*item));
                size_t node_after =
                    arena_accounted_live_bytes(&search->scratch);
                if (node_after > node_before) {
                    search->stats.pending_goal_node_arena_bytes +=
                        node_after - node_before;
                }
                Atom *body_goal = rule->body[body - 1u];

                if (ground_pattern && item &&
                    rule->body_ground[body - 1u]) {
                    search->stats.ground_dense_ground_body_reuses++;
                } else if (ground_pattern && item) {
                    memset(&ground_dense_stats, 0,
                           sizeof(ground_dense_stats));
                    ground_dense_status =
                        cetta_gslt_ground_dense_term_instantiate_v1(
                            &search->ground_dense_workspace,
                            &rule->ground_dense_body[body - 1u],
                            &search->scratch, &body_goal,
                            &ground_dense_stats);
                    pposlf_native_type_vm_v1_add_ground_dense_stats(
                        search, &ground_dense_stats);
                    if (ground_dense_status !=
                        CETTA_GSLT_GROUND_DENSE_OK_V1)
                        body_goal = NULL;
                }

                if (!item || !body_goal ||
                    (ground_pattern && atom_has_vars(body_goal))) {
                    expansion_failed = true;
                    break;
                }
                *item = (PPOSLFNativePendingGoalV1){
                    .goal = body_goal,
                    .depth = frame->goals->depth + 1u,
                    .epoch = epoch,
                    .activation_first_entry =
                        ground_pattern ? 0u : activation_first_entry,
                    .epoch_original = !ground_pattern,
                    .activation_view_admitted =
                        !ground_pattern &&
                        rule->body_activation_view_admitted[body - 1u],
                    .activation_view_refusal_reason = ground_pattern ||
                            rule->body_activation_view_admitted[body - 1u]
                        ? PPOSLF_NATIVE_ACTIVATION_VIEW_ADMITTED_V1
                        : !rule->body_variables_in_head
                            ? PPOSLF_NATIVE_ACTIVATION_VIEW_OPEN_PRODUCER_V1
                            : PPOSLF_NATIVE_ACTIVATION_VIEW_UNSAFE_CONSUMER_V1,
                    .next = expanded,
                };
                expanded = item;
            }
            expansion_after =
                arena_accounted_live_bytes(&search->scratch);
            if (expansion_after > expansion_before) {
                search->stats.body_expansion_arena_bytes +=
                    expansion_after - expansion_before;
            }
            if (ground_pattern)
                cetta_gslt_ground_dense_workspace_discard_match_v1(
                    &search->ground_dense_workspace);
            if (expansion_failed) {
                pposlf_native_type_vm_v1_rollback_search_branch(
                    search, frame);
                frame->saw_resource = true;
                frame->stage = PPOSLF_NATIVE_SEARCH_EXTERNAL_V1;
                continue;
            }
            if (!expanded) {
                outcome = PPOSLF_NATIVE_SEARCH_PROVED_V1;
                break;
            }
            search->stats.generated_continuations++;
            {
                bool raw_tail = false;
                bool shape_proven_tail;

                shape_proven_tail =
                    pposlf_native_type_vm_v1_generated_tail_deterministic(
                        search, frame, &raw_tail,
                        &rejected_tail_candidates);
                if (shape_proven_tail)
                    search->stats
                        .generated_tail_deterministic_continuations++;
                if (raw_tail)
                    search->stats
                        .generated_raw_tail_deterministic_continuations++;
                tail_deterministic = shape_proven_tail &&
                    (raw_tail ||
                     pposlf_native_type_vm_v1_push_failure_debt(
                         search, frame, rejected_tail_candidates));
            }
            if (tail_deterministic) {
                search->stats.generated_tail_frame_reuses++;
                pposlf_native_type_vm_v1_reuse_tail_frame(
                    frame, expanded);
                pposlf_native_type_vm_v1_maybe_collect_deterministic_tail(
                    search, frames, frame_len);
                continue;
            }
            frame->waiting_child = true;
            if (!pposlf_native_type_vm_v1_push_search_frame(
                    &frames, &frame_len, &frame_cap, expanded,
                    search->failure_debt_len)) {
                frame = &frames[frame_len - 1u];
                pposlf_native_type_vm_v1_rollback_search_branch(
                    search, frame);
                frame->saw_resource = true;
                continue;
            }
            if (frame_len > search->stats.maximum_search_frame_depth)
                search->stats.maximum_search_frame_depth = frame_len;
            continue;
        }

        if (frame->stage == PPOSLF_NATIVE_SEARCH_EXTERNAL_V1 &&
            frame->goal_is_activation_view &&
            frame->external_offset < frame->external_end &&
            !pposlf_native_type_vm_v1_materialize_activation_view(
                search, frame)) {
            frame->saw_resource = true;
            frame->external_offset = frame->external_end;
        }

        if (frame->external_offset < frame->external_end) {
            const PPOSLFNativeCapabilityRowV1 *row;
            uint32_t row_index;
            bool row_matched;
            bool row_looped;

            row_index = frame->external_offset++;

            if (search->stats.rule_attempts >=
                search->limits.maximum_rule_attempts) {
                frame->saw_resource = true;
                frame->external_offset = frame->external_end;
                continue;
            }
            row = &search->capabilities->rows[row_index];
            if (!row->fact ||
                row->relation != frame->external_relation) {
                frame->saw_resource = true;
                frame->external_offset = frame->external_end;
                continue;
            }
            search->stats.rule_attempts++;
            search->stats.external_row_candidate_visits++;
            frame->candidate_binding_mark =
                bindings_builder_save(&search->bindings);
            frame->proof_mark = search->proof_event_len;
            row_matched = match_atoms_builder(
                frame->goal, row->fact, &search->bindings);
            row_looped = row_matched && bindings_has_loop(
                bindings_builder_bindings(&search->bindings));
            if (!row_matched || row_looped) {
                pposlf_native_type_vm_v1_rollback_search_branch(
                    search, frame);
                continue;
            }
            search->stats.external_row_matches++;
            if (!pposlf_native_type_vm_v1_push_proof_event(
                    search,
                    PPOSLF_NATIVE_VM_PROOF_EXTERNAL_ROW_V1,
                    row_index)) {
                pposlf_native_type_vm_v1_rollback_search_branch(
                    search, frame);
                frame->saw_resource = true;
                frame->external_offset = frame->external_end;
                continue;
            }
            if (!frame->goals->next) {
                outcome = PPOSLF_NATIVE_SEARCH_PROVED_V1;
                break;
            }
            search->stats.external_continuations++;
            if (frame->external_offset == frame->external_end) {
                search->stats.external_tail_deterministic_continuations++;
                search->stats.external_tail_frame_reuses++;
                pposlf_native_type_vm_v1_reuse_tail_frame(
                    frame, frame->goals->next);
                pposlf_native_type_vm_v1_maybe_collect_deterministic_tail(
                    search, frames, frame_len);
                continue;
            }
            frame->waiting_child = true;
            if (!pposlf_native_type_vm_v1_push_search_frame(
                    &frames, &frame_len, &frame_cap,
                    frame->goals->next, search->failure_debt_len)) {
                frame = &frames[frame_len - 1u];
                pposlf_native_type_vm_v1_rollback_search_branch(
                    search, frame);
                frame->saw_resource = true;
            }
            if (frame_len > search->stats.maximum_search_frame_depth)
                search->stats.maximum_search_frame_depth = frame_len;
            continue;
        }

        outcome = frame->saw_resource
            ? PPOSLF_NATIVE_SEARCH_RESOURCE_V1
            : PPOSLF_NATIVE_SEARCH_NO_PROOF_V1;

finish_frame:
        pposlf_native_type_vm_v1_charge_failure_debt(search, frame);
        if (frame->saw_resource)
            outcome = PPOSLF_NATIVE_SEARCH_RESOURCE_V1;
        if (frame->entry_ready) {
            size_t before_arena_bytes =
                arena_accounted_live_bytes(&search->scratch);

            bindings_builder_rollback(
                &search->bindings, frame->entry_binding_mark);
            search->proof_event_len = frame->entry_proof_mark;
            arena_reset(&search->scratch, frame->entry_arena_mark);
            {
                size_t after_arena_bytes =
                    arena_accounted_live_bytes(&search->scratch);

                if (before_arena_bytes > after_arena_bytes) {
                    search->stats.rollback_arena_bytes_reclaimed +=
                        before_arena_bytes - after_arena_bytes;
                }
            }
        }
        if (frame->entry_failure_debt_len <= search->failure_debt_len)
            search->failure_debt_len = frame->entry_failure_debt_len;
        else {
            search->failure_debt_len = 0u;
            outcome = PPOSLF_NATIVE_SEARCH_RESOURCE_V1;
        }
        frame_len--;
        returning = frame_len > 0u;
    }

    free(frames);
    free(search->failure_debts);
    search->failure_debts = NULL;
    search->failure_debt_len = 0u;
    search->failure_debt_cap = 0u;
    return outcome;
}

static bool pposlf_native_type_vm_v1_prove_with_capabilities_mode(
    const PPOSLFNativeTypeVMV1 *vm,
    const PPOSLFNativeCapabilitySetV1 *capabilities,
    Atom *query,
    PPOSLFNativeVMLimitsV1 limits,
    PPOSLFNativeVMResultV1 *result,
    bool require_capability_digest) {
    const PPOSLFNativeTypeVMImplV1 *impl;
    const PPOSLFNativeCapabilitySetImplV1 *capability_impl = NULL;
    PPOSLFNativePendingGoalV1 root;
    PPOSLFNativeSearchV1 search;
    PPOSLFNativeSearchOutcomeV1 outcome;
    PPOSLFNativeGoalDispatchV1 query_dispatch;

    if (!result)
        return false;
    pposlf_native_vm_result_v1_free(result);
    if (!vm || !vm->impl || !query || atom_has_vars(query))
        return false;
    impl = vm->impl;
    if (!impl->plan ||
        strcmp(impl->program_digest, impl->plan->semantic_digest) != 0 ||
        impl->rule_len != impl->plan->step_schema_len)
        return false;
    if (capabilities) {
        capability_impl = capabilities->impl;
        if (!capability_impl ||
            (require_capability_digest && !capability_impl->digest_ready) ||
            strcmp(capability_impl->program_digest,
                   impl->program_digest) != 0 ||
            capability_impl->external_relation_len !=
                impl->plan->external_relation_len)
            return false;
    }
    if (!pposlf_native_type_vm_v1_goal_dispatch(
            impl, query, &query_dispatch))
        return false;

    memset(&search, 0, sizeof(search));
    search.vm = impl;
    search.capabilities = capability_impl;
    search.limits = limits;
    search.deterministic_collect_after =
        PPOSLF_NATIVE_DETERMINISTIC_ARENA_WINDOW_V1;
    search.binding_growth_collect_after =
        PPOSLF_NATIVE_DETERMINISTIC_BINDING_WINDOW_V1;
    arena_init(&search.scratch);
    arena_set_runtime_kind(
        &search.scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_init(&search.survivor);
    arena_set_runtime_kind(
        &search.survivor, CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
    /*
     * The admitted finite-Horn carrier observes terms structurally, while a
     * proof receipt retains only generated-step and extensional-row indices.
     * Search-local substitutions therefore have no observable pointer
     * identity and must not escape through the process-wide hash-cons table.
     */
    arena_set_hashcons(&search.scratch, NULL);
    arena_set_hashcons(&search.survivor, NULL);
    pposlf_native_prefix_memo_v1_init(&search.prefix_memo);
    if (!bindings_builder_init(&search.bindings, NULL)) {
        pposlf_native_prefix_memo_v1_free(&search.prefix_memo);
        arena_free(&search.survivor);
        arena_free(&search.scratch);
        return false;
    }
    cetta_gslt_ground_dense_workspace_init_v1(
        &search.ground_dense_workspace);
    root = (PPOSLFNativePendingGoalV1){
        .goal = query,
        .depth = 0u,
        .activation_first_entry = 0u,
        .next = NULL,
    };
    outcome = pposlf_native_type_vm_v1_search(&search, &root);
    result->stats = search.stats;
    memcpy(result->program_digest, impl->program_digest,
           sizeof(result->program_digest));
    result->capability_digest_ready =
        !capability_impl || capability_impl->digest_ready;
    if (result->capability_digest_ready)
        memcpy(result->capability_digest,
               capability_impl ? capability_impl->capability_digest
                               : impl->empty_capability_digest,
               sizeof(result->capability_digest));
    if (outcome == PPOSLF_NATIVE_SEARCH_PROVED_V1) {
        result->outcome = PPOSLF_NATIVE_VM_PROVED_V1;
        result->proof_events = search.proof_events;
        result->proof_event_len = search.proof_event_len;
        search.proof_events = NULL;
    } else if (outcome == PPOSLF_NATIVE_SEARCH_RESOURCE_V1) {
        result->outcome = PPOSLF_NATIVE_VM_RESOURCE_EXHAUSTED_V1;
    } else {
        result->outcome = PPOSLF_NATIVE_VM_NO_PROOF_V1;
    }
    free(search.proof_events);
    cetta_gslt_ground_dense_workspace_free_v1(
        &search.ground_dense_workspace);
    bindings_builder_free(&search.bindings);
    pposlf_native_prefix_memo_v1_free(&search.prefix_memo);
    arena_free(&search.survivor);
    arena_free(&search.scratch);
    return true;
}

bool pposlf_native_type_vm_v1_prove_with_capabilities(
    const PPOSLFNativeTypeVMV1 *vm,
    const PPOSLFNativeCapabilitySetV1 *capabilities,
    Atom *query,
    PPOSLFNativeVMLimitsV1 limits,
    PPOSLFNativeVMResultV1 *result) {
    return pposlf_native_type_vm_v1_prove_with_capabilities_mode(
        vm, capabilities, query, limits, result, true);
}

bool pposlf_native_type_vm_v1_prove_with_uncommitted_capabilities(
    const PPOSLFNativeTypeVMV1 *vm,
    const PPOSLFNativeCapabilitySetV1 *capabilities,
    Atom *query,
    PPOSLFNativeVMLimitsV1 limits,
    PPOSLFNativeVMResultV1 *result) {
    if (!capabilities)
        return false;
    return pposlf_native_type_vm_v1_prove_with_capabilities_mode(
        vm, capabilities, query, limits, result, false);
}

bool pposlf_native_type_vm_v1_prove(
    const PPOSLFNativeTypeVMV1 *vm,
    Atom *query,
    PPOSLFNativeVMLimitsV1 limits,
    PPOSLFNativeVMResultV1 *result) {
    return pposlf_native_type_vm_v1_prove_with_capabilities(
        vm, NULL, query, limits, result);
}
