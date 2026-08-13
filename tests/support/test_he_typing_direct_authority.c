#include "eval.h"
#include "generated/he_typing_consistency_core_source_binding_v1.generated.h"
#include "he_typing_authority.h"
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
    assert(source->coverage ==
        CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT);

    CettaNikDirectSourceBindingV1 invalid_source = *source;
    invalid_source.semantic_scope = "";
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid_source));

    Atom *number = atom_symbol(&persistent, "Number");
    Atom *string = atom_symbol(&persistent, "String");
    assert(number && string);
    assert(core_service->classify_consistency(number, number, 64u) ==
        CETTA_HE_EDGE_EXACT);
    assert(core_service->classify_consistency(number, string, 64u) ==
        he_typing_classify_consistency(number, string, 64u));
    assert(core_service->classify_consistency(number, string, 64u) ==
        CETTA_HE_EDGE_NONE);

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

#if CETTA_BUILD_WITH_RUNTIME_STATS
    Atom *cached_subject = atom_symbol(&persistent, "cached-subject");
    assert(cached_subject);
    space_add(
        &space,
        atom_expr3(
            &persistent, atom_symbol(&persistent, ":"),
            cached_subject, number));
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &space, &persistent, cached_subject, &inferred_types);
    assert(type_list_has_symbol(inferred_types, inferred_count, "Number"));
    free(inferred_types);
    inferred_types = NULL;
    inferred_count = profile_service->infer(
        &space, &persistent, cached_subject, &inferred_types);
    assert(type_list_has_symbol(inferred_types, inferred_count, "Number"));
    free(inferred_types);
    CettaRuntimeStats cache_stats;
    cetta_runtime_stats_snapshot(&cache_stats);
    cetta_runtime_stats_disable();
    assert(cache_stats.counters[
        CETTA_RUNTIME_COUNTER_HE_PROFILED_TYPE_CACHE_MISS] >= 1u);
    assert(cache_stats.counters[
        CETTA_RUNTIME_COUNTER_HE_PROFILED_TYPE_CACHE_HIT] >= 1u);
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

    puts("PASS: HE core typing and live profiled inference are certificate-free direct NIK authorities");
    return 0;
}
