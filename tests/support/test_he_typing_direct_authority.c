#include "eval.h"
#include "generated/he_profiled_type_inference_core_source_binding_v1.generated.h"
#include "generated/he_typing_closed_ground_core_source_binding_v1.generated.h"
#include "generated/he_typing_consistency_core_source_binding_v1.generated.h"
#include "he_typing_authority.h"
#include "match.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool type_list_has_symbol(
    Atom **types, uint32_t count, const char *name) {
    for (uint32_t index = 0u; index < count; index++) {
        if (atom_is_symbol(types[index], name))
            return true;
    }
    return false;
}

static bool type_list_matches_symbols(
    Atom **types, uint32_t count, const char *const *names,
    uint32_t expected_count) {
    if (count != expected_count)
        return false;
    for (uint32_t index = 0u; index < count; index++) {
        if (!atom_is_symbol(types[index], names[index]))
            return false;
    }
    return true;
}

static bool type_list_is_drawn_from(
    Atom **types, uint32_t count, const char *const *names,
    uint32_t name_count) {
    for (uint32_t index = 0u; index < count; index++) {
        bool found = false;
        for (uint32_t candidate = 0u; candidate < name_count; candidate++) {
            if (atom_is_symbol(types[index], names[candidate])) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

static CettaTypeInferenceBudget inference_budget(uint32_t type_capacity) {
    return (CettaTypeInferenceBudget){
        .steps_limited = false,
        .steps_remaining = 0u,
        .steps_spent = 0u,
        .work_steps_observed = 0u,
        .type_capacity = type_capacity,
        .max_depth_observed = 0u,
        .complete = true,
        .type_capacity_exhausted = false,
        .evaluator_stack_exhausted = false,
        .evaluator_capacity_exhausted = false,
        .allow_marked_user_type_functions = true,
    };
}

int main(void) {
    Arena persistent;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable variables;

    arena_init(&persistent);
    arena_set_runtime_kind(
        &persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    space_init_with_universe(&space, &universe);

    const CettaHeTypingCoreDirectServiceV1 *core_service =
        &cetta_he_typing_core_direct_service_v1;
    assert(cetta_he_typing_core_direct_service_v1_is_valid(core_service));
    assert(core_service->authority ==
        &cetta_he_typing_core_direct_authority_v1);
    assert(core_service->classify_consistency ==
        he_typing_classify_consistency);
    assert(core_service->normalize_type ==
        he_typing_normalize_type_status_budgeted);
    assert(core_service->check_refinement ==
        he_typing_check_refinement_status_budgeted);
    assert(core_service->check_term ==
        he_typing_check_term_status_budgeted);

    const CettaHeProfiledTypeInferenceDirectServiceV1 *profile_service =
        &cetta_he_profiled_type_inference_direct_service_v1;
    assert(cetta_he_profiled_type_inference_direct_service_v1_is_valid(
        profile_service));
    assert(profile_service->authority ==
        &cetta_he_profiled_type_inference_direct_authority_v1);
    assert(profile_service->infer == eval_get_atom_types_profiled);
    assert(profile_service->infer_transient ==
        eval_get_atom_types_profiled_transient);
    assert(profile_service->infer_budgeted ==
        eval_get_atom_types_profiled_budgeted);
    assert(profile_service->infer_structural ==
        eval_get_atom_types_structural_profiled);
    assert(profile_service->infer_structural_budgeted ==
        eval_get_atom_types_structural_profiled_budgeted);

    assert(cetta_he_inference_contracts_v1_are_valid());
    static const char *const inference_names[] = {
        "eval_get_atom_types_profiled",
        "eval_get_atom_types_profiled_transient",
        "eval_get_atom_types_profiled_budgeted",
        "eval_get_atom_types_structural_profiled",
        "eval_get_atom_types_structural_profiled_budgeted",
    };
    for (unsigned index = 0u; index < CETTA_HE_INFERENCE_API_V1_COUNT;
         index++) {
        const CettaHeCollectionContractV1 *contract =
            cetta_he_inference_contract_v1((CettaHeInferenceApiV1)index);
        assert(contract);
        assert((unsigned)contract->api == index);
        assert(strcmp(contract->name, inference_names[index]) == 0);
        assert(contract->order_semantic);
        assert(contract->multiplicity_semantic);
    }
    assert(cetta_he_inference_contract_v1(
               CETTA_HE_INFERENCE_API_V1_COUNT) == NULL);

    assert(!cetta_he_check_status_is_budget_sensitive(
        CETTA_HE_CHECK_ESTABLISHED));
    assert(!cetta_he_check_status_is_budget_sensitive(
        CETTA_HE_CHECK_REFUTED));
    assert(!cetta_he_check_status_is_budget_sensitive(
        CETTA_HE_CHECK_UNDETERMINED));
    assert(cetta_he_check_status_is_budget_sensitive(
        CETTA_HE_CHECK_INCOMPLETE));
    assert(!cetta_he_normalize_status_is_exhaustion(
        CETTA_HE_NORMALIZE_COMPLETE));
    assert(cetta_he_normalize_status_is_exhaustion(
        CETTA_HE_NORMALIZE_RESOURCE));
    assert(cetta_he_normalize_status_is_exhaustion(
        CETTA_HE_NORMALIZE_DEPTH));
    assert(!cetta_he_normalize_status_is_exhaustion(
        CETTA_HE_NORMALIZE_AMBIGUOUS));
    assert(!cetta_he_normalize_status_is_exhaustion(
        CETTA_HE_NORMALIZE_NO_RESULT));
    assert(!cetta_he_normalize_status_is_exhaustion(
        CETTA_HE_NORMALIZE_INADMISSIBLE));
    assert(!cetta_he_normalize_status_is_exhaustion(
        CETTA_HE_NORMALIZE_PROVISIONAL));

    assert(cetta_he_search_strategy_contracts_v1_are_valid());
    static const char *const search_names[] = {
        "search-inhabitants",
        "search-first-inhabitant",
        "type-forward-step",
        "type-forward-closure",
    };
    for (unsigned index = 0u; index < CETTA_HE_SEARCH_STRATEGY_V1_COUNT;
         index++) {
        const CettaHeSearchStrategyContractV1 *contract =
            cetta_he_search_strategy_contract_v1(
                (CettaHeSearchStrategyApiV1)index);
        assert(contract);
        assert((unsigned)contract->api == index);
        assert(strcmp(contract->name, search_names[index]) == 0);
        assert(!contract->exhaustion_may_reject);
        assert(contract->exhaustive_empty_may_reject ==
               (index == CETTA_HE_SEARCH_FIRST_INHABITANT_V1));
    }
    assert(cetta_he_search_strategy_contract_v1(
               CETTA_HE_SEARCH_STRATEGY_V1_COUNT) == NULL);

    const CettaNikDirectSourceBindingV1 *source =
        &he_typing_consistency_core_source_binding_v1;
    assert(cetta_nik_direct_source_binding_v1_is_valid(source));
    assert(source->authority == core_service->authority);
    assert(source->authority != profile_service->authority);
    assert(strcmp(source->schema_id, "finite-horn-gslt-v1") == 0);
    assert(strcmp(
        source->presentation_id, "he-typing-consistency-core") == 0);
    assert(strcmp(
        source->semantic_scope, "he.typing.consistency-core") == 0);
    assert(strcmp(source->mode, "direct-decision") == 0);
    assert(strcmp(source->certificate_policy, "none") == 0);
    assert(strcmp(source->fiber, "he") == 0);
    assert(strcmp(source->default_outcome, "HCheckUndetermined") == 0);
    assert(source->coverage ==
        CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT);

    const CettaNikDirectSourceBindingV1 *profile_source =
        &he_profiled_type_inference_core_source_binding_v1;
    assert(cetta_nik_direct_source_binding_v1_is_valid(profile_source));
    assert(profile_source->authority == profile_service->authority);
    assert(profile_source->authority != core_service->authority);
    assert(strcmp(profile_source->schema_id, "finite-horn-gslt-v1") == 0);
    assert(strcmp(
        profile_source->presentation_id,
        "he-profiled-type-inference-core") == 0);
    assert(strcmp(
        profile_source->semantic_scope,
        "he.profiled-type-inference.ground-declaration-application-core") == 0);
    assert(strcmp(profile_source->mode, "direct-decision") == 0);
    assert(strcmp(profile_source->certificate_policy, "none") == 0);
    assert(strcmp(profile_source->fiber, "he") == 0);
    assert(strcmp(profile_source->default_outcome, "HCheckUndetermined") == 0);
    assert(profile_source->coverage ==
        CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT);

    const CettaNikDirectSourceBindingV1 *closed_ground_source =
        &he_typing_closed_ground_core_source_binding_v1;
    assert(cetta_nik_direct_source_binding_v1_is_valid(
        closed_ground_source));
    assert(closed_ground_source->authority == core_service->authority);
    assert(closed_ground_source->authority != profile_service->authority);
    assert(strcmp(
        closed_ground_source->schema_id, "finite-horn-gslt-v1") == 0);
    assert(strcmp(
        closed_ground_source->presentation_id,
        "he-typing-closed-ground-core-v1") == 0);
    assert(strcmp(
        closed_ground_source->semantic_scope,
        "he.typing.closed-ground-decision-core") == 0);
    assert(strcmp(closed_ground_source->mode, "direct-decision") == 0);
    assert(strcmp(closed_ground_source->certificate_policy, "none") == 0);
    assert(strcmp(closed_ground_source->fiber, "he") == 0);
    assert(strcmp(
        closed_ground_source->default_outcome, "HCheckUndetermined") == 0);
    assert(closed_ground_source->coverage ==
        CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT);

    CettaNikDirectSourceBindingV1 invalid_source = *source;
    invalid_source.semantic_scope = "";
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid_source));
    invalid_source = *profile_source;
    invalid_source.authority = NULL;
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid_source));
    invalid_source = *profile_source;
    invalid_source.coverage = (CettaNikDirectSourceCoverageV1)0;
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid_source));
    invalid_source = *closed_ground_source;
    invalid_source.presentation_id = "";
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid_source));
    invalid_source = *source;
    invalid_source.certificate_policy = "trace";
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid_source));
    invalid_source = *source;
    invalid_source.fiber = "";
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid_source));

    Atom *number = atom_symbol(&persistent, "Number");
    Atom *string = atom_symbol(&persistent, "String");
    Atom *dynamic = atom_symbol(&persistent, "%Undefined%");
    assert(number && string && dynamic);
    assert(core_service->classify_consistency(number, number, 64u) ==
        CETTA_HE_EDGE_EXACT);
    assert(core_service->classify_consistency(number, string, 64u) ==
        he_typing_classify_consistency(number, string, 64u));
    assert(core_service->classify_consistency(number, string, 64u) ==
        CETTA_HE_EDGE_NONE);
    Atom *space_kind = atom_symbol(&persistent, "SpaceType");
    Atom *space_value_type = atom_expr2(
        &persistent, atom_symbol(&persistent, "Space"),
        atom_symbol(&persistent, "Atom"));
    Bindings space_match;
    bindings_init(&space_match);
    assert(match_types(space_value_type, space_kind, &space_match));
    assert(space_match.len == 0u);
    assert(match_types(space_kind, space_value_type, &space_match));
    assert(space_match.len == 0u);
    assert(core_service->classify_consistency(
               space_value_type, space_kind, 64u) ==
           CETTA_HE_EDGE_STRUCTURAL);
    assert(core_service->classify_consistency(
               space_kind, space_value_type, 64u) ==
           CETTA_HE_EDGE_STRUCTURAL);
    Atom *space_lookalike = atom_symbol(&persistent, "SpaceTypo");
    assert(!type_match_uses_space_class_bridge(
        space_value_type, space_lookalike));
    bindings_free(&space_match);

    Atom *list_number = atom_expr2(
        &persistent, atom_symbol(&persistent, "List"), number);
    Atom *normalized = NULL;
    CettaHeTypingBudget direct_budget;
    he_typing_budget_init(&direct_budget, 0u);
    assert(core_service->normalize_type(
               &persistent, &space, list_number,
               &direct_budget, &normalized) ==
           CETTA_HE_NORMALIZE_COMPLETE);
    assert(normalized == list_number);
    assert(direct_budget.work_steps_observed > 0u);

    Atom *refinement_detail = NULL;
    he_typing_budget_init_unbounded(&direct_budget);
    assert(core_service->check_refinement(
               &persistent, &space, list_number,
               &direct_budget, &refinement_detail) ==
           CETTA_HE_REFINEMENT_VALID);
    assert(refinement_detail == NULL);

    Atom *typing_detail = NULL;
    CettaHeTypingEdge typing_edge = CETTA_HE_EDGE_NONE;
    he_typing_budget_init_unbounded(&direct_budget);
    assert(core_service->check_term(
               &persistent, &space, atom_int(&persistent, 7), number,
               &direct_budget, false, &typing_edge, &typing_detail) ==
           CETTA_HE_CHECK_ESTABLISHED);
    assert(typing_edge == CETTA_HE_EDGE_EXACT);

    typing_detail = NULL;
    typing_edge = CETTA_HE_EDGE_UNKNOWN;
    he_typing_budget_init_unbounded(&direct_budget);
    assert(core_service->check_term(
               &persistent, &space, atom_int(&persistent, 7), string,
               &direct_budget, false, &typing_edge, &typing_detail) ==
           CETTA_HE_CHECK_REFUTED);
    assert(typing_edge == CETTA_HE_EDGE_NONE);

    typing_detail = NULL;
    typing_edge = CETTA_HE_EDGE_NONE;
    he_typing_budget_init_unbounded(&direct_budget);
    assert(core_service->check_term(
               &persistent, &space, atom_int(&persistent, 7), dynamic,
               &direct_budget, false, &typing_edge, &typing_detail) ==
           CETTA_HE_CHECK_ESTABLISHED);
    assert(typing_edge == CETTA_HE_EDGE_DYNAMIC);

    typing_detail = NULL;
    typing_edge = CETTA_HE_EDGE_NONE;
    he_typing_budget_init_unbounded(&direct_budget);
    assert(core_service->check_term(
               &persistent, &space, atom_int(&persistent, 7), dynamic,
               &direct_budget, true, &typing_edge, &typing_detail) ==
           CETTA_HE_CHECK_UNDETERMINED);
    assert(typing_edge == CETTA_HE_EDGE_DYNAMIC);

    Atom **inferred_types = NULL;
    uint32_t inferred_count = profile_service->infer(
        &space, &persistent, atom_int(&persistent, 7), &inferred_types);
    bool inferred_number = type_list_has_symbol(
        inferred_types, inferred_count, "Number");
    free(inferred_types);
    assert(inferred_count > 0u && inferred_number);

    Atom *subject = atom_symbol(&persistent, "typed-subject");
    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &space, &persistent, subject, &inferred_types);
    assert(!type_list_has_symbol(inferred_types, inferred_count, "String"));
    free(inferred_types);
    space_add(
        &space,
        atom_expr3(
            &persistent, atom_symbol(&persistent, ":"), subject, string));
    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &space, &persistent, subject, &inferred_types);
    assert(type_list_has_symbol(inferred_types, inferred_count, "String"));
    free(inferred_types);

    Atom *inferred_id = atom_symbol(&persistent, "inferred-id");
    Atom *number_to_number = atom_expr3(
        &persistent, atom_symbol(&persistent, "->"), number, number);
    assert(inferred_id && number_to_number);
    space_add(
        &space,
        atom_expr3(
            &persistent, atom_symbol(&persistent, ":"),
            inferred_id, number_to_number));
    Atom *accepted_application = atom_expr2(
        &persistent, inferred_id, atom_int(&persistent, 7));
    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &space, &persistent, accepted_application, &inferred_types);
    assert(type_list_has_symbol(inferred_types, inferred_count, "Number"));
    free(inferred_types);

    Atom *refuted_application = atom_expr2(
        &persistent, inferred_id, atom_string(&persistent, "wrong"));
    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &space, &persistent, refuted_application, &inferred_types);
    assert(!type_list_has_symbol(inferred_types, inferred_count, "Number"));
    free(inferred_types);

    /* The five list-valued operations are sequence/bag APIs.  Direct
       annotations and inferred result types retain logical order and repeated
       occurrences; the budgeted forms may return only a sound prefix (or the
       empty prefix) and must lower `complete` when capacity prevents the full
       sequence. */
    Atom *type_one = atom_symbol(&persistent, "ContractTypeOne");
    Atom *type_two = atom_symbol(&persistent, "ContractTypeTwo");
    Atom *contract_subject = atom_symbol(&persistent, "contract-subject");
    assert(type_one && type_two && contract_subject);
    space_add(&space, atom_expr3(
        &persistent, atom_symbol(&persistent, ":"), contract_subject,
        type_one));
    space_add(&space, atom_expr3(
        &persistent, atom_symbol(&persistent, ":"), contract_subject,
        type_two));
    space_add(&space, atom_expr3(
        &persistent, atom_symbol(&persistent, ":"), contract_subject,
        type_one));
    static const char *const direct_sequence[] = {
        "ContractTypeOne", "ContractTypeTwo", "ContractTypeOne",
    };

    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &space, &persistent, contract_subject, &inferred_types);
    assert(type_list_matches_symbols(
        inferred_types, inferred_count, direct_sequence, 3u));
    free(inferred_types);

    /* The second call is served by the profiled memo and must preserve the
       same ordered multiplicity. */
    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &space, &persistent, contract_subject, &inferred_types);
    assert(type_list_matches_symbols(
        inferred_types, inferred_count, direct_sequence, 3u));
    free(inferred_types);

    inferred_types = NULL;
    inferred_count = profile_service->infer_transient(
        &space, &persistent, contract_subject, &inferred_types);
    assert(type_list_matches_symbols(
        inferred_types, inferred_count, direct_sequence, 3u));
    free(inferred_types);

    CettaTypeInferenceBudget complete_inference = inference_budget(3u);
    inferred_types = NULL;
    inferred_count = profile_service->infer_budgeted(
        &space, &persistent, contract_subject, &inferred_types,
        &complete_inference);
    assert(complete_inference.complete);
    assert(!complete_inference.type_capacity_exhausted);
    assert(type_list_matches_symbols(
        inferred_types, inferred_count, direct_sequence, 3u));
    free(inferred_types);

    CettaTypeInferenceBudget partial_inference = inference_budget(2u);
    inferred_types = NULL;
    inferred_count = profile_service->infer_budgeted(
        &space, &persistent, contract_subject, &inferred_types,
        &partial_inference);
    assert(!partial_inference.complete);
    assert(partial_inference.type_capacity_exhausted);
    assert(type_list_is_drawn_from(
        inferred_types, inferred_count, direct_sequence, 3u));
    free(inferred_types);

    Atom *reverse_subject = atom_symbol(&persistent, "reverse-subject");
    assert(reverse_subject);
    space_add(&space, atom_expr3(
        &persistent, atom_symbol(&persistent, ":"), reverse_subject,
        type_two));
    space_add(&space, atom_expr3(
        &persistent, atom_symbol(&persistent, ":"), reverse_subject,
        type_one));
    static const char *const reverse_sequence[] = {
        "ContractTypeTwo", "ContractTypeOne",
    };
    inferred_types = NULL;
    inferred_count = profile_service->infer_transient(
        &space, &persistent, reverse_subject, &inferred_types);
    assert(type_list_matches_symbols(
        inferred_types, inferred_count, reverse_sequence, 2u));
    free(inferred_types);

    Atom *argument_type = atom_symbol(&persistent, "ContractArgument");
    Atom *contract_argument = atom_symbol(&persistent, "contract-argument");
    Atom *contract_operator = atom_symbol(&persistent, "contract-operator");
    assert(argument_type && contract_argument && contract_operator);
    space_add(&space, atom_expr3(
        &persistent, atom_symbol(&persistent, ":"), contract_argument,
        argument_type));
    space_add(&space, atom_expr3(
        &persistent, atom_symbol(&persistent, ":"), contract_operator,
        atom_expr3(&persistent, atom_symbol(&persistent, "->"),
                   argument_type, type_one)));
    space_add(&space, atom_expr3(
        &persistent, atom_symbol(&persistent, ":"), contract_operator,
        atom_expr3(&persistent, atom_symbol(&persistent, "->"),
                   argument_type, type_two)));
    space_add(&space, atom_expr3(
        &persistent, atom_symbol(&persistent, ":"), contract_operator,
        atom_expr3(&persistent, atom_symbol(&persistent, "->"),
                   argument_type, type_one)));
    Atom *contract_application = atom_expr2(
        &persistent, contract_operator, contract_argument);
    inferred_types = NULL;
    inferred_count = profile_service->infer_structural(
        &space, &persistent, contract_application, &inferred_types);
    assert(type_list_matches_symbols(
        inferred_types, inferred_count, direct_sequence, 3u));
    free(inferred_types);

    CettaTypeInferenceBudget structural_budget = inference_budget(3u);
    inferred_types = NULL;
    inferred_count = profile_service->infer_structural_budgeted(
        &space, &persistent, contract_application, &inferred_types,
        &structural_budget);
    assert(structural_budget.complete);
    assert(!structural_budget.type_capacity_exhausted);
    assert(type_list_matches_symbols(
        inferred_types, inferred_count, direct_sequence, 3u));
    free(inferred_types);

    /* Capacity exhaustion is an incomplete judgment, never a refutation. */
    typing_detail = NULL;
    typing_edge = CETTA_HE_EDGE_NONE;
    he_typing_budget_init_unbounded(&direct_budget);
    direct_budget.type_capacity = 2u;
    assert(core_service->check_term(
               &persistent, &space, contract_subject, type_one,
               &direct_budget, false, &typing_edge, &typing_detail) ==
           CETTA_HE_CHECK_INCOMPLETE);
    assert(direct_budget.type_capacity_exhausted);

    typing_detail = NULL;
    typing_edge = CETTA_HE_EDGE_NONE;
    he_typing_budget_init_unbounded(&direct_budget);
    direct_budget.type_capacity = 3u;
    assert(core_service->check_term(
               &persistent, &space, contract_subject, type_one,
               &direct_budget, false, &typing_edge, &typing_detail) ==
           CETTA_HE_CHECK_ESTABLISHED);
    assert(!direct_budget.type_capacity_exhausted);

#if CETTA_BUILD_WITH_RUNTIME_STATS
    Atom *cached_subject = atom_symbol(&persistent, "cached-subject");
    Atom *cached_number_annotation = atom_expr3(
        &persistent, atom_symbol(&persistent, ":"),
        cached_subject, number);
    Atom *cached_string_annotation = atom_expr3(
        &persistent, atom_symbol(&persistent, ":"),
        cached_subject, string);
    assert(cached_subject);
    assert(cached_number_annotation && cached_string_annotation);
    space_add(&space, cached_number_annotation);
    eval_profiled_type_cache_free_for_current_thread();
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
    const uint32_t repeated_queries = 100000u;
    for (uint32_t query = 0u; query < repeated_queries; query++) {
        inferred_types = NULL;
        inferred_count = profile_service->infer(
            &space, &persistent, cached_subject, &inferred_types);
        assert(type_list_has_symbol(
            inferred_types, inferred_count, "Number"));
        free(inferred_types);
    }
    CettaRuntimeStats cache_stats;
    cetta_runtime_stats_snapshot(&cache_stats);
    assert(cache_stats.counters[
        CETTA_RUNTIME_COUNTER_HE_PROFILED_TYPE_CACHE_MISS] == 1u);
    assert(cache_stats.counters[
        CETTA_RUNTIME_COUNTER_HE_PROFILED_TYPE_CACHE_HIT] ==
        repeated_queries - 1u);

    assert(space_remove(&space, cached_number_annotation));
    space_add(&space, cached_string_annotation);
    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &space, &persistent, cached_subject, &inferred_types);
    assert(!type_list_has_symbol(inferred_types, inferred_count, "Number"));
    assert(type_list_has_symbol(inferred_types, inferred_count, "String"));
    free(inferred_types);
    cetta_runtime_stats_snapshot(&cache_stats);
    cetta_runtime_stats_disable();
    assert(cache_stats.counters[
        CETTA_RUNTIME_COUNTER_HE_PROFILED_TYPE_CACHE_MISS] == 2u);
    assert(cache_stats.counters[
        CETTA_RUNTIME_COUNTER_HE_PROFILED_TYPE_CACHE_HIT] ==
        repeated_queries - 1u);

    /* The four other APIs are deliberately transient. */
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
    inferred_types = NULL;
    inferred_count = profile_service->infer_transient(
        &space, &persistent, cached_subject, &inferred_types);
    free(inferred_types);
    inferred_types = NULL;
    inferred_count = profile_service->infer_structural(
        &space, &persistent, cached_subject, &inferred_types);
    free(inferred_types);
    CettaTypeInferenceBudget transient_budget = inference_budget(0u);
    inferred_types = NULL;
    inferred_count = profile_service->infer_budgeted(
        &space, &persistent, cached_subject, &inferred_types,
        &transient_budget);
    free(inferred_types);
    transient_budget = inference_budget(0u);
    inferred_types = NULL;
    inferred_count = profile_service->infer_structural_budgeted(
        &space, &persistent, cached_subject, &inferred_types,
        &transient_budget);
    free(inferred_types);
    cetta_runtime_stats_snapshot(&cache_stats);
    cetta_runtime_stats_disable();
    assert(cache_stats.counters[
        CETTA_RUNTIME_COUNTER_HE_PROFILED_TYPE_CACHE_MISS] == 0u);
    assert(cache_stats.counters[
        CETTA_RUNTIME_COUNTER_HE_PROFILED_TYPE_CACHE_HIT] == 0u);
#endif

    CettaNikDirectAuthorityTokenV1 pure_core_token;
    assert(cetta_he_typing_core_direct_authority_token_v1(
        NULL, 7u, &pure_core_token));
    assert(pure_core_token.length ==
        CETTA_NIK_DIRECT_AUTHORITY_TOKEN_BASE_WORDS);
    assert(cetta_he_typing_core_direct_authority_token_v1_is_current(
        &pure_core_token, NULL, 7u));
    assert(!cetta_he_typing_core_direct_authority_token_v1_is_current(
        &pure_core_token, NULL, 8u));

    CettaNikDirectAuthorityTokenV1 pure_profile_token;
    assert(cetta_he_profiled_type_inference_direct_authority_token_v1(
        NULL, 7u, &pure_profile_token));
    assert(cetta_he_profiled_type_inference_direct_authority_token_v1_is_current(
        &pure_profile_token, NULL, 7u));
    assert(!cetta_he_profiled_type_inference_direct_authority_token_v1_is_current(
        &pure_profile_token, NULL, 8u));
    assert(!cetta_nik_direct_authority_token_v1_equal(
        &pure_core_token, &pure_profile_token));

    CettaNikDirectAuthorityTokenV1 profile_space_token;
    assert(cetta_he_profiled_type_inference_direct_authority_token_v1(
        &space, 9u, &profile_space_token));
    assert(profile_space_token.length ==
        CETTA_NIK_DIRECT_AUTHORITY_TOKEN_BASE_WORDS + 3u);
    assert(cetta_he_profiled_type_inference_direct_authority_token_v1_is_current(
        &profile_space_token, &space, 9u));
    space_add(&space, atom_symbol(&persistent, "authority-mutation"));
    assert(!cetta_he_profiled_type_inference_direct_authority_token_v1_is_current(
        &profile_space_token, &space, 9u));

    /* An overlay's own revision does not change when its visible base is
     * mutated.  Profiled inference nevertheless reads that base, so the
     * admitted result must be invalidated by the authority-wide mutation
     * epoch rather than by the overlay revision alone. */
    Space overlay_base;
    Space overlay;
    space_init_with_universe(&overlay_base, &universe);
    Atom *overlay_subject = atom_symbol(&persistent, "overlay-subject");
    Atom *overlay_filler = atom_symbol(&persistent, "overlay-filler");
    Atom *number_annotation = atom_expr3(
        &persistent, atom_symbol(&persistent, ":"),
        overlay_subject, number);
    Atom *string_annotation = atom_expr3(
        &persistent, atom_symbol(&persistent, ":"),
        overlay_subject, string);
    assert(overlay_subject && overlay_filler &&
           number_annotation && string_annotation);
    space_add(&overlay_base, number_annotation);
    space_add(&overlay_base, overlay_filler);
    space_init_overlay(&overlay, &overlay_base);
    uint64_t overlay_revision = space_revision(&overlay);

    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &overlay, &persistent, overlay_subject, &inferred_types);
    assert(type_list_has_symbol(inferred_types, inferred_count, "Number"));
    free(inferred_types);

    assert(space_remove(&overlay_base, number_annotation));
    space_add(&overlay_base, string_annotation);
    assert(space_revision(&overlay) == overlay_revision);
    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &overlay, &persistent, overlay_subject, &inferred_types);
    assert(!type_list_has_symbol(inferred_types, inferred_count, "Number"));
    assert(type_list_has_symbol(inferred_types, inferred_count, "String"));
    free(inferred_types);

    space_free(&overlay);
    space_free(&overlay_base);

    CettaHeTypingCoreDirectServiceV1 invalid_core = *core_service;
    invalid_core.check_term = NULL;
    assert(!cetta_he_typing_core_direct_service_v1_is_valid(
        &invalid_core));
    invalid_core = *core_service;
    invalid_core.authority = NULL;
    assert(!cetta_he_typing_core_direct_service_v1_is_valid(
        &invalid_core));

    CettaHeProfiledTypeInferenceDirectServiceV1 invalid_profile =
        *profile_service;
    invalid_profile.infer = NULL;
    assert(!cetta_he_profiled_type_inference_direct_service_v1_is_valid(
        &invalid_profile));

    eval_profiled_type_cache_free_for_current_thread();
    space_free(&space);
    g_symbols = NULL;
    g_var_intern = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    term_universe_free(&universe);
    arena_free(&persistent);

    puts("PASS: HE outcomes and five list-inference contracts are live certificate-free NIK authorities");
    return 0;
}
