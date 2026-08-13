#include "atom.h"
#include "eval.h"
#include "generated/prime_nik_authorities_v1.generated.h"
#include "generated/prime_typing_closed_formation_source_binding_v1.generated.h"
#include "generated/prime_typing_native_ground_judgments_source_binding_v1.generated.h"
#include "generated/prime_typing_typed_publication_core_source_binding_v1.generated.h"
#include "gslt_horn_runtime.h"
#include "he_typing.h"
#include "library.h"
#include "match.h"
#include "nik_runtime.h"
#include "parser.h"
#include "prime_semantics.h"
#include "prime_typing_publication.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Atom *copy_expr_with(Arena *arena, Atom *source, uint32_t index,
                            Atom *replacement) {
    if (!source || source->kind != ATOM_EXPR || index >= source->expr.len)
        return NULL;
    Atom **items = arena_alloc(arena, sizeof(Atom *) * source->expr.len);
    for (uint32_t i = 0; i < source->expr.len; i++)
        items[i] = i == index ? replacement : source->expr.elems[i];
    return atom_expr(arena, items, source->expr.len);
}

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static bool expect_direct_typing_status(
    Arena *arena, Space *space,
    const CettaPrimeTypingDirectServiceV1 *service,
    const char *claim_text, const char *expected_status) {
    Atom *claim = parse_one(arena, claim_text);
    Atom *verdict = claim
        ? service->judge(arena, space, claim, false, 0u)
        : NULL;
    if (verdict && verdict->kind == ATOM_EXPR &&
        verdict->expr.len == 4u &&
        atom_is_symbol(verdict->expr.elems[0], "PrimeVerdict") &&
        atom_is_symbol(verdict->expr.elems[1], expected_status)) {
        return true;
    }
    fprintf(stderr, "Prime direct typing status mismatch for %s: ",
            claim_text);
    atom_print(verdict, stderr);
    fprintf(stderr, " (expected %s)\n", expected_status);
    return false;
}

static CettaGsltHornLimits prime_ground_qualification_limits(void) {
    return (CettaGsltHornLimits){
        .max_rule_attempts = 100000u,
        .max_answers = 32u,
        .max_depth = 128u,
    };
}

static int prime_ground_status_index(const Atom *status) {
    static const char *const names[] = {
        "PEstablished", "PRefuted", "PUndetermined", "PIncomplete"};
    for (size_t index = 0u; index < sizeof names / sizeof names[0]; index++)
        if (atom_is_symbol((Atom *)status, names[index])) return (int)index;
    return -1;
}

static bool prime_ground_claim_is_exclusive(
    const CettaGsltHornProgram *program, Arena *queries, Arena *answers,
    const char *claim_text, const char *expected_status) {
    char query_text[512];
    int query_size = snprintf(
        query_text, sizeof query_text,
        "(PrimeJudges %s $status)", claim_text);
    Atom *query = query_size > 0 && (size_t)query_size < sizeof query_text
        ? parse_one(queries, query_text) : NULL;
    CettaGsltHornResult result = {0};
    char error[1024] = {0};
    if (!query || !cetta_gslt_horn_query(
            program, answers, query, prime_ground_qualification_limits(),
            &result, error, sizeof error)) {
        fprintf(stderr, "Prime native-ground query failed for %s: %s\n",
                claim_text, error);
        cetta_gslt_horn_result_free(&result);
        return false;
    }
    if (result.outcome != CETTA_GSLT_HORN_COMPLETED) {
        fprintf(stderr, "Prime native-ground query was resource-incomplete: %s\n",
                claim_text);
        cetta_gslt_horn_result_free(&result);
        return false;
    }

    bool seen[4] = {false, false, false, false};
    size_t distinct = 0u;
    for (size_t index = 0u; index < result.answer_count; index++) {
        Atom *answer = result.answers[index];
        int status_index =
            answer && answer->kind == ATOM_EXPR && answer->expr.len == 3u &&
                    atom_is_symbol(answer->expr.elems[0], "PrimeJudges")
                ? prime_ground_status_index(answer->expr.elems[2]) : -1;
        if (status_index < 0) {
            fprintf(stderr, "Prime native-ground returned a malformed outcome for %s\n",
                    claim_text);
            cetta_gslt_horn_result_free(&result);
            return false;
        }
        if (!seen[(size_t)status_index]) {
            seen[(size_t)status_index] = true;
            distinct++;
        }
    }
    if (distinct > 1u) {
        fprintf(stderr, "Prime native-ground derived conflicting outcomes for %s\n",
                claim_text);
        cetta_gslt_horn_result_free(&result);
        return false;
    }
    const char *actual_status = distinct == 0u ? "PUndetermined" :
        (seen[0] ? "PEstablished" :
         seen[1] ? "PRefuted" :
         seen[2] ? "PUndetermined" : "PIncomplete");
    bool expected = expected_status == NULL ||
                    strcmp(actual_status, expected_status) == 0;
    if (!expected)
        fprintf(stderr, "Prime native-ground status for %s was %s, expected %s\n",
                claim_text, actual_status, expected_status);
    cetta_gslt_horn_result_free(&result);
    return expected;
}

static bool prime_ground_exclusivity_matrix(
    const CettaGsltHornProgram *program, Arena *queries, Arena *answers) {
    static const char *const types[] = {
        "PType", "PDynamic", "PAtom", "PNumber", "PString", "PBool",
        "(PList PNumber)", "(PArrow PNumber PString)"};
    static const char *const values[] = {
        "PVNumber", "PVString", "PVTrue", "PVFalse"};
    static const char *const conversion_terms[] = {
        "PNumber", "PString", "PBool", "PDynamic", "PAtom",
        "(PAdd POne POne)", "PTwo"};
    char claim[256];

    for (size_t type_index = 0u;
         type_index < sizeof types / sizeof types[0]; type_index++) {
        (void)snprintf(claim, sizeof claim, "(PForm %s)", types[type_index]);
        if (!prime_ground_claim_is_exclusive(
                program, queries, answers, claim, NULL)) return false;
        (void)snprintf(claim, sizeof claim, "(PRefine %s)", types[type_index]);
        if (!prime_ground_claim_is_exclusive(
                program, queries, answers, claim, NULL)) return false;
    }
    for (size_t value_index = 0u;
         value_index < sizeof values / sizeof values[0]; value_index++) {
        (void)snprintf(claim, sizeof claim, "(PForm %s)", values[value_index]);
        if (!prime_ground_claim_is_exclusive(
                program, queries, answers, claim, NULL)) return false;
        (void)snprintf(claim, sizeof claim, "(PRefine %s)", values[value_index]);
        if (!prime_ground_claim_is_exclusive(
                program, queries, answers, claim, NULL)) return false;
        (void)snprintf(claim, sizeof claim, "(PSynth %s $type)", values[value_index]);
        if (!prime_ground_claim_is_exclusive(
                program, queries, answers, claim, "PEstablished")) return false;
        for (size_t type_index = 0u;
             type_index < sizeof types / sizeof types[0]; type_index++) {
            (void)snprintf(claim, sizeof claim, "(PCheck %s %s)",
                           values[value_index], types[type_index]);
            if (!prime_ground_claim_is_exclusive(
                    program, queries, answers, claim, NULL)) return false;
            (void)snprintf(claim, sizeof claim, "(PAnalyze %s %s)",
                           values[value_index], types[type_index]);
            if (!prime_ground_claim_is_exclusive(
                    program, queries, answers, claim, NULL)) return false;
        }
    }
    for (size_t left = 0u;
         left < sizeof conversion_terms / sizeof conversion_terms[0]; left++) {
        for (size_t right = 0u;
             right < sizeof conversion_terms / sizeof conversion_terms[0]; right++) {
            (void)snprintf(claim, sizeof claim, "(PConvert %s %s)",
                           conversion_terms[left], conversion_terms[right]);
            if (!prime_ground_claim_is_exclusive(
                    program, queries, answers, claim, NULL)) return false;
        }
    }
    return true;
}

static bool prime_ground_synthesis_returns_type(
    const CettaGsltHornProgram *program, Arena *queries, Arena *answers) {
    Atom *query = parse_one(
        queries, "(PrimeJudges (PSynth PVNumber $type) $status)");
    Atom *expected = parse_one(
        queries, "(PrimeJudges (PSynth PVNumber PNumber) PEstablished)");
    CettaGsltHornResult result = {0};
    char error[1024] = {0};
    bool ran = query && expected && cetta_gslt_horn_query(
        program, answers, query, prime_ground_qualification_limits(),
        &result, error, sizeof error);
    bool exact = ran && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
                 result.answer_count == 1u &&
                 atom_eq(result.answers[0], expected);
    if (!exact)
        fprintf(stderr, "Prime synthesis did not return its exact type: %s\n", error);
    cetta_gslt_horn_result_free(&result);
    return exact;
}

int main(int argc, char **argv) {
    if (argc > 2) {
        fprintf(stderr, "usage: %s [PRIME_NATIVE_GROUND_LANGDEF]\n", argv[0]);
        return 2;
    }
    const char *native_ground_path = argc == 2 ? argv[1] :
        "langdef/prime/generated/native_ground_judgments_v1.metta";
    int rc = 1;
    Arena arena;
    Arena scratch;
    Arena horn_queries;
    Arena horn_answers;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable var_intern;
    CettaLibraryContext libraries;
    bool libraries_ready = false;
    CettaGsltHornProgram *native_ground_program = NULL;

    arena_init(&arena);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&scratch);
    arena_init(&horn_queries);
    arena_init(&horn_answers);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_EVAL);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    space_init_with_universe(&space, &universe);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&var_intern);
    g_symbols = &symbols;
    g_var_intern = &var_intern;
    cetta_library_context_init_for_language_profile(
        &libraries, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    libraries_ready = true;
    eval_set_library_context(&libraries);

    const CettaPrimeTypingDirectServiceV1 *typing_service =
        &cetta_prime_typing_direct_service_v1;
    if (!cetta_prime_typing_direct_service_v1_is_valid(typing_service) ||
        typing_service->authority !=
            &cetta_prime_typing_direct_authority_v1 ||
        typing_service->he_typing_core_backend !=
            &cetta_he_typing_core_direct_service_v1) {
        fprintf(stderr, "Prime direct typing authority is invalid\n");
        goto cleanup;
    }
    const CettaNikDirectSourceBindingV1 *typing_source =
        &prime_typing_closed_formation_source_binding_v1;
    if (!cetta_nik_direct_source_binding_v1_is_valid(typing_source) ||
        typing_source->authority != typing_service->authority ||
        strcmp(
            typing_source->semantic_scope,
            "prime.typing.closed-formation") != 0 ||
        strcmp(typing_source->mode, "direct-decision") != 0 ||
        strcmp(typing_source->certificate_policy, "none") != 0 ||
        strcmp(typing_source->fiber, "prime") != 0 ||
        strcmp(typing_source->default_outcome, "PUndetermined") != 0 ||
        typing_source->coverage !=
            CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT) {
        fprintf(stderr, "Prime typing source binding is invalid\n");
        goto cleanup;
    }
    CettaNikDirectSourceBindingV1 invalid_typing_source = *typing_source;
    invalid_typing_source.coverage = 0;
    if (cetta_nik_direct_source_binding_v1_is_valid(
            &invalid_typing_source)) {
        fprintf(stderr, "Prime accepted an invalid typing source binding\n");
        goto cleanup;
    }

    const CettaNikDirectSourceBindingV1 *native_ground_source =
        &prime_typing_native_ground_judgments_source_binding_v1;
    if (!cetta_nik_direct_source_binding_v1_is_valid(native_ground_source) ||
        native_ground_source->authority != typing_service->authority ||
        strcmp(native_ground_source->semantic_scope,
               "prime.typing.native-ground-judgments") != 0 ||
        strcmp(native_ground_source->mode, "direct-decision") != 0 ||
        strcmp(native_ground_source->certificate_policy, "none") != 0 ||
        strcmp(native_ground_source->fiber, "prime") != 0 ||
        strcmp(native_ground_source->default_outcome, "PUndetermined") != 0 ||
        native_ground_source->coverage !=
            CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT) {
        fprintf(stderr, "Prime native-ground source binding is invalid\n");
        goto cleanup;
    }

    const char *native_ground_paths[] = {native_ground_path};
    char native_ground_error[1024] = {0};
    if (!cetta_gslt_horn_program_load_paths(
            native_ground_paths, 1u, &native_ground_program,
            native_ground_error, sizeof native_ground_error) ||
        cetta_gslt_horn_program_rule_count(native_ground_program) != 44u ||
        !prime_ground_synthesis_returns_type(
            native_ground_program, &horn_queries, &horn_answers) ||
        !prime_ground_claim_is_exclusive(
            native_ground_program, &horn_queries, &horn_answers,
            "(PSynth PVNumber PString)", "PUndetermined") ||
        !prime_ground_exclusivity_matrix(
            native_ground_program, &horn_queries, &horn_answers)) {
        fprintf(stderr, "Prime native-ground langdef qualification failed: %s\n",
                native_ground_error);
        goto cleanup;
    }
    CettaNikDirectSourceBindingV1 invalid_native_ground_source =
        *native_ground_source;
    invalid_native_ground_source.semantic_scope = NULL;
    if (cetta_nik_direct_source_binding_v1_is_valid(
            &invalid_native_ground_source)) {
        fprintf(stderr,
                "Prime accepted an invalid native-ground source binding\n");
        goto cleanup;
    }

    const CettaPrimeTypedPublicationDirectServiceV1 *publication_service =
        &cetta_prime_typed_publication_direct_service_v1;
    if (!cetta_prime_typed_publication_direct_service_v1_is_valid(
            publication_service) ||
        publication_service->authority !=
            &cetta_prime_typed_publication_direct_authority_v1) {
        fprintf(stderr, "Prime typed-publication authority is invalid\n");
        goto cleanup;
    }
    const CettaNikDirectSourceBindingV1 *publication_source =
        &prime_typing_typed_publication_core_source_binding_v1;
    if (!cetta_nik_direct_source_binding_v1_is_valid(publication_source) ||
        publication_source->authority != publication_service->authority ||
        strcmp(publication_source->semantic_scope,
               "prime.typing.result-contract-publication-core") != 0 ||
        strcmp(publication_source->mode, "direct-decision") != 0 ||
        strcmp(publication_source->certificate_policy, "none") != 0 ||
        strcmp(publication_source->fiber, "prime") != 0 ||
        strcmp(publication_source->default_outcome, "PUndetermined") != 0 ||
        publication_source->coverage !=
            CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT) {
        fprintf(stderr, "Prime typed-publication source binding is invalid\n");
        goto cleanup;
    }
    Atom *publication_number = parse_one(&scratch, "Number");
    Atom *publication_string = parse_one(&scratch, "String");
    Atom *publication_bool = parse_one(&scratch, "Bool");
    Atom *publication_dynamic = parse_one(&scratch, "%Undefined%");
    Atom *publication_atom = parse_one(&scratch, "Atom");
    Atom *actual_number[1] = {publication_number};
    Atom *alternative_contracts[2] = {
        publication_string, publication_number};
    Atom *refuted_contracts[2] = {
        publication_string, publication_bool};
    Atom *dynamic_contract[1] = {publication_dynamic};
    Atom *top_contract[1] = {publication_atom};
    if (!publication_number || !publication_string || !publication_bool ||
        !publication_dynamic || !publication_atom ||
        !publication_service->accepts_any(
            actual_number, 1u, alternative_contracts, 2u) ||
        publication_service->accepts_any(
            actual_number, 1u, refuted_contracts, 2u) ||
        !publication_service->accepts_any(
            actual_number, 1u, dynamic_contract, 1u) ||
        !publication_service->accepts_any(
            actual_number, 1u, top_contract, 1u) ||
        publication_service->accepts_any(
            NULL, 1u, alternative_contracts, 2u)) {
        fprintf(stderr,
                "Prime typed-publication direct judgments are inconsistent\n");
        goto cleanup;
    }
    CettaPrimeTypedPublicationDirectServiceV1 invalid_publication_service =
        *publication_service;
    invalid_publication_service.accepts_any = NULL;
    if (cetta_prime_typed_publication_direct_service_v1_is_valid(
            &invalid_publication_service)) {
        fprintf(stderr,
                "Prime accepted an incomplete typed-publication service\n");
        goto cleanup;
    }

    if (!expect_direct_typing_status(
            &scratch, &space, typing_service,
            "(Form Number)", "Established") ||
        !expect_direct_typing_status(
            &scratch, &space, typing_service,
            "(Form 7)", "Refuted") ||
        !expect_direct_typing_status(
            &scratch, &space, typing_service,
            "(Synth 7)", "Established") ||
        !expect_direct_typing_status(
            &scratch, &space, typing_service,
            "(Check 7 Number)", "Established") ||
        !expect_direct_typing_status(
            &scratch, &space, typing_service,
            "(Check 7 String)", "Refuted") ||
        !expect_direct_typing_status(
            &scratch, &space, typing_service,
            "(Analyze 7 %Undefined%)", "Established") ||
        !expect_direct_typing_status(
            &scratch, &space, typing_service,
            "(Check 7 %Undefined%)", "Undetermined") ||
        !expect_direct_typing_status(
            &scratch, &space, typing_service,
            "(Convert (+ 1 1) 2)", "Established") ||
        !expect_direct_typing_status(
            &scratch, &space, typing_service,
            "(Convert Number String)", "Refuted") ||
        !expect_direct_typing_status(
            &scratch, &space, typing_service,
            "(Refine Number)", "Established")) {
        goto cleanup;
    }
    Atom *native_form = parse_one(&scratch, "(Form Number)");
    Atom *external_check = parse_one(
        &scratch, "(Check EXTERNAL-AUTHORITY goal proof)");
    Atom *evaluation_claim = parse_one(
        &scratch, "(May (PrimeAnswers Complete) Number)");
    if (typing_service->judge(
            &scratch, &space, external_check, false, 0u) ||
        typing_service->judge(
            &scratch, &space, evaluation_claim, false, 0u) ||
        typing_service->judge(
            &scratch, &space, native_form, true, 0u)) {
        fprintf(stderr,
                "Prime direct typing authority crossed a non-typing boundary\n");
        goto cleanup;
    }
    CettaPrimeTypingDirectServiceV1 invalid_typing_service = *typing_service;
    invalid_typing_service.judge = NULL;
    if (cetta_prime_typing_direct_service_v1_is_valid(
            &invalid_typing_service)) {
        fprintf(stderr, "Prime accepted an incomplete direct typing service\n");
        goto cleanup;
    }
    invalid_typing_service = *typing_service;
    invalid_typing_service.he_typing_core_backend = NULL;
    if (cetta_prime_typing_direct_service_v1_is_valid(
            &invalid_typing_service)) {
        fprintf(stderr, "Prime accepted a missing HE typing backend\n");
        goto cleanup;
    }

    CettaNikDirectAuthorityTokenV1 pure_typing_token;
    if (!cetta_prime_typing_direct_authority_token_v1(
            NULL, 17u, &pure_typing_token) ||
        !cetta_prime_typing_direct_authority_token_v1_is_current(
            &pure_typing_token, NULL, 17u) ||
        cetta_prime_typing_direct_authority_token_v1_is_current(
            &pure_typing_token, NULL, 18u)) {
        fprintf(stderr, "Prime pure typing token did not pin policy identity\n");
        goto cleanup;
    }
    Space token_space;
    space_init_with_universe(&token_space, &universe);
    CettaNikDirectAuthorityTokenV1 mutable_typing_token;
    bool mutable_token_ok =
        cetta_prime_typing_direct_authority_token_v1(
            &token_space, 19u, &mutable_typing_token) &&
        cetta_prime_typing_direct_authority_token_v1_is_current(
            &mutable_typing_token, &token_space, 19u);
    space_add(
        &token_space, atom_symbol(&arena, "prime-typing-token-mutation"));
    bool stale_token_rejected =
        !cetta_prime_typing_direct_authority_token_v1_is_current(
            &mutable_typing_token, &token_space, 19u);
    space_free(&token_space);
    if (!mutable_token_ok || !stale_token_rejected) {
        fprintf(stderr, "Prime typing token did not pin space revision\n");
        goto cleanup;
    }

    Atom *package = prime_semantics_package_atom(&arena);
    if (!package || !prime_semantics_validate_package(package)) {
        fprintf(stderr, "valid PrimeDefV2 package was rejected\n");
        goto cleanup;
    }

    Atom *bad_root = copy_expr_with(
        &arena, package, 0, atom_symbol(&arena, "BrokenPrimeDefV2"));
    if (!bad_root || prime_semantics_validate_package(bad_root)) {
        fprintf(stderr, "broken PrimeDef root was accepted\n");
        goto cleanup;
    }

    Atom *language = package->expr.elems[2];
    Atom *swapped_language = copy_expr_with(
        &arena, language, 3, language->expr.elems[4]);
    if (!swapped_language) goto cleanup;
    swapped_language = copy_expr_with(
        &arena, swapped_language, 4, language->expr.elems[3]);
    Atom *swapped_package = copy_expr_with(&arena, package, 2,
                                           swapped_language);
    if (!swapped_package ||
        prime_semantics_validate_package(swapped_package)) {
        fprintf(stderr, "equations/rewrites swap was accepted\n");
        goto cleanup;
    }

    Atom *binders = language->expr.elems[2];
    Atom *noncanonical_binders = copy_expr_with(
        &arena, binders, 3, atom_symbol(&arena, "NamedOnlyBinders"));
    Atom *noncanonical_language = copy_expr_with(
        &arena, language, 2, noncanonical_binders);
    Atom *noncanonical_package = copy_expr_with(
        &arena, package, 2, noncanonical_language);
    if (!noncanonical_package ||
        prime_semantics_validate_package(noncanonical_package)) {
        fprintf(stderr, "missing canonical-binder capability was accepted\n");
        goto cleanup;
    }

    Atom *checking = package->expr.elems[4];
    Atom *mandatory_graduality = parse_one(
        &arena, "(Gradual MandatoryTyping)");
    Atom *mandatory_checking = copy_expr_with(
        &arena, checking, 1, mandatory_graduality);
    Atom *mandatory_package = copy_expr_with(
        &arena, package, 4, mandatory_checking);
    if (!mandatory_package ||
        prime_semantics_validate_package(mandatory_package)) {
        fprintf(stderr, "mandatory typing replaced Prime graduality\n");
        goto cleanup;
    }

    Atom *catalog = checking->expr.elems[3];
    Atom *wrong_catalog = copy_expr_with(
        &arena, catalog, 1,
        atom_string(
            &arena,
            "0000000000000000000000000000000000000000000000000000000000000000"));
    Atom *wrong_catalog_checking = copy_expr_with(
        &arena, checking, 3, wrong_catalog);
    Atom *wrong_catalog_package = copy_expr_with(
        &arena, package, 4, wrong_catalog_checking);
    if (!wrong_catalog_package ||
        prime_semantics_validate_package(wrong_catalog_package)) {
        fprintf(stderr, "wrong NIK authority catalog identity was accepted\n");
        goto cleanup;
    }

    char nik_error[512] = {0};
    CettaNikRuntimeV1 *nik_runtime = cetta_library_context_nik_runtime(
        &libraries, nik_error, sizeof(nik_error));
    if (!nik_runtime || cetta_nik_runtime_v1_admission_count(nik_runtime) != 0u) {
        fprintf(stderr, "Prime NIK runtime did not begin lazily: %s\n", nik_error);
        goto cleanup;
    }
    const CettaNikAuthorityV1 *dtt = &cetta_prime_nik_authorities_v1[0];
    Atom *dtt_goal = parse_one(&scratch, dtt->positive_goal_metta);
    Atom *dtt_proof = parse_one(&scratch, dtt->positive_proof_metta);
    Atom *other_proof = parse_one(
        &scratch, cetta_prime_nik_authorities_v1[1].positive_proof_metta);
    Atom *prime_judge_head = atom_symbol(&scratch, "prime-judge");
    Atom *dtt_judgment = atom_expr(
        &scratch,
        (Atom *[]){atom_symbol(&scratch, "Check"),
                   atom_symbol(&scratch, dtt->alias),
                   dtt_goal, dtt_proof},
        4u);
    Atom *judge_args[2] = {
        atom_space(&scratch, &space),
        dtt_judgment,
    };
    Atom *established = prime_semantics_dispatch(
        &scratch, prime_judge_head, judge_args, 2u);
    if (!established || established->kind != ATOM_EXPR ||
        established->expr.len < 2u ||
        !atom_is_symbol(established->expr.elems[0], "PrimeVerdict") ||
        !atom_is_symbol(established->expr.elems[1], "Established") ||
        cetta_nik_runtime_v1_admission_count(nik_runtime) != 1u) {
        fprintf(stderr, "Prime did not admit and reuse its NIK authority: ");
        atom_print(established, stderr);
        fprintf(
            stderr, " (admissions=%zu)\n",
            cetta_nik_runtime_v1_admission_count(nik_runtime));
        goto cleanup;
    }
    Atom *dtt_dag_nodes = parse_one(
        &scratch,
        "(LCons (GDNode 0 (GRuleInst \"j\" LNil) LNil) "
        "  (LCons (GDNode 1 (GRuleInst \"z\" LNil) LNil) "
        "    (LCons (GDNode 2 "
        "      (GRuleInst \"k\" "
        "        (LCons (PApp \"i\" LNil) "
        "          (LCons (PApp \"Z\" LNil) "
        "            (LCons (PApp \"N\" LNil) "
        "              (LCons (PApp \"N\" LNil) LNil))))) "
        "      (LCons (GRNode 0) (LCons (GRNode 1) LNil))) LNil)))");
    Atom *dtt_dag_proof = atom_expr(
        &scratch,
        (Atom *[]){atom_symbol(&scratch, "GProofDAG"), atom_int(&scratch, 1),
                   dtt_dag_nodes, atom_int(&scratch, 2), dtt_goal},
        5u);
    Atom *dag_judgment = atom_expr(
        &scratch,
        (Atom *[]){atom_symbol(&scratch, "Check"),
                   atom_symbol(&scratch, dtt->alias),
                   dtt_goal, dtt_dag_proof},
        4u);
    judge_args[1] = dag_judgment;
    Atom *dag_established = prime_semantics_dispatch(
        &scratch, prime_judge_head, judge_args, 2u);
    if (!dag_established || dag_established->kind != ATOM_EXPR ||
        dag_established->expr.len < 2u ||
        !atom_is_symbol(dag_established->expr.elems[0], "PrimeVerdict") ||
        !atom_is_symbol(dag_established->expr.elems[1], "Established") ||
        cetta_nik_runtime_v1_admission_count(nik_runtime) != 1u) {
        fprintf(stderr,
                "Prime did not replay a compact article through its admitted NIK authority\n");
        goto cleanup;
    }
    Atom *wrong_dtt_judgment = atom_expr(
        &scratch,
        (Atom *[]){atom_symbol(&scratch, "Check"),
                   atom_symbol(&scratch, dtt->alias),
                   dtt_goal, other_proof},
        4u);
    judge_args[1] = wrong_dtt_judgment;
    Atom *refuted = prime_semantics_dispatch(
        &scratch, prime_judge_head, judge_args, 2u);
    if (!refuted || refuted->kind != ATOM_EXPR ||
        refuted->expr.len < 2u ||
        !atom_is_symbol(refuted->expr.elems[0], "PrimeVerdict") ||
        !atom_is_symbol(refuted->expr.elems[1], "Refuted") ||
        cetta_nik_runtime_v1_admission_count(nik_runtime) != 1u) {
        fprintf(stderr, "Prime rebuilt or corrupted its admitted NIK authority\n");
        goto cleanup;
    }

    Atom *alpha_x = parse_one(&arena, "(-> (: $x Type) $x)");
    Atom *alpha_y = parse_one(&arena, "(-> (: $renamed Type) $renamed)");
    Atom *alpha_expected = parse_one(&arena, "(Pi Type (idx 0))");
    Atom *alpha_x_canonical = prime_semantics_canonicalize_type(
        &arena, alpha_x);
    Atom *alpha_y_canonical = prime_semantics_canonicalize_type(
        &arena, alpha_y);
    if (!alpha_x_canonical || !alpha_y_canonical || !alpha_expected ||
        !atom_eq(alpha_x_canonical, alpha_expected) ||
        !atom_eq(alpha_y_canonical, alpha_expected)) {
        fprintf(stderr, "alpha-renamed telescopes did not canonicalize alike\n");
        goto cleanup;
    }

    Atom *nested = parse_one(
        &arena, "(-> (: $x Type) (: $y $x) $x)");
    Atom *nested_abt_expected = parse_one(
        &arena, "(Pi Type (Pi (idx 0) (idx 1)))");
    Atom *nested_canonical = prime_semantics_canonicalize_type(
        &arena, nested);
    if (!nested_canonical || !nested_abt_expected ||
        !atom_eq(nested_canonical, nested_abt_expected)) {
        fprintf(stderr, "dependent telescope indices were not canonical\n");
        goto cleanup;
    }

    SymbolId shadow_spelling = symbol_intern_cstr(&symbols, "shadow");
    Atom *outer = atom_var_with_spelling(
        &arena, shadow_spelling, fresh_var_id());
    Atom *inner = atom_var_with_spelling(
        &arena, shadow_spelling, fresh_var_id());
    Atom *typed_outer = atom_expr3(
        &arena, atom_symbol(&arena, ":"), outer,
        atom_symbol(&arena, "Type"));
    Atom *typed_inner = atom_expr3(
        &arena, atom_symbol(&arena, ":"), inner, outer);
    Atom *shadowed = atom_expr(
        &arena,
        (Atom *[]){atom_symbol(&arena, "->"), typed_outer, typed_inner, inner},
        4u);
    Atom *shadowed_expected = parse_one(
        &arena, "(Pi Type (Pi (idx 0) (idx 0)))");
    Atom *shadowed_canonical = prime_semantics_canonicalize_type(
        &arena, shadowed);
    if (!shadowed_canonical || !shadowed_expected ||
        !atom_eq(shadowed_canonical, shadowed_expected)) {
        fprintf(stderr, "same-spelling lexical shadowing captured a name\n");
        goto cleanup;
    }

    Atom *free_same_spelling = atom_var_with_spelling(
        &arena, shadow_spelling, fresh_var_id());
    Atom *capture_attempt = atom_expr3(
        &arena, atom_symbol(&arena, "->"), typed_outer, free_same_spelling);
    if (prime_semantics_canonicalize_type(&arena, capture_attempt)) {
        fprintf(stderr, "free same-spelling variable was captured\n");
        goto cleanup;
    }
    Atom *already_canonical = parse_one(&arena, "(Pi Nat (idx 0))");
    Atom *loose_canonical = parse_one(&arena, "(idx 0)");
    if (prime_semantics_canonicalize_type(&arena, already_canonical) !=
            already_canonical ||
        prime_semantics_canonicalize_type(&arena, loose_canonical)) {
        fprintf(stderr, "canonical scope admission accepted a loose index\n");
        goto cleanup;
    }

    /* `sealed` owns free-metavariable standardization apart, not object
       binding.  Its traversal must preserve canonical ABT indices, preserve
       DAG sharing, reject variable-bearing cycles, and remain stack-safe. */
    Atom *sealed_ignore_none = atom_symbol(&arena, "NoIgnoredVariables");
    Atom *sealed_canonical_mix = parse_one(
        &arena, "(ABTLetScopeV1 (pair (idx 0) $sealed-free))");
    Atom *sealed_canonical_result = rename_vars_except(
        &scratch, sealed_canonical_mix, sealed_ignore_none);
    if (!sealed_canonical_result ||
        sealed_canonical_mix->kind != ATOM_EXPR ||
        sealed_canonical_result->kind != ATOM_EXPR ||
        sealed_canonical_mix->expr.len != 2u ||
        sealed_canonical_result->expr.len != 2u) {
        fprintf(stderr, "sealed canonical-boundary fixture was malformed\n");
        goto cleanup;
    }
    Atom *sealed_pair_before = sealed_canonical_mix->expr.elems[1];
    Atom *sealed_pair_after = sealed_canonical_result->expr.elems[1];
    if (sealed_pair_before->kind != ATOM_EXPR ||
        sealed_pair_after->kind != ATOM_EXPR ||
        sealed_pair_before->expr.len != 3u ||
        sealed_pair_after->expr.len != 3u ||
        sealed_pair_after->expr.elems[1] != sealed_pair_before->expr.elems[1] ||
        sealed_pair_before->expr.elems[2]->kind != ATOM_VAR ||
        sealed_pair_after->expr.elems[2]->kind != ATOM_VAR ||
        sealed_pair_after->expr.elems[2]->var_id ==
            sealed_pair_before->expr.elems[2]->var_id) {
        fprintf(stderr,
                "sealed changed an ABT index or retained a free metavariable\n");
        goto cleanup;
    }

    Atom *shared_free = atom_var(&arena, "sealed-shared");
    Atom *shared_branch = atom_expr2(
        &arena, atom_symbol(&arena, "SharedBranch"), shared_free);
    Atom *shared_root = atom_expr3(
        &arena, atom_symbol(&arena, "SharedPair"),
        shared_branch, shared_branch);
    Atom *shared_result = rename_vars_except(
        &scratch, shared_root, sealed_ignore_none);
    if (!shared_result || shared_result->kind != ATOM_EXPR ||
        shared_result->expr.len != 3u ||
        shared_result->expr.elems[1] == shared_branch ||
        shared_result->expr.elems[1] != shared_result->expr.elems[2]) {
        fprintf(stderr, "sealed did not preserve transformed DAG sharing\n");
        goto cleanup;
    }

    const uint32_t sealed_deep_depth = 4096u;
    Atom *sealed_deep_leaf = atom_var(&arena, "sealed-deep");
    Atom *sealed_deep = sealed_deep_leaf;
    Atom *sealed_deep_head = atom_symbol(&arena, "SealedDeep");
    for (uint32_t i = 0u; i < sealed_deep_depth; i++)
        sealed_deep = atom_expr2(&arena, sealed_deep_head, sealed_deep);
    Atom *sealed_deep_result = rename_vars_except(
        &scratch, sealed_deep, sealed_ignore_none);
    Atom *sealed_cursor = sealed_deep_result;
    for (uint32_t i = 0u; i < sealed_deep_depth; i++) {
        if (!sealed_cursor || sealed_cursor->kind != ATOM_EXPR ||
            sealed_cursor->expr.len != 2u ||
            sealed_cursor->expr.elems[0] != sealed_deep_head) {
            fprintf(stderr, "sealed deep traversal lost structure\n");
            goto cleanup;
        }
        sealed_cursor = sealed_cursor->expr.elems[1];
    }
    if (!sealed_cursor || sealed_cursor->kind != ATOM_VAR ||
        sealed_cursor->var_id == sealed_deep_leaf->var_id) {
        fprintf(stderr, "sealed deep traversal did not freshen its leaf\n");
        goto cleanup;
    }

    Atom *sealed_cycle = atom_expr2(
        &arena, atom_space(&arena, NULL),
        atom_var(&arena, "sealed-cycle"));
    /* The contextual child keeps this test-only node out of hash-consing. */
    sealed_cycle->expr.elems[1] = sealed_cycle;
    if (rename_vars_except(
            &scratch, sealed_cycle, sealed_ignore_none) != NULL) {
        fprintf(stderr, "sealed accepted a cyclic variable-bearing graph\n");
        goto cleanup;
    }

    const uint32_t deep_telescope_arity = 2048u;
    Atom **deep_telescope_items = arena_alloc(
        &arena, sizeof(*deep_telescope_items) *
                    (size_t)(deep_telescope_arity + 2u));
    deep_telescope_items[0] = atom_symbol(&arena, "->");
    Atom *outermost_deep_binder = NULL;
    for (uint32_t i = 0; i < deep_telescope_arity; i++) {
        char name[40];
        snprintf(name, sizeof name, "canonical-depth-%u", i);
        Atom *binder = atom_var(&arena, name);
        if (i == 0u) outermost_deep_binder = binder;
        deep_telescope_items[i + 1u] = atom_expr3(
            &arena, atom_symbol(&arena, ":"), binder,
            atom_symbol(&arena, "Type"));
    }
    deep_telescope_items[deep_telescope_arity + 1u] =
        outermost_deep_binder;
    Atom *deep_telescope = atom_expr(
        &arena, deep_telescope_items, deep_telescope_arity + 2u);
    Atom *deep_canonical = prime_semantics_canonicalize_type(
        &arena, deep_telescope);
    Atom *deep_cursor = deep_canonical;
    for (uint32_t i = 0; i < deep_telescope_arity && deep_cursor; i++) {
        if (deep_cursor->kind != ATOM_EXPR || deep_cursor->expr.len != 3u ||
            !atom_is_symbol(deep_cursor->expr.elems[0], "Pi") ||
            !atom_is_symbol(deep_cursor->expr.elems[1], "Type")) {
            deep_cursor = NULL;
            break;
        }
        deep_cursor = deep_cursor->expr.elems[2];
    }
    Atom *deep_expected_index = parse_one(&arena, "(idx 2047)");
    if (!deep_cursor || !deep_expected_index ||
        !atom_eq(deep_cursor, deep_expected_index)) {
        fprintf(stderr, "deep canonical telescope lost its outer index\n");
        goto cleanup;
    }

    Atom *effects_resources = package->expr.elems[6];
    Atom *resources = effects_resources->expr.elems[2];
    Atom *hidden_default_resources = copy_expr_with(
        &arena, resources, 1,
        atom_symbol(&arena, "ImplicitFiniteStepBound"));
    Atom *hidden_default_effects = copy_expr_with(
        &arena, effects_resources, 2, hidden_default_resources);
    Atom *hidden_default_package = copy_expr_with(
        &arena, package, 6, hidden_default_effects);
    if (!hidden_default_package ||
        prime_semantics_validate_package(hidden_default_package)) {
        fprintf(stderr, "implicit finite resource policy was accepted\n");
        goto cleanup;
    }

    Atom *metered_default_resources = copy_expr_with(
        &arena, resources, 5,
        atom_symbol(&arena, "UnboundedModeMetersSteps"));
    Atom *metered_default_effects = copy_expr_with(
        &arena, effects_resources, 2, metered_default_resources);
    Atom *metered_default_package = copy_expr_with(
        &arena, package, 6, metered_default_effects);
    if (!metered_default_package ||
        prime_semantics_validate_package(metered_default_package)) {
        fprintf(stderr, "unrequested resource accounting was accepted\n");
        goto cleanup;
    }

    Atom *fixed_traversal = parse_one(
        &arena, "(StructuralTypeTraversal Fixed512)");
    Atom *fixed_traversal_resources = copy_expr_with(
        &arena, resources, 6, fixed_traversal);
    Atom *fixed_traversal_effects = copy_expr_with(
        &arena, effects_resources, 2, fixed_traversal_resources);
    Atom *fixed_traversal_package = copy_expr_with(
        &arena, package, 6, fixed_traversal_effects);
    if (!fixed_traversal_package ||
        prime_semantics_validate_package(fixed_traversal_package)) {
        fprintf(stderr, "fixed structural-depth frontier was accepted\n");
        goto cleanup;
    }

    Atom *fixed_type_storage = parse_one(
        &arena, "(TypeStorage Fixed64)");
    Atom *fixed_type_resources = copy_expr_with(
        &arena, resources, 7, fixed_type_storage);
    Atom *fixed_type_effects = copy_expr_with(
        &arena, effects_resources, 2, fixed_type_resources);
    Atom *fixed_type_package = copy_expr_with(
        &arena, package, 6, fixed_type_effects);
    if (!fixed_type_package ||
        prime_semantics_validate_package(fixed_type_package)) {
        fprintf(stderr, "fixed inferred-type frontier was accepted\n");
        goto cleanup;
    }

    Atom *fixed_applicability_storage = parse_one(
        &arena, "(ApplicabilityStorage Fixed64)");
    Atom *fixed_applicability_resources = copy_expr_with(
        &arena, resources, 8, fixed_applicability_storage);
    Atom *fixed_applicability_effects = copy_expr_with(
        &arena, effects_resources, 2, fixed_applicability_resources);
    Atom *fixed_applicability_package = copy_expr_with(
        &arena, package, 6, fixed_applicability_effects);
    if (!fixed_applicability_package ||
        prime_semantics_validate_package(fixed_applicability_package)) {
        fprintf(stderr, "fixed applicability frontier was accepted\n");
        goto cleanup;
    }

    Atom *deep_type = atom_symbol(&arena, "DeepLeaf");
    Atom *deep_head = atom_symbol(&arena, "DeepBox");
    for (uint32_t i = 0; i < 2048u; i++)
        deep_type = atom_expr2(&arena, deep_head, deep_type);
    CettaHeTypingBudget deep_budget;
    he_typing_budget_init_unbounded(&deep_budget);
    Atom *deep_normal = NULL;
    CettaHeNormalizeStatus deep_normal_status =
        he_typing_normalize_type_status_budgeted(
            &arena, &space, deep_type, &deep_budget, &deep_normal);
    if (deep_normal_status != CETTA_HE_NORMALIZE_COMPLETE ||
        deep_normal != deep_type || deep_budget.work_steps_observed != 0 ||
        deep_budget.max_depth_observed != 0) {
        fprintf(stderr, "dynamic type normalization failed above depth 512\n");
        goto cleanup;
    }
    Atom *deep_detail = NULL;
    CettaHeRefinementStatus deep_refinement_status =
        he_typing_check_refinement_status_budgeted(
            &arena, &space, deep_type, &deep_budget, &deep_detail);
    if (deep_refinement_status != CETTA_HE_REFINEMENT_VALID || deep_detail) {
        fprintf(stderr, "dynamic refinement traversal failed above depth 512\n");
        goto cleanup;
    }
    if (deep_budget.work_steps_observed != 0 ||
        deep_budget.max_depth_observed != 0) {
        fprintf(stderr, "unbounded type traversal performed budget accounting\n");
        goto cleanup;
    }

    CettaHeTypingBudget tracked_deep_budget;
    he_typing_budget_init(&tracked_deep_budget, 1);
    deep_normal_status = he_typing_normalize_type_status_budgeted(
        &arena, &space, deep_type, &tracked_deep_budget, &deep_normal);
    if (deep_normal_status != CETTA_HE_NORMALIZE_COMPLETE ||
        tracked_deep_budget.work_steps_observed == 0 ||
        tracked_deep_budget.max_depth_observed < 2048u) {
        fprintf(stderr, "explicit type budget did not enable accounting\n");
        goto cleanup;
    }

    Atom *nested_head = atom_symbol(&arena, "Nested");
    Atom *nested_actual = atom_symbol(&arena, "NestedLeaf");
    Atom *nested_expected = atom_symbol(&arena, "NestedLeaf");
    for (uint32_t i = 0; i < 128u; i++) {
        nested_actual = atom_expr2(&arena, nested_head, nested_actual);
        nested_expected = atom_expr2(&arena, nested_head, nested_expected);
    }
    Atom *arg_name = atom_symbol(&arena, "deep-arg");
    Atom *fn_name = atom_symbol(&arena, "deep-fn");
    Atom *nat_type = atom_symbol(&arena, "Nat");
    space_add(&space, atom_expr3(
        &arena, atom_symbol(&arena, ":"), arg_name, nested_actual));
    space_add(&space, atom_expr3(
        &arena, atom_symbol(&arena, ":"), fn_name,
        atom_expr3(&arena, atom_symbol(&arena, "->"),
                   nested_expected, nat_type)));
    Atom *deep_call = atom_expr2(&arena, fn_name, arg_name);
    Atom **deep_call_types = NULL;
    uint32_t deep_call_type_count = eval_get_atom_types_profiled(
        &space, &arena, deep_call, &deep_call_types);
    bool deep_call_has_nat = false;
    for (uint32_t i = 0; i < deep_call_type_count; i++)
        if (atom_eq(deep_call_types[i], nat_type)) deep_call_has_nat = true;
    free(deep_call_types);
    if (!deep_call_has_nat) {
        fprintf(stderr, "type applicability failed above structural depth 64\n");
        goto cleanup;
    }

    Atom *deep_match_left = atom_symbol(&arena, "DeepMatchLeaf");
    Atom *deep_match_right = atom_symbol(&arena, "DeepMatchLeaf");
    Atom *deep_match_head = atom_symbol(&arena, "DeepMatchBox");
    for (uint32_t i = 0; i < 520u; i++) {
        deep_match_left = atom_expr2(
            &arena, deep_match_head, deep_match_left);
        deep_match_right = atom_expr2(
            &arena, deep_match_head, deep_match_right);
    }
    Bindings deep_match_bindings;
    bindings_init(&deep_match_bindings);
    if (!match_atoms(deep_match_left, deep_match_right,
                     &deep_match_bindings)) {
        fprintf(stderr, "decoded matcher failed above structural depth 64\n");
        bindings_free(&deep_match_bindings);
        goto cleanup;
    }
    bindings_free(&deep_match_bindings);

    Bindings deep_match_base;
    BindingsBuilder deep_match_builder;
    bindings_init(&deep_match_base);
    if (!bindings_builder_init(&deep_match_builder, &deep_match_base) ||
        !match_atoms_builder(deep_match_left, deep_match_right,
                             &deep_match_builder)) {
        fprintf(stderr, "builder matcher failed above structural depth 64\n");
        bindings_builder_free(&deep_match_builder);
        bindings_free(&deep_match_base);
        goto cleanup;
    }
    bindings_builder_free(&deep_match_builder);
    bindings_free(&deep_match_base);

    bindings_init(&deep_match_bindings);
    if (!match_atoms_epoch(deep_match_left, deep_match_right,
                           &deep_match_bindings, &arena, 17u)) {
        fprintf(stderr, "epoch matcher failed above structural depth 64\n");
        bindings_free(&deep_match_bindings);
        goto cleanup;
    }
    bindings_free(&deep_match_bindings);

    Bindings deep_binding_chain;
    bindings_init(&deep_binding_chain);
    Atom *deep_binding_vars[600];
    for (uint32_t i = 0; i < 600u; i++) {
        char name[32];
        snprintf(name, sizeof name, "deep-binding-%u", i);
        deep_binding_vars[i] = atom_var(&arena, name);
    }
    Atom *deep_binding_leaf = atom_symbol(&arena, "DeepBindingLeaf");
    for (uint32_t i = 0; i < 600u; i++) {
        Atom *value = i + 1u < 600u
            ? deep_binding_vars[i + 1u] : deep_binding_leaf;
        if (!bindings_add_var(
                &deep_binding_chain, deep_binding_vars[i], value)) {
            fprintf(stderr, "deep acyclic binding chain was rejected\n");
            bindings_free(&deep_binding_chain);
            goto cleanup;
        }
    }
    Atom *deep_binding_resolved = bindings_resolve_atom_preview(
        &deep_binding_chain, deep_binding_vars[0]);
    if (deep_binding_resolved != deep_binding_leaf ||
        bindings_has_loop(&deep_binding_chain)) {
        fprintf(stderr, "acyclic binding traversal failed above depth 512\n");
        bindings_free(&deep_binding_chain);
        goto cleanup;
    }
    bindings_free(&deep_binding_chain);

    Bindings cyclic_bindings;
    bindings_init(&cyclic_bindings);
    Atom *cycle_left = atom_var(&arena, "cycle-left");
    Atom *cycle_right = atom_var(&arena, "cycle-right");
    Atom *cycle_expr = atom_expr2(
        &arena, atom_symbol(&arena, "Cycle"), cycle_left);
    if (!bindings_add_var(&cyclic_bindings, cycle_left, cycle_right) ||
        !bindings_add_var(&cyclic_bindings, cycle_right, cycle_expr) ||
        !bindings_has_loop(&cyclic_bindings)) {
        fprintf(stderr, "binding cycle was not detected\n");
        bindings_free(&cyclic_bindings);
        goto cleanup;
    }
    if (match_atoms(cycle_expr, cycle_expr, &cyclic_bindings)) {
        fprintf(stderr, "decoded matcher accepted a cyclic finite term\n");
        bindings_free(&cyclic_bindings);
        goto cleanup;
    }
    if (match_atoms_epoch(cycle_expr, cycle_expr, &cyclic_bindings,
                          &arena, 0u)) {
        fprintf(stderr, "epoch matcher accepted a cyclic finite term\n");
        bindings_free(&cyclic_bindings);
        goto cleanup;
    }
    AtomId cycle_expr_id = term_universe_store_atom_id(
        &universe, &arena, cycle_expr);
    if (cycle_expr_id == CETTA_ATOM_ID_NONE ||
        match_atoms_atom_id_epoch(
            cycle_expr, &universe, cycle_expr_id,
            &cyclic_bindings, &arena, 0u)) {
        fprintf(stderr, "store-backed matcher accepted a cyclic finite term\n");
        bindings_free(&cyclic_bindings);
        goto cleanup;
    }
    Atom *unrelated_cycle_probe = atom_symbol(&arena, "UnrelatedCycleProbe");
    if (!match_atoms(unrelated_cycle_probe, unrelated_cycle_probe,
                     &cyclic_bindings)) {
        fprintf(stderr, "unrelated binding cycle rejected a finite match\n");
        bindings_free(&cyclic_bindings);
        goto cleanup;
    }
    bindings_free(&cyclic_bindings);

    Bindings guarded_bindings;
    bindings_init(&guarded_bindings);
    if (bindings_add_id_acyclic(
            &guarded_bindings, cycle_left->var_id,
            cycle_left->sym_id, cycle_expr) ||
        guarded_bindings.len != 0u ||
        bindings_has_loop(&guarded_bindings)) {
        fprintf(stderr, "direct occurs check was not atomic\n");
        bindings_free(&guarded_bindings);
        goto cleanup;
    }
    if (!bindings_add_id_acyclic(
            &guarded_bindings, cycle_left->var_id,
            cycle_left->sym_id, cycle_right) ||
        bindings_add_id_acyclic(
            &guarded_bindings, cycle_right->var_id,
            cycle_right->sym_id, cycle_expr) ||
        bindings_lookup_id(&guarded_bindings, cycle_right->var_id) != NULL ||
        bindings_has_loop(&guarded_bindings)) {
        fprintf(stderr, "indirect occurs check was not atomic\n");
        bindings_free(&guarded_bindings);
        goto cleanup;
    }
    if (!bindings_add_id_acyclic(
            &guarded_bindings, cycle_right->var_id,
            cycle_right->sym_id, deep_binding_leaf) ||
        bindings_resolve_atom_preview(
            &guarded_bindings, cycle_left) != deep_binding_leaf ||
        bindings_has_loop(&guarded_bindings)) {
        fprintf(stderr, "acyclic guarded binding was rejected\n");
        bindings_free(&guarded_bindings);
        goto cleanup;
    }
    bindings_free(&guarded_bindings);

    AtomId deep_match_id = term_universe_store_atom_id(
        &universe, &arena, deep_match_right);
    bindings_init(&deep_match_bindings);
    if (deep_match_id == CETTA_ATOM_ID_NONE ||
        !match_atoms_atom_id_epoch(
            deep_match_left, &universe, deep_match_id,
            &deep_match_bindings, &arena, 19u)) {
        fprintf(stderr, "store-backed matcher failed above structural depth 64\n");
        bindings_free(&deep_match_bindings);
        goto cleanup;
    }
    bindings_free(&deep_match_bindings);

    Atom *deep_search_query = atom_var(&arena, "deep-search-value");
    Atom *deep_search_fact_type = atom_symbol(&arena, "DeepSearchLeaf");
    Atom *deep_search_head = atom_symbol(&arena, "DeepSearchBox");
    for (uint32_t i = 0; i < 520u; i++) {
        deep_search_query = atom_expr2(
            &arena, deep_search_head, deep_search_query);
        deep_search_fact_type = atom_expr2(
            &arena, deep_search_head, deep_search_fact_type);
    }
    space_add(&space, atom_expr3(
        &arena, atom_symbol(&arena, ":"),
        atom_symbol(&arena, "deep-search-proof"), deep_search_fact_type));
    Atom *search_args[4] = {
        atom_space(&arena, &space), deep_search_query,
        atom_int(&arena, 0), atom_int(&arena, 5000000),
    };
    Atom *deep_search_result = he_typing_dispatch(
        &arena, atom_symbol(&arena, "search-first-inhabitant"),
        search_args, 4);
    if (!deep_search_result || deep_search_result->kind != ATOM_EXPR ||
        deep_search_result->expr.len != 2 ||
        !atom_is_symbol(deep_search_result->expr.elems[0], "he-accept")) {
        char *result_text = deep_search_result
            ? atom_to_string(&scratch, deep_search_result) : NULL;
        fprintf(stderr, "typed search failed above structural depth 512: %s\n",
                result_text ? result_text : "<null>");
        goto cleanup;
    }

    Atom *accounting_equation = parse_one(
        &scratch, "(= (prime-accounting-probe) PrimeAccountingDone)");
    Atom *accounting_call = parse_one(&scratch, "(prime-accounting-probe)");
    if (!accounting_equation || !accounting_call) {
        fprintf(stderr, "accounting fixture did not parse\n");
        goto cleanup;
    }
    space_add(&space, accounting_equation);
    EvalOutcome unbounded_outcome;
    eval_outcome_init(&unbounded_outcome);
    metta_eval_outcome(&space, &scratch, NULL, accounting_call, -1,
                       &unbounded_outcome);
    if (unbounded_outcome.completion != CETTA_EVAL_COMPLETE ||
        unbounded_outcome.budget_limited ||
        unbounded_outcome.steps_spent != 0 ||
        unbounded_outcome.results.len == 0) {
        fprintf(stderr, "unbounded evaluation performed budget accounting\n");
        eval_outcome_free(&unbounded_outcome);
        goto cleanup;
    }
    eval_outcome_free(&unbounded_outcome);

    EvalOutcome bounded_outcome;
    eval_outcome_init(&bounded_outcome);
    metta_eval_outcome(&space, &scratch, NULL, accounting_call, 64,
                       &bounded_outcome);
    if (bounded_outcome.completion != CETTA_EVAL_COMPLETE ||
        !bounded_outcome.budget_limited || bounded_outcome.steps_spent == 0 ||
        bounded_outcome.results.len == 0) {
        fprintf(stderr, "explicit evaluation budget did not enable accounting\n");
        eval_outcome_free(&bounded_outcome);
        goto cleanup;
    }
    eval_outcome_free(&bounded_outcome);

    /* Prime's exact report retains sibling fault occurrences upstream of
       every success-only projection.  Faults remain nonzero occurrences, and
       the derived value/fault views preserve their relative source order. */
    Atom *mixed_report_expr = parse_one(
        &scratch,
        "(superpose ((Error PrimeReportSource PrimeReportFault) "
        "  PrimeReportValue))");
    if (!mixed_report_expr) {
        fprintf(stderr, "mixed Prime report fixture did not parse\n");
        goto cleanup;
    }
    EvalOutcome mixed_report;
    eval_outcome_init(&mixed_report);
    metta_eval_outcome(&space, &scratch, NULL, mixed_report_expr, -1,
                       &mixed_report);
    ResultSet mixed_values;
    ResultSet mixed_faults;
    eval_outcome_project_values(&mixed_report, &mixed_values);
    eval_outcome_project_faults(&mixed_report, &mixed_faults);
    if (mixed_report.completion != CETTA_EVAL_COMPLETE ||
        mixed_report.results.len != 2u ||
        eval_outcome_value_count(&mixed_report) != 1u ||
        eval_outcome_fault_count(&mixed_report) != 1u ||
        eval_outcome_zero_status(&mixed_report) != CETTA_EVAL_NONZERO ||
        mixed_values.len != 1u || mixed_faults.len != 1u ||
        !atom_is_symbol(mixed_values.items[0], "PrimeReportValue") ||
        !atom_is_error(mixed_faults.items[0])) {
        fprintf(stderr, "Prime exact report lost or misclassified an occurrence\n");
        result_set_free(&mixed_values);
        result_set_free(&mixed_faults);
        eval_outcome_free(&mixed_report);
        goto cleanup;
    }
    result_set_free(&mixed_values);
    result_set_free(&mixed_faults);
    eval_outcome_free(&mixed_report);

    Atom *report_zero_expr = parse_one(&scratch, "(empty)");
    EvalOutcome report_zero;
    eval_outcome_init(&report_zero);
    metta_eval_outcome(&space, &scratch, NULL, report_zero_expr, -1,
                       &report_zero);
    if (eval_outcome_zero_status(&report_zero) != CETTA_EVAL_ZERO_COMPLETE ||
        eval_outcome_value_count(&report_zero) != 0u ||
        eval_outcome_fault_count(&report_zero) != 0u) {
        fprintf(stderr, "completed Prime zero was not reported exactly\n");
        eval_outcome_free(&report_zero);
        goto cleanup;
    }
    eval_outcome_free(&report_zero);

    /* A valid empty sequence has no head/tail decomposition, while a
       non-sequence argument is malformed.  This keeps known no-solution
       separate from an operational fault after success-bias is removed. */
    Atom *decons_zero_expr = parse_one(&scratch, "(decons-atom ())");
    EvalOutcome decons_zero;
    eval_outcome_init(&decons_zero);
    metta_eval_outcome(&space, &scratch, NULL, decons_zero_expr, -1,
                       &decons_zero);
    if (eval_outcome_zero_status(&decons_zero) !=
            CETTA_EVAL_ZERO_COMPLETE ||
        decons_zero.results.len != 0u) {
        fprintf(stderr, "empty Prime sequence did not decompose to zero\n");
        eval_outcome_free(&decons_zero);
        goto cleanup;
    }
    eval_outcome_free(&decons_zero);

    Atom *decons_fault_expr =
        parse_one(&scratch, "(decons-atom PrimeNotASequence)");
    EvalOutcome decons_fault;
    eval_outcome_init(&decons_fault);
    metta_eval_outcome(&space, &scratch, NULL, decons_fault_expr, -1,
                       &decons_fault);
    if (decons_fault.results.len != 1u ||
        eval_outcome_fault_count(&decons_fault) != 1u ||
        eval_outcome_zero_status(&decons_fault) != CETTA_EVAL_NONZERO) {
        fprintf(stderr, "malformed Prime decons did not remain a fault\n");
        eval_outcome_free(&decons_fault);
        goto cleanup;
    }
    eval_outcome_free(&decons_fault);

#if CETTA_BUILD_WITH_PYTHON
    /* The symbolic Python convenience surface must preserve its occurrence
       bag in Prime.  In particular, zero and many results cannot be encoded
       as the Empty datum and one tuple-shaped datum. */
    Atom *foreign_zero_expr = parse_one(
        &scratch, "(py-call (builtins.list))");
    EvalOutcome foreign_zero;
    eval_outcome_init(&foreign_zero);
    metta_eval_outcome(&space, &scratch, NULL, foreign_zero_expr, -1,
                       &foreign_zero);
    if (foreign_zero.completion != CETTA_EVAL_COMPLETE ||
        foreign_zero.results.len != 0u ||
        eval_outcome_zero_status(&foreign_zero) !=
            CETTA_EVAL_ZERO_COMPLETE) {
        fprintf(stderr, "Prime foreign zero was encoded as a datum\n");
        eval_outcome_free(&foreign_zero);
        goto cleanup;
    }
    eval_outcome_free(&foreign_zero);

    Atom *foreign_many_expr = parse_one(
        &scratch, "(py-call (builtins.divmod 8 3))");
    EvalOutcome foreign_many;
    eval_outcome_init(&foreign_many);
    metta_eval_outcome(&space, &scratch, NULL, foreign_many_expr, -1,
                       &foreign_many);
    if (foreign_many.results.len != 2u ||
        foreign_many.results.items[0]->kind != ATOM_GROUNDED ||
        foreign_many.results.items[0]->ground.gkind != GV_INT ||
        foreign_many.results.items[0]->ground.ival != 2 ||
        foreign_many.results.items[1]->kind != ATOM_GROUNDED ||
        foreign_many.results.items[1]->ground.gkind != GV_INT ||
        foreign_many.results.items[1]->ground.ival != 2) {
        fprintf(stderr, "Prime foreign result bag was tuple-encoded\n");
        eval_outcome_free(&foreign_many);
        goto cleanup;
    }
    eval_outcome_free(&foreign_many);

    Atom *foreign_empty_datum_expr = parse_one(
        &scratch, "(py-call (cetta_bridge.CettaAtom Empty))");
    EvalOutcome foreign_empty_datum;
    eval_outcome_init(&foreign_empty_datum);
    metta_eval_outcome(&space, &scratch, NULL, foreign_empty_datum_expr, -1,
                       &foreign_empty_datum);
    if (foreign_empty_datum.results.len != 1u ||
        !atom_is_empty(foreign_empty_datum.results.items[0])) {
        fprintf(stderr, "Prime foreign Empty datum acquired zero authority\n");
        eval_outcome_free(&foreign_empty_datum);
        goto cleanup;
    }
    eval_outcome_free(&foreign_empty_datum);

    Atom *foreign_nested_expr = parse_one(
        &scratch,
        "(let $item (py-call (builtins.divmod 8 3)) (seen $item))");
    Atom *foreign_nested_expected = parse_one(&scratch, "(seen 2)");
    EvalOutcome foreign_nested;
    eval_outcome_init(&foreign_nested);
    metta_eval_outcome(&space, &scratch, NULL, foreign_nested_expr, -1,
                       &foreign_nested);
    if (foreign_nested.results.len != 2u ||
        !atom_eq(foreign_nested.results.items[0], foreign_nested_expected) ||
        !atom_eq(foreign_nested.results.items[1], foreign_nested_expected)) {
        fprintf(stderr, "Prime foreign occurrences did not stream through let\n");
        eval_outcome_free(&foreign_nested);
        goto cleanup;
    }
    eval_outcome_free(&foreign_nested);
#endif

    /* Prime case's positional fallback observes only a completed-zero
       source.  An empty open prefix must remain incomplete and must never
       manufacture the fallback occurrence. */
    Atom *source_zero_case = parse_one(
        &scratch,
        "(case (empty) (($value UnexpectedValue)) SourceZeroFallback)");
    Atom *source_zero_fallback = atom_symbol(
        &scratch, "SourceZeroFallback");
    if (!source_zero_case) {
        fprintf(stderr, "source-zero case fixture did not parse\n");
        goto cleanup;
    }
    EvalOutcome source_zero_outcome;
    eval_outcome_init(&source_zero_outcome);
    metta_eval_outcome(&space, &scratch, NULL, source_zero_case, -1,
                       &source_zero_outcome);
    if (source_zero_outcome.completion != CETTA_EVAL_COMPLETE ||
        source_zero_outcome.results.len != 1u ||
        !atom_eq(source_zero_outcome.results.items[0],
                 source_zero_fallback)) {
        fprintf(stderr, "completed source zero did not select case fallback\n");
        eval_outcome_free(&source_zero_outcome);
        goto cleanup;
    }
    eval_outcome_free(&source_zero_outcome);

    Atom *open_source_equation = parse_one(
        &scratch, "(= (prime-open-source) (prime-open-source))");
    Atom *open_source_case = parse_one(
        &scratch,
        "(case (prime-open-source) (($value UnexpectedValue)) "
        "  UnexpectedFallback)");
    if (!open_source_equation || !open_source_case) {
        fprintf(stderr, "open-source case fixture did not parse\n");
        goto cleanup;
    }
    space_add(&space, open_source_equation);
    EvalOutcome open_source_outcome;
    eval_outcome_init(&open_source_outcome);
    metta_eval_outcome(&space, &scratch, NULL, open_source_case, 64,
                       &open_source_outcome);
    if (open_source_outcome.completion != CETTA_EVAL_INCOMPLETE_FUEL ||
        open_source_outcome.results.len != 0u ||
        eval_outcome_zero_status(&open_source_outcome) !=
            CETTA_EVAL_ZERO_PENDING) {
        fprintf(stderr,
                "open empty source was laundered into case fallback\n");
        eval_outcome_free(&open_source_outcome);
        goto cleanup;
    }
    eval_outcome_free(&open_source_outcome);

    Atom *chain_accounting = parse_one(
        &scratch,
        "(chain (prime-accounting-probe) $chain-value "
        "  (PrimeChainResult $chain-value))");
    Atom *chain_expected = parse_one(
        &scratch, "(PrimeChainResult PrimeAccountingDone)");
    if (!chain_accounting || !chain_expected) {
        fprintf(stderr, "Prime ABT chain accounting fixture did not parse\n");
        goto cleanup;
    }

    EvalOutcome bounded_chain_outcome;
    eval_outcome_init(&bounded_chain_outcome);
    metta_eval_outcome(&space, &scratch, NULL, chain_accounting, 1,
                       &bounded_chain_outcome);
    if (bounded_chain_outcome.completion != CETTA_EVAL_INCOMPLETE_FUEL ||
        !bounded_chain_outcome.budget_limited ||
        bounded_chain_outcome.steps_spent == 0) {
        fprintf(stderr,
                "Prime ABT chain lost finite-prefix completion accounting\n");
        eval_outcome_free(&bounded_chain_outcome);
        goto cleanup;
    }
    eval_outcome_free(&bounded_chain_outcome);

    EvalOutcome unbounded_chain_outcome;
    eval_outcome_init(&unbounded_chain_outcome);
    metta_eval_outcome(&space, &scratch, NULL, chain_accounting, -1,
                       &unbounded_chain_outcome);
    if (unbounded_chain_outcome.completion != CETTA_EVAL_COMPLETE ||
        unbounded_chain_outcome.budget_limited ||
        unbounded_chain_outcome.results.len != 1 ||
        !atom_eq(unbounded_chain_outcome.results.items[0], chain_expected)) {
        fprintf(stderr,
                "Prime ABT chain changed unbounded result or completion\n");
        eval_outcome_free(&unbounded_chain_outcome);
        goto cleanup;
    }
    eval_outcome_free(&unbounded_chain_outcome);

    Atom *let_accounting = parse_one(
        &scratch,
        "(let $let-value (prime-accounting-probe) "
        "  (PrimeLetResult $let-value))");
    Atom *let_expected = parse_one(
        &scratch, "(PrimeLetResult PrimeAccountingDone)");
    if (!let_accounting || !let_expected) {
        fprintf(stderr, "Prime ABT let accounting fixture did not parse\n");
        goto cleanup;
    }

    EvalOutcome bounded_let_outcome;
    eval_outcome_init(&bounded_let_outcome);
    metta_eval_outcome(&space, &scratch, NULL, let_accounting, 1,
                       &bounded_let_outcome);
    if (bounded_let_outcome.completion != CETTA_EVAL_INCOMPLETE_FUEL ||
        !bounded_let_outcome.budget_limited ||
        bounded_let_outcome.steps_spent == 0) {
        fprintf(stderr,
                "Prime ABT let lost finite-prefix completion accounting\n");
        eval_outcome_free(&bounded_let_outcome);
        goto cleanup;
    }
    eval_outcome_free(&bounded_let_outcome);

    EvalOutcome unbounded_let_outcome;
    eval_outcome_init(&unbounded_let_outcome);
    metta_eval_outcome(&space, &scratch, NULL, let_accounting, -1,
                       &unbounded_let_outcome);
    if (unbounded_let_outcome.completion != CETTA_EVAL_COMPLETE ||
        unbounded_let_outcome.budget_limited ||
        unbounded_let_outcome.results.len != 1 ||
        !atom_eq(unbounded_let_outcome.results.items[0], let_expected)) {
        fprintf(stderr,
                "Prime ABT let changed unbounded result or completion\n");
        eval_outcome_free(&unbounded_let_outcome);
        goto cleanup;
    }
    eval_outcome_free(&unbounded_let_outcome);

    const uint32_t deep_chain_depth = 4096u;
    SymbolId deep_chain_spelling = symbol_intern_cstr(
        &symbols, "prime-deep-chain-binder");
    Atom *deep_chain = atom_symbol(&scratch, "PrimeDeepChainDone");
    Atom *deep_chain_value = atom_symbol(&scratch, "PrimeDeepChainValue");
    for (uint32_t i = 0; i < deep_chain_depth; i++) {
        Atom *binder = atom_var_with_spelling(
            &scratch, deep_chain_spelling, fresh_var_id());
        deep_chain = atom_expr(
            &scratch,
            (Atom *[]){atom_symbol_id(&scratch, g_builtin_syms.chain),
                       deep_chain_value, binder, deep_chain},
            4u);
    }
    Atom *deep_chain_expected = atom_symbol(&scratch, "PrimeDeepChainDone");
    EvalOutcome deep_chain_outcome;
    eval_outcome_init(&deep_chain_outcome);
    metta_eval_outcome(&space, &scratch, NULL, deep_chain, -1,
                       &deep_chain_outcome);
    if (deep_chain_outcome.completion != CETTA_EVAL_COMPLETE ||
        deep_chain_outcome.results.len != 1 ||
        !atom_eq(deep_chain_outcome.results.items[0], deep_chain_expected)) {
        fprintf(stderr,
                "deep Prime ABT chain did not elaborate and evaluate iteratively\n");
        eval_outcome_free(&deep_chain_outcome);
        goto cleanup;
    }
    eval_outcome_free(&deep_chain_outcome);

    const uint32_t deep_let_depth = 4096u;
    SymbolId deep_let_spelling = symbol_intern_cstr(
        &symbols, "prime-deep-let-binder");
    Atom *deep_let_binder = atom_var_with_spelling(
        &scratch, deep_let_spelling, fresh_var_id());
    Atom *deep_let_value = atom_symbol(&scratch, "PrimeDeepLetValue");
    Atom *deep_let = deep_let_binder;
    for (uint32_t i = 0u; i < deep_let_depth; i++)
        deep_let = atom_expr(
            &scratch,
            (Atom *[]){atom_symbol_id(&scratch, g_builtin_syms.let),
                       deep_let_binder, deep_let_value, deep_let},
            4u);
    EvalOutcome deep_let_outcome;
    eval_outcome_init(&deep_let_outcome);
    metta_eval_outcome(&space, &scratch, NULL, deep_let, -1,
                       &deep_let_outcome);
    if (deep_let_outcome.completion != CETTA_EVAL_COMPLETE ||
        deep_let_outcome.results.len != 1 ||
        !atom_eq(deep_let_outcome.results.items[0], deep_let_value)) {
        fprintf(stderr,
                "deep Prime ABT let did not elaborate and evaluate iteratively\n");
        eval_outcome_free(&deep_let_outcome);
        goto cleanup;
    }
    eval_outcome_free(&deep_let_outcome);

    Atom *certificate = parse_one(
        &scratch,
        "(PrimeConversionCertificateV1 "
        "  (Original (+ 1 1) 2) "
        "  (NormalForms 2 2) "
        "  (Relation Equal) "
        "  (Fragment TypePureGroundedFragment))");
    bool equal = false;
    if (!certificate ||
        !prime_semantics_replay_conversion_certificate(
            &scratch, &space, certificate, &equal) || !equal) {
        fprintf(stderr, "valid conversion certificate did not replay\n");
        goto cleanup;
    }

    Atom *bad_normal_forms = copy_expr_with(
        &scratch, certificate->expr.elems[2], 1, atom_int(&scratch, 3));
    Atom *bad_certificate = copy_expr_with(
        &scratch, certificate, 2, bad_normal_forms);
    if (!bad_certificate ||
        prime_semantics_replay_conversion_certificate(
            &scratch, &space, bad_certificate, &equal)) {
        fprintf(stderr, "corrupted conversion certificate replayed\n");
        goto cleanup;
    }

    Atom *user_equation = parse_one(
        &scratch, "(= (user-normal $x) TagA)");
    Atom *user_marker = parse_one(
        &scratch, "(type-level-function user-normal)");
    if (!user_equation || !user_marker) {
        fprintf(stderr, "user-rule fixture did not parse\n");
        goto cleanup;
    }
    space_add(&space, user_equation);
    space_add(&space, user_marker);
    Atom *user_certificate = parse_one(
        &scratch,
        "(PrimeConversionCertificateV1 "
        "  (Original (user-normal q) TagA) "
        "  (NormalForms TagA TagA) "
        "  (Relation Equal) "
        "  (Fragment TypePureGroundedFragment))");
    if (!user_certificate ||
        prime_semantics_replay_conversion_certificate(
            &scratch, &space, user_certificate, &equal)) {
        fprintf(stderr, "ordinary user rule entered conversion replay\n");
        goto cleanup;
    }

    puts("PASS: PrimeDefV2 validation, native-ground exclusivity, and conversion-certificate replay");
    rc = 0;

cleanup:
    cetta_gslt_horn_program_free(native_ground_program);
    eval_set_library_context(NULL);
    if (libraries_ready) cetta_library_context_free(&libraries);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&var_intern);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&horn_answers);
    arena_free(&horn_queries);
    arena_free(&arena);
    return rc;
}
