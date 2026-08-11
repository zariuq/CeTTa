#include "generated/prime_nik_authorities_v1.generated.h"
#include "generated/prime_nik_runtime_v1.generated.h"
#include "gslt_language_runtime.h"
#include "nik_runtime.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

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

static CettaNikOutcome check(
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

int main(void) {
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

    CHECK(cetta_prime_nik_authorities_v1_count >= 2u,
          "Prime begins with plural checking authorities");
    const CettaNikAuthorityV1 *dtt = &cetta_prime_nik_authorities_v1[0];
    const CettaNikAuthorityV1 *hotg = &cetta_prime_nik_authorities_v1[1];
    CHECK(strcmp(dtt->alias, "DTT") == 0 &&
              strcmp(hotg->alias, "HOTG") == 0,
          "generated catalog preserves both calibration identities");

    Atom *dtt_goal = parse_one(&arena, dtt->positive_goal_metta);
    Atom *dtt_proof = parse_one(&arena, dtt->positive_proof_metta);
    Atom *hotg_goal = parse_one(&arena, hotg->positive_goal_metta);
    Atom *hotg_proof = parse_one(&arena, hotg->positive_proof_metta);

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

    CHECK(check(&arena, "DTT", dtt_goal, dtt_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_ACCEPTED,
          "DTT article is accepted");
    CHECK(receipt.native_ran && receipt.reference_ran &&
              receipt.compiled_ran &&
              receipt.native_accepted && receipt.reference_accepted &&
              receipt.compiled_accepted,
          "DTT acceptance agrees across all three realizations");
    CHECK(strcmp(receipt.system_id, "prime.dtt.calibration") == 0 &&
              strlen(receipt.authority_digest) == 64u &&
              strlen(receipt.catalog_digest) == 64u,
          "DTT receipt retains exact authority and catalog identity");

    CHECK(check(&arena, "HOTG", hotg_goal, hotg_proof,
                (CettaNikLimits){0}, &receipt) == CETTA_NIK_ACCEPTED,
          "HOTG article with an open-universe witness is accepted");
    CHECK(receipt.native_accepted && receipt.reference_accepted &&
              receipt.compiled_accepted,
          "HOTG acceptance agrees across all three realizations");

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
           "realizations=3 cross-rejections=2)\n",
           checks, cetta_prime_nik_authorities_v1_count);
    return 0;
}
