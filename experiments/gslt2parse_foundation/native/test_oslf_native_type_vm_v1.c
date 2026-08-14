#include "oslf_native_type_plan_v1.h"
#include "oslf_native_type_vm_v1.h"

#include "atom.h"
#include "match.h"
#include "symbol.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t checks_run;
static uint32_t checks_failed;

static bool expect(bool condition, const char *message) {
    checks_run++;
    if (!condition) {
        checks_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

static bool exercise_stats_accumulation(void) {
    PPOSLFNativeVMStatsV1 aggregate = {
        .rule_attempts = UINT64_MAX - 1u,
        .maximum_goal_depth = 3u,
        .maximum_search_frame_depth = 8u,
    };
    const PPOSLFNativeVMStatsV1 sample = {
        .goals_entered = 2u,
        .rule_attempts = 3u,
        .deferred_epoch_goal_materializations = 77u,
        .activation_view_rule_attempts = 13u,
        .activation_view_fallback_materializations = 17u,
        .epoch_goal_materializations_not_admitted = 29u,
        .epoch_goal_materializations_not_range_restricted = 11u,
        .epoch_goal_materializations_consumer_unsafe = 18u,
        .epoch_goal_materializations_stale = 31u,
        .non_epoch_goal_materialization_attempts = 37u,
        .ground_dense_view_nodes = 19u,
        .ground_dense_ground_body_reuses = 23u,
        .body_expansion_arena_bytes = 5u,
        .pending_goal_node_arena_bytes = 7u,
        .maximum_goal_depth = 11u,
        .maximum_search_frame_depth = 4u,
    };

    pposlf_native_vm_stats_v1_accumulate(&aggregate, &sample);
    return expect(aggregate.rule_attempts == UINT64_MAX,
                  "OSLF profile aggregation wrapped a saturated counter") &&
           expect(aggregate.goals_entered == 2u &&
                      aggregate.activation_view_rule_attempts == 13u &&
                      aggregate.activation_view_fallback_materializations ==
                          17u &&
                      aggregate.deferred_epoch_goal_materializations == 77u &&
                      aggregate.epoch_goal_materializations_not_admitted ==
                          29u &&
                      aggregate
                          .epoch_goal_materializations_not_range_restricted ==
                          11u &&
                      aggregate.epoch_goal_materializations_consumer_unsafe ==
                          18u &&
                      aggregate.epoch_goal_materializations_stale == 31u &&
                      aggregate.non_epoch_goal_materialization_attempts ==
                          37u &&
                      aggregate.ground_dense_view_nodes == 19u &&
                      aggregate.ground_dense_ground_body_reuses == 23u &&
                      aggregate.body_expansion_arena_bytes == 5u &&
                      aggregate.pending_goal_node_arena_bytes == 7u,
                  "OSLF profile aggregation lost an additive counter") &&
           expect(aggregate.maximum_goal_depth == 11u &&
                      aggregate.maximum_search_frame_depth == 8u,
                  "OSLF profile aggregation did not preserve maxima");
}

static Atom *unary(Arena *arena, const char *head, Atom *argument) {
    Atom *elements[2] = {
        atom_symbol(arena, head),
        argument,
    };

    return atom_expr(arena, elements, 2u);
}

static Atom *binary(
    Arena *arena, const char *head, Atom *left, Atom *right) {
    Atom *elements[3] = {
        atom_symbol(arena, head),
        left,
        right,
    };

    return atom_expr(arena, elements, 3u);
}

static Atom *ternary(
    Arena *arena, const char *head,
    Atom *first, Atom *second, Atom *third) {
    Atom *elements[4] = {
        atom_symbol(arena, head),
        first,
        second,
        third,
    };

    return atom_expr(arena, elements, 4u);
}

static bool exercise_positional_linear_builder_view(void) {
    const uint32_t epoch = 91u;
    Arena arena;
    Bindings base;
    BindingsBuilder builder;
    Atom *x;
    Atom *y;
    Atom *query_var;
    Atom *lhs;
    Atom *ground_query;
    Atom *open_query;
    ArenaMark arena_checkpoint;
    uint32_t binding_checkpoint;
    bool builder_ready = false;
    bool ok = false;

    arena_init(&arena);
    bindings_init(&base);
    x = atom_var_with_id(&arena, "view-x", 81001u);
    y = atom_var_with_id(&arena, "view-y", 81002u);
    query_var = atom_var_with_id(&arena, "view-query", 81003u);
    lhs = binary(&arena, "view-pair", x, y);
    ground_query = binary(
        &arena, "view-pair", atom_int(&arena, 1), atom_int(&arena, 2));
    open_query = binary(
        &arena, "view-pair", unary(&arena, "view-wrap", query_var),
        atom_int(&arena, 2));
    if (!expect(x && y && query_var && lhs && ground_query && open_query,
                "positional-view fixture allocation failed") ||
        !expect(bindings_builder_init(&builder, &base),
                "positional-view builder did not initialize"))
        goto done;
    builder_ready = true;

    binding_checkpoint = bindings_builder_save(&builder);
    arena_checkpoint = arena_mark(&arena);
    ok = expect(match_atoms_epoch_positional_linear_builder(
                    ground_query, lhs, &builder, &arena, epoch),
                "flat linear ground view did not match") &&
         expect(bindings_lookup_id(
                    &builder.current, var_epoch_id(x->var_id, epoch)) ==
                    ground_query->expr.elems[1] &&
                    bindings_lookup_id(
                        &builder.current, var_epoch_id(y->var_id, epoch)) ==
                    ground_query->expr.elems[2],
                "positional view produced the wrong bindings");
    bindings_builder_rollback(&builder, binding_checkpoint);
    arena_reset(&arena, arena_checkpoint);
    ok = expect(builder.current.len == 0u,
                "positional-view rollback retained a binding") && ok;

    binding_checkpoint = bindings_builder_save(&builder);
    arena_checkpoint = arena_mark(&arena);
    ok = expect(!match_atoms_epoch_positional_linear_builder(
                    open_query, lhs, &builder, &arena, epoch),
                "positional view accepted an open query argument") &&
         expect(builder.current.len == 0u,
                "positional-view refusal changed the builder") && ok;
    bindings_builder_rollback(&builder, binding_checkpoint);
    arena_reset(&arena, arena_checkpoint);
    ok = expect(match_atoms_epoch_builder(
                    open_query, lhs, &builder, &arena, epoch),
                "general matcher did not cover a refused positional view") &&
         ok;

done:
    if (builder_ready)
        bindings_builder_free(&builder);
    bindings_free(&base);
    arena_free(&arena);
    return ok;
}

static bool result_contains_step(
    const PPOSLFNativeVMResultV1 *result, uint32_t step) {
    if (!result)
        return false;
    for (uint32_t index = 0u; index < result->proof_event_len; index++) {
        if (result->proof_events[index].kind ==
                PPOSLF_NATIVE_VM_PROOF_GENERATED_STEP_V1 &&
            result->proof_events[index].index == step)
            return true;
    }
    return false;
}

static bool result_contains_event_kind(
    const PPOSLFNativeVMResultV1 *result,
    PPOSLFNativeVMProofEventKindV1 kind) {
    if (!result)
        return false;
    for (uint32_t index = 0u; index < result->proof_event_len; index++) {
        if (result->proof_events[index].kind == kind)
            return true;
    }
    return false;
}

static bool expect_materialization_partition(
    const PPOSLFNativeVMStatsV1 *stats) {
    return expect(
        stats && stats->deferred_epoch_goal_materializations ==
            stats->epoch_goal_materializations_not_admitted +
            stats->epoch_goal_materializations_stale +
            stats->activation_view_fallback_materializations &&
            stats->epoch_goal_materializations_not_admitted ==
                stats->epoch_goal_materializations_not_range_restricted +
                stats->epoch_goal_materializations_consumer_unsafe,
        "deferred epoch materializations escaped their cause partition");
}

static bool prepare_program(
    const char *path,
    uint32_t expected_steps,
    PPOSLFNativeTypePlanV1 *plan,
    PPOSLFNativeTypeVMV1 *vm) {
    char error[512] = {0};
    bool loaded;

    pposlf_native_type_plan_v1_init(plan);
    pposlf_native_type_vm_v1_init(vm);
    loaded = pposlf_native_type_plan_v1_load(
        plan, path, error, sizeof(error));
    return expect(loaded,
                  error[0] ? error : "native NTT plan did not load") &&
           expect(plan->step_schema_len == expected_steps,
                  "native NTT step count changed") &&
           expect(pposlf_native_type_vm_v1_prepare(
                      vm, plan, error, sizeof(error)),
                  error[0] ? error : "native NTT VM did not prepare");
}

static bool prepare_large_program(const char *path, uint32_t expected_steps) {
    PPOSLFNativeTypePlanV1 plan;
    PPOSLFNativeTypeVMV1 vm;
    bool ok;

    ok = prepare_program(path, expected_steps, &plan, &vm);
    pposlf_native_type_vm_v1_free(&vm);
    pposlf_native_type_plan_v1_free(&plan);
    return ok;
}

static bool prove_expect(
    const PPOSLFNativeTypeVMV1 *vm,
    Atom *query,
    PPOSLFNativeVMLimitsV1 limits,
    PPOSLFNativeVMOutcomeV1 expected,
    PPOSLFNativeVMResultV1 *result,
    const char *message) {
    bool executed = pposlf_native_type_vm_v1_prove(
        vm, query, limits, result);

    if (executed && result->outcome != expected) {
        fprintf(stderr,
                "proof outcome=%u expected=%u goals=%llu attempts=%llu "
                "matches=%llu view-goals=%llu "
                "view-attempts=%llu view-matches=%llu "
                "materializations=%llu unsafe=%llu stale=%llu "
                "tail-reuses=%llu tail-proven=%llu tail-raw=%llu\n",
                (unsigned)result->outcome, (unsigned)expected,
                (unsigned long long)result->stats.goals_entered,
                (unsigned long long)result->stats.rule_attempts,
                (unsigned long long)result->stats.rule_matches,
                (unsigned long long)
                    result->stats.activation_view_goal_admissions,
                (unsigned long long)
                    result->stats.activation_view_rule_attempts,
                (unsigned long long)
                    result->stats.activation_view_rule_matches,
                (unsigned long long)
                    result->stats.deferred_epoch_goal_materializations,
                (unsigned long long)
                    result->stats.epoch_goal_materializations_consumer_unsafe,
                (unsigned long long)
                    result->stats.epoch_goal_materializations_stale,
                (unsigned long long)
                    result->stats.generated_tail_frame_reuses,
                (unsigned long long)
                    result->stats.generated_tail_deterministic_continuations,
                (unsigned long long)result->stats
                    .generated_raw_tail_deterministic_continuations);
    }
    return expect(executed, "native NTT proof request was invalid") &&
           expect(result->outcome == expected, message) &&
           expect(result->capability_digest_ready,
                  "closed native NTT proof omitted its input commitment") &&
           expect((expected == PPOSLF_NATIVE_VM_PROVED_V1) ==
                      (result->proof_event_len > 0u),
                  "native NTT proof receipt disagrees with its verdict") &&
           expect_materialization_partition(&result->stats);
}

static bool prove_with_capabilities_expect(
    const PPOSLFNativeTypeVMV1 *vm,
    const PPOSLFNativeCapabilitySetV1 *capabilities,
    Atom *query,
    PPOSLFNativeVMLimitsV1 limits,
    PPOSLFNativeVMOutcomeV1 expected,
    PPOSLFNativeVMResultV1 *result,
    const char *message) {
    bool executed = pposlf_native_type_vm_v1_prove_with_capabilities(
        vm, capabilities, query, limits, result);

    if (executed && result->outcome != expected) {
        fprintf(stderr,
                "capability outcome=%u expected=%u goals=%llu "
                "attempts=%llu matches=%llu external-visits=%llu "
                "external-matches=%llu ground-pattern=%llu/%llu\n",
                (unsigned)result->outcome, (unsigned)expected,
                (unsigned long long)result->stats.goals_entered,
                (unsigned long long)result->stats.rule_attempts,
                (unsigned long long)result->stats.rule_matches,
                (unsigned long long)
                    result->stats.external_row_candidate_visits,
                (unsigned long long)result->stats.external_row_matches,
                (unsigned long long)
                    result->stats.ground_pattern_rule_matches,
                (unsigned long long)
                    result->stats.ground_pattern_rule_attempts);
    }
    return expect(executed, "native NTT capability proof request was invalid") &&
           expect(result->outcome == expected, message) &&
           expect(result->capability_digest_ready,
                  "certified capability proof omitted its input commitment") &&
           expect((expected == PPOSLF_NATIVE_VM_PROVED_V1) ==
                      (result->proof_event_len > 0u),
                  "capability proof receipt disagrees with its verdict") &&
           expect_materialization_partition(&result->stats);
}

static bool exercise_canary(
    const char *full_path, const char *deleted_path) {
    const uint32_t deep_len = 8192u;
    const uint32_t invalid_len = 8u;
    const uint64_t invalid_attempts = invalid_len * 2u + 2u;
    const PPOSLFNativeVMLimitsV1 ample = {
        .maximum_rule_attempts = 1000u,
        .maximum_goal_depth = 32u,
    };
    PPOSLFNativeTypePlanV1 full_plan;
    PPOSLFNativeTypePlanV1 deleted_plan;
    PPOSLFNativeTypeVMV1 full_vm;
    PPOSLFNativeTypeVMV1 deleted_vm;
    PPOSLFNativeVMResultV1 result;
    Arena query_arena;
    Atom *root;
    Atom *leaf;
    Atom *seven;
    Atom *root_to_seven;
    Atom *root_to_leaf;
    Atom *leaf_to_root;
    Atom *view_start;
    Atom *view_choice_start;
    Atom *view_defer_start;
    Atom *ground_body_start;
    Atom *dynamic_body_start;
    Atom *wide;
    Atom *deep_list;
    Atom *deep_query;
    Atom *invalid_list;
    Atom *invalid_query;
    Atom *tail_choice;
    uint32_t transitive_begin = 0u;
    uint32_t transitive_end = 0u;
    uint32_t tail_dead_begin = 0u;
    uint32_t tail_dead_end = 0u;
    uint32_t tail_live_begin = 0u;
    uint32_t tail_live_end = 0u;
    uint32_t retained_head;
    uint32_t retained_index;
    uint32_t cyclic_head;
    uint32_t cyclic_edge;
    uint32_t retained_edge;
    char error[512] = {0};
    bool ok = false;

    if (!prepare_program(full_path, 30u, &full_plan, &full_vm))
        return false;
    if (!prepare_program(deleted_path, 29u, &deleted_plan, &deleted_vm)) {
        pposlf_native_type_vm_v1_free(&full_vm);
        pposlf_native_type_plan_v1_free(&full_plan);
        return false;
    }
    pposlf_native_vm_result_v1_init(&result);
    arena_init(&query_arena);
    root = atom_string(&query_arena, "root-λ");
    leaf = atom_string(&query_arena, "leaf");
    seven = atom_int(&query_arena, 7);
    root_to_seven = binary(
        &query_arena, "canary-path-v1", root, seven);
    root_to_leaf = binary(
        &query_arena, "canary-path-v1", root, leaf);
    leaf_to_root = binary(
        &query_arena, "canary-path-v1", leaf, root);
    view_start = unary(
        &query_arena, "canary-view-start-v1",
        atom_symbol(&query_arena, "go"));
    view_choice_start = unary(
        &query_arena, "canary-view-choice-start-v1",
        atom_symbol(&query_arena, "go"));
    view_defer_start = unary(
        &query_arena, "canary-view-defer-start-v1",
        atom_symbol(&query_arena, "go"));
    ground_body_start = unary(
        &query_arena, "canary-ground-body-start-v1",
        atom_symbol(&query_arena, "go"));
    dynamic_body_start = unary(
        &query_arena, "canary-dynamic-body-start-v1",
        atom_symbol(&query_arena, "witness"));
    wide = unary(
        &query_arena, "canary-wide-v1",
        atom_bigint(
            &query_arena, "123456789012345678901234567890"));
    deep_list = atom_symbol(&query_arena, "canary-list-nil-v1");
    for (uint32_t index = 0u; index < deep_len; index++) {
        deep_list = binary(
            &query_arena, "canary-list-cons-v1",
            atom_int(&query_arena, index), deep_list);
        if (!deep_list)
            goto done;
    }
    deep_query = unary(
        &query_arena, "canary-list-proof-v1", deep_list);
    invalid_list = atom_symbol(&query_arena, "canary-list-invalid-v1");
    for (uint32_t index = 0u; index < invalid_len; index++) {
        invalid_list = binary(
            &query_arena, "canary-list-cons-v1",
            atom_int(&query_arena, index), invalid_list);
        if (!invalid_list)
            goto done;
    }
    invalid_query = unary(
        &query_arena, "canary-list-proof-v1", invalid_list);
    tail_choice = unary(
        &query_arena, "canary-tail-choice-v1",
        atom_symbol(&query_arena, "go"));
    if (!view_start || !view_choice_start || !view_defer_start ||
        !ground_body_start || !dynamic_body_start ||
        !deep_query || !invalid_query || !tail_choice)
        goto done;

    if (!expect(pposlf_native_type_plan_v1_step_range(
                    &full_plan, "OSLFNativeProgramCanaryV1",
                    "canary-path-transitive-v1",
                    &transitive_begin, &transitive_end) &&
                    transitive_end == transitive_begin + 1u,
                "generated transitive rule is absent from the program") ||
        !prove_expect(
            &full_vm, root_to_seven, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "direct generated path rule did not prove"))
        goto done;
    ok = expect(result.proof_event_len == 2u,
                "direct generated proof has the wrong tree size") &&
         expect(result.stats.full_scan_candidate_visits == 0u &&
                    result.stats.indexed_candidate_visits > 0u,
                "closed generated query did not use its head index");
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, ground_body_start, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "structurally ground generated body did not prove"))
        goto done;
    ok = expect(result.stats.ground_dense_ground_body_reuses == 1u &&
                    result.stats.ground_dense_expression_materializations ==
                        0u,
                "ground body did not bypass dynamic instantiation exactly") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, dynamic_body_start, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "variable-bearing generated body did not prove"))
        goto done;
    ok = expect(result.stats.ground_dense_ground_body_reuses == 0u &&
                    result.stats.ground_dense_expression_materializations >
                        0u,
                "variable-bearing body was incorrectly treated as ground") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, view_choice_start, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "non-raw activation-view choice did not prove"))
        goto done;
    if (!(result.stats.activation_view_goal_admissions > 0u &&
          result.stats.activation_view_fallback_materializations == 0u &&
          result.stats.generated_tail_frame_reuses > 0u &&
          result.stats.rigid_coordinate_dispatches > 0u &&
          result.stats.rigid_coordinate_rejections > 0u)) {
        fprintf(stderr,
                "activation-choice stats: admissions=%llu attempts=%llu "
                "matches=%llu materializations=%llu tail-reuses=%llu "
                "rigid-dispatches=%llu rigid-rejections=%llu\n",
                (unsigned long long)result.stats
                    .activation_view_goal_admissions,
                (unsigned long long)result.stats
                    .activation_view_rule_attempts,
                (unsigned long long)result.stats
                    .activation_view_rule_matches,
                (unsigned long long)result.stats
                    .activation_view_fallback_materializations,
                (unsigned long long)result.stats
                    .generated_tail_frame_reuses,
                (unsigned long long)result.stats.rigid_coordinate_dispatches,
                (unsigned long long)result.stats.rigid_coordinate_rejections);
    }
    ok = expect(result.stats.activation_view_goal_admissions > 0u &&
                    result.stats
                        .activation_view_fallback_materializations == 0u &&
                    result.stats.generated_tail_frame_reuses > 0u &&
                    result.stats.rigid_coordinate_dispatches > 0u &&
                    result.stats.rigid_coordinate_rejections > 0u,
                "a non-raw activation view did not traverse alternatives directly") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, root_to_leaf, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "recursive generated path rule did not prove"))
        goto done;
    ok = expect(result_contains_step(&result, transitive_begin),
                "recursive proof receipt omitted its generated rule") &&
         expect(result.proof_event_len == 4u,
                "recursive generated proof has the wrong tree size") &&
         expect(result.stats.rule_matches > result.proof_event_len,
                "conjunction search did not backtrack over a dead prefix") &&
         expect(result.stats.positional_linear_rule_attempts > 0u &&
                    result.stats.positional_linear_rule_matches > 0u,
                "generated head-linearity did not select its positional view") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, view_start, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "range-restricted activation-view chain did not prove"))
        goto done;
    if (!(result.stats.activation_view_goal_admissions > 0u &&
          result.stats.activation_view_rule_attempts > 0u &&
          result.stats.activation_view_rule_matches > 0u &&
          result.stats.ground_dense_view_nodes > 0u &&
          result.stats.ground_dense_view_variable_resolutions > 0u &&
          result.stats.activation_view_fallback_materializations == 0u)) {
        fprintf(stderr,
                "activation-dense stats: admissions=%llu attempts=%llu "
                "matches=%llu view-nodes=%llu resolutions=%llu defers=%llu "
                "materializations=%llu\n",
                (unsigned long long)result.stats
                    .activation_view_goal_admissions,
                (unsigned long long)result.stats
                    .activation_view_rule_attempts,
                (unsigned long long)result.stats
                    .activation_view_rule_matches,
                (unsigned long long)result.stats.ground_dense_view_nodes,
                (unsigned long long)result.stats
                    .ground_dense_view_variable_resolutions,
                (unsigned long long)result.stats.ground_dense_view_deferrals,
                (unsigned long long)result.stats
                    .activation_view_fallback_materializations);
    }
    ok = expect(result.stats.activation_view_goal_admissions > 0u &&
                    result.stats.activation_view_rule_attempts > 0u &&
                    result.stats.activation_view_rule_matches > 0u &&
                    result.stats.ground_dense_view_nodes > 0u &&
                    result.stats
                        .ground_dense_view_variable_resolutions > 0u &&
                    result.stats
                        .activation_view_fallback_materializations == 0u,
                "range-restricted rule did not use its admitted activation view") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, view_defer_start, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "an unresolved dense view did not fall back exactly"))
        goto done;
    ok = expect(result.stats.activation_view_goal_admissions > 0u &&
                    result.stats.activation_view_rule_matches > 0u &&
                    result.stats.ground_dense_view_nodes > 0u &&
                    result.stats.ground_dense_view_deferrals > 0u &&
                    result.stats
                        .activation_view_fallback_materializations == 0u,
                "an unresolved dense view did not use the general view fallback") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, leaf_to_root, ample,
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "negative generated path query unexpectedly proved"))
        goto done;
    ok = expect(result.stats.rule_attempts > 0u,
                "negative generated path query performed no work") && ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, wide, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "generated wide-integer fact did not prove"))
        goto done;
    ok = expect(result.proof_event_len == 1u,
                "generated wide-integer receipt is not a fact") && ok;
    pposlf_native_vm_result_v1_free(&result);

    if (!prove_expect(
            &full_vm, deep_query,
            (PPOSLFNativeVMLimitsV1){
                .maximum_rule_attempts = deep_len * 3u,
                .maximum_goal_depth = deep_len,
            },
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "deep generated tail recursion did not prove"))
        goto done;
    if (result.stats.generated_continuations != deep_len ||
        result.stats.generated_tail_deterministic_continuations != deep_len)
        fprintf(stderr,
                "deep-tail stats: continuations=%llu deterministic=%llu "
                "frames=%u\n",
                (unsigned long long)result.stats.generated_continuations,
                (unsigned long long)result.stats
                    .generated_tail_deterministic_continuations,
                result.stats.maximum_search_frame_depth);
    if (getenv("CETTA_TEST_OSLF_STATS_V1"))
        fprintf(stderr,
                "deep-tail-collection collections=%llu failures=%llu "
                "roots=%llu binding-items-discarded=%llu "
                "trail-discarded=%llu copied=%llu reclaimed=%llu "
                "materialize=%llu match-bytes=%llu expand-bytes=%llu "
                "nodes=%llu rollback-reclaimed=%llu\n",
                (unsigned long long)result.stats
                    .deterministic_tail_collections,
                (unsigned long long)result.stats
                    .deterministic_tail_collection_failures,
                (unsigned long long)result.stats
                    .deterministic_goal_roots_scanned,
                (unsigned long long)result.stats
                    .deterministic_binding_items_discarded,
                (unsigned long long)result.stats
                    .deterministic_trail_entries_discarded,
                (unsigned long long)result.stats
                    .deterministic_arena_bytes_copied,
                (unsigned long long)result.stats
                    .deterministic_arena_bytes_reclaimed,
                (unsigned long long)result.stats
                    .goal_materialization_arena_bytes,
                (unsigned long long)result.stats
                    .generated_match_arena_bytes,
                (unsigned long long)result.stats
                    .body_expansion_arena_bytes,
                (unsigned long long)result.stats
                    .pending_goal_node_arena_bytes,
                (unsigned long long)result.stats
                    .rollback_arena_bytes_reclaimed);
    ok = expect(result.proof_event_len == deep_len + 1u,
                "deep generated proof lost a logical step") &&
         expect(result.stats.maximum_goal_depth == deep_len,
                "deep generated proof reported the wrong depth") &&
         expect(result.stats.generated_continuations == deep_len &&
                    result.stats
                        .generated_tail_deterministic_continuations ==
                        deep_len &&
                    result.stats
                        .generated_raw_tail_deterministic_continuations == 0u &&
                    result.stats.generated_tail_frame_reuses == deep_len &&
                    result.stats.deferred_shape_guard_candidates == deep_len &&
                    result.stats.deferred_shape_guard_attempts == 0u,
                "deep generated tail recursion was not classified locally") &&
         expect(result.stats.maximum_search_frame_depth == 1u,
                "deep generated tail recursion retained search frames") &&
         expect(result.stats.structural_shape_guard_attempts > 0u &&
                    result.stats.structural_shape_guard_rejections > 0u &&
                    result.stats.structural_shape_guard_unknowns == 0u,
                "rigid rule shapes did not drive the structural guard") && ok;
    pposlf_native_vm_result_v1_free(&result);

    if (!prove_expect(
            &full_vm, invalid_query,
            (PPOSLFNativeVMLimitsV1){
                .maximum_rule_attempts = invalid_attempts,
                .maximum_goal_depth = invalid_len,
            },
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "failed deterministic chain changed to a resource outcome"))
        goto done;
    ok = expect(result.stats.rule_attempts == invalid_attempts &&
                    result.stats.deferred_shape_guard_attempts == invalid_len,
                "failed deterministic chain did not charge exact guard debt") &&
         expect(result.stats.generated_tail_frame_reuses == invalid_len &&
                    result.stats.maximum_search_frame_depth == 1u,
                "failed deterministic chain retained redundant frames") && ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, invalid_query,
            (PPOSLFNativeVMLimitsV1){
                .maximum_rule_attempts = invalid_attempts - 1u,
                .maximum_goal_depth = invalid_len,
            },
            PPOSLF_NATIVE_VM_RESOURCE_EXHAUSTED_V1, &result,
            "deferred guard debt crossed the exact fuel boundary"))
        goto done;
    ok = expect(result.stats.rule_attempts == invalid_attempts - 1u &&
                    result.stats.deferred_shape_guard_attempts ==
                        invalid_len - 1u,
                "deferred guard debt reported the wrong exhausted prefix") && ok;
    pposlf_native_vm_result_v1_free(&result);

    if (!expect(pposlf_native_type_plan_v1_step_range(
                    &full_plan, "OSLFNativeProgramCanaryV1",
                    "canary-tail-choice-dead-v1",
                    &tail_dead_begin, &tail_dead_end) &&
                    tail_dead_end == tail_dead_begin + 1u &&
                    pposlf_native_type_plan_v1_step_range(
                        &full_plan, "OSLFNativeProgramCanaryV1",
                        "canary-tail-choice-live-v1",
                        &tail_live_begin, &tail_live_end) &&
                    tail_live_end == tail_live_begin + 1u,
                "generated tail-rollback rules are absent") ||
        !prove_expect(
            &full_vm, tail_choice, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "tail-frame rollback did not reach the later alternative"))
        goto done;
    ok = expect(!result_contains_step(&result, tail_dead_begin) &&
                    result_contains_step(&result, tail_live_begin),
                "failed tail chain escaped into the proof receipt") &&
         expect(result.proof_event_len == 3u,
                "tail-frame rollback produced the wrong proof tree") &&
         expect(result.stats.generated_tail_frame_reuses == 3u &&
                    result.stats.maximum_search_frame_depth == 2u,
                "raw deterministic continuations did not reuse frames") && ok;
    pposlf_native_vm_result_v1_free(&result);

    if (!prove_expect(
            &full_vm, root_to_seven,
            (PPOSLFNativeVMLimitsV1){
                .maximum_rule_attempts = 0u,
                .maximum_goal_depth = 32u,
            },
            PPOSLF_NATIVE_VM_RESOURCE_EXHAUSTED_V1, &result,
            "zero fuel did not refuse all generated rules"))
        goto done;
    ok = expect(result.stats.rule_attempts == 0u,
                "zero fuel executed a generated rule") && ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, root_to_seven,
            (PPOSLFNativeVMLimitsV1){
                .maximum_rule_attempts = 1000u,
                .maximum_goal_depth = 0u,
            },
            PPOSLF_NATIVE_VM_RESOURCE_EXHAUSTED_V1, &result,
            "zero premise depth masqueraded as rejection"))
        goto done;
    ok = expect(result.stats.maximum_goal_depth == 1u,
                "exact depth limit did not expose the refused premise") && ok;
    pposlf_native_vm_result_v1_free(&result);

    if (!prove_expect(
            &deleted_vm, root_to_leaf, ample,
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "deleting the recursive authored rule left its verdict inert"))
        goto done;
    ok = expect(strcmp(full_plan.semantic_digest,
                       deleted_plan.semantic_digest) != 0,
                "authored rule deletion retained the program digest") && ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &deleted_vm, root_to_seven, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "rule deletion damaged an independent generated proof"))
        goto done;

    /* A rejected replacement must not destroy the admitted VM. */
    retained_head = full_plan.step_schemas[0].head;
    full_plan.step_schemas[0].head = full_plan.term_len;
    error[0] = '\0';
    ok = expect(!pposlf_native_type_vm_v1_prepare(
                    &full_vm, &full_plan, error, sizeof(error)) &&
                    error[0] != '\0',
                "native VM accepted an invalid replacement") && ok;
    full_plan.step_schemas[0].head = retained_head;
    retained_index = full_plan.head_step_indices[0];
    full_plan.head_step_indices[0] = full_plan.head_step_indices[1];
    error[0] = '\0';
    ok = expect(!pposlf_native_type_vm_v1_prepare(
                    &full_vm, &full_plan, error, sizeof(error)) &&
                    error[0] != '\0',
                "native VM accepted a duplicated generated index") && ok;
    full_plan.head_step_indices[0] = retained_index;
    cyclic_head = full_plan.step_schemas[0].head;
    if (!expect(cyclic_head < full_plan.term_len &&
                    full_plan.terms[cyclic_head].kind ==
                        PPOSLF_NATIVE_TERM_APPLICATION_V1 &&
                    full_plan.terms[cyclic_head].edge_len > 0u,
                "native VM canary has no reachable application edge"))
        goto done;
    cyclic_edge = full_plan.terms[cyclic_head].edge_begin;
    retained_edge = full_plan.term_edges[cyclic_edge];
    full_plan.term_edges[cyclic_edge] = cyclic_head;
    error[0] = '\0';
    ok = expect(!pposlf_native_type_vm_v1_prepare(
                    &full_vm, &full_plan, error, sizeof(error)) &&
                    error[0] != '\0',
                "native VM accepted a cyclic compiled term") && ok;
    full_plan.term_edges[cyclic_edge] = retained_edge;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &full_vm, root_to_leaf, ample,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "rejected VM replacement damaged the admitted program"))
        goto done;
    ok = true && ok;

done:
    pposlf_native_vm_result_v1_free(&result);
    arena_free(&query_arena);
    pposlf_native_type_vm_v1_free(&deleted_vm);
    pposlf_native_type_plan_v1_free(&deleted_plan);
    pposlf_native_type_vm_v1_free(&full_vm);
    pposlf_native_type_plan_v1_free(&full_plan);
    return ok;
}

static bool exercise_search_hashcons_isolation(const char *program_path) {
    const PPOSLFNativeVMLimitsV1 limits = {
        .maximum_rule_attempts = 1000u,
        .maximum_goal_depth = 32u,
    };
    PPOSLFNativeTypePlanV1 plan;
    PPOSLFNativeTypeVMV1 vm;
    PPOSLFNativeVMResultV1 result;
    HashConsTable hashcons;
    HashConsTable *prior_hashcons = g_hashcons;
    Arena query_arena;
    Arena control_arena;
    Atom *query;
    uint32_t before_control;
    uint32_t before_proof;
    bool hashcons_ready = false;
    bool query_arena_ready = false;
    bool control_arena_ready = false;
    bool ok = false;

    if (!prepare_program(program_path, 30u, &plan, &vm))
        return false;
    pposlf_native_vm_result_v1_init(&result);
    arena_init(&query_arena);
    query_arena_ready = true;
    query = binary(
        &query_arena, "canary-path-v1",
        atom_string(&query_arena, "root-λ"),
        atom_string(&query_arena, "leaf"));
    if (!query)
        goto done;

    hashcons_init(&hashcons);
    hashcons_ready = true;
    g_hashcons = &hashcons;
    arena_init(&control_arena);
    control_arena_ready = true;
    before_control = hashcons.used;
    if (!binary(
            &control_arena, "hashcons-isolation-control-v1",
            atom_symbol(&control_arena, "hashcons-control-left-v1"),
            atom_symbol(&control_arena, "hashcons-control-right-v1")))
        goto done;
    before_proof = hashcons.used;
    if (!expect(before_proof > before_control,
                "hash-cons isolation control did not exercise interning") ||
        !prove_expect(
            &vm, query, limits, PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "hash-cons isolation query did not prove"))
        goto done;
    ok = expect(hashcons.used == before_proof,
                "search-local terms escaped into global hash-cons storage");

done:
    g_hashcons = prior_hashcons;
    pposlf_native_vm_result_v1_free(&result);
    if (control_arena_ready)
        arena_free(&control_arena);
    if (hashcons_ready)
        hashcons_free(&hashcons);
    if (query_arena_ready)
        arena_free(&query_arena);
    pposlf_native_type_vm_v1_free(&vm);
    pposlf_native_type_plan_v1_free(&plan);
    return ok;
}

static bool exercise_open_program(
    const char *path, const char *reflected_fact_path) {
    const PPOSLFNativeVMLimitsV1 limits = {
        .maximum_rule_attempts = 64u,
        .maximum_goal_depth = 8u,
    };
    PPOSLFNativeTypePlanV1 plan;
    PPOSLFNativeTypeVMV1 vm;
    PPOSLFNativeCapabilitySetV1 capabilities;
    PPOSLFNativeCapabilitySetV1 reflected;
    PPOSLFNativeCapabilitySetV1 reordered;
    PPOSLFNativeCapabilitySetV1 borrowed;
    PPOSLFNativeCapabilitySetV1 stable_prefix;
    PPOSLFNativeCapabilitySetV1 overlay;
    PPOSLFNativeCapabilitySetV1 deferred;
    PPOSLFNativeCapabilitySetV1 deferred_overlay;
    PPOSLFNativeCapabilitySetV1 rejected;
    PPOSLFNativeVMResultV1 result;
    Arena query_arena;
    Atom *query;
    Atom *missing_query;
    Atom *source_query;
    Atom *missing_source_query;
    Atom *two_query;
    Atom *left;
    Atom *right;
    Atom *zeta;
    Atom *edge;
    Atom *other_edge;
    Atom *wrong_relation;
    Atom *wrong_arity;
    Atom *variable_row;
    Atom *unknown_term;
    Atom *unknown_row;
    Atom *rows[2];
    Atom *reverse_rows[2];
    Atom *duplicate_rows[2];
    Atom *prefix_rows[1];
    Atom *extension_rows[1];
    Atom *bad_row[1];
    char first_digest[65] = {0};
    uint32_t first_external_index = UINT32_MAX;
    char error[512] = {0};
    bool ok = false;

    if (!prepare_program(path, 4u, &plan, &vm))
        return false;
    pposlf_native_capability_set_v1_init(&capabilities);
    pposlf_native_capability_set_v1_init(&reflected);
    pposlf_native_capability_set_v1_init(&reordered);
    pposlf_native_capability_set_v1_init(&borrowed);
    pposlf_native_capability_set_v1_init(&stable_prefix);
    pposlf_native_capability_set_v1_init(&overlay);
    pposlf_native_capability_set_v1_init(&deferred);
    pposlf_native_capability_set_v1_init(&deferred_overlay);
    pposlf_native_capability_set_v1_init(&rejected);
    pposlf_native_vm_result_v1_init(&result);
    arena_init(&query_arena);
    left = atom_symbol(&query_arena, "left");
    right = atom_symbol(&query_arena, "right");
    zeta = atom_symbol(&query_arena, "zeta");
    query = binary(
        &query_arena, "canary-open-path-v1", left, right);
    missing_query = binary(
        &query_arena, "canary-open-path-v1", left, zeta);
    source_query = unary(
        &query_arena, "canary-open-source-v1", left);
    missing_source_query = unary(
        &query_arena, "canary-open-source-v1", right);
    two_query = unary(
        &query_arena, "canary-open-two-v1", left);
    edge = binary(
        &query_arena, "canary-open-edge-v1", left, right);
    other_edge = binary(
        &query_arena, "canary-open-edge-v1", zeta, zeta);
    wrong_relation = binary(
        &query_arena, "canary-open-path-v1", left, right);
    wrong_arity = unary(
        &query_arena, "canary-open-edge-v1", left);
    variable_row = binary(
        &query_arena, "canary-open-edge-v1",
        atom_var(&query_arena, "$row"), right);
    unknown_term = unary(
        &query_arena, "canary-open-unknown-v1", left);
    unknown_row = binary(
        &query_arena, "canary-open-edge-v1", unknown_term, right);
    rows[0] = edge;
    rows[1] = other_edge;
    reverse_rows[0] = other_edge;
    reverse_rows[1] = edge;
    duplicate_rows[0] = edge;
    duplicate_rows[1] = edge;
    prefix_rows[0] = other_edge;
    extension_rows[0] = edge;
    if (!expect(plan.external_relation_len == 1u &&
                    plan.open_head_len == 1u,
                "open-program inventory changed") ||
        !prove_expect(
            &vm, query, limits, PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "an interface declaration invented an extensional row"))
        goto done;
    ok = expect(result.stats.rule_attempts > 0u,
                "open-program query did not execute its generated rule");
    pposlf_native_vm_result_v1_free(&result);

    if (!expect(pposlf_native_capability_set_v1_prepare(
                    &capabilities, &plan, rows, 2u,
                    error, sizeof(error)),
                error[0] ? error :
                    "authored extensional rows were not admitted") ||
        !prove_with_capabilities_expect(
            &vm, &capabilities, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "an authored extensional row did not close the open proof"))
        goto done;
    ok = expect(result.proof_event_len == 2u &&
                    result.proof_events[0].kind ==
                        PPOSLF_NATIVE_VM_PROOF_GENERATED_STEP_V1 &&
                    result.proof_events[1].kind ==
                        PPOSLF_NATIVE_VM_PROOF_EXTERNAL_ROW_V1,
                "capability proof is not a generated-step/external-row tree") &&
         expect(result.stats.external_row_candidate_visits == 1u &&
                    result.stats.external_row_matches == 1u,
                "capability proof did not use the canonical row index") &&
         expect(result.stats.external_exact_key_lookups == 1u &&
                    result.stats.external_exact_key_hits == 1u,
                "ground external goal did not use exact capability lookup") &&
         expect(result.stats.ground_pattern_rule_attempts > 0u &&
                    result.stats.ground_pattern_rule_matches > 0u,
                "range-restricted rule did not use ground specialization") &&
         expect(result.stats.ground_dense_match_nodes > 0u &&
                    result.stats.ground_dense_slot_writes > 0u &&
                    result.stats
                        .ground_dense_expression_materializations > 0u,
                "ground specialization did not execute dense matching and instantiation") &&
         expect(result.stats.deferred_epoch_goal_materializations == 0u,
                "ground specialization retained a deferred epoch goal") &&
         expect(result.stats.compiled_application_dispatches > 0u,
                "fixed signature did not drive compiled goal dispatch") &&
         expect(result.capability_digest[0] != '\0',
                "capability proof omitted its input digest") && ok;
    memcpy(first_digest, result.capability_digest, sizeof(first_digest));
    first_external_index = result.proof_events[1].index;
    pposlf_native_vm_result_v1_free(&result);

    if (!prove_with_capabilities_expect(
            &vm, &capabilities, two_query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "two open premises did not share their structural prefix"))
        goto done;
    ok = expect(result.stats.external_prefix_key_lookups == 2u &&
                    result.stats.external_prefix_memo_misses == 1u &&
                    result.stats.external_prefix_memo_hits == 1u &&
                    result.stats.external_row_matches == 2u,
                "repeated open prefixes did not exercise memo reuse") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);

    if (!prove_with_capabilities_expect(
            &vm, &capabilities, missing_query, limits,
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "an absent ground capability row unexpectedly proved"))
        goto done;
    ok = expect(result.stats.external_exact_key_lookups == 1u &&
                    result.stats.external_exact_key_hits == 0u &&
                    result.stats.external_row_candidate_visits == 0u,
                "an absent ground capability did not fail by exact lookup") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);

    if (!prove_with_capabilities_expect(
            &vm, &capabilities, source_query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "a partially ground external goal did not prove"))
        goto done;
    ok = expect(result.stats.external_exact_key_lookups == 0u &&
                    result.stats.external_prefix_key_lookups == 1u &&
                    result.stats.external_prefix_key_hits == 1u &&
                    result.stats.external_prefix_key_candidates == 1u &&
                    result.stats.external_prefix_memo_hits == 0u &&
                    result.stats.external_prefix_memo_misses == 1u &&
                    result.stats.external_row_candidate_visits == 1u &&
                    result.stats.external_row_matches == 1u,
                "partially ground external goal did not use its prefix range") &&
         expect(result.stats.ground_pattern_rule_attempts == 0u &&
                    result.stats.ground_pattern_rule_matches == 0u,
                "non-range-restricted rule used ground specialization") &&
         expect(result.stats.ground_dense_match_nodes == 0u &&
                    result.stats.ground_dense_slot_writes == 0u &&
                    result.stats
                        .ground_dense_expression_materializations == 0u,
                "non-range-restricted rule entered the dense ground machine") &&
         expect(result.stats.deferred_epoch_goal_materializations ==
                        result.stats.epoch_goal_materializations_not_admitted &&
                    result.stats.activation_view_goal_admissions == 0u &&
                    result.stats.activation_view_fallback_materializations ==
                        0u &&
                    result.stats
                        .epoch_goal_materializations_not_range_restricted ==
                        result.stats.deferred_epoch_goal_materializations &&
                    result.stats
                        .epoch_goal_materializations_consumer_unsafe == 0u &&
                    result.stats.epoch_goal_materializations_stale == 0u,
                "non-range-restricted body entered activation-view admission") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);

    if (!prove_with_capabilities_expect(
            &vm, &capabilities, missing_source_query, limits,
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "an absent partially ground capability unexpectedly proved"))
        goto done;
    ok = expect(result.stats.external_exact_key_lookups == 0u &&
                    result.stats.external_prefix_key_lookups == 1u &&
                    result.stats.external_prefix_key_hits == 0u &&
                    result.stats.external_prefix_key_candidates == 0u &&
                    result.stats.external_prefix_memo_hits == 0u &&
                    result.stats.external_prefix_memo_misses == 1u &&
                    result.stats.external_row_candidate_visits == 0u &&
                    result.stats.external_row_matches == 0u,
                "absent partially ground capability did not fail by prefix range") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);

    error[0] = '\0';
    if (!expect(
            pposlf_native_capability_set_v1_prepare_reflected_facts(
                &reflected, &plan, reflected_fact_path,
                "OSLFNativeOpenCapabilityV1", error, sizeof(error)),
            error[0] ? error :
                "reflected extensional facts were not admitted") ||
        !prove_with_capabilities_expect(
            &vm, &reflected, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "reflected extensional facts did not close the open proof"))
        goto done;
    ok = expect(strcmp(first_digest, result.capability_digest) == 0,
                "reflection changed the canonical capability meaning") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);
    error[0] = '\0';
    ok = expect(
             !pposlf_native_capability_set_v1_prepare_reflected_facts(
                 &reflected, &plan, reflected_fact_path,
                 "WrongProviderV1", error, sizeof(error)) &&
                 error[0] != '\0',
             "a reflected capability crossed its source-owner boundary") &&
         ok;
    if (!prove_with_capabilities_expect(
            &vm, &reflected, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "rejected reflected replacement damaged the admitted facts"))
        goto done;
    ok = expect(strcmp(first_digest, result.capability_digest) == 0,
                "rejected reflected replacement changed its digest") && ok;
    pposlf_native_vm_result_v1_free(&result);

    error[0] = '\0';
    if (!expect(pposlf_native_capability_set_v1_prepare(
                    &reordered, &plan, reverse_rows, 2u,
                    error, sizeof(error)),
                error[0] ? error :
                    "reordered extensional rows were not admitted") ||
        !prove_with_capabilities_expect(
            &vm, &reordered, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "reordered extensional rows changed provability"))
        goto done;
    ok = expect(strcmp(first_digest, result.capability_digest) == 0,
                "capability digest depends on input row order") && ok;
    pposlf_native_vm_result_v1_free(&result);

    error[0] = '\0';
    if (!expect(pposlf_native_capability_set_v1_prepare_borrowed(
                    &borrowed, &plan, reverse_rows, 2u,
                    error, sizeof(error)),
                error[0] ? error :
                    "borrowed extensional rows were not admitted") ||
        !prove_with_capabilities_expect(
            &vm, &borrowed, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "borrowed extensional rows changed provability"))
        goto done;
    ok = expect(strcmp(first_digest, result.capability_digest) == 0,
                "borrowed capability changed canonical meaning") && ok;
    pposlf_native_vm_result_v1_free(&result);

    error[0] = '\0';
    if (!expect(pposlf_native_capability_set_v1_prepare_borrowed(
                    &stable_prefix, &plan, prefix_rows, 1u,
                    error, sizeof(error)),
                error[0] ? error :
                    "stable borrowed capability prefix was not admitted") ||
        !expect(pposlf_native_capability_set_v1_prepare_borrowed_overlay(
                    &overlay, &plan, &stable_prefix,
                    extension_rows, 1u, error, sizeof(error)),
                error[0] ? error :
                    "capability overlay was not admitted") ||
        !prove_with_capabilities_expect(
            &vm, &overlay, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "capability overlay changed provability"))
        goto done;
    ok = expect(strcmp(first_digest, result.capability_digest) == 0,
                "capability overlay changed canonical meaning") &&
         expect(result.proof_event_len == 2u &&
                    result.proof_events[1].kind ==
                        PPOSLF_NATIVE_VM_PROOF_EXTERNAL_ROW_V1 &&
                    result.proof_events[1].index == first_external_index,
                "capability overlay changed proof-event indexing") && ok;
    pposlf_native_vm_result_v1_free(&result);

    error[0] = '\0';
    if (!expect(pposlf_native_capability_set_v1_append_borrowed(
                    &stable_prefix, &plan, extension_rows, 1u,
                    error, sizeof(error)),
                error[0] ? error :
                    "borrowed capability prefix did not extend") ||
        !prove_with_capabilities_expect(
            &vm, &stable_prefix, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "incrementally extended capability changed provability"))
        goto done;
    ok = expect(strcmp(first_digest, result.capability_digest) == 0,
                "incremental capability changed canonical meaning") &&
         expect(result.proof_event_len == 2u &&
                    result.proof_events[1].kind ==
                        PPOSLF_NATIVE_VM_PROOF_EXTERNAL_ROW_V1 &&
                    result.proof_events[1].index == first_external_index,
                "incremental capability changed proof-event indexing") && ok;
    pposlf_native_vm_result_v1_free(&result);

    error[0] = '\0';
    ok = expect(!pposlf_native_capability_set_v1_append_borrowed(
                    &stable_prefix, &plan, extension_rows, 1u,
                    error, sizeof(error)) && error[0] != '\0',
                "duplicate incremental capability row was admitted") && ok;
    if (!prove_with_capabilities_expect(
            &vm, &stable_prefix, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "rejected incremental extension damaged the stable prefix"))
        goto done;
    ok = expect(strcmp(first_digest, result.capability_digest) == 0,
                "rejected incremental extension changed its digest") && ok;
    pposlf_native_vm_result_v1_free(&result);

    error[0] = '\0';
    if (!expect(pposlf_native_capability_set_v1_prepare_borrowed(
                    &deferred, &plan, prefix_rows, 1u,
                    error, sizeof(error)),
                error[0] ? error :
                    "deferred capability prefix was not admitted") ||
        !expect(pposlf_native_capability_set_v1_append_borrowed_deferred(
                    &deferred, &plan, extension_rows, 1u,
                    error, sizeof(error)),
                error[0] ? error :
                    "deferred capability prefix did not extend"))
        goto done;
    ok = expect(!pposlf_native_type_vm_v1_prove_with_capabilities(
                    &vm, &deferred, query, limits, &result),
                "uncommitted capability prefix executed directly") && ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!expect(pposlf_native_capability_set_v1_commit_digest(
                    &deferred, error),
                "deferred capability prefix did not commit") ||
        !prove_with_capabilities_expect(
            &vm, &deferred, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "committed deferred capability changed provability"))
        goto done;
    ok = expect(strcmp(first_digest, error) == 0 &&
                    strcmp(first_digest, result.capability_digest) == 0,
                "deferred capability commitment changed canonical meaning") &&
         expect(result.proof_event_len == 2u &&
                    result.proof_events[1].index == first_external_index,
                "deferred capability commitment changed proof indexing") && ok;
    pposlf_native_vm_result_v1_free(&result);

    error[0] = '\0';
    if (!expect(pposlf_native_capability_set_v1_prepare_borrowed(
                    &stable_prefix, &plan, prefix_rows, 1u,
                    error, sizeof(error)),
                error[0] ? error :
                    "deferred-overlay base was not admitted") ||
        !expect(
            pposlf_native_capability_set_v1_prepare_borrowed_overlay_deferred(
                &deferred_overlay, &plan, &stable_prefix,
                extension_rows, 1u, error, sizeof(error)),
            error[0] ? error :
                "deferred capability overlay was not admitted"))
        goto done;
    ok = expect(!pposlf_native_type_vm_v1_prove_with_capabilities(
                    &vm, &deferred_overlay, query, limits, &result),
                "deferred overlay escaped through the certified proof API") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!expect(
            pposlf_native_type_vm_v1_prove_with_uncommitted_capabilities(
                &vm, &deferred_overlay, query, limits, &result),
            "deferred overlay could not execute internally") ||
        !expect(result.outcome == PPOSLF_NATIVE_VM_PROVED_V1,
                "deferred overlay changed provability"))
        goto done;
    ok = expect(!result.capability_digest_ready &&
                    result.capability_digest[0] == '\0',
                "uncommitted proof masqueraded as a certificate") &&
         expect(result.proof_event_len == 2u &&
                    result.proof_events[1].index == first_external_index,
                "deferred overlay changed proof indexing") && ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!expect(pposlf_native_capability_set_v1_commit_digest(
                    &deferred_overlay, error),
                "deferred overlay digest could not be forced") ||
        !prove_with_capabilities_expect(
            &vm, &deferred_overlay, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "committed overlay changed provability"))
        goto done;
    ok = expect(strcmp(first_digest, error) == 0 &&
                    strcmp(first_digest, result.capability_digest) == 0,
                "lazy overlay commitment changed canonical meaning") && ok;
    pposlf_native_vm_result_v1_free(&result);

    if (!prove_with_capabilities_expect(
            &vm, &capabilities, query,
            (PPOSLFNativeVMLimitsV1){
                .maximum_rule_attempts = 1u,
                .maximum_goal_depth = 8u,
            },
            PPOSLF_NATIVE_VM_RESOURCE_EXHAUSTED_V1, &result,
            "external rows bypassed the exact attempt limit"))
        goto done;
    ok = expect(result.stats.rule_attempts == 1u &&
                    result.stats.external_row_candidate_visits == 0u,
                "external-row fuel accounting is not exact") && ok;
    pposlf_native_vm_result_v1_free(&result);

    bad_row[0] = wrong_relation;
    error[0] = '\0';
    ok = expect(!pposlf_native_capability_set_v1_prepare(
                    &capabilities, &plan, bad_row, 1u,
                    error, sizeof(error)) && error[0] != '\0',
                "a closed relation was accepted as an external capability") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_with_capabilities_expect(
            &vm, &capabilities, query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "rejected capability replacement damaged the admitted rows"))
        goto done;
    ok = expect(strcmp(first_digest, result.capability_digest) == 0,
                "rejected capability replacement changed its digest") && ok;
    pposlf_native_vm_result_v1_free(&result);

    bad_row[0] = wrong_arity;
    error[0] = '\0';
    ok = expect(!pposlf_native_capability_set_v1_prepare(
                    &rejected, &plan, bad_row, 1u,
                    error, sizeof(error)) && error[0] != '\0',
                "a wrong-arity capability row was admitted") && ok;
    bad_row[0] = variable_row;
    error[0] = '\0';
    ok = expect(!pposlf_native_capability_set_v1_prepare(
                    &rejected, &plan, bad_row, 1u,
                    error, sizeof(error)) && error[0] != '\0',
                "a non-ground capability row was admitted") && ok;
    bad_row[0] = unknown_row;
    error[0] = '\0';
    ok = expect(!pposlf_native_capability_set_v1_prepare(
                    &rejected, &plan, bad_row, 1u,
                    error, sizeof(error)) && error[0] != '\0',
                "an undeclared constructor crossed the capability boundary") &&
         ok;
    error[0] = '\0';
    ok = expect(!pposlf_native_capability_set_v1_prepare(
                    &rejected, &plan, duplicate_rows, 2u,
                    error, sizeof(error)) && error[0] != '\0',
                "duplicate capability rows were admitted") && ok;

done:
    pposlf_native_vm_result_v1_free(&result);
    pposlf_native_capability_set_v1_free(&deferred_overlay);
    pposlf_native_capability_set_v1_free(&deferred);
    pposlf_native_capability_set_v1_free(&overlay);
    pposlf_native_capability_set_v1_free(&stable_prefix);
    pposlf_native_capability_set_v1_free(&borrowed);
    pposlf_native_capability_set_v1_free(&rejected);
    pposlf_native_capability_set_v1_free(&reordered);
    pposlf_native_capability_set_v1_free(&reflected);
    pposlf_native_capability_set_v1_free(&capabilities);
    arena_free(&query_arena);
    pposlf_native_type_vm_v1_free(&vm);
    pposlf_native_type_plan_v1_free(&plan);
    return ok;
}

static bool exercise_trace_program(
    const char *program_path,
    const char *reflected_fact_path,
    const char *deleted_fact_path) {
    const PPOSLFNativeVMLimitsV1 limits = {
        .maximum_rule_attempts = 100000u,
        .maximum_goal_depth = 256u,
    };
    PPOSLFNativeTypePlanV1 plan;
    PPOSLFNativeTypeVMV1 vm;
    PPOSLFNativeCapabilitySetV1 capabilities;
    PPOSLFNativeCapabilitySetV1 deleted;
    PPOSLFNativeVMResultV1 result;
    Arena query_arena;
    Atom *label_nil;
    Atom *positive_labels;
    Atom *negative_labels;
    Atom *positive_query;
    Atom *negative_query;
    Atom *trace_zero;
    Atom *trace_one;
    Atom *trace_two;
    Atom *trace_three;
    Atom *add_query;
    char full_capability_digest[65] = {0};
    char error[512] = {0};
    bool ok = false;

    if (!prepare_program(program_path, 173u, &plan, &vm))
        return false;
    pposlf_native_capability_set_v1_init(&capabilities);
    pposlf_native_capability_set_v1_init(&deleted);
    pposlf_native_vm_result_v1_init(&result);
    arena_init(&query_arena);
    trace_zero = atom_symbol(&query_arena, "ProofTraceNatZeroV1");
    trace_one = unary(
        &query_arena, "ProofTraceNatSuccV1", trace_zero);
    trace_two = unary(
        &query_arena, "ProofTraceNatSuccV1", trace_one);
    trace_three = unary(
        &query_arena, "ProofTraceNatSuccV1", trace_two);
    add_query = ternary(
        &query_arena, "ProofTraceNatAddV1",
        trace_two, trace_one, trace_three);
    label_nil = atom_symbol(&query_arena, "ProofTraceLabelNilV1");
    positive_labels = binary(
        &query_arena, "ProofTraceLabelConsV1",
        atom_symbol(&query_arena, "input-identity-v1"), label_nil);
    positive_labels = binary(
        &query_arena, "ProofTraceLabelConsV1",
        atom_symbol(&query_arena, "input-hx-v1"), positive_labels);
    negative_labels = binary(
        &query_arena, "ProofTraceLabelConsV1",
        atom_symbol(&query_arena, "input-hdummy-v1"), label_nil);
    positive_query = binary(
        &query_arena, "ProofTraceInputVerifyNormalV1",
        atom_symbol(&query_arena, "input-request-v1"), positive_labels);
    negative_query = binary(
        &query_arena, "ProofTraceInputVerifyNormalV1",
        atom_symbol(&query_arena, "input-request-v1"), negative_labels);

    if (!expect(trace_zero && trace_one && trace_two && trace_three &&
                    add_query,
                "compiled-relation integration fixture allocation failed") ||
        !prove_expect(
            &vm, add_query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "recognized unary addition did not prove") ||
        !expect(result.proof_event_len == 1u &&
                    result.proof_events[0].kind ==
                        PPOSLF_NATIVE_VM_PROOF_COMPILED_RELATION_V1 &&
                    result.stats.compiled_relation_dispatches == 1u &&
                    result.stats.compiled_relation_matches == 1u &&
                    result.stats.rule_attempts == 4u &&
                    result.stats.rule_matches == 3u,
                "compiled relation lost its receipt or source cost"))
        goto done;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &vm, add_query,
            (PPOSLFNativeVMLimitsV1){
                .maximum_rule_attempts = 3u,
                .maximum_goal_depth = limits.maximum_goal_depth,
            },
            PPOSLF_NATIVE_VM_RESOURCE_EXHAUSTED_V1, &result,
            "compiled relation changed the exact rule-attempt limit") ||
        !expect(result.stats.compiled_relation_deferrals > 0u,
                "insufficient compiled-relation budget did not fail closed"))
        goto done;
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_expect(
            &vm, add_query,
            (PPOSLFNativeVMLimitsV1){
                .maximum_rule_attempts = limits.maximum_rule_attempts,
                .maximum_goal_depth = 1u,
            },
            PPOSLF_NATIVE_VM_RESOURCE_EXHAUSTED_V1, &result,
            "compiled relation changed the exact logical-depth limit") ||
        !expect(result.stats.compiled_relation_deferrals > 0u &&
                    result.stats.maximum_goal_depth == 2u,
                "logical-depth refusal did not preserve source evidence"))
        goto done;
    pposlf_native_vm_result_v1_free(&result);

    if (!expect(plan.external_relation_len == 16u &&
                    plan.open_head_len == 16u,
                "proof-trace extensional inventory changed") ||
        !prove_expect(
            &vm, positive_query, limits,
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "an empty proof-state capability invented a proof"))
        goto done;
    pposlf_native_vm_result_v1_free(&result);
    if (!expect(
            pposlf_native_capability_set_v1_prepare_reflected_facts(
                &capabilities, &plan, reflected_fact_path,
                "ProofGSLTTraceInputCanaryV1", error, sizeof(error)),
            error[0] ? error :
                "reflected proof-state facts were not admitted") ||
        !prove_with_capabilities_expect(
            &vm, &capabilities, positive_query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "generated proof-trace program rejected its positive proof"))
        goto done;
    ok = expect(
             result_contains_event_kind(
                 &result, PPOSLF_NATIVE_VM_PROOF_GENERATED_STEP_V1) &&
                 result_contains_event_kind(
                     &result, PPOSLF_NATIVE_VM_PROOF_EXTERNAL_ROW_V1),
             "proof-trace receipt omitted generated or extensional evidence") &&
         expect(result.stats.external_row_matches > 0u,
                "proof-trace execution did not consume proof-state rows");
    memcpy(full_capability_digest, result.capability_digest,
           sizeof(full_capability_digest));
    pposlf_native_vm_result_v1_free(&result);
    if (!prove_with_capabilities_expect(
            &vm, &capabilities, negative_query, limits,
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "wrong-target proof trace unexpectedly verified"))
        goto done;
    ok = expect(result.stats.rule_attempts > 0u,
                "negative proof trace performed no generated work") && ok;
    pposlf_native_vm_result_v1_free(&result);

    error[0] = '\0';
    if (!expect(
            pposlf_native_capability_set_v1_prepare_reflected_facts(
                &deleted, &plan, deleted_fact_path,
                "ProofGSLTTraceInputCanaryV1", error, sizeof(error)),
            error[0] ? error :
                "deleted proof-state capability was not admitted") ||
        !prove_with_capabilities_expect(
            &vm, &deleted, positive_query, limits,
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "deleting the assertion row left proof verification inert"))
        goto done;
    ok = expect(strcmp(full_capability_digest,
                       result.capability_digest) != 0,
                "proof-state deletion retained the capability digest") && ok;

done:
    pposlf_native_vm_result_v1_free(&result);
    arena_free(&query_arena);
    pposlf_native_capability_set_v1_free(&deleted);
    pposlf_native_capability_set_v1_free(&capabilities);
    pposlf_native_type_vm_v1_free(&vm);
    pposlf_native_type_plan_v1_free(&plan);
    return ok;
}

static bool exercise_full_proof_machine(
    const char *program_path,
    const char *reflected_fact_path,
    const char *deleted_fact_path) {
    const PPOSLFNativeVMLimitsV1 limits = {
        .maximum_rule_attempts = 500000u,
        .maximum_goal_depth = 512u,
    };
    PPOSLFNativeTypePlanV1 plan;
    PPOSLFNativeTypeVMV1 vm;
    PPOSLFNativeCapabilitySetV1 capabilities;
    PPOSLFNativeCapabilitySetV1 deleted;
    PPOSLFNativeVMResultV1 result;
    Arena query_arena;
    Atom *owner;
    Atom *request;
    Atom *label_nil;
    Atom *active_label;
    Atom *dummy_label;
    Atom *identity_label;
    Atom *positive_labels;
    Atom *negative_labels;
    Atom *positive_query;
    Atom *negative_query;
    char full_capability_digest[65] = {0};
    char error[512] = {0};
    bool ok = false;

    if (!prepare_program(program_path, 647u, &plan, &vm))
        return false;
    pposlf_native_capability_set_v1_init(&capabilities);
    pposlf_native_capability_set_v1_init(&deleted);
    pposlf_native_vm_result_v1_init(&result);
    arena_init(&query_arena);
    owner = atom_symbol(&query_arena, "MetamathProofV1");
    request = binary(
        &query_arena, "ProofRelationalRequestV1", owner,
        atom_symbol(&query_arena, "capability-request-v1"));
    active_label = binary(
        &query_arena, "ProofRelationalLabelV1", owner,
        atom_symbol(&query_arena, "capability-ha-v1"));
    dummy_label = binary(
        &query_arena, "ProofRelationalLabelV1", owner,
        atom_symbol(&query_arena, "capability-hdummy-v1"));
    identity_label = binary(
        &query_arena, "ProofRelationalLabelV1", owner,
        atom_symbol(&query_arena, "capability-identity-v1"));
    label_nil = atom_symbol(&query_arena, "ProofTraceLabelNilV1");
    positive_labels = binary(
        &query_arena, "ProofTraceLabelConsV1", identity_label, label_nil);
    positive_labels = binary(
        &query_arena, "ProofTraceLabelConsV1", active_label,
        positive_labels);
    negative_labels = binary(
        &query_arena, "ProofTraceLabelConsV1", dummy_label, label_nil);
    positive_query = binary(
        &query_arena, "ProofTraceInputVerifyNormalV1", request,
        positive_labels);
    negative_query = binary(
        &query_arena, "ProofTraceInputVerifyNormalV1", request,
        negative_labels);

    if (!expect(plan.external_relation_len == 25u &&
                    plan.open_head_len == 14u,
                "complete proof-machine extensional inventory changed") ||
        !prove_expect(
            &vm, positive_query, limits,
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "an empty state capability invented a complete-machine proof"))
        goto done;
    pposlf_native_vm_result_v1_free(&result);

    if (!expect(
            pposlf_native_capability_set_v1_prepare_reflected_facts(
                &capabilities, &plan, reflected_fact_path,
                "MetamathProofMachineCapabilityCanaryV1",
                error, sizeof(error)),
            error[0] ? error :
                "reflected complete-machine state was not admitted") ||
        !prove_with_capabilities_expect(
            &vm, &capabilities, positive_query, limits,
            PPOSLF_NATIVE_VM_PROVED_V1, &result,
            "complete generated proof machine rejected its positive proof"))
        goto done;
    ok = expect(
             result_contains_event_kind(
                 &result, PPOSLF_NATIVE_VM_PROOF_GENERATED_STEP_V1) &&
                 result_contains_event_kind(
                     &result, PPOSLF_NATIVE_VM_PROOF_EXTERNAL_ROW_V1),
             "complete-machine receipt omitted generated or state evidence") &&
         expect(result.stats.external_row_matches > 0u,
                "complete machine did not consume reflected state rows");
    memcpy(full_capability_digest, result.capability_digest,
           sizeof(full_capability_digest));
    pposlf_native_vm_result_v1_free(&result);

    if (!prove_with_capabilities_expect(
            &vm, &capabilities, negative_query, limits,
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "wrong complete-machine proof unexpectedly verified"))
        goto done;
    ok = expect(result.stats.rule_attempts > 0u,
                "negative complete-machine proof performed no generated work") &&
         ok;
    pposlf_native_vm_result_v1_free(&result);

    error[0] = '\0';
    if (!expect(
            pposlf_native_capability_set_v1_prepare_reflected_facts(
                &deleted, &plan, deleted_fact_path,
                "MetamathProofMachineCapabilityCanaryV1",
                error, sizeof(error)),
            error[0] ? error :
                "deleted complete-machine state was not admitted") ||
        !prove_with_capabilities_expect(
            &vm, &deleted, positive_query, limits,
            PPOSLF_NATIVE_VM_NO_PROOF_V1, &result,
            "deleting a state formula left the complete proof inert"))
        goto done;
    ok = expect(strcmp(full_capability_digest,
                       result.capability_digest) != 0,
                "complete-machine state deletion retained its digest") && ok;

done:
    pposlf_native_vm_result_v1_free(&result);
    arena_free(&query_arena);
    pposlf_native_capability_set_v1_free(&deleted);
    pposlf_native_capability_set_v1_free(&capabilities);
    pposlf_native_type_vm_v1_free(&vm);
    pposlf_native_type_plan_v1_free(&plan);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    bool ok;

    if (argc != 12) {
        fprintf(stderr,
                "usage: %s LARGE-PROGRAM SECOND-PROGRAM CANARY "
                "DELETED-STEP OPEN-PROGRAM OPEN-FACTS TRACE-PROGRAM "
                "TRACE-FACTS TRACE-FACTS-DELETED FULL-FACTS "
                "FULL-FACTS-DELETED\n",
                argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    ok = exercise_stats_accumulation() &&
         exercise_positional_linear_builder_view() &&
         prepare_large_program(argv[1], 647u) &&
         prepare_large_program(argv[2], 989u) &&
         exercise_canary(argv[3], argv[4]) &&
         exercise_search_hashcons_isolation(argv[3]) &&
         exercise_open_program(argv[5], argv[6]) &&
         exercise_trace_program(argv[7], argv[8], argv[9]) &&
         exercise_full_proof_machine(argv[1], argv[10], argv[11]);

    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("(OSLFNativeTypeVMV1Summary %u %u %u)\n",
           checks_run, checks_run - checks_failed, checks_failed);
    return ok ? 0 : 1;
}
