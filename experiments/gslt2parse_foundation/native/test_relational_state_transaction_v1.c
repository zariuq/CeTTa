#include "finite_horn_answer_stream_v1.h"
#include "relational_state_program_v1.h"

#include "symbol.h"

#include <stdint.h>
#include <stdio.h>
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

static bool table_id(
    const PPRelationalStateProgramV1Plan *plan,
    const char *name,
    uint32_t *table_id_out) {
    uint32_t index;

    if (!plan || !name || !table_id_out)
        return false;
    for (index = 0u; index < plan->table_len; index++) {
        if (strcmp(plan->tables[index].name, name) == 0) {
            *table_id_out = index;
            return true;
        }
    }
    return false;
}

static bool load_plan(
    const char *path,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    FHAnswerStreamV1 *answers,
    PPRelationalStateProgramV1Plan *state_plan) {
    char error[512] = {0};

    return expect(
               fh_answer_stream_v1_read(
                   answers, path, error, sizeof(error)),
               error[0] ? error : "state canary answers did not load") &&
           expect(
               pprelational_state_program_v1_plan_build(
                   occurrence_plan, answers->terms, answers->len,
                   answers->digest, state_plan, error, sizeof(error)),
               error[0] ? error : "state canary plan was not admitted");
}

static bool exercise_transactional_plan(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *state_plan) {
    PPRelationalStateProgramV1Run run = {0};
    PPOccurrenceFoldV1Backend backend;
    PPOccurrenceFoldV1Step prime = {
        .kind = PPOCCURRENCE_FOLD_V1_STEP_REDUCE,
        .operation_id = 0u,
    };
    PPOccurrenceFoldV1Step probe = {
        .kind = PPOCCURRENCE_FOLD_V1_STEP_REDUCE,
        .operation_id = 1u,
    };
    PPRelationalStoreV1 store;
    uint32_t output_table;
    uint32_t scratch_table;
    uint32_t arity;
    uint32_t key_arity;
    uint32_t row_len;
    char error[512] = {0};
    bool ok = false;

    if (!expect(table_id(state_plan, "guest-output-v1", &output_table) &&
                    table_id(state_plan, "guest-scratch-v1", &scratch_table),
                "state canary tables were not generated") ||
        !expect(
            state_plan->tables[output_table].lifetime ==
                    PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT &&
                state_plan->tables[scratch_table].lifetime ==
                    PPRELATIONAL_STATE_LIFETIME_V1_TRANSACTIONAL,
            "state canary lifetimes did not come from the generated plan") ||
        !expect(
            pprelational_state_program_v1_run_init(
                &run, occurrence_plan, state_plan, NULL, NULL,
                PPRELATIONAL_STATE_OBSERVATION_V1_EXACT_RECEIPT,
                error, sizeof(error)),
            error[0] ? error : "transactional state run did not initialize"))
        goto done;
    backend = pprelational_state_program_v1_backend(&run);
    if (!expect(backend.apply(
                    backend.context, &prime, error, sizeof(error)),
                error[0] ? error :
                    "scratch row was unavailable in its transaction") ||
        !expect(pprelational_state_program_v1_store(&run, &store),
                "transactional state store was unavailable") ||
        !expect(store.table_shape(
                    store.context, scratch_table,
                    &arity, &key_arity, &row_len) && row_len == 0u,
                "scratch row escaped its transaction") ||
        !expect(backend.apply(
                    backend.context, &probe, error, sizeof(error)),
                error[0] ? error :
                    "next transaction still observed the scratch row") ||
        !expect(backend.commit(
                    backend.context, error, sizeof(error)),
                error[0] ? error :
                    "transactional state run did not commit") ||
        !expect(run.receipt.committed && run.receipt.step_len == 2u &&
                    run.receipt.action_len == 4u &&
                    run.receipt.row_len == 1u,
                "transactional state receipt changed") ||
        !expect(store.table_shape(
                    store.context, output_table,
                    &arity, &key_arity, &row_len) && row_len == 1u,
                "persistent observation was not published"))
        goto done;
    ok = true;

done:
    pprelational_state_program_v1_run_free(&run);
    return ok;
}

static bool exercise_persistent_mutation(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *state_plan) {
    PPRelationalStateProgramV1Run run = {0};
    PPOccurrenceFoldV1Backend backend;
    PPOccurrenceFoldV1Step prime = {
        .kind = PPOCCURRENCE_FOLD_V1_STEP_REDUCE,
        .operation_id = 0u,
    };
    PPOccurrenceFoldV1Step probe = {
        .kind = PPOCCURRENCE_FOLD_V1_STEP_REDUCE,
        .operation_id = 1u,
    };
    uint32_t scratch_table;
    char error[512] = {0};
    bool ok = false;

    if (!expect(table_id(state_plan, "guest-scratch-v1", &scratch_table),
                "mutated scratch table was not generated") ||
        !expect(
            state_plan->tables[scratch_table].lifetime ==
                PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT,
            "lifetime mutation did not reach the generated plan") ||
        !expect(
            pprelational_state_program_v1_run_init(
                &run, occurrence_plan, state_plan, NULL, NULL,
                PPRELATIONAL_STATE_OBSERVATION_V1_EXACT_RECEIPT,
                error, sizeof(error)),
            error[0] ? error : "mutated state run did not initialize"))
        goto done;
    backend = pprelational_state_program_v1_backend(&run);
    if (!expect(backend.apply(
                    backend.context, &prime, error, sizeof(error)),
                error[0] ? error :
                    "mutated state did not execute its first operation") ||
        !expect(!backend.apply(
                    backend.context, &probe, error, sizeof(error)) &&
                    run.receipt.failure ==
                        PPRELATIONAL_STATE_FAILURE_V1_REJECTED,
                "persistent lifetime mutation was observationally inert"))
        goto done;
    backend.abort(backend.context);
    ok = true;

done:
    pprelational_state_program_v1_run_free(&run);
    return ok;
}

int main(int argc, char **argv) {
    static char *operations[] = {
        "guest-prime-v1",
        "guest-probe-v1",
    };
    SymbolTable symbols;
    PPOccurrenceFoldV1Plan occurrence_plan = {0};
    FHAnswerStreamV1 transactional_answers;
    FHAnswerStreamV1 persistent_answers;
    PPRelationalStateProgramV1Plan transactional_plan;
    PPRelationalStateProgramV1Plan persistent_plan;
    bool ok = false;

    if (argc != 3) {
        fprintf(stderr, "usage: %s TRANSACTIONAL-STATE PERSISTENT-STATE\n",
                argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    fh_answer_stream_v1_init(&transactional_answers);
    fh_answer_stream_v1_init(&persistent_answers);
    pprelational_state_program_v1_plan_init(&transactional_plan);
    pprelational_state_program_v1_plan_init(&persistent_plan);
    occurrence_plan.operations = operations;
    occurrence_plan.operation_len =
        sizeof(operations) / sizeof(operations[0]);
    memset(occurrence_plan.plan_digest, 'a', 64u);
    occurrence_plan.plan_digest[64] = '\0';

    if (load_plan(argv[1], &occurrence_plan,
                  &transactional_answers, &transactional_plan) &&
        load_plan(argv[2], &occurrence_plan,
                  &persistent_answers, &persistent_plan) &&
        exercise_transactional_plan(
            &occurrence_plan, &transactional_plan) &&
        exercise_persistent_mutation(
            &occurrence_plan, &persistent_plan))
        ok = true;

    pprelational_state_program_v1_plan_free(&persistent_plan);
    pprelational_state_program_v1_plan_free(&transactional_plan);
    fh_answer_stream_v1_free(&persistent_answers);
    fh_answer_stream_v1_free(&transactional_answers);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("(RelationalStateTransactionV1Summary %u %u %u)\n",
           checks_run, checks_run - checks_failed, checks_failed);
    return ok ? 0 : 1;
}
