#include "certificate_gslt_relational_assertion_v1.h"

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
        PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN] = {
            2u, 2u, 2u, 2u, 3u, 2u, 3u, 2u, 2u,
        };
    static const uint32_t key_arities[
        PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN] = {
            1u, 1u, 1u, 2u, 2u, 2u, 3u, 2u, 1u,
        };
    SymbolTable symbols;
    PPCertificateGSLTPlanV1 proof_plan;
    PPCertificateGSLTPlanV1 other_plan;
    PPCertificateGSLTRelationalAssertionPlanV1 bridge;
    PPRelationalStateProgramV1Plan state_plan;
    PPRelationalStateTableV1 tables[
        PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN];
    PPCertificateGSLTArticleV1Result result;
    char error[512] = {0};
    uint32_t index;

    if (argc != 7 + PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN) {
        fprintf(stderr,
                "usage: %s PROOF_PLAN BRIDGE NO_APARTNESS INCOMPLETE OTHER_PLAN"
                " TABLE... NO_EXECUTION\n",
                argv[0]);
        return 2;
    }
    ppcertificate_gslt_plan_v1_init(&proof_plan);
    ppcertificate_gslt_plan_v1_init(&other_plan);
    ppcertificate_gslt_relational_assertion_v1_init(&bridge);
    memset(&state_plan, 0, sizeof(state_plan));
    memset(tables, 0, sizeof(tables));
    for (index = 0u;
         index < PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN; index++) {
        tables[index].name = argv[6u + index];
        tables[index].arity = arities[index];
        tables[index].key_arity = key_arities[index];
    }
    state_plan.tables = tables;
    state_plan.table_len = PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN;
    memset(state_plan.plan_digest, '1', 64u);
    state_plan.plan_digest[64] = '\0';

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    result = ppcertificate_gslt_plan_v1_load(
        &proof_plan, argv[1], NULL, error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
          "relational proof plan loads");
    if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        result = ppcertificate_gslt_relational_assertion_v1_load(
            &bridge, argv[2], &proof_plan, &state_plan,
            error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
          "generated relational assertion ABI loads");
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              bridge.semantic_digest[0] != '\0' &&
              bridge.proof_plan_digest[0] != '\0' &&
              bridge.state_plan_digest[0] != '\0',
          "relational assertion ABI records all three stage identities");
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              bridge.resolved_table_ids[
                  PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS] ==
                  PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS &&
              bridge.table_binding_len ==
                  PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN &&
              bridge.table_presence[
                  PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_FORMULA] ==
                  PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_REQUIRED &&
              bridge.table_presence[
                  PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_ASSERTION_DISJOINT] ==
                  PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_OPTIONAL_EMPTY &&
              bridge.table_presence[
                  PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_ACTIVE_APARTNESS] ==
                  PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_OPTIONAL_EMPTY &&
              bridge.selectors[
                  PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE]
                      .len != 0u,
          "compiled table and selector roles are populated");
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              bridge.execution.machine &&
              strcmp(bridge.execution.machine,
                     "mm-stack-proof-machine-v1") == 0 &&
              bridge.execution.unknown_token.bytes &&
              bridge.execution.unknown_token.len != 0u &&
              bridge.execution.terminal_low == 65u &&
              bridge.execution.terminal_high == 84u &&
              bridge.execution.continuation_low == 85u &&
              bridge.execution.continuation_high == 89u &&
              bridge.execution.save_byte == 90u &&
              bridge.execution.unknown_byte == 63u &&
              bridge.execution.terminal_radix == 20u &&
              bridge.execution.terminal_digit_bias == 0u &&
              bridge.execution.continuation_radix == 5u &&
              bridge.execution.continuation_digit_bias == 1u &&
              bridge.execution.unknown_policy ==
                  PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM &&
              bridge.execution.save_placement ==
                  CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1,
          "generated execution descriptor owns machine and decoder policy");

    result = ppcertificate_gslt_relational_assertion_v1_load(
        &bridge, argv[3], &proof_plan, &state_plan,
        error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              bridge.table_binding_len + 2u ==
                  PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN &&
              bridge.resolved_table_ids[
                  PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_ASSERTION_DISJOINT] ==
                  UINT32_MAX &&
              bridge.resolved_table_ids[
                  PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_ACTIVE_APARTNESS] ==
                  UINT32_MAX,
          "generated relation with no apartness capability loads");

    tables[PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_FORMULA].arity = 3u;
    result = ppcertificate_gslt_relational_assertion_v1_load(
        &bridge, argv[2], &proof_plan, &state_plan,
        error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED,
          "state-table shape falsification is rejected");
    tables[PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_FORMULA].arity = 2u;

    result = ppcertificate_gslt_relational_assertion_v1_load(
        &bridge, argv[4], &proof_plan, &state_plan,
        error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
          "deleted authored role leaves an incomplete ABI");

    result = ppcertificate_gslt_plan_v1_load(
        &other_plan, argv[5], NULL, error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
          "independent proof plan loads for identity negative");
    if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        result = ppcertificate_gslt_relational_assertion_v1_load(
            &bridge, argv[2], &other_plan, &state_plan,
            error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
          "relational ABI refuses a different proof extension");

    result = ppcertificate_gslt_relational_assertion_v1_load(
        &bridge, argv[6u + PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN],
        &proof_plan, &state_plan, error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
          "deleting generated execution authority fails closed");

    ppcertificate_gslt_relational_assertion_v1_free(&bridge);
    ppcertificate_gslt_plan_v1_free(&other_plan);
    ppcertificate_gslt_plan_v1_free(&proof_plan);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    printf("---\n%u passed, %u failed\n", passed, failed);
    return failed == 0u ? 0 : 1;
}
