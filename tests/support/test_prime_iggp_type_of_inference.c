#include "inference_checker.h"
#include "gdl_type_of_host.h"
#include "gdl_type_of_native.h"
#include "parser.h"
#include "prime_typed_flow_boundary.h"
#include "rule_machine.h"
#include "symbol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                             \
    do {                                                                    \
        checks++;                                                           \
        if (!(condition)) {                                                 \
            fprintf(stderr, "FAIL: %s\n", (label));                       \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static bool expr_named(
    const Atom *atom, const char *name, CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
        atom_is_symbol(atom->expr.elems[0], name);
}

static bool grounded_size(const Atom *atom, size_t *value_out) {
    if (!atom || !value_out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0 ||
        (uint64_t)atom->ground.ival > SIZE_MAX)
        return false;
    *value_out = (size_t)atom->ground.ival;
    return true;
}

static bool argument_size(const char *text, size_t *value_out) {
    if (!text || !*text || !value_out) return false;
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed == 0u ||
        parsed > (unsigned long long)SIZE_MAX) {
        return false;
    }
    *value_out = (size_t)parsed;
    return true;
}

static Atom *parse_one(Arena *arena, const char *source) {
    size_t position = 0u;
    Atom *atom = parse_sexpr(arena, source, &position);
    return atom && parser_rest_is_delimiters(source, &position) ? atom : NULL;
}

static Atom *wire_list_item(Atom *list, size_t index) {
    size_t position = 0u;
    Atom *cursor = list;
    while (!atom_is_symbol(cursor, "LNil")) {
        if (!expr_named(cursor, "LCons", 3u))
            return NULL;
        if (position == index)
            return cursor->expr.elems[1];
        position++;
        cursor = cursor->expr.elems[2];
    }
    return NULL;
}

static bool papp_arguments(
    Atom *pattern, const char *head, size_t arity, Atom **arguments_out) {
    if (!expr_named(pattern, "PApp", 3u) ||
        pattern->expr.elems[1]->kind != ATOM_GROUNDED ||
        pattern->expr.elems[1]->ground.gkind != GV_STRING ||
        !pattern->expr.elems[1]->ground.sval ||
        strcmp(pattern->expr.elems[1]->ground.sval, head) != 0)
        return false;
    Atom *arguments = pattern->expr.elems[2];
    size_t count = 0u;
    Atom *cursor = arguments;
    while (!atom_is_symbol(cursor, "LNil")) {
        if (!expr_named(cursor, "LCons", 3u))
            return false;
        count++;
        cursor = cursor->expr.elems[2];
    }
    if (count != arity)
        return false;
    *arguments_out = arguments;
    return true;
}

static Atom *run_artifact(
    Arena *arena, Atom *artifact, Atom *goal,
    int64_t max_states, int64_t max_occurrences) {
    Atom *head = atom_symbol(arena, "compile:run");
    Atom *quoted_goal_items[] = {atom_symbol(arena, "quote"), goal};
    Atom *quoted_goal = atom_expr(arena, quoted_goal_items, 2u);
    Atom *arguments[] = {
        artifact,
        atom_int(arena, 128),
        atom_int(arena, max_states),
        atom_int(arena, max_occurrences),
        quoted_goal,
    };
    return cetta_rule_machine_dispatch(arena, head, arguments, 5u);
}

static bool complete_occurrences(
    Atom *result, Atom *revision, Atom **occurrences_out) {
    Atom *occurrences = expr_named(result, "compile-result", 5u)
        ? result->expr.elems[2]
        : NULL;
    if (!expr_named(result, "compile-result", 5u) ||
        !atom_is_symbol(result->expr.elems[1], "proof-occurrence-bag") ||
        !occurrences || occurrences->kind != ATOM_EXPR ||
        occurrences->expr.len == 0u ||
        !atom_is_symbol(occurrences->expr.elems[0], "occurrences") ||
        !atom_eq(result->expr.elems[4], revision))
        return false;
    *occurrences_out = occurrences;
    return true;
}

static Atom *checked_proof(
    Arena *arena, const CettaInferenceChecker *checker,
    Atom *goal, Atom *occurrence, CettaInferenceReplayStats *stats_out,
    char *error, size_t error_size) {
    if (!expr_named(occurrence, "occurrence", 2u) ||
        !expr_named(occurrence->expr.elems[1], "quote", 2u))
        return NULL;
    Atom *proof = cetta_prime_typed_boundary_splice_explicit_v1(
        arena, occurrence->expr.elems[1]->expr.elems[1]);
    if (!proof || atom_has_vars(proof))
        return NULL;
    CettaInferenceReplayStats stats = {0};
    CettaInferenceStatus status = cetta_inference_checker_check_raw_proof(
        checker, goal, proof, (CettaInferenceReplayLimits){0}, &stats,
        arena, error, error_size);
    if (status != CETTA_INFERENCE_OK)
        return NULL;
    if (stats_out)
        *stats_out = stats;
    return proof;
}

static bool native_proof_bags_equal(
    const CettaGdlTypeOfNativeQueryV1 *left,
    const CettaGdlTypeOfNativeQueryV1 *right) {
    size_t index;
    if (!left || !right || left->kind != right->kind ||
        left->kind != CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1 ||
        left->value.outcome != right->value.outcome ||
        left->proof_count != right->proof_count)
        return false;
    for (index = 0u; index < left->proof_count; index++)
        if (!left->proofs || !right->proofs ||
            !atom_eq(left->proofs[index], right->proofs[index]))
            return false;
    return true;
}

int main(int argc, char **argv) {
    size_t expected_rule_count = 0u;
    size_t expected_case_count = 0u;
    size_t expected_proof_count = 0u;
    if (argc != 11 ||
        !argument_size(argv[8], &expected_rule_count) ||
        !argument_size(argv[9], &expected_case_count) ||
        !argument_size(argv[10], &expected_proof_count) ||
        expected_case_count > UINT32_MAX - 1u) {
        fprintf(
            stderr,
            "usage: %s PROGRAM SOURCE-PACKAGE LABEL SOURCE-DIGEST "
            "PROFILE-DIGEST REVISION SOURCE-REVISION RULES CASES PROOFS\n",
            argv[0]);
        return 2;
    }
    const char *label = argv[3];

    SymbolTable symbols;
    VarInternTable variable_names;
    Arena program_arena;
    Arena source_arena;
    Atom **forms = NULL;
    Atom **source_forms = NULL;
    CettaInferenceChecker *checker = NULL;
    CettaGdlTypeOfNativeV1 *native = NULL;
    CettaGdlTypeOfNativeV1 *source_native = NULL;
    CettaGdlTypeOfNativeV1 *authored_source_native = NULL;
    CettaGdlTypeOfHostV1 *hosted_native = NULL;
    CettaGdlTypeOfHostV1 *refreshed_hosted_native = NULL;
    CettaGdlTypeOfHostCacheV1 *host_cache = NULL;
    Space hosted_space;
    bool hosted_space_initialized = false;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&program_arena);
    arena_init(&source_arena);
    space_init(&hosted_space);
    hosted_space_initialized = true;

    int form_count = parse_metta_file(argv[1], &program_arena, &forms);
    int source_form_count =
        parse_metta_file(argv[2], &source_arena, &source_forms);
    Atom *program = form_count == 1 && forms ? forms[0] : NULL;
    Atom *source_program = source_form_count == 1 && source_forms
        ? source_forms[0]
        : NULL;
    if (form_count != 1)
        fprintf(stderr, "parsed top-level forms: %d\n", form_count);
    if (source_form_count != 1)
        fprintf(stderr, "parsed source-package forms: %d\n", source_form_count);
    CHECK(expr_named(program, "gdl-type-of-inference-v2", 8u),
          "one authority-free GDL inference program parses");
    CHECK(expr_named(source_program, "gdl-type-source-v1", 6u),
          "one compact authored GDL source package parses");
    if (!expr_named(program, "gdl-type-of-inference-v2", 8u) ||
        !expr_named(source_program, "gdl-type-source-v1", 6u))
        goto done;

    Atom *source_field = program->expr.elems[1];
    Atom *profile_field = program->expr.elems[2];
    Atom *revision_field = program->expr.elems[3];
    Atom *presentation_field = program->expr.elems[4];
    Atom *rules = program->expr.elems[5];
    Atom *package_field = program->expr.elems[6];
    Atom *cases = program->expr.elems[7];
    Atom *expected_source = atom_string(&program_arena, argv[4]);
    Atom *expected_profile = atom_string(&program_arena, argv[5]);
    CHECK(expr_named(source_field, "source-digest", 2u) &&
              atom_eq(source_field->expr.elems[1], expected_source) &&
              expr_named(profile_field, "profile-digest", 2u) &&
              atom_eq(profile_field->expr.elems[1], expected_profile),
          "the inference program retains both pinned authored inputs");
    CHECK(expr_named(revision_field, "revision", 2u) &&
              atom_is_symbol(revision_field->expr.elems[1], argv[6]) &&
              expr_named(presentation_field, "presentation", 2u) &&
              expr_named(
                  rules, "rules", (CettaExprLen)expected_rule_count + 1u) &&
              expr_named(package_field, "rule-package", 2u) &&
              expr_named(
                  cases, "cases", (CettaExprLen)expected_case_count + 1u),
          "presentation, rule package, revision, and cases share one program");
    if (!expr_named(revision_field, "revision", 2u) ||
        !expr_named(presentation_field, "presentation", 2u) ||
        !expr_named(
            rules, "rules", (CettaExprLen)expected_rule_count + 1u) ||
        !expr_named(package_field, "rule-package", 2u) ||
        !expr_named(
            cases, "cases", (CettaExprLen)expected_case_count + 1u))
        goto done;

    Atom *revision = revision_field->expr.elems[1];
    Atom *presentation = presentation_field->expr.elems[1];
    Atom *package = package_field->expr.elems[1];
    CettaGdlTypeOfNativeAdmissionV1 native_admission =
        cetta_gdl_type_of_native_admit_v1(
            program, argv[4], argv[5], argv[6],
            (CettaGdlTypeOfNativeLimitsV1){0});
    if (native_admission.kind != CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1)
        fprintf(stderr, "native admission kind: %d\n",
                (int)native_admission.kind);
    native = native_admission.native;
    CHECK(native_admission.kind == CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 &&
              native,
          "the exact GDL structure earns a native type-construction kernel");
    if (!native)
        goto done;
    CettaNikDirectAuthorityTokenV1 native_token;
    CettaGdlTypeOfNativeStatsV1 native_stats;
    CHECK(cetta_nik_direct_authority_v1_is_valid(
              cetta_gdl_type_of_native_authority_v1()) &&
              cetta_gdl_type_of_native_token_v1(native, &native_token) &&
              cetta_gdl_type_of_native_token_is_current_v1(
                  native, &native_token) &&
              cetta_gdl_type_of_native_stats_v1(native, &native_stats) &&
              native_stats.source_nodes != 0u &&
              native_stats.signatures != 0u &&
              native_stats.type_proof_occurrences != 0u &&
              native_stats.constructed_proof_nodes >
                  native_stats.type_proof_occurrences,
          "native construction retains a current NIK identity and proof inventory");

    CettaGdlTypeOfNativeAdmissionV1 source_admission =
        cetta_gdl_type_of_native_admit_source_v1(
            source_program, argv[4], argv[5], argv[7],
            (CettaGdlTypeOfNativeLimitsV1){0});
    if (source_admission.kind != CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1)
        fprintf(stderr, "source-native admission kind: %d\n",
                (int)source_admission.kind);
    source_native = source_admission.native;
    CHECK(source_admission.kind == CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 &&
              source_native,
          "authored GDL and profile derive the native calculus directly in C");
    if (!source_native)
        goto done;
    CettaNikDirectAuthorityTokenV1 source_native_token;
    CettaGdlTypeOfNativeStatsV1 source_native_stats;
    CHECK(cetta_gdl_type_of_native_token_v1(
              source_native, &source_native_token) &&
              cetta_gdl_type_of_native_token_is_current_v1(
                  source_native, &source_native_token) &&
              cetta_gdl_type_of_native_stats_v1(
                  source_native, &source_native_stats) &&
              source_native_stats.source_forms != 0u &&
              source_native_stats.profile_statements != 0u &&
              source_native_stats.typing_components != 0u &&
              source_native_stats.typing_acceptance_constraints != 0u &&
              source_native_stats.source_nodes == native_stats.source_nodes &&
              source_native_stats.signatures == native_stats.signatures &&
              source_native_stats.variable_bindings ==
                  native_stats.variable_bindings &&
              source_native_stats.subtype_edges ==
                  native_stats.subtype_edges &&
              source_native_stats.type_proof_occurrences ==
                  native_stats.type_proof_occurrences &&
              source_native_stats.literal_proof_occurrences ==
                  native_stats.literal_proof_occurrences,
          "direct source inference reconstructs the semantic inventory");
    const char *admitted_source_digest = NULL;
    const char *admitted_profile_digest = NULL;
    const char *admitted_revision = NULL;
    CHECK(cetta_gdl_type_of_native_identity_v1(
              source_native, &admitted_source_digest,
              &admitted_profile_digest, &admitted_revision) &&
              strcmp(admitted_source_digest, argv[4]) == 0 &&
              strcmp(admitted_profile_digest, argv[5]) == 0 &&
              strcmp(admitted_revision, argv[7]) == 0,
          "source-native identity retains exact authored content and revision");
    CettaGdlTypeOfNativeSourceJudgmentV1 source_root = {0};
    bool source_root_found =
        cetta_gdl_type_of_native_source_judgment_v1(
            source_native, 0u, "root", &source_root) ||
        cetta_gdl_type_of_native_source_judgment_v1(
            source_native, 0u, "1", &source_root);
    CHECK(source_root_found &&
              source_root.occurrence && source_root.term &&
              source_root.type && source_root.type_name &&
              source_root.type_proofs && source_root.type_proof_count != 0u &&
              source_root.literal_proofs &&
              source_root.literal_proof_count != 0u,
          "later native calculi can borrow the complete open source-judgment fibre");
    CettaGdlTypeOfNativeSourceJudgmentV1 missing_source = {0};
    CHECK(!cetta_gdl_type_of_native_source_judgment_v1(
              source_native, SIZE_MAX, "missing", &missing_source) &&
              !missing_source.occurrence &&
              missing_source.type_proof_count == 0u &&
              missing_source.literal_proof_count == 0u,
          "an absent source coordinate cannot manufacture a judgment fibre");

    CettaGdlTypeOfNativeAdmissionV1 authored_source_admission =
        cetta_gdl_type_of_native_admit_authored_source_v1(
            source_program, (CettaGdlTypeOfNativeLimitsV1){0});
    authored_source_native = authored_source_admission.native;
    CettaNikDirectAuthorityTokenV1 authored_source_token;
    CettaGdlTypeOfNativeStatsV1 authored_source_stats;
    CHECK(authored_source_admission.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 &&
              authored_source_native &&
              cetta_gdl_type_of_native_token_v1(
                  authored_source_native, &authored_source_token) &&
              cetta_nik_direct_authority_token_v1_equal(
                  &authored_source_token, &source_native_token) &&
              cetta_gdl_type_of_native_stats_v1(
                  authored_source_native, &authored_source_stats) &&
              memcmp(&authored_source_stats, &source_native_stats,
                     sizeof(authored_source_stats)) == 0,
          "content validation admits the same authored calculus without catalog authority");
    if (!authored_source_native)
        goto done;
    Atom *tampered_parts[6];
    for (size_t part = 0u; part < 6u; part++)
        tampered_parts[part] = source_program->expr.elems[part];
    tampered_parts[1] = atom_expr2(
        &program_arena, atom_symbol(&program_arena, "source-digest"),
        atom_string(
            &program_arena,
            "0000000000000000000000000000000000000000000000000000000000000000"));
    Atom *tampered_source = atom_expr(&program_arena, tampered_parts, 6u);
    CettaGdlTypeOfNativeAdmissionV1 tampered_admission =
        cetta_gdl_type_of_native_admit_authored_source_v1(
            tampered_source, (CettaGdlTypeOfNativeLimitsV1){0});
    CHECK(tampered_admission.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1 &&
              !tampered_admission.native,
          "self-declared content identity cannot authorize changed source data");
    cetta_gdl_type_of_native_destroy_v1(tampered_admission.native);

    CettaIndex hosted_package_occurrence = space_length64(&hosted_space);
    CHECK(space_admit_atom_from_source_arena(
              &hosted_space, NULL, &source_arena, source_program) &&
              space_length64(&hosted_space) ==
                  hosted_package_occurrence + 1u,
          "an authored GDL source package enters Space as ordinary data");
    CettaGdlTypeOfHostAdmissionV1 host_admission =
        cetta_gdl_type_of_host_admit_v1(
            &hosted_space, hosted_package_occurrence, argv[7],
            (CettaGdlTypeOfNativeLimitsV1){0});
    hosted_native = host_admission.host;
    CettaGdlTypeOfHostReceiptV1 host_receipt = {0};
    CHECK(host_admission.kind == CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1 &&
              hosted_native &&
              cetta_gdl_type_of_host_is_current_v1(
                  hosted_native, &hosted_space) &&
              cetta_gdl_type_of_host_receipt_v1(
                  hosted_native, &hosted_space, &host_receipt) &&
              host_receipt.package_occurrence ==
                  hosted_package_occurrence &&
              host_receipt.read.instance_id ==
                  space_instance_id(&hosted_space) &&
              host_receipt.read.revision ==
                  space_revision(&hosted_space) &&
              cetta_nik_direct_authority_token_v1_equal(
                  &host_receipt.authority, &authored_source_token),
          "the hosted calculus retains exact Space occurrence, revision, and native identity");
    host_cache = cetta_gdl_type_of_host_cache_create_v1();
    CettaGdlTypeOfHostResolutionV1 first_host_resolution =
        cetta_gdl_type_of_host_cache_resolve_v1(
            host_cache, &hosted_space, hosted_package_occurrence,
            argv[7], (CettaGdlTypeOfNativeLimitsV1){0});
    CettaGdlTypeOfHostResolutionV1 repeated_host_resolution =
        cetta_gdl_type_of_host_cache_resolve_v1(
            host_cache, &hosted_space, hosted_package_occurrence,
            argv[7], (CettaGdlTypeOfNativeLimitsV1){0});
    CHECK(host_cache &&
              first_host_resolution.kind ==
                  CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1 &&
              first_host_resolution.host &&
              !first_host_resolution.cache_hit &&
              repeated_host_resolution.kind ==
                  CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1 &&
              repeated_host_resolution.host ==
                  first_host_resolution.host &&
              repeated_host_resolution.cache_hit,
          "an episode cache reuses only the exact current hosted context");
    CettaGdlTypeOfHostAdmissionV1 wrong_host_revision =
        cetta_gdl_type_of_host_admit_v1(
            &hosted_space, hosted_package_occurrence,
            "gdl-type-source-wrong-revision",
            (CettaGdlTypeOfNativeLimitsV1){0});
    CHECK(wrong_host_revision.kind ==
                  CETTA_GDL_TYPE_OF_HOST_OUTSIDE_FRAGMENT_V1 &&
              !wrong_host_revision.host,
          "a request cannot borrow authority from a different source revision");
    cetta_gdl_type_of_host_destroy_v1(wrong_host_revision.host);
    CettaGdlTypeOfHostAdmissionV1 bounded_host_admission =
        cetta_gdl_type_of_host_admit_v1(
            &hosted_space, hosted_package_occurrence, argv[7],
            (CettaGdlTypeOfNativeLimitsV1){.max_source_nodes = 1u});
    CHECK(bounded_host_admission.kind ==
                  CETTA_GDL_TYPE_OF_HOST_INCOMPLETE_V1 &&
              !bounded_host_admission.host,
          "bounded hosted admission remains Incomplete rather than refuting source data");
    cetta_gdl_type_of_host_destroy_v1(bounded_host_admission.host);
    char error[1024] = {0};
    CettaInferenceStatus create_status = cetta_inference_checker_create(
        presentation, &checker, error, sizeof(error));
    if (create_status != CETTA_INFERENCE_OK)
        fprintf(stderr, "checker admission: %s\n", error);
    CHECK(create_status == CETTA_INFERENCE_OK && checker &&
              cetta_inference_checker_rule_count(checker) == 0u,
          "the generic checker admits the shared declarations once");
    if (!checker)
        goto done;
    bool all_rules_admitted = true;
    for (CettaExprIndex index = 1u; index < rules->expr.len; index++) {
        CettaInferenceRuleHandle handle = CETTA_INFERENCE_RULE_HANDLE_NONE;
        error[0] = '\0';
        if (cetta_inference_checker_add_rule(
                checker, rules->expr.elems[index], &handle,
                error, sizeof(error)) != CETTA_INFERENCE_OK ||
            handle != (CettaInferenceRuleHandle)(index - 1u)) {
            fprintf(stderr, "rule %llu admission: %s\n",
                    (unsigned long long)index, error);
            all_rules_admitted = false;
            break;
        }
    }
    CHECK(all_rules_admitted &&
              cetta_inference_checker_rule_count(checker) ==
                  expected_rule_count,
          "the generic checker incrementally admits the shared flat rules");
    if (!all_rules_admitted)
        goto done;

    Atom *compile_head = atom_symbol(&program_arena, "compile:rule-package");
    Atom *compile_arguments[] = {revision, package};
    Atom *artifact = cetta_rule_machine_dispatch(
        &program_arena, compile_head, compile_arguments, 2u);
    CHECK(expr_named(artifact, "compiled-artifact", 6u) &&
              atom_eq(artifact->expr.elems[2], revision),
          "RuleMachine compiles the same rule inventory at the pinned revision");
    if (!expr_named(artifact, "compiled-artifact", 6u))
        goto done;

    size_t checked_cases = 0u;
    size_t checked_occurrences = 0u;
    size_t replay_nodes = 0u;
    size_t declared_occurrences = 0u;
    Atom *multiplicity_case = NULL;
    size_t multiplicity_expected = 0u;
    for (CettaExprIndex index = 1u; index < cases->expr.len; index++) {
        Atom *test_case = cases->expr.elems[index];
        size_t expected = 0u;
        if (!expr_named(test_case, "case", 3u) ||
            !grounded_size(test_case->expr.elems[1], &expected) ||
            expected == 0u || expected > INT64_MAX - 1u) {
            CHECK(false, "every generated inference case is well formed");
            continue;
        }
        if (SIZE_MAX - declared_occurrences < expected) {
            CHECK(false, "declared proof occurrence count fits native size");
            continue;
        }
        declared_occurrences += expected;
        if (!multiplicity_case && expected > 1u) {
            multiplicity_case = test_case;
            multiplicity_expected = expected;
        }

        CettaGdlTypeOfNativeQueryV1 native_query =
            cetta_gdl_type_of_native_serve_v1(
                native, &native_token, test_case->expr.elems[2], 0u);
        CettaGdlTypeOfNativeQueryV1 source_native_query =
            cetta_gdl_type_of_native_serve_v1(
                source_native, &source_native_token,
                test_case->expr.elems[2], 0u);
        CHECK(native_query.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1 &&
                  native_query.value.outcome ==
                      CETTA_NIK_OUTCOME_ESTABLISHED &&
                  native_query.selection.kind ==
                      CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
                  native_query.selection.eligible_count == 1u &&
                  native_query.selection.frontier_count == 1u &&
                  native_query.selection.greatest_index == 0u &&
                  native_query.selected_realization_identity ==
                      cetta_gdl_type_of_native_authority_v1()
                          ->realization_identity &&
                  native_query.proof_count == expected &&
                  native_query.proofs,
              "request-local selection serves the exact native proof fibre");
        CHECK(native_proof_bags_equal(
                  &source_native_query, &native_query),
              "authored-source inference reconstructs the exact ordered proof fibre");
        if (index == 1u) {
            Atom *arguments = NULL;
            Atom *occurrence = NULL;
            Atom *term = NULL;
            Atom *expected_type = NULL;
            CettaGdlTypeOfNativeQueryV1 synthesized = {0};
            bool exact_type_case = papp_arguments(
                test_case->expr.elems[2], "type:of", 3u, &arguments);
            if (exact_type_case) {
                occurrence = wire_list_item(arguments, 0u);
                term = wire_list_item(arguments, 1u);
                expected_type = wire_list_item(arguments, 2u);
                synthesized = cetta_gdl_type_of_native_synthesize_v1(
                    authored_source_native, &authored_source_token,
                    occurrence, term, 0u);
            }
            CHECK(exact_type_case && occurrence && term && expected_type &&
                      synthesized.kind ==
                          CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1 &&
                      synthesized.value.outcome ==
                          CETTA_NIK_OUTCOME_ESTABLISHED &&
                      synthesized.selection.kind ==
                          CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
                      synthesized.type &&
                      atom_eq(synthesized.type, expected_type) &&
                      native_proof_bags_equal(
                          &synthesized, &native_query),
                  "occurrence-indexed synthesis returns the exact type and proof fibre");
            CettaGdlTypeOfHostQueryV1 hosted_synthesized =
                exact_type_case
                    ? cetta_gdl_type_of_host_synthesize_v1(
                          hosted_native, &hosted_space,
                          occurrence, term, 0u)
                    : (CettaGdlTypeOfHostQueryV1){0};
            CHECK(exact_type_case && hosted_native &&
                      hosted_synthesized.native.kind ==
                          CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1 &&
                      hosted_synthesized.native.value.outcome ==
                          CETTA_NIK_OUTCOME_ESTABLISHED &&
                      hosted_synthesized.native.selection.kind ==
                          CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
                      hosted_synthesized.native.type &&
                      atom_eq(
                          hosted_synthesized.native.type,
                          expected_type) &&
                      native_proof_bags_equal(
                          &hosted_synthesized.native,
                          &native_query) &&
                      space_read_token_matches_live_space(
                          hosted_synthesized.receipt.read,
                          &hosted_space),
                  "Space-hosted synthesis selects the exact native proof constructor");
            CettaGdlTypeOfHostQueryV1 cached_synthesized =
                exact_type_case && repeated_host_resolution.host
                    ? cetta_gdl_type_of_host_synthesize_v1(
                          repeated_host_resolution.host, &hosted_space,
                          occurrence, term, 0u)
                    : (CettaGdlTypeOfHostQueryV1){0};
            CHECK(cached_synthesized.native.kind ==
                      CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1 &&
                      cached_synthesized.native.value.outcome ==
                          CETTA_NIK_OUTCOME_ESTABLISHED &&
                      native_proof_bags_equal(
                          &cached_synthesized.native, &native_query),
                  "cached hosting preserves the exact native type/proof fibre");
        }

        Arena run_arena;
        arena_init(&run_arena);
        Atom *result = run_artifact(
            &run_arena, artifact, test_case->expr.elems[2],
            2000000, (int64_t)expected + 1);
        Atom *occurrences = NULL;
        bool complete = complete_occurrences(result, revision, &occurrences);
        size_t observed = complete ? (size_t)occurrences->expr.len - 1u : 0u;
        if (!complete || observed != expected) {
            fprintf(stderr, "case %llu expected %zu occurrences; result=",
                    (unsigned long long)index, expected);
            if (result)
                atom_print(result, stderr);
            else
                fputs("<missing>", stderr);
            fputc('\n', stderr);
            CHECK(false, "relational search returns the exact proof bag");
            arena_free(&run_arena);
            continue;
        }

        bool case_ok = true;
        for (CettaExprIndex occurrence_index = 1u;
             occurrence_index < occurrences->expr.len;
             occurrence_index++) {
            CettaInferenceReplayStats stats = {0};
            error[0] = '\0';
            Atom *proof = checked_proof(
                &run_arena, checker, test_case->expr.elems[2],
                occurrences->expr.elems[occurrence_index], &stats,
                error, sizeof(error));
            if (!proof) {
                fprintf(stderr, "case %llu proof replay: %s\n",
                        (unsigned long long)index, error);
                case_ok = false;
                break;
            }
            if (!native_query.proofs ||
                occurrence_index - 1u >= native_query.proof_count ||
                !atom_eq(
                    proof,
                    native_query.proofs[occurrence_index - 1u])) {
                fprintf(stderr,
                        "case %llu native proof occurrence order diverged\n",
                        (unsigned long long)index);
                case_ok = false;
                break;
            }
            checked_occurrences++;
            replay_nodes += stats.nodes;
        }
        CHECK(case_ok, "every proposed proof is accepted by generic replay");
        if (case_ok)
            checked_cases++;
        arena_free(&run_arena);
    }
    CHECK(declared_occurrences == expected_proof_count &&
              checked_cases == expected_case_count &&
              checked_occurrences == expected_proof_count &&
              replay_nodes > checked_occurrences,
          "all exact type:of and literal judgments retain checked derivations");
    if (multiplicity_case) {
        CettaGdlTypeOfNativeQueryV1 bounded_native =
            cetta_gdl_type_of_native_serve_v1(
                native, &native_token, multiplicity_case->expr.elems[2],
                multiplicity_expected - 1u);
        CettaGdlTypeOfNativeQueryV1 bounded_source_native =
            cetta_gdl_type_of_native_serve_v1(
                source_native, &source_native_token,
                multiplicity_case->expr.elems[2],
                multiplicity_expected - 1u);
        CHECK(bounded_native.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1 &&
                  bounded_native.value.outcome ==
                      CETTA_NIK_OUTCOME_INCOMPLETE &&
                  bounded_native.proof_count == 0u,
              "bounded native publication preserves multiplicity as Incomplete");
        CHECK(native_proof_bags_equal(
                  &bounded_source_native, &bounded_native),
              "source-derived publication preserves the same incomplete frontier");
    }

    Atom *first_case = cases->expr.elems[1];
    Atom *second_case = cases->expr.elems[2];
    Arena negative_arena;
    arena_init(&negative_arena);
    size_t first_expected = 0u;
    bool first_expected_ok =
        grounded_size(first_case->expr.elems[1], &first_expected) &&
        first_expected > 0u && first_expected < INT64_MAX;
    Atom *first_result = first_expected_ok
        ? run_artifact(
              &negative_arena, artifact, first_case->expr.elems[2], 2000000,
              (int64_t)first_expected + 1)
        : NULL;
    Atom *first_occurrences = NULL;
    CHECK(complete_occurrences(first_result, revision, &first_occurrences) &&
              first_occurrences->expr.len == first_expected + 1u,
          "negative controls begin from a genuine exact proof bag");
    Atom *first_proof = first_occurrences && first_occurrences->expr.len > 1u &&
            expr_named(first_occurrences->expr.elems[1], "occurrence", 2u) &&
            expr_named(first_occurrences->expr.elems[1]->expr.elems[1],
                       "quote", 2u)
        ? cetta_prime_typed_boundary_splice_explicit_v1(
              &negative_arena,
              first_occurrences->expr.elems[1]->expr.elems[1]->expr.elems[1])
        : NULL;
    error[0] = '\0';
    CHECK(first_proof &&
              cetta_inference_checker_check_raw_proof(
                  checker, second_case->expr.elems[2], first_proof,
                  (CettaInferenceReplayLimits){0}, NULL,
                  &negative_arena, error, sizeof(error)) ==
                  CETTA_INFERENCE_FINAL_MISMATCH,
          "an exact proof occurrence cannot be rebound to another source node");
    CettaGdlTypeOfNativeQueryV1 rebound_native =
        cetta_gdl_type_of_native_serve_v1(
            native, &native_token, second_case->expr.elems[2], 0u);
    CHECK(rebound_native.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1 &&
              rebound_native.value.outcome == CETTA_NIK_OUTCOME_ESTABLISHED,
          "the native calculus independently reconstructs the second source fibre");
    Atom *unknown_proof = parse_one(
        &negative_arena,
        "(GProof (GRuleInst \"gdl:unknown\" LNil) PrNil)");
    error[0] = '\0';
    CHECK(unknown_proof &&
              cetta_inference_checker_check_raw_proof(
                  checker, first_case->expr.elems[2], unknown_proof,
                  (CettaInferenceReplayLimits){0}, NULL,
                  &negative_arena, error, sizeof(error)) ==
                  CETTA_INFERENCE_UNKNOWN_RULE,
          "an unauthored proof constructor cannot mint a type judgment");

    Atom *incomplete = run_artifact(
        &negative_arena, artifact, first_case->expr.elems[2], 1, 2);
    CHECK(expr_named(incomplete, "compile-incomplete", 6u) &&
              atom_is_symbol(incomplete->expr.elems[1], "state-limit") &&
              atom_eq(incomplete->expr.elems[5], revision),
          "bounded proof search reports Incomplete rather than rejection");
    Atom *unsupported_goal = parse_one(
        &negative_arena,
        "(PApp \"type:of\" "
        " (LCons (PApp \"gdl:occurrence:unsupported\" LNil) "
        "  (LCons (PApp \"gdl:application\" "
        "    (LCons (PApp \"gdl:name:00\" LNil) "
        "      (LCons (PApp \"gdl:nil\" LNil) LNil))) "
        "   (LCons (PApp \"gdl:type:00\" LNil) LNil))))");
    Atom *unsupported_result = unsupported_goal
        ? run_artifact(
              &negative_arena, artifact, unsupported_goal, 2000000, 2)
        : NULL;
    Atom *unsupported_occurrences = NULL;
    CHECK(unsupported_goal &&
              complete_occurrences(
                  unsupported_result, revision, &unsupported_occurrences) &&
              unsupported_occurrences->expr.len == 1u,
          "unsupported source data produces no certificate and no false proof");
    CettaGdlTypeOfNativeQueryV1 unsupported_native =
        cetta_gdl_type_of_native_serve_v1(
            native, &native_token, unsupported_goal, 0u);
    CHECK(unsupported_native.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1 &&
              unsupported_native.value.outcome ==
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT &&
              unsupported_native.proof_count == 0u,
          "unsupported source data remains outside the native fragment");
    CettaNikDirectAuthorityTokenV1 stale_native_token = native_token;
    stale_native_token.words[stale_native_token.length - 1u] ^= UINT64_C(1);
    CettaGdlTypeOfNativeQueryV1 stale_native =
        cetta_gdl_type_of_native_serve_v1(
            native, &stale_native_token, first_case->expr.elems[2], 0u);
    CHECK(stale_native.kind == CETTA_GDL_TYPE_OF_NATIVE_QUERY_STALE_V1 &&
              stale_native.selection.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1 &&
              stale_native.selected_realization_identity == 0u &&
              stale_native.proof_count == 0u,
          "a stale native admission deoptimizes without a semantic verdict");
    CettaNikDirectAuthorityTokenV1 stale_source_token = source_native_token;
    stale_source_token.words[stale_source_token.length - 1u] ^= UINT64_C(1);
    CettaGdlTypeOfNativeQueryV1 stale_source_native =
        cetta_gdl_type_of_native_serve_v1(
            source_native, &stale_source_token,
            first_case->expr.elems[2], 0u);
    CHECK(stale_source_native.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_QUERY_STALE_V1 &&
              stale_source_native.proof_count == 0u,
          "a stale source-derived admission deoptimizes without a verdict");
    CettaGdlTypeOfNativeAdmissionV1 mismatched_pin =
        cetta_gdl_type_of_native_admit_v1(
            program,
            "0000000000000000000000000000000000000000000000000000000000000000",
            argv[5], argv[6], (CettaGdlTypeOfNativeLimitsV1){0});
    CHECK(mismatched_pin.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1 &&
              mismatched_pin.native == NULL,
          "a mismatched authored-source pin cannot earn native admission");
    CettaGdlTypeOfNativeAdmissionV1 mismatched_source_pin =
        cetta_gdl_type_of_native_admit_source_v1(
            source_program,
            "0000000000000000000000000000000000000000000000000000000000000000",
            argv[5], argv[7], (CettaGdlTypeOfNativeLimitsV1){0});
    CHECK(mismatched_source_pin.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1 &&
              mismatched_source_pin.native == NULL,
          "a mismatched raw-source pin cannot earn native admission");
    Atom *malformed_profile_program = parse_one(
        &negative_arena,
        "(gdl-type-source-v1 "
        "(source-digest "
        "\"05a88bd547e35ae22579222130629622095aa53e137e7a8807c15cbb7b26a86e\") "
        "(profile-digest "
        "\"2c84adaa62b241af53e8b3e40758f187e7a883770de6245d5c1fbaad63254cf2\") "
        "(revision "
        "gdl-type-source-603bb58a675cebfb6fee4179b070eb446d19384b5f5d05f45cc71631f6252741) "
        "(source-text \"(foo)\\n\") "
        "(profile-text \"foo :: bool\\n\"))");
    CettaGdlTypeOfNativeAdmissionV1 malformed_profile_admission =
        cetta_gdl_type_of_native_admit_source_v1(
            malformed_profile_program,
            "05a88bd547e35ae22579222130629622095aa53e137e7a8807c15cbb7b26a86e",
            "2c84adaa62b241af53e8b3e40758f187e7a883770de6245d5c1fbaad63254cf2",
            "gdl-type-source-603bb58a675cebfb6fee4179b070eb446d19384b5f5d05f45cc71631f6252741",
            (CettaGdlTypeOfNativeLimitsV1){0});
    CHECK(malformed_profile_admission.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1 &&
              malformed_profile_admission.native == NULL,
          "a pinned but malformed typing profile remains outside the calculus");
    Atom *ill_typed_source_program = parse_one(
        &negative_arena,
        "(gdl-type-source-v1 "
        "(source-digest "
        "\"05a88bd547e35ae22579222130629622095aa53e137e7a8807c15cbb7b26a86e\") "
        "(profile-digest "
        "\"e868efd704ba1f703cdba0a9622471099f85e7016b50d9feb3dee93eda318f81\") "
        "(revision "
        "gdl-type-source-d36197f899c401d82a22acd75cb3db5b9a07feec33903bf4aca8ae85f8ca1db9) "
        "(source-text \"(foo)\\n\") "
        "(profile-text \"foo :: int.\\n\"))");
    CettaGdlTypeOfNativeAdmissionV1 ill_typed_source_admission =
        cetta_gdl_type_of_native_admit_source_v1(
            ill_typed_source_program,
            "05a88bd547e35ae22579222130629622095aa53e137e7a8807c15cbb7b26a86e",
            "e868efd704ba1f703cdba0a9622471099f85e7016b50d9feb3dee93eda318f81",
            "gdl-type-source-d36197f899c401d82a22acd75cb3db5b9a07feec33903bf4aca8ae85f8ca1db9",
            (CettaGdlTypeOfNativeLimitsV1){0});
    CHECK(ill_typed_source_admission.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1 &&
              ill_typed_source_admission.native == NULL,
          "a checked literal-type obstruction cannot mint native admission");
    CettaGdlTypeOfNativeAdmissionV1 bounded_admission =
        cetta_gdl_type_of_native_admit_v1(
            program, argv[4], argv[5], argv[6],
            (CettaGdlTypeOfNativeLimitsV1){.max_source_nodes = 1u});
    CHECK(bounded_admission.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_INCOMPLETE_V1 &&
              bounded_admission.native == NULL,
          "bounded native admission reports Incomplete rather than rejection");
    CettaGdlTypeOfNativeAdmissionV1 bounded_source_admission =
        cetta_gdl_type_of_native_admit_source_v1(
            source_program, argv[4], argv[5], argv[7],
            (CettaGdlTypeOfNativeLimitsV1){.max_source_nodes = 1u});
    CHECK(bounded_source_admission.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_INCOMPLETE_V1 &&
              bounded_source_admission.native == NULL,
          "bounded source inference reports Incomplete rather than rejection");

    Atom *first_arguments = NULL;
    Atom *first_occurrence = NULL;
    Atom *first_term = NULL;
    Atom *hosted_first_case = cases->expr.elems[1];
    if (expr_named(hosted_first_case, "case", 3u) &&
        papp_arguments(
            hosted_first_case->expr.elems[2], "type:of", 3u,
            &first_arguments)) {
        first_occurrence = wire_list_item(first_arguments, 0u);
        first_term = wire_list_item(first_arguments, 1u);
    }
    SpaceReadToken hosted_read_before = space_read_token(&hosted_space);
    space_add(
        &hosted_space,
        atom_symbol(&program_arena, "gdl-host-revision-change"));
    CettaGdlTypeOfHostQueryV1 stale_host_query =
        cetta_gdl_type_of_host_synthesize_v1(
            hosted_native, &hosted_space,
            first_occurrence, first_term, 0u);
    CHECK(first_occurrence && first_term &&
              !space_read_token_matches_live_space(
                  hosted_read_before, &hosted_space) &&
              !cetta_gdl_type_of_host_is_current_v1(
                  hosted_native, &hosted_space) &&
              stale_host_query.native.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_QUERY_STALE_V1 &&
              stale_host_query.native.selection.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
          "a changed hosting Space deauthorizes the old native context without refutation");
    CettaGdlTypeOfHostAdmissionV1 refreshed_host_admission =
        cetta_gdl_type_of_host_admit_v1(
            &hosted_space, hosted_package_occurrence, argv[7],
            (CettaGdlTypeOfNativeLimitsV1){0});
    refreshed_hosted_native = refreshed_host_admission.host;
    CettaGdlTypeOfHostQueryV1 refreshed_host_query =
        cetta_gdl_type_of_host_synthesize_v1(
            refreshed_hosted_native, &hosted_space,
            first_occurrence, first_term, 0u);
    CHECK(refreshed_host_admission.kind ==
                  CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1 &&
              refreshed_hosted_native &&
              refreshed_host_query.native.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1 &&
              refreshed_host_query.native.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              refreshed_host_query.native.selection.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
              space_read_token_matches_live_space(
                  refreshed_host_query.receipt.read, &hosted_space),
          "the unchanged authored package may be re-admitted at the new Space revision");
    CettaGdlTypeOfHostResolutionV1 refreshed_cached_resolution =
        cetta_gdl_type_of_host_cache_resolve_v1(
            host_cache, &hosted_space, hosted_package_occurrence,
            argv[7], (CettaGdlTypeOfNativeLimitsV1){0});
    CHECK(refreshed_cached_resolution.kind ==
                  CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1 &&
              refreshed_cached_resolution.host &&
              !refreshed_cached_resolution.cache_hit &&
              cetta_gdl_type_of_host_is_current_v1(
                  refreshed_cached_resolution.host, &hosted_space),
          "a stale episode-cache entry is replaced rather than revived");

    Atom **reordered_rule_items = arena_alloc(
        &negative_arena, (size_t)rules->expr.len * sizeof(Atom *));
    memcpy(reordered_rule_items, rules->expr.elems,
           (size_t)rules->expr.len * sizeof(Atom *));
    Atom *temporary = reordered_rule_items[15u];
    reordered_rule_items[15u] = reordered_rule_items[16u];
    reordered_rule_items[16u] = temporary;
    Atom *reordered_rules = atom_expr(
        &negative_arena, reordered_rule_items, rules->expr.len);
    Atom *reordered_program_items[8];
    memcpy(reordered_program_items, program->expr.elems,
           sizeof(reordered_program_items));
    reordered_program_items[5] = reordered_rules;
    Atom *reordered_program = atom_expr(
        &negative_arena, reordered_program_items, 8u);
    CettaGdlTypeOfNativeAdmissionV1 reordered_admission =
        cetta_gdl_type_of_native_admit_v1(
            reordered_program, argv[4], argv[5], argv[6],
            (CettaGdlTypeOfNativeLimitsV1){0});
    CettaNikDirectAuthorityTokenV1 reordered_token;
    CHECK(reordered_admission.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 &&
              reordered_admission.native &&
              cetta_gdl_type_of_native_token_v1(
                  reordered_admission.native, &reordered_token) &&
              !cetta_nik_direct_authority_token_v1_equal(
                  &native_token, &reordered_token),
          "native currentness identity binds the exact admitted rule document");
    cetta_gdl_type_of_native_destroy_v1(reordered_admission.native);

    reordered_rule_items[1u] = reordered_rule_items[2u];
    Atom *wrong_core_rules = atom_expr(
        &negative_arena, reordered_rule_items, rules->expr.len);
    reordered_program_items[5] = wrong_core_rules;
    Atom *wrong_core_program = atom_expr(
        &negative_arena, reordered_program_items, 8u);
    CettaGdlTypeOfNativeAdmissionV1 wrong_core_admission =
        cetta_gdl_type_of_native_admit_v1(
            wrong_core_program, argv[4], argv[5], argv[6],
            (CettaGdlTypeOfNativeLimitsV1){0});
    CHECK(wrong_core_admission.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1 &&
              wrong_core_admission.native == NULL,
          "a lookalike rule inventory cannot name the native calculus");
    arena_free(&negative_arena);

done:
    cetta_gdl_type_of_host_cache_destroy_v1(host_cache);
    cetta_gdl_type_of_host_destroy_v1(refreshed_hosted_native);
    cetta_gdl_type_of_host_destroy_v1(hosted_native);
    cetta_gdl_type_of_native_destroy_v1(authored_source_native);
    cetta_gdl_type_of_native_destroy_v1(source_native);
    cetta_gdl_type_of_native_destroy_v1(native);
    cetta_inference_checker_destroy(checker);
    free(source_forms);
    free(forms);
    if (hosted_space_initialized)
        space_free(&hosted_space);
    arena_free(&source_arena);
    arena_free(&program_arena);
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;

    if (failures) {
        fprintf(stderr, "%u/%u IGGP type:of inference checks failed\n",
                failures, checks);
        return 1;
    }
    printf(
        "PASS: IGGP %s relational type:of proofs: %zu cases / "
        "%zu proof occurrences / %u checks / "
        "%zu source forms / %zu typing components / "
        "%zu acceptance constraints\n",
        label, expected_case_count, expected_proof_count, checks,
        source_native_stats.source_forms,
        source_native_stats.typing_components,
        source_native_stats.typing_acceptance_constraints);
    return 0;
}
