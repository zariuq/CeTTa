#include "finite_horn_answer_stream_v1.h"
#include "relational_state_program_v1.h"

#include "symbol.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t checks_run;
static uint32_t checks_failed;

static const uint8_t dynamic_value_bytes[] =
    "transaction-canary-dynamic-v1";
static const PPOccurrenceFoldV1Value prime_values[] = {{
    .role_id = 0u,
    .bytes = dynamic_value_bytes,
    .byte_len = sizeof(dynamic_value_bytes) - 1u,
}};

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
        .values = prime_values,
        .value_len = sizeof(prime_values) / sizeof(prime_values[0]),
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
                    run.receipt.action_len == 8u &&
                    run.receipt.row_len == 2u,
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
        .values = prime_values,
        .value_len = sizeof(prime_values) / sizeof(prime_values[0]),
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

static bool exercise_source_column_admission(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    PPRelationalStateProgramV1Plan *state_plan) {
    PPRelationalStateActionV1 *copy = NULL;
    uint32_t index;
    uint32_t original_column;
    char error[512] = {0};

    for (index = 0u; index < state_plan->action_len; index++) {
        if (state_plan->actions[index].kind ==
                PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING) {
            copy = &state_plan->actions[index];
            break;
        }
    }
    if (!expect(copy && copy->condition_operand_len == 1u &&
                    copy->condition_operands[0].kind ==
                        PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN &&
                    copy->operand_len == 1u &&
                    copy->operands[0].kind ==
                        PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN,
                "canonical source-column canary was not generated"))
        return false;

    original_column = copy->condition_operands[0].input_index;
    copy->condition_operands[0].input_index =
        state_plan->tables[copy->source_table_id].arity;
    if (!expect(
            !pprelational_state_program_v1_plan_validate(
                occurrence_plan, state_plan, error, sizeof(error)) &&
                strcmp(error, "invalid state operation action") == 0,
            "out-of-range source column did not fail closed")) {
        copy->condition_operands[0].input_index = original_column;
        return false;
    }
    copy->condition_operands[0].input_index = original_column;
    error[0] = '\0';
    return expect(
        pprelational_state_program_v1_plan_validate(
            occurrence_plan, state_plan, error, sizeof(error)),
        error[0] ? error : "restored source-column plan was not admitted");
}

static bool exercise_prepared_literal_ids(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *state_plan) {
    static const uint8_t novel[] = "transaction-canary-novel-v1";
    const PPRelationalStateOperandV1 *literal = NULL;
    PPRelationalStateProgramV1Run run = {0};
    PPRelationalStoreV1 store;
    uint32_t action_index;
    uint32_t operand_index;
    uint32_t novel_id;
    uint32_t literal_id;
    char error[512] = {0};
    bool ok = false;

    for (action_index = 0u;
         action_index < state_plan->action_len && !literal;
         action_index++) {
        const PPRelationalStateActionV1 *action =
            &state_plan->actions[action_index];
        for (operand_index = 0u;
             operand_index < action->operand_len; operand_index++) {
            if (action->operands[operand_index].kind ==
                PPRELATIONAL_STATE_OPERAND_V1_LITERAL) {
                literal = &action->operands[operand_index];
                break;
            }
        }
    }
    if (!expect(literal != NULL,
                "prepared-literal canary was not generated") ||
        !expect(
            pprelational_state_program_v1_run_init(
                &run, occurrence_plan, state_plan, NULL, NULL,
                PPRELATIONAL_STATE_OBSERVATION_V1_EXACT_RECEIPT,
                error, sizeof(error)),
            error[0] ? error : "prepared-literal run did not initialize") ||
        !expect(pprelational_state_program_v1_store(&run, &store),
                "prepared-literal store was unavailable"))
        goto done;
    if (!expect(
            store.value_intern(
                store.context, novel, sizeof(novel) - 1u, &novel_id) &&
                store.value_intern(
                    store.context, literal->literal,
                    literal->literal_len, &literal_id) &&
                literal_id < novel_id,
            "immutable plan literal was not prepared before execution"))
        goto done;
    ok = true;

done:
    pprelational_state_program_v1_run_free(&run);
    return ok;
}

static bool exercise_occurrence_cache_bounds(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *state_plan) {
    PPRelationalStateProgramV1Run run = {0};
    PPOccurrenceFoldV1Backend backend;
    PPOccurrenceFoldV1Step prime = {
        .kind = PPOCCURRENCE_FOLD_V1_STEP_REDUCE,
        .operation_id = 0u,
    };
    char error[512] = {0};
    bool ok = false;

    if (!expect(
            pprelational_state_program_v1_run_init(
                &run, occurrence_plan, state_plan, NULL, NULL,
                PPRELATIONAL_STATE_OBSERVATION_V1_EXACT_RECEIPT,
                error, sizeof(error)),
            error[0] ? error :
                "occurrence-cache bounds run did not initialize"))
        goto done;
    backend = pprelational_state_program_v1_backend(&run);
    if (!expect(!backend.apply(
                    backend.context, &prime, error, sizeof(error)) &&
                    run.receipt.failure ==
                        PPRELATIONAL_STATE_FAILURE_V1_INVALID,
                "missing occurrence role did not fail closed"))
        goto done;
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
    static char *roles[] = {
        "guest-value-role-v1",
    };
    static PPOccurrenceFoldV1TerminalBinding terminals[] = {{
        .source_index = 0u,
        .role_id = 0u,
        .shift_operation_id = 0u,
        .value_production_label = UINT32_MAX,
    }};
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
    occurrence_plan.roles = roles;
    occurrence_plan.role_len = sizeof(roles) / sizeof(roles[0]);
    occurrence_plan.terminals = terminals;
    occurrence_plan.terminal_len =
        sizeof(terminals) / sizeof(terminals[0]);
    memset(occurrence_plan.plan_digest, 'a', 64u);
    occurrence_plan.plan_digest[64] = '\0';

    if (load_plan(argv[1], &occurrence_plan,
                  &transactional_answers, &transactional_plan) &&
        load_plan(argv[2], &occurrence_plan,
                  &persistent_answers, &persistent_plan) &&
        exercise_source_column_admission(
            &occurrence_plan, &transactional_plan) &&
        exercise_prepared_literal_ids(
            &occurrence_plan, &transactional_plan) &&
        exercise_occurrence_cache_bounds(
            &occurrence_plan, &transactional_plan) &&
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
