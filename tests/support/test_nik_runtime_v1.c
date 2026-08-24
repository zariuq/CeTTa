#include "generated/prime_nik_authorities_v1.generated.h"
#include "generated/prime_nik_runtime_v1.generated.h"
#include "generated/prime_nik_side_condition_provider_catalog_v1.generated.h"
#include "gslt_language_runtime.h"
#include "inference_side_condition_provider.h"
#include "nik_runtime.h"
#include "nik_runtime_internal.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

typedef enum {
    NIK_QUERY_NORMAL = 0,
    NIK_QUERY_COMPILED_DISAGREES,
    NIK_QUERY_COMPILED_ZERO_WORK,
    NIK_QUERY_COMPILED_FAULTS,
} NikQueryTestMode;

static NikQueryTestMode query_test_mode;

#define CHECK(condition, label)                                              \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL: %s\n", (label));                         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static Atom *parse_one(Arena *arena, const char *source) {
    size_t position = 0u;
    Atom *atom = parse_sexpr(arena, source, &position);
    if (!atom || !parser_rest_is_delimiters(source, &position)) {
        fprintf(stderr, "cannot parse test atom: %s\n", source);
        exit(2);
    }
    return atom;
}

static CettaNikOutcome check_production(
    Arena *arena, const char *authority, Atom *goal, Atom *proof,
    CettaNikLimits limits, CettaNikReceiptV1 *receipt) {
    char error[1024] = {0};
    CettaNikOutcome outcome = cetta_nik_check_v1(
        authority, goal, proof, limits, arena, receipt,
        error, sizeof(error));
    if (outcome == CETTA_NIK_FAULT)
        fprintf(stderr, "NIK fault: %s\n", error);
    return outcome;
}

static CettaNikOutcome check(
    Arena *arena, const char *authority, Atom *goal, Atom *proof,
    CettaNikLimits limits, CettaNikReceiptV1 *receipt) {
    char error[1024] = {0};
    CettaNikOutcome outcome = cetta_nik_check_differential_v1(
        authority, goal, proof, limits, arena, receipt,
        error, sizeof(error));
    if (outcome == CETTA_NIK_FAULT)
        fprintf(stderr, "NIK differential fault: %s\n", error);
    return outcome;
}

static bool controlled_query(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    Arena *output_arena,
    Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error,
    size_t error_size) {
    if (realization == CETTA_GSLT_REALIZATION_COMPILED_WORKLIST &&
        query_test_mode == NIK_QUERY_COMPILED_FAULTS) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size,
                           "injected compiled realization fault");
        return false;
    }
    bool ok = cetta_gslt_language_query_with_providers_v1(
        language, realization,
        &cetta_prime_nik_side_condition_provider_catalog_v1,
        cetta_inference_side_condition_provider_registry_v1(),
        output_arena, query, limits, result, error, error_size);
    if (!ok || realization != CETTA_GSLT_REALIZATION_COMPILED_WORKLIST)
        return ok;
    if (query_test_mode == NIK_QUERY_COMPILED_DISAGREES)
        result->answer_count = 0u;
    if (query_test_mode == NIK_QUERY_COMPILED_ZERO_WORK)
        result->rule_attempts = 0u;
    return true;
}

static CettaNikOutcome check_with_controlled_query(
    Arena *arena, const char *authority, Atom *goal, Atom *proof,
    CettaNikReceiptV1 *receipt, char *error, size_t error_size) {
    return cetta_nik_check_with_query_v1(
        authority, goal, proof, (CettaNikLimits){0}, arena, receipt,
        error, error_size, controlled_query);
}

static char *read_text_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    size_t size = (size_t)length;
    if ((long)size != length || size == SIZE_MAX) {
        fclose(file);
        return NULL;
    }
    char *text = malloc(size + 1u);
    if (!text) {
        fclose(file);
        return NULL;
    }
    if (fread(text, 1u, size, file) != size || fclose(file) != 0) {
        free(text);
        return NULL;
    }
    text[size] = '\0';
    return text;
}

static int run_differential_file(const char *path) {
    SymbolTable symbols;
    VarInternTable variable_names;
    Arena arena;
    CettaNikReceiptV1 receipt;
    char error[1024] = {0};
    int result = 2;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&arena);

    char *text = read_text_file(path);
    if (!text) {
        fprintf(stderr, "cannot read NIK differential request: %s\n", path);
        goto done;
    }
    Atom *request = parse_one(&arena, text);
    free(text);
    if (request->kind != ATOM_EXPR || request->expr.len != 4u ||
        request->expr.elems[0]->kind != ATOM_SYMBOL ||
        strcmp(atom_name_cstr(request->expr.elems[0]),
               "NIKDifferentialV1") != 0 ||
        request->expr.elems[1]->kind != ATOM_SYMBOL) {
        fprintf(stderr, "malformed NIK differential request\n");
        goto done;
    }
    const char *authority = atom_name_cstr(request->expr.elems[1]);
    CettaNikOutcome outcome = cetta_nik_check_differential_v1(
        authority, request->expr.elems[2], request->expr.elems[3],
        (CettaNikLimits){0}, &arena, &receipt, error, sizeof(error));
    bool conclusive = outcome == CETTA_NIK_ACCEPTED ||
        outcome == CETTA_NIK_REJECTED;
    bool agreement = conclusive && receipt.native_ran &&
        receipt.reference_ran && receipt.compiled_ran &&
        receipt.native_accepted == receipt.reference_accepted &&
        receipt.reference_accepted == receipt.compiled_accepted;
    printf(
        "(NikDifferentialV1 (Outcome %s) (Agreement %s) "
        "(NativeStatus %s) (Native %s %llu) (HornReference %s %llu) "
        "(CompiledWorklist %s %llu))\n",
        cetta_nik_outcome_name(outcome), agreement ? "True" : "False",
        cetta_inference_status_name(receipt.native_status),
        receipt.native_accepted ? "True" : "False",
        (unsigned long long)receipt.native_nodes,
        receipt.reference_accepted ? "True" : "False",
        (unsigned long long)receipt.reference_rule_attempts,
        receipt.compiled_accepted ? "True" : "False",
        (unsigned long long)receipt.compiled_rule_attempts);
    if (!agreement && error[0])
        fprintf(stderr, "%s\n", error);
    result = agreement ? 0 : 1;

done:
    arena_free(&arena);
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    return result;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--differential-file") == 0)
        return run_differential_file(argv[2]);
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--differential-file REQUEST]\n", argv[0]);
        return 2;
    }

    SymbolTable symbols;
    VarInternTable variable_names;
    Arena arena;
    CettaNikReceiptV1 receipt;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&arena);

    CHECK(cetta_prime_nik_authorities_v1_count >= 4u,
          "Prime begins with four checking authorities");
    const CettaNikAuthorityV1 *dtt = &cetta_prime_nik_authorities_v1[0];
    const CettaNikAuthorityV1 *hotg = &cetta_prime_nik_authorities_v1[1];
    const CettaNikAuthorityV1 *megalodon_imp =
        &cetta_prime_nik_authorities_v1[2];
    const CettaNikAuthorityV1 *megalodon_term =
        &cetta_prime_nik_authorities_v1[3];
    CHECK(strcmp(dtt->alias, "DTT") == 0 &&
              strcmp(hotg->alias, "HOTG") == 0 &&
              strcmp(megalodon_imp->alias, "MEGALODON-IMP") == 0 &&
              strcmp(megalodon_term->alias, "MEGALODON-TERM") == 0,
          "generated catalog preserves all authority identities");
    CHECK(cetta_nik_authority_catalog_valid_v1(
              cetta_prime_nik_authorities_v1,
              cetta_prime_nik_authorities_v1_count,
              cetta_prime_nik_authorities_v1_catalog_sha256),
          "runtime recomputes the exact semantic authority catalog identity");

    CettaNikAuthorityV1 changed_presentation = *dtt;
    changed_presentation.presentation_metta = hotg->presentation_metta;
    CHECK(!cetta_nik_authority_descriptor_valid_v1(&changed_presentation),
          "runtime rejects a presentation that does not match its authority digest");

    CettaNikAuthorityV1 *changed_catalog = malloc(
        cetta_prime_nik_authorities_v1_count * sizeof(*changed_catalog));
    if (!changed_catalog) {
        fprintf(stderr, "cannot allocate changed NIK catalog\n");
        return 2;
    }
    memcpy(changed_catalog, cetta_prime_nik_authorities_v1,
           cetta_prime_nik_authorities_v1_count * sizeof(*changed_catalog));
    changed_catalog[0].alias = "DTT-mutated";
    CHECK(!cetta_nik_authority_catalog_valid_v1(
              changed_catalog, cetta_prime_nik_authorities_v1_count,
              cetta_prime_nik_authorities_v1_catalog_sha256),
          "runtime rejects an authority identity mutation under the admitted catalog digest");
    free(changed_catalog);

    Atom *dtt_goal = parse_one(&arena, dtt->positive_goal_metta);
    Atom *dtt_proof = parse_one(&arena, dtt->positive_proof_metta);
    Atom *dtt_dag_nodes = parse_one(
        &arena,
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
        &arena,
        (Atom *[]){atom_symbol(&arena, "GProofDAG"), atom_int(&arena, 1),
                   dtt_dag_nodes, atom_int(&arena, 2), dtt_goal},
        5u);
    Atom *hotg_goal = parse_one(&arena, hotg->positive_goal_metta);
    Atom *hotg_proof = parse_one(&arena, hotg->positive_proof_metta);
    Atom *megalodon_goal =
        parse_one(&arena, megalodon_imp->positive_goal_metta);
    Atom *megalodon_proof =
        parse_one(&arena, megalodon_imp->positive_proof_metta);
    Atom *megalodon_term_goal =
        parse_one(&arena, megalodon_term->positive_goal_metta);
    Atom *megalodon_term_proof =
        parse_one(&arena, megalodon_term->positive_proof_metta);

    CettaGsltLanguage *query_service = NULL;
    char service_error[512] = {0};
    CHECK(cetta_gslt_language_load_embedded(
              &cetta_prime_nik_runtime_v1, &query_service,
              service_error, sizeof(service_error)),
          "generated NIK query service loads");
    CettaGsltHornLimits service_limits = {
        .max_rule_attempts = 64u,
        .max_answers = 8u,
        .max_depth = 32u,
    };
    CettaGsltHornResult query_result = {0};
    Atom *wrong_relation = parse_one(&arena, "(not-nik-check a b c)");
    CHECK(!cetta_gslt_language_query_v1(
              query_service, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
              &arena, wrong_relation, service_limits, &query_result,
              service_error, sizeof(service_error)),
          "query service rejects a relation outside its admitted entry");
    Atom *wrong_arity = parse_one(&arena, "(nik-check a b)");
    CHECK(!cetta_gslt_language_query_v1(
              query_service, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
              &arena, wrong_arity, service_limits, &query_result,
              service_error, sizeof(service_error)),
          "query service rejects the wrong admitted arity");
    CettaGsltLanguageResult document_result = {0};
    Atom *document_forms[1] = {wrong_relation};
    CHECK(!cetta_gslt_language_execute_atoms(
              query_service, document_forms, 1u, &arena, service_limits,
              &document_result, service_error, sizeof(service_error)),
          "query service cannot be invoked as a document language");
    cetta_gslt_language_result_free(&document_result);
    cetta_gslt_language_free(query_service);

    CHECK(check_production(
              &arena, "DTT", dtt_goal, dtt_proof,
              (CettaNikLimits){0}, &receipt) == CETTA_NIK_ACCEPTED &&
              receipt.native_ran && receipt.native_accepted &&
              !receipt.reference_ran && !receipt.compiled_ran &&
              receipt.total_work == receipt.native_nodes,
          "ordinary NIK execution runs one selected realization");
    CHECK(check_production(
              &arena, "DTT", dtt_goal, hotg_proof,
              (CettaNikLimits){0}, &receipt) == CETTA_NIK_REJECTED &&
              receipt.native_ran && !receipt.native_accepted &&
              !receipt.reference_ran && !receipt.compiled_ran,
          "ordinary NIK rejection does not invoke qualification machines");

    char admitted_error[1024] = {0};
    CettaNikRuntimeV1 *admitted_runtime = cetta_nik_runtime_v1_new(
        admitted_error, sizeof(admitted_error));
    CHECK(admitted_runtime != NULL &&
              cetta_nik_runtime_v1_admission_count(admitted_runtime) == 0u,
          "a new NIK runtime begins without eagerly admitted authorities");
    if (admitted_runtime) {
        CHECK(cetta_nik_runtime_v1_check(
                  admitted_runtime, "DTT", dtt_goal, dtt_proof,
                  (CettaNikLimits){0}, &arena, &receipt,
                  admitted_error, sizeof(admitted_error)) ==
                      CETTA_NIK_ACCEPTED &&
                  cetta_nik_runtime_v1_admission_count(admitted_runtime) == 1u,
              "first use admits and indexes one authority revision");
        CHECK(cetta_nik_runtime_v1_check(
                  admitted_runtime, "DTT", dtt_goal, hotg_proof,
                  (CettaNikLimits){0}, &arena, &receipt,
                  admitted_error, sizeof(admitted_error)) ==
                      CETTA_NIK_REJECTED &&
                  cetta_nik_runtime_v1_admission_count(admitted_runtime) == 1u,
              "later checks reuse the admitted authority index");
        CHECK(cetta_nik_runtime_v1_check(
                  admitted_runtime, "DTT", dtt_goal, dtt_dag_proof,
                  (CettaNikLimits){0}, &arena, &receipt,
                  admitted_error, sizeof(admitted_error)) ==
                      CETTA_NIK_ACCEPTED &&
                  receipt.native_nodes == 3u &&
                  receipt.native_status == CETTA_INFERENCE_OK &&
                  cetta_nik_runtime_v1_admission_count(admitted_runtime) == 1u,
              "the admitted authority replays an exact chronological DAG");
        Atom *wrong_version_dag = atom_expr(
            &arena,
            (Atom *[]){atom_symbol(&arena, "GProofDAG"), atom_int(&arena, 2),
                       dtt_dag_nodes, atom_int(&arena, 2), dtt_goal},
            5u);
        CHECK(cetta_nik_runtime_v1_check(
                  admitted_runtime, "DTT", dtt_goal, wrong_version_dag,
                  (CettaNikLimits){0}, &arena, &receipt,
                  admitted_error, sizeof(admitted_error)) ==
                      CETTA_NIK_MALFORMED &&
                  receipt.native_status == CETTA_INFERENCE_MALFORMED_PROOF,
              "an unknown chronological article version fails closed");
        Atom *missing_reference_nodes = parse_one(
            &arena,
            "(LCons (GDNode 0 (GRuleInst \"j\" LNil) LNil) "
            "  (LCons (GDNode 2 "
            "    (GRuleInst \"k\" "
            "      (LCons (PApp \"i\" LNil) "
            "        (LCons (PApp \"Z\" LNil) "
            "          (LCons (PApp \"N\" LNil) "
            "            (LCons (PApp \"N\" LNil) LNil))))) "
            "    (LCons (GRNode 0) (LCons (GRNode 1) LNil))) LNil))");
        Atom *missing_reference_dag = atom_expr(
            &arena,
            (Atom *[]){atom_symbol(&arena, "GProofDAG"), atom_int(&arena, 1),
                       missing_reference_nodes, atom_int(&arena, 2), dtt_goal},
            5u);
        CHECK(cetta_nik_runtime_v1_check(
                  admitted_runtime, "DTT", dtt_goal, missing_reference_dag,
                  (CettaNikLimits){0}, &arena, &receipt,
                  admitted_error, sizeof(admitted_error)) ==
                      CETTA_NIK_REJECTED &&
                  receipt.native_status == CETTA_INFERENCE_BAD_REFERENCE,
              "a forward or absent DAG reference is rejected");
        CHECK(cetta_nik_runtime_v1_check(
                  admitted_runtime, "HOTG", hotg_goal, hotg_proof,
                  (CettaNikLimits){0}, &arena, &receipt,
                  admitted_error, sizeof(admitted_error)) ==
                      CETTA_NIK_ACCEPTED &&
                  cetta_nik_runtime_v1_admission_count(admitted_runtime) == 2u,
              "a distinct authority revision receives a distinct admission");
        CHECK(cetta_nik_runtime_v1_check(
                  admitted_runtime, "NOT-AN-AUTHORITY", dtt_goal, dtt_proof,
                  (CettaNikLimits){0}, &arena, &receipt,
                  admitted_error, sizeof(admitted_error)) ==
                      CETTA_NIK_UNSUPPORTED &&
                  cetta_nik_runtime_v1_admission_count(admitted_runtime) == 2u,
              "an unknown authority cannot mutate the admitted inventory");
        cetta_nik_runtime_v1_free(admitted_runtime);
    }

    CHECK(check(&arena, "DTT", dtt_goal, dtt_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_ACCEPTED,
          "DTT article is accepted");
    CHECK(receipt.native_ran && receipt.reference_ran &&
              receipt.compiled_ran &&
              receipt.native_accepted && receipt.reference_accepted &&
              receipt.compiled_accepted,
          "DTT acceptance agrees across all three realizations");
    CHECK(receipt.native_nodes > 0u &&
              receipt.reference_rule_attempts > 0u &&
              receipt.compiled_rule_attempts > 0u,
          "completed realization receipts contain nonzero work evidence");
    CHECK(strcmp(receipt.system_id, "prime.dtt.calibration") == 0 &&
              strlen(receipt.authority_digest) == 64u &&
              strlen(receipt.catalog_digest) == 64u,
          "DTT receipt retains exact authority and catalog identity");

    Atom *dtt_beta_goal = parse_one(
        &arena,
        "(PApp \"E\" (LCons (PLam BNone (Var 0)) "
        "  (LCons (PApp \"Z\" LNil) "
        "    (LCons (PApp \"Z\" LNil) LNil))))");
    Atom *dtt_beta_proof = parse_one(
        &arena,
        "(GProof (GRuleInst \"b\" "
        "  (LCons (Var 0) (LCons (PApp \"Z\" LNil) "
        "    (LCons (PApp \"Z\" LNil) LNil)))) PrNil)");
    CHECK(check(&arena, "DTT", dtt_beta_goal, dtt_beta_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_ACCEPTED &&
              receipt.native_accepted && receipt.reference_accepted &&
              receipt.compiled_accepted,
          "binder substitution agrees across all NIK realizations");
    Atom *dtt_fabricated_beta_goal = parse_one(
        &arena,
        "(PApp \"E\" (LCons (PLam BNone (Var 0)) "
        "  (LCons (PApp \"Z\" LNil) "
        "    (LCons (PApp \"N\" LNil) LNil))))");
    Atom *dtt_fabricated_beta_proof = parse_one(
        &arena,
        "(GProof (GRuleInst \"b\" "
        "  (LCons (Var 0) (LCons (PApp \"Z\" LNil) "
        "    (LCons (PApp \"N\" LNil) LNil)))) PrNil)");
    CHECK(check(&arena, "DTT", dtt_fabricated_beta_goal,
                dtt_fabricated_beta_proof, (CettaNikLimits){0},
                &receipt) == CETTA_NIK_REJECTED &&
              receipt.native_status == CETTA_INFERENCE_INVALID_ARGUMENTS &&
              !receipt.native_accepted && !receipt.reference_accepted &&
              !receipt.compiled_accepted,
          "a fabricated binder-substitution result fails closed");

    Atom *dtt_unscoped_beta_goal = parse_one(
        &arena,
        "(PApp \"E\" (LCons (PLam BNone (Var 1)) "
        "  (LCons (PApp \"Z\" LNil) (LCons (Var 0) LNil))))");
    Atom *dtt_unscoped_beta_proof = parse_one(
        &arena,
        "(GProof (GRuleInst \"b\" "
        "  (LCons (Var 1) (LCons (PApp \"Z\" LNil) "
        "    (LCons (Var 0) LNil)))) PrNil)");
    CHECK(check(&arena, "DTT", dtt_unscoped_beta_goal,
                dtt_unscoped_beta_proof, (CettaNikLimits){0},
                &receipt) == CETTA_NIK_REJECTED &&
              receipt.native_status == CETTA_INFERENCE_INVALID_ARGUMENTS &&
              !receipt.native_accepted && !receipt.reference_accepted &&
              !receipt.compiled_accepted,
          "all realizations reject arguments outside their binder support");

    Atom *dtt_bad_collection_goal = parse_one(
        &arena,
        "(PApp \"E\" (LCons (PLam BNone (Var 0)) "
        "  (LCons (PCollection \"NotACollType\" LNil RNone) "
        "    (LCons (PCollection \"NotACollType\" LNil RNone) LNil))))");
    Atom *dtt_bad_collection_proof = parse_one(
        &arena,
        "(GProof (GRuleInst \"b\" (LCons (Var 0) "
        "  (LCons (PCollection \"NotACollType\" LNil RNone) "
        "    (LCons (PCollection \"NotACollType\" LNil RNone) LNil)))) "
        "  PrNil)");
    CHECK(check(&arena, "DTT", dtt_bad_collection_goal,
                dtt_bad_collection_proof, (CettaNikLimits){0},
                &receipt) == CETTA_NIK_REJECTED &&
              receipt.native_status == CETTA_INFERENCE_INVALID_ARGUMENTS &&
              !receipt.native_accepted && !receipt.reference_accepted &&
              !receipt.compiled_accepted,
          "all realizations reject collection tags outside exact Pattern wire");

    Atom *dtt_structural_beta_goal = parse_one(
        &arena,
        "(PApp \"E\" (LCons "
        "  (PLam BNone (PApp \"@\" (LCons "
        "    (PMultiLam 2 LNil (Var 2)) "
        "    (LCons (PSubst (Var 1) "
        "      (PCollection "
        "        \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" "
        "        (LCons (Var 0) LNil) RNone)) LNil)))) "
        "  (LCons (PApp \"Z\" LNil) "
        "    (LCons (PApp \"@\" (LCons "
        "      (PMultiLam 2 LNil (PApp \"Z\" LNil)) "
        "      (LCons (PSubst (PApp \"Z\" LNil) "
        "        (PCollection "
        "          \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" "
        "          (LCons (PApp \"Z\" LNil) LNil) RNone)) LNil))) "
        "      LNil))))");
    Atom *dtt_structural_beta_proof = parse_one(
        &arena,
        "(GProof (GRuleInst \"b\" (LCons "
        "  (PApp \"@\" (LCons (PMultiLam 2 LNil (Var 2)) "
        "    (LCons (PSubst (Var 1) "
        "      (PCollection "
        "        \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" "
        "        (LCons (Var 0) LNil) RNone)) LNil))) "
        "  (LCons (PApp \"Z\" LNil) "
        "    (LCons (PApp \"@\" (LCons "
        "      (PMultiLam 2 LNil (PApp \"Z\" LNil)) "
        "      (LCons (PSubst (PApp \"Z\" LNil) "
        "        (PCollection "
        "          \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" "
        "          (LCons (PApp \"Z\" LNil) LNil) RNone)) LNil))) "
        "      LNil)))) PrNil)");
    CHECK(check(&arena, "DTT", dtt_structural_beta_goal,
                dtt_structural_beta_proof, (CettaNikLimits){0}, &receipt) ==
              CETTA_NIK_ACCEPTED && receipt.native_accepted &&
              receipt.reference_accepted && receipt.compiled_accepted,
          "all Pattern binder fields refine through the generic ABT carrier");

    CHECK(check(&arena, "HOTG", hotg_goal, hotg_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_ACCEPTED,
          "HOTG article with a declared witness is accepted");
    CHECK(receipt.native_accepted && receipt.reference_accepted &&
              receipt.compiled_accepted,
          "HOTG acceptance agrees across all three realizations");

    CHECK(check(&arena, "MEGALODON-IMP", megalodon_goal,
                megalodon_proof, (CettaNikLimits){0}, &receipt) ==
              CETTA_NIK_ACCEPTED,
          "Megalodon implicational modus ponens article is accepted");
    CHECK(receipt.native_accepted && receipt.reference_accepted &&
              receipt.compiled_accepted &&
              strcmp(receipt.system_id,
                     "megalodon.mathdata.implicational") == 0,
          "Megalodon fragment acceptance agrees across all realizations");

    CHECK(check(&arena, "MEGALODON-TERM", megalodon_term_goal,
                megalodon_term_proof, (CettaNikLimits){0}, &receipt) ==
              CETTA_NIK_ACCEPTED,
          "Megalodon ordered theory-admission article is accepted");
    CHECK(receipt.native_accepted && receipt.reference_accepted &&
              receipt.compiled_accepted &&
              strcmp(receipt.system_id,
                     "megalodon.mathdata.definition-conversion") == 0 &&
              strcmp(receipt.revision, "10") == 0,
          "primitive, axiom, and theorem admission share the revised authority");

    CHECK(check(&arena, "DTT", dtt_goal, hotg_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_REJECTED &&
              !receipt.native_accepted && !receipt.reference_accepted &&
              !receipt.compiled_accepted,
          "HOTG proof is rejected by the DTT authority");
    CHECK(check(&arena, "HOTG", hotg_goal, dtt_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_REJECTED &&
              !receipt.native_accepted && !receipt.reference_accepted &&
              !receipt.compiled_accepted,
          "DTT proof is rejected by the HOTG authority");
    CHECK(check(&arena, "DTT", hotg_goal, dtt_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_REJECTED,
          "a proof cannot be replayed at a different goal");
    CHECK(check(&arena, "DTT", dtt_goal, megalodon_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_REJECTED,
          "Megalodon proof is rejected by the DTT authority");
    CHECK(check(&arena, "MEGALODON-IMP", megalodon_goal, dtt_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_REJECTED,
          "DTT proof is rejected by the Megalodon fragment authority");
    CHECK(check(&arena, "MEGALODON-TERM", megalodon_term_goal,
                megalodon_proof, (CettaNikLimits){0}, &receipt) ==
              CETTA_NIK_REJECTED,
          "an implicational-fragment proof cannot forge a term article");
    CHECK(check(&arena, "MEGALODON-IMP", megalodon_goal,
                megalodon_term_proof, (CettaNikLimits){0}, &receipt) ==
              CETTA_NIK_REJECTED,
          "a checked-environment proof cannot forge an implicational article");

    Atom *megalodon_wrong_exact_goal = parse_one(
        &arena,
        "(PApp \"MProves\" (LCons (PApp \"MCtxNil\" LNil) "
        "  (LCons (PApp \"MImp\" "
        "    (LCons (PApp \"MAtom\" "
        "      (LCons (PApp \"MZero\" LNil) LNil)) "
        "      (LCons (PApp \"MAtom\" "
        "        (LCons (PApp \"MZero\" LNil) LNil)) LNil))) LNil)))");
    CHECK(check(&arena, "MEGALODON-IMP", megalodon_wrong_exact_goal,
                megalodon_proof, (CettaNikLimits){0}, &receipt) ==
              CETTA_NIK_REJECTED,
          "Megalodon modus ponens article is rejected at an identity goal");

    char injected_error[256] = {0};
    query_test_mode = NIK_QUERY_COMPILED_DISAGREES;
    CHECK(check_with_controlled_query(
              &arena, "DTT", dtt_goal, dtt_proof, &receipt,
              injected_error, sizeof(injected_error)) == CETTA_NIK_FAULT &&
              receipt.native_ran && receipt.reference_ran &&
              receipt.compiled_ran && receipt.native_accepted &&
              receipt.reference_accepted && !receipt.compiled_accepted &&
              strstr(injected_error, "disagree") != NULL,
          "realization disagreement fails closed with an explicit fault");

    memset(injected_error, 0, sizeof(injected_error));
    query_test_mode = NIK_QUERY_COMPILED_ZERO_WORK;
    CHECK(check_with_controlled_query(
              &arena, "DTT", dtt_goal, dtt_proof, &receipt,
              injected_error, sizeof(injected_error)) == CETTA_NIK_FAULT &&
              receipt.compiled_ran && receipt.compiled_rule_attempts == 0u &&
              strstr(injected_error, "reported no work") != NULL,
          "a completed realization cannot claim to run without work evidence");

    memset(injected_error, 0, sizeof(injected_error));
    query_test_mode = NIK_QUERY_COMPILED_FAULTS;
    CHECK(check_with_controlled_query(
              &arena, "DTT", dtt_goal, dtt_proof, &receipt,
              injected_error, sizeof(injected_error)) == CETTA_NIK_FAULT &&
              receipt.native_ran && receipt.reference_ran &&
              !receipt.compiled_ran &&
              strstr(injected_error, "injected compiled") != NULL,
          "a failed realization invocation is not reported as having run");
    query_test_mode = NIK_QUERY_NORMAL;

    Atom *swapped_dtt_premises = parse_one(
        &arena,
        "(GProof "
        "  (GRuleInst \"k\" "
        "    (LCons (PApp \"i\" LNil) "
        "      (LCons (PApp \"Z\" LNil) "
        "        (LCons (PApp \"N\" LNil) "
        "          (LCons (PApp \"N\" LNil) LNil))))) "
        "  (PrCons (GProof (GRuleInst \"z\" LNil) PrNil) "
        "    (PrCons (GProof (GRuleInst \"j\" LNil) PrNil) PrNil)))");
    CHECK(check(&arena, "DTT", dtt_goal, swapped_dtt_premises,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_REJECTED &&
              receipt.native_status == CETTA_INFERENCE_PREMISE_MISMATCH &&
              !receipt.native_accepted && !receipt.reference_accepted &&
              !receipt.compiled_accepted,
          "swapped premises are rejected as a premise mismatch");

    Atom *undeclared_hotg_goal = parse_one(
        &arena,
        "(PApp \"H\" (LCons "
        "  (PApp \"C\" (LCons (PApp \"U\" "
        "    (LCons (PApp \"TotallyUndeclared\" "
        "      (LCons (PApp \"OpaqueAtom\" LNil) LNil)) LNil)) LNil)) "
        "  LNil))");
    Atom *undeclared_hotg_proof = parse_one(
        &arena,
        "(GProof (GRuleInst \"r\" "
        "  (LCons (PApp \"TotallyUndeclared\" "
        "    (LCons (PApp \"OpaqueAtom\" LNil) LNil)) LNil)) PrNil)");
    CHECK(check(&arena, "HOTG", undeclared_hotg_goal,
                undeclared_hotg_proof, (CettaNikLimits){0}, &receipt) ==
              CETTA_NIK_REJECTED &&
              receipt.native_status == CETTA_INFERENCE_INVALID_ARGUMENTS &&
              !receipt.native_accepted && !receipt.reference_accepted &&
              !receipt.compiled_accepted,
          "undeclared structural constructors cannot enter through rule arguments");

    Atom *opaque_hotg_goal = parse_one(
        &arena,
        "(PApp \"H\" (LCons "
        "  (PApp \"C\" (LCons (PApp \"U\" "
        "    (LCons (PApp \"OpaqueAtom\" LNil) LNil)) LNil)) LNil))");
    Atom *opaque_hotg_proof = parse_one(
        &arena,
        "(GProof (GRuleInst \"r\" "
        "  (LCons (PApp \"OpaqueAtom\" LNil) LNil)) PrNil)");
    CHECK(check(&arena, "HOTG", opaque_hotg_goal,
                opaque_hotg_proof, (CettaNikLimits){0}, &receipt) ==
              CETTA_NIK_ACCEPTED &&
              receipt.native_accepted && receipt.reference_accepted &&
              receipt.compiled_accepted,
          "open nullary atom data agrees across all NIK realizations");

    Atom *wrong_arity_hotg_goal = parse_one(
        &arena,
        "(PApp \"H\" (LCons "
        "  (PApp \"C\" (LCons (PApp \"U\" "
        "    (LCons (PApp \"U\" LNil) LNil)) LNil)) LNil))");
    Atom *wrong_arity_hotg_proof = parse_one(
        &arena,
        "(GProof (GRuleInst \"r\" "
        "  (LCons (PApp \"U\" LNil) LNil)) PrNil)");
    CHECK(check(&arena, "HOTG", wrong_arity_hotg_goal,
                wrong_arity_hotg_proof, (CettaNikLimits){0}, &receipt) ==
              CETTA_NIK_REJECTED &&
              receipt.native_status == CETTA_INFERENCE_INVALID_ARGUMENTS &&
              !receipt.native_accepted && !receipt.reference_accepted &&
              !receipt.compiled_accepted,
          "declared constructors retain their exact arity in arguments");

    Atom *malformed = parse_one(
        &arena, "(GProof (GRuleInst \"z\" LNil) BrokenChildren)");
    CHECK(check(&arena, "DTT", dtt_goal, malformed,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_MALFORMED,
          "malformed proof-list encoding is distinguished from rejection");
    CHECK(check(&arena, "UNKNOWN", dtt_goal, dtt_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_UNSUPPORTED &&
              !receipt.native_ran && !receipt.reference_ran &&
              !receipt.compiled_ran && receipt.catalog_digest != NULL,
          "unknown authority is distinguished from a rejected proof");
    Atom *open_proof = parse_one(&arena, "$openProof");
    CHECK(check(&arena, "DTT", dtt_goal, open_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_MALFORMED,
          "open proof articles are rejected at the wire boundary");

    CettaNikLimits replay_bound = {0};
    replay_bound.replay.max_nodes = 1u;
    CHECK(check(&arena, "DTT", dtt_goal, dtt_proof,
                replay_bound, &receipt) == CETTA_NIK_INCOMPLETE,
          "native replay limits remain distinct from semantic rejection");
    CettaNikLimits gslt_bound = {0};
    gslt_bound.gslt.max_rule_attempts = 1u;
    gslt_bound.gslt.max_answers = 8u;
    gslt_bound.gslt.max_depth = 32u;
    CHECK(check(&arena, "DTT", dtt_goal, dtt_proof,
                gslt_bound, &receipt) == CETTA_NIK_INCOMPLETE,
          "GSLT work limits remain distinct from semantic rejection");
    CettaNikLimits aggregate_bound = {0};
    aggregate_bound.max_total_work = 1u;
    CHECK(check(&arena, "DTT", dtt_goal, dtt_proof,
                aggregate_bound, &receipt) == CETTA_NIK_INCOMPLETE &&
              receipt.total_work == 1u && receipt.native_ran &&
              !receipt.reference_ran && !receipt.compiled_ran,
          "one aggregate bound spans all NIK realizations");

    char inference_error[512] = {0};
    Atom *binder_presentation = parse_one(
        &arena,
        "(GPresentationV1 1 "
        "  (LCons (CDecl \"K\" 0) LNil) "
        "  (LCons (JDecl \"J\" 1) LNil) "
        "  (LCons (GRuleV1 \"under-lambda\" "
        "    (LCons (Formal \"body\" 1) LNil) LNil "
        "    (PApp \"J\" (LCons (PLam BNone (FVar \"body\")) LNil)) "
        "    LNil) LNil) "
        "  GNoConversion)");
    CettaInferenceChecker *binder_checker = NULL;
    CHECK(cetta_inference_checker_create(
              binder_presentation, &binder_checker,
              inference_error, sizeof(inference_error)) == CETTA_INFERENCE_OK,
          "version-one presentation admits an exact depth-one formal");
    CettaInferenceTrace binder_trace;
    cetta_inference_trace_init(&binder_trace, binder_checker, &arena);
    Atom *open_body = parse_one(&arena, "(Var 0)");
    Atom *binder_arguments[1] = {open_body};
    CHECK(cetta_inference_trace_apply_named(
              &binder_trace, "under-lambda", binder_arguments, 1u,
              inference_error, sizeof(inference_error)) == CETTA_INFERENCE_OK,
          "depth-one open argument is accepted at its exact support");
    Atom *binder_goal = parse_one(
        &arena,
        "(PApp \"J\" (LCons (PLam BNone (Var 0)) LNil))");
    CHECK(cetta_inference_trace_finish(
              &binder_trace, binder_goal,
              inference_error, sizeof(inference_error)) == CETTA_INFERENCE_OK,
          "native instantiation preserves the supported binder index");
    cetta_inference_trace_reset(&binder_trace);
    Atom *unbound_body = parse_one(&arena, "(Var 1)");
    Atom *unbound_arguments[1] = {unbound_body};
    CHECK(cetta_inference_trace_apply_named(
              &binder_trace, "under-lambda", unbound_arguments, 1u,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_INVALID_ARGUMENTS,
          "argument outside its declared support fails closed");
    cetta_inference_trace_free(&binder_trace);
    cetta_inference_checker_destroy(binder_checker);

    Atom *guarded_presentation = parse_one(
        &arena,
        "(GPresentationV1 1 "
        "  (LCons (CDecl \"K\" 0) LNil) "
        "  (LCons (JDecl \"J\" 2) LNil) "
        "  (LCons (GRuleV1 \"guarded\" "
        "    (LCons (Formal \"body\" 1) "
        "      (LCons (Formal \"result\" 0) LNil)) LNil "
        "    (PApp \"J\" (LCons (PLam BNone (FVar \"body\")) "
        "      (LCons (FVar \"result\") LNil))) "
        "    (LCons (GUnusedBinderElimination 0 0 1) LNil)) LNil) "
        "  GNoConversion)");
    CettaInferenceChecker *guarded_checker = NULL;
    CHECK(cetta_inference_checker_create(
              guarded_presentation, &guarded_checker,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_OK && guarded_checker != NULL,
          "an exact unused-binder side condition is admitted");
    CettaInferenceTrace guarded_trace;
    cetta_inference_trace_init(&guarded_trace, guarded_checker, &arena);
    Atom *constant_pattern = parse_one(&arena, "(PApp \"K\" LNil)");
    Atom *guarded_arguments[2] = {constant_pattern, constant_pattern};
    CHECK(cetta_inference_trace_apply_named(
              &guarded_trace, "guarded", guarded_arguments, 2u,
              inference_error, sizeof(inference_error)) == CETTA_INFERENCE_OK,
          "unused-binder elimination executes through the ABT provider");
    Atom *guarded_goal = parse_one(
        &arena,
        "(PApp \"J\" (LCons (PLam BNone (PApp \"K\" LNil)) "
        "  (LCons (PApp \"K\" LNil) LNil)))");
    CHECK(cetta_inference_trace_finish(
              &guarded_trace, guarded_goal,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_OK,
          "unused-binder elimination produces the declared result");
    cetta_inference_trace_reset(&guarded_trace);
    Atom *used_binder = parse_one(&arena, "(Var 0)");
    Atom *fabricated_guarded_arguments[2] = {
        used_binder, constant_pattern,
    };
    CHECK(cetta_inference_trace_apply_named(
              &guarded_trace, "guarded", fabricated_guarded_arguments, 2u,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_INVALID_ARGUMENTS,
          "unused-binder elimination rejects a used binder");
    cetta_inference_trace_free(&guarded_trace);
    cetta_inference_checker_destroy(guarded_checker);

    Atom *ambient_beta_presentation = parse_one(
        &arena,
        "(GPresentationV1 1 "
        "  (LCons (CDecl \"K\" 0) LNil) "
        "  (LCons (JDecl \"J3\" 3) LNil) "
        "  (LCons (GRuleV1 \"ambient-beta\" "
        "    (LCons (Formal \"result\" 1) "
        "      (LCons (Formal \"replacement\" 1) "
        "        (LCons (Formal \"body\" 2) LNil))) LNil "
        "    (PApp \"J3\" "
        "      (LCons (PLam BNone (FVar \"result\")) "
        "        (LCons (PLam BNone (FVar \"replacement\")) "
        "          (LCons (PLam BNone (PLam BNone (FVar \"body\"))) "
        "            LNil)))) "
        "    (LCons (GExplicitSubstitution 1 2 1 0) LNil)) LNil) "
        "  GNoConversion)");
    CettaInferenceChecker *ambient_beta_checker = NULL;
    CHECK(cetta_inference_checker_create(
              ambient_beta_presentation, &ambient_beta_checker,
              inference_error, sizeof(inference_error)) ==
                  CETTA_INFERENCE_OK &&
              ambient_beta_checker != NULL,
          "nonzero-ambient side conditions admit permuted argument positions");
    CettaInferenceTrace ambient_beta_trace;
    cetta_inference_trace_init(
        &ambient_beta_trace, ambient_beta_checker, &arena);
    Atom *ambient_result = parse_one(&arena, "(Var 0)");
    Atom *ambient_replacement = parse_one(&arena, "(Var 0)");
    Atom *ambient_body = parse_one(&arena, "(Var 0)");
    Atom *ambient_beta_arguments[3] = {
        ambient_result, ambient_replacement, ambient_body,
    };
    CHECK(cetta_inference_trace_apply_named(
              &ambient_beta_trace, "ambient-beta", ambient_beta_arguments, 3u,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_OK,
          "nonzero-ambient explicit substitution executes by declared positions");
    Atom *ambient_beta_goal = parse_one(
        &arena,
        "(PApp \"J3\" "
        "  (LCons (PLam BNone (Var 0)) "
        "    (LCons (PLam BNone (Var 0)) "
        "      (LCons (PLam BNone (PLam BNone (Var 0))) LNil))))");
    CHECK(cetta_inference_trace_finish(
              &ambient_beta_trace, ambient_beta_goal,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_OK,
          "nonzero-ambient substitution preserves the instantiated conclusion");
    cetta_inference_trace_reset(&ambient_beta_trace);
    Atom *fabricated_ambient_result = parse_one(
        &arena, "(PApp \"K\" LNil)");
    Atom *fabricated_ambient_arguments[3] = {
        fabricated_ambient_result, ambient_replacement, ambient_body,
    };
    CHECK(cetta_inference_trace_apply_named(
              &ambient_beta_trace, "ambient-beta",
              fabricated_ambient_arguments, 3u,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_INVALID_ARGUMENTS,
          "nonzero-ambient substitution rejects a fabricated result");
    cetta_inference_trace_free(&ambient_beta_trace);
    cetta_inference_checker_destroy(ambient_beta_checker);

    Atom *wrong_version_presentation = parse_one(
        &arena,
        "(GPresentationV1 2 LNil LNil LNil GNoConversion)");
    CettaInferenceChecker *wrong_version_checker = NULL;
    CHECK(cetta_inference_checker_create(
              wrong_version_presentation, &wrong_version_checker,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_INVALID_PRESENTATION &&
              wrong_version_checker == NULL,
          "unknown presentation wire versions fail closed");

    Atom *shared_dag_presentation = parse_one(
        &arena,
        "(GPresentationV1 1 "
        "  (LCons (CDecl \"K\" 0) (LCons (CDecl \"R\" 0) LNil)) "
        "  (LCons (JDecl \"J\" 1) LNil) "
        "  (LCons (GRuleV1 \"shared-ax\" LNil LNil "
        "    (PApp \"J\" (LCons (PApp \"K\" LNil) LNil)) LNil) "
        "    (LCons (GRuleV1 \"shared-pair\" LNil "
        "      (LCons (PApp \"J\" (LCons (PApp \"K\" LNil) LNil)) "
        "        (LCons (PApp \"J\" (LCons (PApp \"K\" LNil) LNil)) LNil)) "
        "      (PApp \"J\" (LCons (PApp \"R\" LNil) LNil)) LNil) LNil)) "
        "  GNoConversion)");
    Atom *shared_dag_goal = parse_one(
        &arena, "(PApp \"J\" (LCons (PApp \"R\" LNil) LNil))");
    Atom *shared_dag_article = parse_one(
        &arena,
        "(GProofDAG 1 "
        "  (LCons (GDNode 0 (GRuleInst \"shared-ax\" LNil) LNil) "
        "    (LCons (GDNode 1 (GRuleInst \"shared-pair\" LNil) "
        "      (LCons (GRNode 0) (LCons (GRNode 0) LNil))) LNil)) "
        "  1 (PApp \"J\" (LCons (PApp \"R\" LNil) LNil)))");
    CettaInferenceReplayStats shared_dag_stats = {0};
    CHECK(cetta_inference_check_dag_article(
              shared_dag_presentation, shared_dag_goal, shared_dag_article,
              (CettaInferenceReplayLimits){0}, &shared_dag_stats, &arena,
              inference_error, sizeof(inference_error)) == CETTA_INFERENCE_OK &&
              shared_dag_stats.nodes == 2u &&
              shared_dag_stats.max_depth_observed == 2u,
          "one checked DAG leaf can discharge two ordered premise occurrences");

    Atom *missing_shared_child = parse_one(
        &arena,
        "(GProofDAG 1 "
        "  (LCons (GDNode 0 (GRuleInst \"shared-ax\" LNil) LNil) "
        "    (LCons (GDNode 1 (GRuleInst \"shared-pair\" LNil) "
        "      (LCons (GRNode 0) LNil)) LNil)) "
        "  1 (PApp \"J\" (LCons (PApp \"R\" LNil) LNil)))");
    CHECK(cetta_inference_check_dag_article(
              shared_dag_presentation, shared_dag_goal, missing_shared_child,
              (CettaInferenceReplayLimits){0}, NULL, &arena,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_PREMISE_MISMATCH,
          "DAG sharing cannot erase a required premise occurrence");

    Atom *duplicate_shared_node = parse_one(
        &arena,
        "(GProofDAG 1 "
        "  (LCons (GDNode 0 (GRuleInst \"shared-ax\" LNil) LNil) "
        "    (LCons (GDNode 0 (GRuleInst \"shared-ax\" LNil) LNil) LNil)) "
        "  0 (PApp \"J\" (LCons (PApp \"K\" LNil) LNil)))");
    Atom *shared_ax_goal = parse_one(
        &arena, "(PApp \"J\" (LCons (PApp \"K\" LNil) LNil))");
    CHECK(cetta_inference_check_dag_article(
              shared_dag_presentation, shared_ax_goal, duplicate_shared_node,
              (CettaInferenceReplayLimits){0}, NULL, &arena,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_MALFORMED_PROOF,
          "duplicate chronological node identity fails closed");

    Atom *open_premise_article = parse_one(
        &arena,
        "(GProofDAG 1 "
        "  (LCons (GDNode 1 (GRuleInst \"shared-pair\" LNil) "
        "    (LCons (GRPremise 0) (LCons (GRPremise 1) LNil))) LNil) "
        "  1 (PApp \"J\" (LCons (PApp \"R\" LNil) LNil)))");
    CHECK(cetta_inference_check_dag_article(
              shared_dag_presentation, shared_dag_goal, open_premise_article,
              (CettaInferenceReplayLimits){0}, NULL, &arena,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_BAD_REFERENCE,
          "closed NIK replay rejects open premise references");

    CHECK(cetta_inference_check_dag_article(
              shared_dag_presentation, shared_dag_goal, shared_dag_article,
              (CettaInferenceReplayLimits){.max_nodes = 1u}, NULL, &arena,
              inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_RESOURCE_LIMIT,
          "DAG replay retains an explicit unique-node resource bound");

    Atom *shared_pattern_presentation = parse_one(
        &arena,
        "(GPresentationV1 1 "
        "  (LCons (CDecl \"K\" 0) (LCons (CDecl \"Pair\" 2) LNil)) "
        "  (LCons (JDecl \"J\" 1) LNil) "
        "  (LCons (GRuleV1 \"argument-ax\" "
        "    (LCons (Formal \"x\" 0) LNil) LNil "
        "    (PApp \"J\" (LCons (FVar \"x\") LNil)) LNil) LNil) "
        "  GNoConversion)");
    Atom *shared_pattern_goal = parse_one(
        &arena,
        "(PApp \"J\" (LCons (PApp \"Pair\" "
        "  (LCons (PApp \"K\" LNil) "
        "    (LCons (PApp \"K\" LNil) LNil))) LNil))");
    const char *shared_pattern_nodes =
        "(LCons (GPatternNode 0 (GPKApply \"K\" LNil)) "
        "  (LCons (GPatternNode 1 (GPKApply \"Pair\" "
        "    (LCons 0 (LCons 0 LNil)))) "
        "    (LCons (GPatternNode 2 (GPKApply \"J\" "
        "      (LCons 1 LNil))) LNil)))";
    char shared_pattern_article_text[2048];
    (void)snprintf(
        shared_pattern_article_text, sizeof(shared_pattern_article_text),
        "(GProofDAG 2 %s "
        "  (LCons (GDNode 0 (GRuleRefs \"argument-ax\" "
        "    (LCons 1 LNil)) LNil) LNil) 0 2)",
        shared_pattern_nodes);
    Atom *shared_pattern_article = parse_one(
        &arena, shared_pattern_article_text);
    CettaInferenceReplayStats shared_pattern_stats = {0};
    CHECK(cetta_inference_check_dag_article(
              shared_pattern_presentation, shared_pattern_goal,
              shared_pattern_article, (CettaInferenceReplayLimits){0},
              &shared_pattern_stats, &arena,
              inference_error, sizeof(inference_error)) == CETTA_INFERENCE_OK &&
              shared_pattern_stats.nodes == 4u &&
              shared_pattern_stats.max_depth_observed == 3u,
          "version-2 DAG shares ground Pattern subtrees before exact replay");

    char wrong_shared_argument_text[2048];
    (void)snprintf(
        wrong_shared_argument_text, sizeof(wrong_shared_argument_text),
        "(GProofDAG 2 %s "
        "  (LCons (GDNode 0 (GRuleRefs \"argument-ax\" "
        "    (LCons 0 LNil)) LNil) LNil) 0 2)",
        shared_pattern_nodes);
    Atom *wrong_shared_argument = parse_one(
        &arena, wrong_shared_argument_text);
    CHECK(cetta_inference_check_dag_article(
              shared_pattern_presentation, shared_pattern_goal,
              wrong_shared_argument, (CettaInferenceReplayLimits){0},
              NULL, &arena, inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_FINAL_MISMATCH,
          "shared Pattern ids cannot change a rule argument silently");

    Atom *forward_shared_pattern = parse_one(
        &arena,
        "(GProofDAG 2 "
        "  (LCons (GPatternNode 0 (GPKApply \"Pair\" "
        "    (LCons 1 (LCons 1 LNil)))) LNil) "
        "  (LCons (GDNode 0 (GRuleRefs \"argument-ax\" "
        "    (LCons 0 LNil)) LNil) LNil) 0 0)");
    CHECK(cetta_inference_check_dag_article(
              shared_pattern_presentation, shared_pattern_goal,
              forward_shared_pattern, (CettaInferenceReplayLimits){0},
              NULL, &arena, inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_BAD_REFERENCE,
          "shared Pattern children must refer to earlier nodes");

    char unknown_shared_target_text[2048];
    (void)snprintf(
        unknown_shared_target_text, sizeof(unknown_shared_target_text),
        "(GProofDAG 2 %s "
        "  (LCons (GDNode 0 (GRuleRefs \"argument-ax\" "
        "    (LCons 1 LNil)) LNil) LNil) 0 3)",
        shared_pattern_nodes);
    Atom *unknown_shared_target = parse_one(
        &arena, unknown_shared_target_text);
    CHECK(cetta_inference_check_dag_article(
              shared_pattern_presentation, shared_pattern_goal,
              unknown_shared_target, (CettaInferenceReplayLimits){0},
              NULL, &arena, inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_BAD_REFERENCE,
          "shared article target must name an admitted Pattern node");

    CHECK(cetta_inference_check_dag_article(
              shared_pattern_presentation, shared_pattern_goal,
              shared_pattern_article,
              (CettaInferenceReplayLimits){.max_nodes = 3u},
              NULL, &arena, inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_RESOURCE_LIMIT,
          "shared Pattern materialization is charged to the node bound");
    CHECK(cetta_inference_check_dag_article(
              shared_pattern_presentation, shared_pattern_goal,
              shared_pattern_article,
              (CettaInferenceReplayLimits){.max_depth = 2u},
              NULL, &arena, inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_RESOURCE_LIMIT,
          "shared Pattern materialization retains a depth bound");

    Atom *shared_abt_goal = parse_one(
        &arena,
        "(PApp \"J\" (LCons (PCollection "
        "  \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" "
        "  (LCons (PLam BNone (Var 0)) "
        "    (LCons (PMultiLam 2 LNil (Var 1)) "
        "      (LCons (PSubst (Var 0) (PApp \"K\" LNil)) LNil))) "
        "  RNone) LNil))");
    Atom *shared_abt_article = parse_one(
        &arena,
        "(GProofDAG 2 "
        "  (LCons (GPatternNode 0 (GPKBVar 0)) "
        "    (LCons (GPatternNode 1 (GPKLambda BNone 0)) "
        "      (LCons (GPatternNode 2 (GPKBVar 1)) "
        "        (LCons (GPatternNode 3 "
        "          (GPKMultiLambda 2 LNil 2)) "
        "          (LCons (GPatternNode 4 (GPKApply \"K\" LNil)) "
        "            (LCons (GPatternNode 5 (GPKSubst 0 4)) "
        "              (LCons (GPatternNode 6 (GPKCollection "
        "                \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" "
        "                (LCons 1 (LCons 3 (LCons 5 LNil))) RNone)) "
        "                (LCons (GPatternNode 7 (GPKApply \"J\" "
        "                  (LCons 6 LNil))) LNil)))))))) "
        "  (LCons (GDNode 0 (GRuleRefs \"argument-ax\" "
        "    (LCons 6 LNil)) LNil) LNil) 0 7)");
    CettaInferenceReplayStats shared_abt_stats = {0};
    CHECK(cetta_inference_check_dag_article(
              shared_pattern_presentation, shared_abt_goal,
              shared_abt_article, (CettaInferenceReplayLimits){0},
              &shared_abt_stats, &arena,
              inference_error, sizeof(inference_error)) == CETTA_INFERENCE_OK &&
              shared_abt_stats.nodes == 9u &&
              shared_abt_stats.max_depth_observed == 4u,
          "shared carrier preserves every support-indexed Pattern constructor");

    Atom *shared_fvar_goal = parse_one(
        &arena, "(PApp \"J\" (LCons (FVar \"x\") LNil))");
    Atom *shared_fvar_article = parse_one(
        &arena,
        "(GProofDAG 2 "
        "  (LCons (GPatternNode 0 (GPKFVar \"x\")) "
        "    (LCons (GPatternNode 1 (GPKApply \"J\" "
        "      (LCons 0 LNil))) LNil)) "
        "  (LCons (GDNode 0 (GRuleRefs \"argument-ax\" "
        "    (LCons 0 LNil)) LNil) LNil) 0 1)");
    CHECK(cetta_inference_check_dag_article(
              shared_pattern_presentation, shared_fvar_goal,
              shared_fvar_article, (CettaInferenceReplayLimits){0},
              NULL, &arena, inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_INVALID_ARGUMENTS,
          "shared transport cannot turn an open metavariable into evidence");

    Atom *nonordinal_shared_patterns = parse_one(
        &arena,
        "(GProofDAG 2 "
        "  (LCons (GPatternNode 1 (GPKApply \"K\" LNil)) LNil) "
        "  (LCons (GDNode 0 (GRuleRefs \"argument-ax\" "
        "    (LCons 0 LNil)) LNil) LNil) 0 0)");
    CHECK(cetta_inference_check_dag_article(
              shared_pattern_presentation, shared_ax_goal,
              nonordinal_shared_patterns, (CettaInferenceReplayLimits){0},
              NULL, &arena, inference_error, sizeof(inference_error)) ==
              CETTA_INFERENCE_MALFORMED_PROOF,
          "shared Pattern identities must be chronological ordinals");

    arena_free(&arena);
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;

    if (failures != 0u) {
        fprintf(stderr, "NIK runtime: %u/%u checks failed\n",
                failures, checks);
        return 1;
    }
    printf("(NikRuntimeV1Summary checks=%u authorities=%zu "
           "realizations=3 cross-rejections=4)\n",
           checks, cetta_prime_nik_authorities_v1_count);
    return 0;
}
