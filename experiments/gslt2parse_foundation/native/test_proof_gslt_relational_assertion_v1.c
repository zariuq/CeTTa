#include "proof_gslt_relational_assertion_v1.h"

#include "atom.h"
#include "symbol.h"

#include <stdio.h>
#include <string.h>

static uint32_t passed;
static uint32_t failed;

static void check(bool condition, const char *name) {
    if (condition) {
        passed++;
        printf("PASS: %s\n", name);
    } else {
        failed++;
        printf("FAIL: %s\n", name);
    }
}

int main(int argc, char **argv) {
    static const uint32_t arities[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN] = {
            2u, 2u, 2u, 2u, 3u, 2u, 3u, 2u,
        };
    static const uint32_t key_arities[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN] = {
            1u, 1u, 1u, 2u, 2u, 2u, 3u, 1u,
        };
    SymbolTable symbols;
    PPProofGSLTPlanV1 proof_plan;
    PPProofGSLTPlanV1 other_plan;
    PPProofGSLTRelationalAssertionPlanV1 bridge;
    PPRelationalStateProgramV1Plan state_plan;
    PPRelationalStateTableV1 tables[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN];
    PPProofGSLTArticleV1Result result;
    char error[512] = {0};
    uint32_t index;

    if (argc != 5 + PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN) {
        fprintf(stderr,
                "usage: %s PROOF_PLAN BRIDGE INCOMPLETE OTHER_PLAN"
                " TABLE...\n",
                argv[0]);
        return 2;
    }
    ppproof_gslt_plan_v1_init(&proof_plan);
    ppproof_gslt_plan_v1_init(&other_plan);
    ppproof_gslt_relational_assertion_v1_init(&bridge);
    memset(&state_plan, 0, sizeof(state_plan));
    memset(tables, 0, sizeof(tables));
    for (index = 0u;
         index < PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN; index++) {
        tables[index].name = argv[5u + index];
        tables[index].arity = arities[index];
        tables[index].key_arity = key_arities[index];
    }
    state_plan.tables = tables;
    state_plan.table_len = PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN;
    memset(state_plan.plan_digest, '1', 64u);
    state_plan.plan_digest[64] = '\0';

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    result = ppproof_gslt_plan_v1_load(
        &proof_plan, argv[1], NULL, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "relational proof plan loads");
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_relational_assertion_v1_load(
            &bridge, argv[2], &proof_plan, &state_plan,
            error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "generated relational assertion ABI loads");
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
              bridge.semantic_digest[0] != '\0' &&
              bridge.proof_plan_digest[0] != '\0' &&
              bridge.state_plan_digest[0] != '\0',
          "relational assertion ABI records all three stage identities");
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
              bridge.tables[
                  PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS] ==
                  PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS &&
              bridge.selectors[
                  PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE]
                      .len != 0u,
          "compiled table and selector roles are populated");

    tables[PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA].arity = 3u;
    result = ppproof_gslt_relational_assertion_v1_load(
        &bridge, argv[2], &proof_plan, &state_plan,
        error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED,
          "state-table shape falsification is rejected");
    tables[PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA].arity = 2u;

    result = ppproof_gslt_relational_assertion_v1_load(
        &bridge, argv[3], &proof_plan, &state_plan,
        error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_INVALID,
          "deleted authored role leaves an incomplete ABI");

    result = ppproof_gslt_plan_v1_load(
        &other_plan, argv[4], NULL, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "independent proof plan loads for identity negative");
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_relational_assertion_v1_load(
            &bridge, argv[2], &other_plan, &state_plan,
            error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_INVALID,
          "relational ABI refuses a different proof extension");

    ppproof_gslt_relational_assertion_v1_free(&bridge);
    ppproof_gslt_plan_v1_free(&other_plan);
    ppproof_gslt_plan_v1_free(&proof_plan);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    printf("---\n%u passed, %u failed\n", passed, failed);
    return failed == 0u ? 0 : 1;
}
