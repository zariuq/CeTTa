#include "relational_state_program_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "relational_value_list_v1.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPRELATIONAL_STATE_V1_MAX_RECORDS = 65536u,
    PPRELATIONAL_STATE_V1_INITIAL_CAPACITY = 16u
};

static _Atomic uint64_t ppstate_v1_next_store_identity = UINT64_C(1);
static atomic_bool ppstate_v1_store_identity_exhausted = false;

static bool ppstate_v1_fresh_store_identity(uint64_t *identity_out) {
    uint64_t identity;

    if (!identity_out || atomic_load_explicit(
            &ppstate_v1_store_identity_exhausted,
            memory_order_relaxed))
        return false;
    identity = atomic_fetch_add_explicit(
        &ppstate_v1_next_store_identity, UINT64_C(1),
        memory_order_relaxed);
    if (identity == 0u || identity == UINT64_MAX) {
        atomic_store_explicit(
            &ppstate_v1_store_identity_exhausted, true,
            memory_order_relaxed);
        return false;
    }
    *identity_out = identity;
    return true;
}

typedef struct {
    PPRelationalStateTableV1 table;
} PPRelationalStateRawTableV1;

enum { PPSTATE_V1_PROOF_TABLE_LEN = 9u };

typedef struct {
    PPRelationalStateProofMachineV1 machine;
    char *table_names[PPSTATE_V1_PROOF_TABLE_LEN];
} PPRelationalStateRawProofMachineV1;

typedef struct {
    char *operation_name;
    char *role_name;
    char *table_name;
    char *source_table_name;
    char *condition_table_name;
    char *proof_machine_name;
    char *proof_role_names[4];
    char *operand_role_names[PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY];
    char *condition_operand_role_names[
        PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY];
    uint32_t index;
    PPRelationalStateActionV1 action;
} PPRelationalStateRawActionV1;

static void ppstate_v1_set_error(char *buf, size_t size,
                                 const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppstate_v1_expr_head(const Atom *atom, const char *head,
                                 CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == argument_len + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static bool ppstate_v1_digest_valid(const char *digest) {
    size_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        char value = digest[index];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f')))
            return false;
    }
    return true;
}

static bool ppstate_v1_array_fits(size_t count, size_t item_size) {
    return item_size == 0u || count <= SIZE_MAX / item_size;
}

static char *ppstate_v1_text_dup(const char *text) {
    size_t len;
    char *copy;

    if (!text)
        return NULL;
    len = strlen(text);
    copy = malloc(len + 1u);
    if (copy)
        memcpy(copy, text, len + 1u);
    return copy;
}

static uint8_t *ppstate_v1_bytes_dup(const uint8_t *bytes, uint32_t len) {
    uint8_t *copy;

    if ((!bytes && len > 0u) || len == 0u)
        return NULL;
    copy = malloc(len);
    if (copy)
        memcpy(copy, bytes, len);
    return copy;
}

static const char *ppstate_v1_symbol(const Atom *atom) {
    return atom && atom->kind == ATOM_SYMBOL ? atom_name_cstr((Atom *)atom)
                                             : NULL;
}

static bool ppstate_v1_u32(const Atom *atom, uint32_t *out) {
    int64_t value;

    if (!atom || !out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT)
        return false;
    value = atom->ground.ival;
    if (value < 0 || (uint64_t)value > UINT32_MAX)
        return false;
    *out = (uint32_t)value;
    return true;
}

static int ppstate_v1_text_compare(const void *left, const void *right) {
    const char *const *lhs = left;
    const char *const *rhs = right;
    return strcmp(*lhs, *rhs);
}

static int ppstate_v1_table_compare(const void *left, const void *right) {
    const PPRelationalStateRawTableV1 *lhs = left;
    const PPRelationalStateRawTableV1 *rhs = right;
    return strcmp(lhs->table.name, rhs->table.name);
}

static int ppstate_v1_proof_machine_compare(
    const void *left, const void *right) {
    const PPRelationalStateRawProofMachineV1 *lhs = left;
    const PPRelationalStateRawProofMachineV1 *rhs = right;
    return strcmp(lhs->machine.name, rhs->machine.name);
}

static int ppstate_v1_action_compare(const void *left, const void *right) {
    const PPRelationalStateRawActionV1 *lhs = left;
    const PPRelationalStateRawActionV1 *rhs = right;
    int comparison = strcmp(lhs->operation_name, rhs->operation_name);
    if (comparison != 0)
        return comparison;
    return lhs->index < rhs->index ? -1 : lhs->index > rhs->index ? 1 : 0;
}

static int ppstate_v1_final_compare(const void *left, const void *right) {
    const PPRelationalStateRawActionV1 *lhs = left;
    const PPRelationalStateRawActionV1 *rhs = right;
    return lhs->index < rhs->index ? -1 : lhs->index > rhs->index ? 1 : 0;
}

static int32_t ppstate_v1_name_find(char *const *names, uint32_t len,
                                    const char *name) {
    uint32_t low = 0u;
    uint32_t high = len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int comparison = strcmp(names[middle], name);
        if (comparison < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= len || strcmp(names[low], name) != 0)
        return -1;
    return (int32_t)low;
}

static int32_t ppstate_v1_table_find(
    const PPRelationalStateProgramV1Plan *plan, const char *name) {
    uint32_t low = 0u;
    uint32_t high = plan ? plan->table_len : 0u;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int comparison = strcmp(plan->tables[middle].name, name);
        if (comparison < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (!plan || low >= plan->table_len ||
        strcmp(plan->tables[low].name, name) != 0)
        return -1;
    return (int32_t)low;
}

static int32_t ppstate_v1_proof_machine_find(
    const PPRelationalStateProgramV1Plan *plan, const char *name) {
    uint32_t low = 0u;
    uint32_t high = plan ? plan->proof_machine_len : 0u;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int comparison = strcmp(plan->proof_machines[middle].name, name);
        if (comparison < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (!plan || low >= plan->proof_machine_len ||
        strcmp(plan->proof_machines[low].name, name) != 0)
        return -1;
    return (int32_t)low;
}

static void ppstate_v1_literal_free(PPRelationalStateLiteralV1 *literal) {
    if (!literal)
        return;
    free(literal->bytes);
    memset(literal, 0, sizeof(*literal));
}

static void ppstate_v1_proof_machine_free(
    PPRelationalStateProofMachineV1 *machine) {
    if (!machine)
        return;
    free(machine->name);
    ppstate_v1_literal_free(&machine->binder_hypothesis_kind);
    ppstate_v1_literal_free(&machine->matching_hypothesis_kind);
    ppstate_v1_literal_free(&machine->rule_kind_first);
    ppstate_v1_literal_free(&machine->rule_kind_second);
    ppstate_v1_literal_free(&machine->variable_symbol_kind);
    ppstate_v1_literal_free(&machine->unknown_token);
    memset(machine, 0, sizeof(*machine));
}

static void ppstate_v1_action_free(PPRelationalStateActionV1 *action) {
    uint32_t index;

    if (!action)
        return;
    for (index = 0u; index < action->operand_len &&
                     index < PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY;
         index++)
        free(action->operands[index].literal);
    for (index = 0u; index < action->condition_operand_len &&
                     index < PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY;
         index++)
        free(action->condition_operands[index].literal);
    memset(action, 0, sizeof(*action));
}

static void ppstate_v1_raw_action_free(
    PPRelationalStateRawActionV1 *action) {
    uint32_t index;

    if (!action)
        return;
    free(action->operation_name);
    free(action->role_name);
    free(action->table_name);
    free(action->source_table_name);
    free(action->condition_table_name);
    free(action->proof_machine_name);
    for (index = 0u; index < 4u; index++)
        free(action->proof_role_names[index]);
    for (index = 0u; index < PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY;
         index++)
        free(action->operand_role_names[index]);
    for (index = 0u; index < PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY;
         index++)
        free(action->condition_operand_role_names[index]);
    ppstate_v1_action_free(&action->action);
    memset(action, 0, sizeof(*action));
}

static void ppstate_v1_raw_proof_machine_free(
    PPRelationalStateRawProofMachineV1 *machine) {
    uint32_t index;
    if (!machine)
        return;
    ppstate_v1_proof_machine_free(&machine->machine);
    for (index = 0u; index < PPSTATE_V1_PROOF_TABLE_LEN; index++)
        free(machine->table_names[index]);
    memset(machine, 0, sizeof(*machine));
}

void pprelational_state_program_v1_plan_init(
    PPRelationalStateProgramV1Plan *plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

void pprelational_state_program_v1_plan_free(
    PPRelationalStateProgramV1Plan *plan) {
    uint32_t index;

    if (!plan)
        return;
    for (index = 0u; index < plan->table_len; index++)
        free(plan->tables[index].name);
    for (index = 0u; index < plan->proof_machine_len; index++)
        ppstate_v1_proof_machine_free(&plan->proof_machines[index]);
    for (index = 0u; index < plan->action_len; index++)
        ppstate_v1_action_free(&plan->actions[index]);
    for (index = 0u; index < plan->final_action_len; index++)
        ppstate_v1_action_free(&plan->final_actions[index]);
    free(plan->tables);
    free(plan->proof_machines);
    free(plan->actions);
    free(plan->final_actions);
    free(plan->operations);
    memset(plan, 0, sizeof(*plan));
}

static bool ppstate_v1_parse_lifetime(
    const Atom *term, PPRelationalStateLifetimeV1 *out) {
    if (atom_is_symbol((Atom *)term, "state-persistent-v1")) {
        *out = PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT;
        return true;
    }
    if (atom_is_symbol((Atom *)term, "state-scoped-v1")) {
        *out = PPRELATIONAL_STATE_LIFETIME_V1_SCOPED;
        return true;
    }
    if (atom_is_symbol((Atom *)term, "state-transactional-v1")) {
        *out = PPRELATIONAL_STATE_LIFETIME_V1_TRANSACTIONAL;
        return true;
    }
    return false;
}

static bool ppstate_v1_parse_write_policy(
    const Atom *term, PPRelationalStateWriteV1 *out) {
    if (atom_is_symbol((Atom *)term, "state-require-key-absent-v1")) {
        *out = PPRELATIONAL_STATE_WRITE_V1_REQUIRE_KEY_ABSENT;
        return true;
    }
    if (atom_is_symbol(
            (Atom *)term, "state-insert-or-require-equal-v1")) {
        *out = PPRELATIONAL_STATE_WRITE_V1_INSERT_OR_REQUIRE_EQUAL;
        return true;
    }
    return false;
}

static bool ppstate_v1_parse_literal(
    const Atom *term, PPRelationalStateLiteralV1 *out) {
    uint8_t *rendered = NULL;
    size_t rendered_len = 0u;
    uint32_t byte_value;

    if (!out)
        return false;
    if (ppstate_v1_expr_head(term, "state-byte-literal-v1", 1u) &&
        ppstate_v1_u32(term->expr.elems[1], &byte_value) &&
        byte_value <= UINT8_MAX) {
        out->bytes = malloc(1u);
        if (!out->bytes)
            return false;
        out->bytes[0] = (uint8_t)byte_value;
        out->len = 1u;
        return true;
    }
    if (!ppstate_v1_expr_head(term, "state-literal-v1", 1u) ||
        !fh_ground_term_v1_render(term->expr.elems[1], &rendered,
                                  &rendered_len, NULL, 0u) ||
        rendered_len == 0u || rendered_len > UINT32_MAX) {
        free(rendered);
        return false;
    }
    out->bytes = rendered;
    out->len = (uint32_t)rendered_len;
    return true;
}

static bool ppstate_v1_parse_proof_machine(
    const Atom *term, PPRelationalStateRawProofMachineV1 *out) {
    const Atom *tables;
    const Atom *kinds;
    const Atom *decoder;
    uint32_t decoder_values[10];
    uint32_t index;
    const char *policy;
    const char *save_placement;
    const char *header_hypothesis_policy;

    if (!out ||
        !ppstate_v1_expr_head(term, "state-stack-proof-machine-v1", 3u))
        return false;
    tables = term->expr.elems[1];
    kinds = term->expr.elems[2];
    decoder = term->expr.elems[3];
    if (!ppstate_v1_expr_head(tables, "state-proof-tables-v1", 9u) ||
        !ppstate_v1_expr_head(kinds, "state-proof-kinds-v1", 6u) ||
        !ppstate_v1_expr_head(decoder, "state-proof-decoder-v1", 13u))
        return false;
    for (index = 0u; index < PPSTATE_V1_PROOF_TABLE_LEN; index++) {
        const char *name = ppstate_v1_symbol(tables->expr.elems[index + 1u]);
        if (!name || !(out->table_names[index] = ppstate_v1_text_dup(name)))
            return false;
    }
    if (!ppstate_v1_parse_literal(
            kinds->expr.elems[1], &out->machine.binder_hypothesis_kind) ||
        !ppstate_v1_parse_literal(
            kinds->expr.elems[2], &out->machine.matching_hypothesis_kind) ||
        !ppstate_v1_parse_literal(
            kinds->expr.elems[3], &out->machine.rule_kind_first) ||
        !ppstate_v1_parse_literal(
            kinds->expr.elems[4], &out->machine.rule_kind_second) ||
        !ppstate_v1_parse_literal(
            kinds->expr.elems[5], &out->machine.variable_symbol_kind) ||
        !ppstate_v1_parse_literal(
            kinds->expr.elems[6], &out->machine.unknown_token))
        return false;
    for (index = 0u; index < 10u; index++) {
        if (!ppstate_v1_u32(decoder->expr.elems[index + 1u],
                            &decoder_values[index]))
            return false;
    }
    policy = ppstate_v1_symbol(decoder->expr.elems[11]);
    save_placement = ppstate_v1_symbol(decoder->expr.elems[12]);
    header_hypothesis_policy =
        ppstate_v1_symbol(decoder->expr.elems[13]);
    if (!policy || !save_placement || !header_hypothesis_policy ||
        decoder_values[0] > UINT8_MAX ||
        decoder_values[1] > UINT8_MAX || decoder_values[2] > UINT8_MAX ||
        decoder_values[3] > UINT8_MAX || decoder_values[4] > UINT8_MAX ||
        decoder_values[5] > UINT8_MAX)
        return false;
    out->machine.terminal_low = (uint8_t)decoder_values[0];
    out->machine.terminal_high = (uint8_t)decoder_values[1];
    out->machine.continuation_low = (uint8_t)decoder_values[2];
    out->machine.continuation_high = (uint8_t)decoder_values[3];
    out->machine.save_byte = (uint8_t)decoder_values[4];
    out->machine.unknown_byte = (uint8_t)decoder_values[5];
    out->machine.terminal_radix = decoder_values[6];
    out->machine.terminal_digit_bias = decoder_values[7];
    out->machine.continuation_radix = decoder_values[8];
    out->machine.continuation_digit_bias = decoder_values[9];
    if (strcmp(policy, "state-proof-unknown-reject-v1") == 0) {
        out->machine.unknown_policy =
            PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_REJECT;
    } else if (strcmp(policy, "state-proof-unknown-push-claim-v1") == 0) {
        out->machine.unknown_policy =
            PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM;
    } else {
        return false;
    }
    if (strcmp(save_placement,
               "state-proof-save-immediately-after-use-v1") == 0) {
        out->machine.save_placement =
            CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1;
    } else if (strcmp(save_placement,
                      "state-proof-save-repeatable-v1") == 0) {
        out->machine.save_placement =
            CETTA_GSLT_INDEXED_SAVE_REPEATABLE_AFTER_USE_V1;
    } else {
        return false;
    }
    if (strcmp(header_hypothesis_policy,
               "state-proof-header-nonmandatory-only-v1") == 0) {
        out->machine.header_hypothesis_policy =
            CETTA_GSLT_HEADER_HYPOTHESIS_NONMANDATORY_ONLY_V1;
    } else if (strcmp(header_hypothesis_policy,
                      "state-proof-header-any-active-v1") == 0) {
        out->machine.header_hypothesis_policy =
            CETTA_GSLT_HEADER_HYPOTHESIS_ANY_ACTIVE_V1;
    } else {
        return false;
    }
    return true;
}

static bool ppstate_v1_parse_operand(
    const Atom *term, PPRelationalStateOperandV1 *out,
    char **role_name_out) {
    const char *role_name;
    uint8_t *rendered = NULL;
    size_t rendered_len = 0u;

    if (!role_name_out)
        return false;
    *role_name_out = NULL;
    memset(out, 0, sizeof(*out));
    out->role_id = UINT32_MAX;
    if (atom_is_symbol((Atom *)term, "state-input-v1")) {
        out->kind = PPRELATIONAL_STATE_OPERAND_V1_INPUT;
        return true;
    }
    if ((ppstate_v1_expr_head(term, "state-role-at-v1", 2u) ||
         ppstate_v1_expr_head(term, "state-role-list-v1", 2u)) &&
        (role_name = ppstate_v1_symbol(term->expr.elems[1])) &&
        ppstate_v1_u32(term->expr.elems[2], &out->input_index)) {
        out->kind = ppstate_v1_expr_head(
                        term, "state-role-at-v1", 2u)
                        ? PPRELATIONAL_STATE_OPERAND_V1_ROLE_AT
                        : PPRELATIONAL_STATE_OPERAND_V1_ROLE_LIST;
        *role_name_out = ppstate_v1_text_dup(role_name);
        return *role_name_out != NULL;
    }
    if (ppstate_v1_expr_head(term, "state-source-column-v1", 1u) &&
        ppstate_v1_u32(term->expr.elems[1], &out->input_index)) {
        out->kind = PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN;
        return true;
    }
    if (atom_is_symbol((Atom *)term, "state-source-row-index-v1")) {
        out->kind = PPRELATIONAL_STATE_OPERAND_V1_SOURCE_ROW_INDEX;
        return true;
    }
    if (atom_is_symbol((Atom *)term, "state-source-match-index-v1")) {
        out->kind = PPRELATIONAL_STATE_OPERAND_V1_SOURCE_MATCH_INDEX;
        return true;
    }
    if (!ppstate_v1_expr_head(term, "state-literal-v1", 1u) ||
        !fh_ground_term_v1_render(term->expr.elems[1], &rendered,
                                  &rendered_len, NULL, 0u) ||
        rendered_len == 0u || rendered_len > UINT32_MAX) {
        free(rendered);
        return false;
    }
    out->kind = PPRELATIONAL_STATE_OPERAND_V1_LITERAL;
    out->literal = rendered;
    out->literal_len = (uint32_t)rendered_len;
    return true;
}

static bool ppstate_v1_parse_row(
    const Atom *term, PPRelationalStateOperandV1 *operands,
    char **role_names, uint32_t *operand_len_out) {
    const Atom *cursor = term;
    uint32_t count = 0u;

    if (!operands || !role_names || !operand_len_out)
        return false;
    while (ppstate_v1_expr_head(cursor, "state-row-cons-v1", 2u)) {
        if (count >= PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY ||
            !ppstate_v1_parse_operand(cursor->expr.elems[1],
                                      &operands[count],
                                      &role_names[count]))
            return false;
        count++;
        cursor = cursor->expr.elems[2];
    }
    if (!atom_is_symbol((Atom *)cursor, "state-row-nil-v1") || count == 0u)
        return false;
    *operand_len_out = count;
    return true;
}

static bool ppstate_v1_parse_action(
    const Atom *term, PPRelationalStateRawActionV1 *out) {
    const char *role_name;
    const char *table_name;

    out->action.operation_id = UINT32_MAX;
    out->action.role_id = UINT32_MAX;
    out->action.table_id = UINT32_MAX;
    out->action.source_table_id = UINT32_MAX;
    out->action.condition_table_id = UINT32_MAX;
    out->action.proof_machine_id = UINT32_MAX;
    out->action.proof_label_role_id = UINT32_MAX;
    out->action.proof_formula_role_id = UINT32_MAX;
    out->action.proof_step_role_id = UINT32_MAX;
    out->action.proof_code_role_id = UINT32_MAX;
    if (ppstate_v1_expr_head(term, "state-require-depth-v1", 1u)) {
        out->action.kind = PPRELATIONAL_STATE_ACTION_V1_REQUIRE_DEPTH;
        return ppstate_v1_u32(term->expr.elems[1],
                              &out->action.required_depth);
    }
    if (atom_is_symbol((Atom *)term, "state-push-scope-v1")) {
        out->action.kind = PPRELATIONAL_STATE_ACTION_V1_PUSH_SCOPE;
        return true;
    }
    if (atom_is_symbol((Atom *)term, "state-pop-scope-v1")) {
        out->action.kind = PPRELATIONAL_STATE_ACTION_V1_POP_SCOPE;
        return true;
    }
    if (atom_is_symbol((Atom *)term, "state-noop-v1")) {
        out->action.kind = PPRELATIONAL_STATE_ACTION_V1_NOOP;
        return true;
    }
    if (ppstate_v1_expr_head(term, "state-resolve-source-v1", 3u)) {
        const char *completed_policy =
            ppstate_v1_symbol(term->expr.elems[2]);
        const char *active_policy =
            ppstate_v1_symbol(term->expr.elems[3]);
        role_name = ppstate_v1_symbol(term->expr.elems[1]);
        if (!role_name || !completed_policy || !active_policy ||
            (strcmp(completed_policy,
                    "state-skip-completed-source-v1") != 0 &&
             strcmp(completed_policy,
                    "state-reject-completed-source-v1") != 0) ||
            (strcmp(active_policy,
                    "state-skip-active-source-v1") != 0 &&
             strcmp(active_policy,
                    "state-reject-active-source-v1") != 0))
            return false;
        out->action.kind = PPRELATIONAL_STATE_ACTION_V1_RESOLVE_SOURCE;
        out->action.skip_completed_sources =
            strcmp(completed_policy,
                   "state-skip-completed-source-v1") == 0;
        out->action.reject_active_source_cycles =
            strcmp(active_policy,
                   "state-reject-active-source-v1") == 0;
        out->role_name = ppstate_v1_text_dup(role_name);
        return out->role_name != NULL;
    }
    if (ppstate_v1_expr_head(term, "state-require-each-v1", 3u)) {
        role_name = ppstate_v1_symbol(term->expr.elems[1]);
        table_name = ppstate_v1_symbol(term->expr.elems[2]);
        if (!role_name || !table_name ||
            !ppstate_v1_parse_row(
                term->expr.elems[3], out->action.operands,
                out->operand_role_names, &out->action.operand_len))
            return false;
        out->action.kind = PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH;
        out->role_name = ppstate_v1_text_dup(role_name);
        out->table_name = ppstate_v1_text_dup(table_name);
        return out->role_name && out->table_name;
    }
    if (ppstate_v1_expr_head(
            term, "state-require-each-key-absent-v1", 3u)) {
        role_name = ppstate_v1_symbol(term->expr.elems[1]);
        table_name = ppstate_v1_symbol(term->expr.elems[2]);
        if (!role_name || !table_name ||
            !ppstate_v1_parse_row(
                term->expr.elems[3], out->action.operands,
                out->operand_role_names, &out->action.operand_len))
            return false;
        out->action.kind =
            PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH_KEY_ABSENT;
        out->role_name = ppstate_v1_text_dup(role_name);
        out->table_name = ppstate_v1_text_dup(table_name);
        return out->role_name && out->table_name;
    }
    if (ppstate_v1_expr_head(
            term, "state-insert-unordered-pairs-v1", 3u)) {
        role_name = ppstate_v1_symbol(term->expr.elems[1]);
        table_name = ppstate_v1_symbol(term->expr.elems[2]);
        if (!role_name || !table_name ||
            !ppstate_v1_parse_write_policy(
                term->expr.elems[3], &out->action.write_policy))
            return false;
        out->action.kind =
            PPRELATIONAL_STATE_ACTION_V1_INSERT_UNORDERED_PAIRS;
        out->role_name = ppstate_v1_text_dup(role_name);
        out->table_name = ppstate_v1_text_dup(table_name);
        return out->role_name && out->table_name;
    }
    if (ppstate_v1_expr_head(term, "state-require-row-v1", 2u) ||
        ppstate_v1_expr_head(
            term, "state-require-row-key-absent-v1", 2u)) {
        bool absent = ppstate_v1_expr_head(
            term, "state-require-row-key-absent-v1", 2u);
        table_name = ppstate_v1_symbol(term->expr.elems[1]);
        if (!table_name ||
            !ppstate_v1_parse_row(
                term->expr.elems[2], out->action.operands,
                out->operand_role_names, &out->action.operand_len))
            return false;
        out->action.kind =
            absent ? PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW_KEY_ABSENT
                   : PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW;
        out->table_name = ppstate_v1_text_dup(table_name);
        return out->table_name != NULL;
    }
    if (ppstate_v1_expr_head(term, "state-insert-row-v1", 3u)) {
        table_name = ppstate_v1_symbol(term->expr.elems[1]);
        if (!table_name ||
            !ppstate_v1_parse_write_policy(
                term->expr.elems[2], &out->action.write_policy) ||
            !ppstate_v1_parse_row(
                term->expr.elems[3], out->action.operands,
                out->operand_role_names, &out->action.operand_len))
            return false;
        out->action.kind = PPRELATIONAL_STATE_ACTION_V1_INSERT_ROW;
        out->table_name = ppstate_v1_text_dup(table_name);
        return out->table_name != NULL;
    }
    if (ppstate_v1_expr_head(
            term, "state-insert-each-matching-v1", 6u)) {
        const char *condition_table_name;
        role_name = ppstate_v1_symbol(term->expr.elems[1]);
        condition_table_name = ppstate_v1_symbol(term->expr.elems[2]);
        table_name = ppstate_v1_symbol(term->expr.elems[4]);
        if (!role_name || !condition_table_name || !table_name ||
            !ppstate_v1_parse_row(
                term->expr.elems[3], out->action.condition_operands,
                out->condition_operand_role_names,
                &out->action.condition_operand_len) ||
            !ppstate_v1_parse_write_policy(
                term->expr.elems[5], &out->action.write_policy) ||
            !ppstate_v1_parse_row(
                term->expr.elems[6], out->action.operands,
                out->operand_role_names, &out->action.operand_len))
            return false;
        out->action.kind =
            PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING;
        out->role_name = ppstate_v1_text_dup(role_name);
        out->condition_table_name =
            ppstate_v1_text_dup(condition_table_name);
        out->table_name = ppstate_v1_text_dup(table_name);
        return out->role_name && out->condition_table_name &&
               out->table_name;
    }
    if (ppstate_v1_expr_head(term, "state-copy-each-row-v1", 4u)) {
        const char *source_table_name =
            ppstate_v1_symbol(term->expr.elems[1]);
        table_name = ppstate_v1_symbol(term->expr.elems[2]);
        if (!source_table_name || !table_name ||
            !ppstate_v1_parse_write_policy(
                term->expr.elems[3], &out->action.write_policy) ||
            !ppstate_v1_parse_row(
                term->expr.elems[4], out->action.operands,
                out->operand_role_names, &out->action.operand_len))
            return false;
        out->action.kind = PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW;
        out->source_table_name =
            ppstate_v1_text_dup(source_table_name);
        out->table_name = ppstate_v1_text_dup(table_name);
        return out->source_table_name && out->table_name;
    }
    if (ppstate_v1_expr_head(
            term, "state-copy-each-row-matching-v1", 6u)) {
        const char *source_table_name =
            ppstate_v1_symbol(term->expr.elems[1]);
        const char *condition_table_name =
            ppstate_v1_symbol(term->expr.elems[2]);
        table_name = ppstate_v1_symbol(term->expr.elems[4]);
        if (!source_table_name || !condition_table_name || !table_name ||
            !ppstate_v1_parse_row(
                term->expr.elems[3], out->action.condition_operands,
                out->condition_operand_role_names,
                &out->action.condition_operand_len) ||
            !ppstate_v1_parse_write_policy(
                term->expr.elems[5], &out->action.write_policy) ||
            !ppstate_v1_parse_row(
                term->expr.elems[6], out->action.operands,
                out->operand_role_names, &out->action.operand_len))
            return false;
        out->action.kind =
            PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING;
        out->source_table_name = ppstate_v1_text_dup(source_table_name);
        out->condition_table_name =
            ppstate_v1_text_dup(condition_table_name);
        out->table_name = ppstate_v1_text_dup(table_name);
        return out->source_table_name && out->condition_table_name &&
               out->table_name;
    }
    if (ppstate_v1_expr_head(
            term, "state-check-proof-normal-v1", 4u) ||
        ppstate_v1_expr_head(
            term, "state-check-proof-compressed-v1", 5u)) {
        bool compressed = ppstate_v1_expr_head(
            term, "state-check-proof-compressed-v1", 5u);
        uint32_t role_len = compressed ? 4u : 3u;
        const char *machine_name = ppstate_v1_symbol(term->expr.elems[1]);
        uint32_t index;
        if (!machine_name)
            return false;
        out->proof_machine_name = ppstate_v1_text_dup(machine_name);
        if (!out->proof_machine_name)
            return false;
        for (index = 0u; index < role_len; index++) {
            const char *proof_role =
                ppstate_v1_symbol(term->expr.elems[index + 2u]);
            if (!proof_role ||
                !(out->proof_role_names[index] =
                      ppstate_v1_text_dup(proof_role)))
                return false;
        }
        out->action.kind = compressed
            ? PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_COMPRESSED
            : PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_NORMAL;
        return true;
    }
    if (!ppstate_v1_expr_head(term, "state-insert-each-v1", 4u) ||
        !(role_name = ppstate_v1_symbol(term->expr.elems[1])) ||
        !(table_name = ppstate_v1_symbol(term->expr.elems[2])) ||
        !ppstate_v1_parse_write_policy(term->expr.elems[3],
                                       &out->action.write_policy) ||
        !ppstate_v1_parse_row(
            term->expr.elems[4], out->action.operands,
            out->operand_role_names, &out->action.operand_len))
        return false;
    out->action.kind = PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH;
    out->role_name = ppstate_v1_text_dup(role_name);
    out->table_name = ppstate_v1_text_dup(table_name);
    return out->role_name && out->table_name;
}

static bool ppstate_v1_answer_set_digest(
    Atom *const *records, uint32_t record_len, char digest[65]) {
    static const uint8_t domain[] = "FiniteHornAnswerSetV1\n";
    char **texts = NULL;
    CettaNativeSha256 sha;
    uint32_t index;
    bool ok = false;

    if (!records || record_len == 0u || !digest ||
        !ppstate_v1_array_fits(record_len, sizeof(*texts)))
        return false;
    texts = calloc(record_len, sizeof(*texts));
    if (!texts)
        return false;
    for (index = 0u; index < record_len; index++) {
        uint8_t *rendered = NULL;
        size_t rendered_len = 0u;
        if (!fh_ground_term_v1_render(records[index], &rendered,
                                      &rendered_len, NULL, 0u) ||
            rendered_len == 0u || rendered_len > SIZE_MAX - 1u) {
            free(rendered);
            goto done;
        }
        texts[index] = (char *)rendered;
    }
    qsort(texts, record_len, sizeof(*texts), ppstate_v1_text_compare);
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    for (index = 0u; index < record_len; index++) {
        if (index > 0u && strcmp(texts[index - 1u], texts[index]) == 0)
            goto done;
        cetta_native_sha256_update(
            &sha, (const uint8_t *)texts[index], strlen(texts[index]));
        cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    }
    cetta_native_sha256_finish_hex(&sha, digest);
    ok = true;

done:
    for (index = 0u; index < record_len; index++)
        free(texts[index]);
    free(texts);
    return ok;
}

static void ppstate_v1_sha_u32(CettaNativeSha256 *sha, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)(value >> 24u), (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u), (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppstate_v1_sha_bytes(CettaNativeSha256 *sha,
                                 const uint8_t *bytes, uint32_t len) {
    ppstate_v1_sha_u32(sha, len);
    if (len > 0u)
        cetta_native_sha256_update(sha, bytes, len);
}

static void ppstate_v1_sha_text(CettaNativeSha256 *sha, const char *text) {
    ppstate_v1_sha_bytes(sha, (const uint8_t *)text,
                         text ? (uint32_t)strlen(text) : 0u);
}

static void ppstate_v1_sha_action(
    CettaNativeSha256 *sha, const PPRelationalStateActionV1 *action) {
    uint32_t index;

    ppstate_v1_sha_u32(sha, (uint32_t)action->kind);
    ppstate_v1_sha_u32(sha, action->operation_id);
    ppstate_v1_sha_u32(sha, action->role_id);
    ppstate_v1_sha_u32(sha, action->table_id);
    ppstate_v1_sha_u32(sha, action->source_table_id);
    ppstate_v1_sha_u32(sha, action->condition_table_id);
    ppstate_v1_sha_u32(sha, (uint32_t)action->write_policy);
    ppstate_v1_sha_u32(sha, action->required_depth);
    ppstate_v1_sha_u32(sha, action->proof_machine_id);
    ppstate_v1_sha_u32(sha, action->proof_label_role_id);
    ppstate_v1_sha_u32(sha, action->proof_formula_role_id);
    ppstate_v1_sha_u32(sha, action->proof_step_role_id);
    ppstate_v1_sha_u32(sha, action->proof_code_role_id);
    ppstate_v1_sha_u32(
        sha, action->skip_completed_sources ? 1u : 0u);
    ppstate_v1_sha_u32(
        sha, action->reject_active_source_cycles ? 1u : 0u);
    ppstate_v1_sha_u32(sha, action->operand_len);
    for (index = 0u; index < action->operand_len; index++) {
        ppstate_v1_sha_u32(sha, (uint32_t)action->operands[index].kind);
        ppstate_v1_sha_u32(sha, action->operands[index].role_id);
        ppstate_v1_sha_u32(sha, action->operands[index].input_index);
        ppstate_v1_sha_bytes(sha, action->operands[index].literal,
                             action->operands[index].literal_len);
    }
    ppstate_v1_sha_u32(sha, action->condition_operand_len);
    for (index = 0u; index < action->condition_operand_len; index++) {
        ppstate_v1_sha_u32(
            sha, (uint32_t)action->condition_operands[index].kind);
        ppstate_v1_sha_u32(
            sha, action->condition_operands[index].role_id);
        ppstate_v1_sha_u32(
            sha, action->condition_operands[index].input_index);
        ppstate_v1_sha_bytes(
            sha, action->condition_operands[index].literal,
            action->condition_operands[index].literal_len);
    }
}

static void ppstate_v1_sha_proof_machine(
    CettaNativeSha256 *sha,
    const PPRelationalStateProofMachineV1 *machine) {
    ppstate_v1_sha_text(sha, machine->name);
    ppstate_v1_sha_u32(sha, machine->label_kind_table);
    ppstate_v1_sha_u32(sha, machine->formula_table);
    ppstate_v1_sha_u32(sha, machine->binder_variable_table);
    ppstate_v1_sha_u32(sha, machine->mandatory_variable_table);
    ppstate_v1_sha_u32(sha, machine->assertion_hypothesis_table);
    ppstate_v1_sha_u32(sha, machine->assertion_disjoint_table);
    ppstate_v1_sha_u32(sha, machine->active_hypothesis_table);
    ppstate_v1_sha_u32(sha, machine->active_disjoint_table);
    ppstate_v1_sha_u32(sha, machine->symbol_kind_table);
    ppstate_v1_sha_bytes(sha, machine->binder_hypothesis_kind.bytes,
                         machine->binder_hypothesis_kind.len);
    ppstate_v1_sha_bytes(sha, machine->matching_hypothesis_kind.bytes,
                         machine->matching_hypothesis_kind.len);
    ppstate_v1_sha_bytes(sha, machine->rule_kind_first.bytes,
                         machine->rule_kind_first.len);
    ppstate_v1_sha_bytes(sha, machine->rule_kind_second.bytes,
                         machine->rule_kind_second.len);
    ppstate_v1_sha_bytes(sha, machine->variable_symbol_kind.bytes,
                         machine->variable_symbol_kind.len);
    ppstate_v1_sha_bytes(sha, machine->unknown_token.bytes,
                         machine->unknown_token.len);
    ppstate_v1_sha_u32(sha, machine->terminal_low);
    ppstate_v1_sha_u32(sha, machine->terminal_high);
    ppstate_v1_sha_u32(sha, machine->continuation_low);
    ppstate_v1_sha_u32(sha, machine->continuation_high);
    ppstate_v1_sha_u32(sha, machine->save_byte);
    ppstate_v1_sha_u32(sha, machine->unknown_byte);
    ppstate_v1_sha_u32(sha, machine->terminal_radix);
    ppstate_v1_sha_u32(sha, machine->terminal_digit_bias);
    ppstate_v1_sha_u32(sha, machine->continuation_radix);
    ppstate_v1_sha_u32(sha, machine->continuation_digit_bias);
    ppstate_v1_sha_u32(sha, (uint32_t)machine->unknown_policy);
    ppstate_v1_sha_u32(sha, (uint32_t)machine->save_placement);
}

static bool ppstate_v1_plan_digest(
    const PPRelationalStateProgramV1Plan *plan, char digest[65]) {
    static const uint8_t domain[] = "RelationalStateProgramV1";
    CettaNativeSha256 sha;
    uint32_t index;

    if (!plan || !digest ||
        !ppstate_v1_digest_valid(plan->occurrence_fold_plan_digest) ||
        !ppstate_v1_digest_valid(plan->compiler_answer_digest))
        return false;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    ppstate_v1_sha_text(&sha, plan->occurrence_fold_plan_digest);
    ppstate_v1_sha_text(&sha, plan->compiler_answer_digest);
    ppstate_v1_sha_u32(&sha, plan->table_len);
    ppstate_v1_sha_u32(&sha, plan->proof_machine_len);
    ppstate_v1_sha_u32(&sha, plan->action_len);
    ppstate_v1_sha_u32(&sha, plan->final_action_len);
    ppstate_v1_sha_u32(&sha, plan->operation_len);
    for (index = 0u; index < plan->table_len; index++) {
        const PPRelationalStateTableV1 *table = &plan->tables[index];
        ppstate_v1_sha_text(&sha, table->name);
        ppstate_v1_sha_u32(&sha, table->arity);
        ppstate_v1_sha_u32(&sha, table->key_arity);
        ppstate_v1_sha_u32(&sha, (uint32_t)table->lifetime);
    }
    for (index = 0u; index < plan->proof_machine_len; index++)
        ppstate_v1_sha_proof_machine(
            &sha, &plan->proof_machines[index]);
    for (index = 0u; index < plan->action_len; index++)
        ppstate_v1_sha_action(&sha, &plan->actions[index]);
    for (index = 0u; index < plan->final_action_len; index++)
        ppstate_v1_sha_action(&sha, &plan->final_actions[index]);
    for (index = 0u; index < plan->operation_len; index++) {
        ppstate_v1_sha_u32(&sha, plan->operations[index].action_begin);
        ppstate_v1_sha_u32(&sha, plan->operations[index].action_len);
    }
    cetta_native_sha256_finish_hex(&sha, digest);
    return true;
}

static bool ppstate_v1_role_allowed(
    const PPOccurrenceFoldV1Plan *fold, uint32_t operation_id,
    uint32_t role_id) {
    uint32_t index;

    for (index = 0u; index < fold->terminal_len; index++) {
        if (fold->terminals[index].shift_operation_id == operation_id &&
            fold->terminals[index].role_id == role_id)
            return true;
    }
    for (index = 0u; index < fold->production_len; index++) {
        const PPOccurrenceFoldV1ProductionBinding *binding =
            &fold->productions[index];
        const PPOccurrenceFoldV1Contract *contract;
        uint32_t transition_index;
        if (binding->operation_id != operation_id)
            continue;
        contract = &fold->contracts[binding->contract_index];
        for (transition_index = 0u;
             transition_index < contract->transition_len;
             transition_index++) {
            const PPOccurrenceFoldV1Transition *transition =
                &fold->transitions[
                    contract->transition_begin + transition_index];
            if (transition->kind ==
                    PPOCCURRENCE_FOLD_V1_TRANSITION_ROLE &&
                transition->role_id == role_id)
                return true;
        }
    }
    return false;
}

static bool ppstate_v1_operand_validate(
    const PPOccurrenceFoldV1Plan *fold,
    const PPRelationalStateActionV1 *action,
    const PPRelationalStateOperandV1 *operand,
    bool driver_action) {
    if (operand->kind == PPRELATIONAL_STATE_OPERAND_V1_INPUT) {
        return driver_action && operand->role_id == UINT32_MAX &&
               operand->input_index == 0u && !operand->literal &&
               operand->literal_len == 0u;
    }
    if (operand->kind == PPRELATIONAL_STATE_OPERAND_V1_LITERAL) {
        return operand->role_id == UINT32_MAX &&
               operand->input_index == 0u && operand->literal &&
               operand->literal_len > 0u;
    }
    if (operand->kind == PPRELATIONAL_STATE_OPERAND_V1_ROLE_AT ||
        operand->kind == PPRELATIONAL_STATE_OPERAND_V1_ROLE_LIST) {
        return operand->role_id < fold->role_len && !operand->literal &&
               operand->literal_len == 0u &&
               ppstate_v1_role_allowed(
                   fold, action->operation_id, operand->role_id);
    }
    if (operand->kind ==
        PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN) {
        return (action->kind ==
                    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW ||
                action->kind ==
                    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING) &&
               operand->role_id == UINT32_MAX && !operand->literal &&
               operand->literal_len == 0u;
    }
    if (operand->kind ==
        PPRELATIONAL_STATE_OPERAND_V1_SOURCE_ROW_INDEX) {
        return (action->kind ==
                    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW ||
                action->kind ==
                    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING) &&
               operand->role_id == UINT32_MAX &&
               operand->input_index == 0u && !operand->literal &&
               operand->literal_len == 0u;
    }
    if (operand->kind ==
        PPRELATIONAL_STATE_OPERAND_V1_SOURCE_MATCH_INDEX) {
        return action->kind ==
                   PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING &&
               operand->role_id == UINT32_MAX &&
               operand->input_index == 0u && !operand->literal &&
               operand->literal_len == 0u;
    }
    return false;
}

static bool ppstate_v1_action_validate(
    const PPOccurrenceFoldV1Plan *fold,
    const PPRelationalStateProgramV1Plan *plan,
    const PPRelationalStateActionV1 *action, bool final) {
    const PPRelationalStateTableV1 *table;
    bool driver_action;
    bool row_action;
    uint32_t required_len;
    uint32_t index;

    if (action->kind >
        PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING)
        return false;
    if (final) {
        return action->operation_id == UINT32_MAX &&
               action->role_id == UINT32_MAX &&
               action->table_id == UINT32_MAX &&
               action->source_table_id == UINT32_MAX &&
               action->condition_table_id == UINT32_MAX &&
               action->proof_machine_id == UINT32_MAX &&
               action->proof_label_role_id == UINT32_MAX &&
               action->proof_formula_role_id == UINT32_MAX &&
               action->proof_step_role_id == UINT32_MAX &&
               action->proof_code_role_id == UINT32_MAX &&
               !action->skip_completed_sources &&
               !action->reject_active_source_cycles &&
               action->operand_len == 0u &&
               action->condition_operand_len == 0u &&
               (action->kind ==
                    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_DEPTH ||
                action->kind == PPRELATIONAL_STATE_ACTION_V1_NOOP);
    }
    if (action->operation_id >= plan->operation_len)
        return false;
    if (action->kind == PPRELATIONAL_STATE_ACTION_V1_RESOLVE_SOURCE) {
        return action->role_id < fold->role_len &&
               ppstate_v1_role_allowed(
                   fold, action->operation_id, action->role_id) &&
               action->table_id == UINT32_MAX &&
               action->source_table_id == UINT32_MAX &&
               action->condition_table_id == UINT32_MAX &&
               action->proof_machine_id == UINT32_MAX &&
               action->proof_label_role_id == UINT32_MAX &&
               action->proof_formula_role_id == UINT32_MAX &&
               action->proof_step_role_id == UINT32_MAX &&
               action->proof_code_role_id == UINT32_MAX &&
               action->operand_len == 0u &&
               action->condition_operand_len == 0u;
    }
    if (action->kind ==
            PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_NORMAL ||
        action->kind ==
            PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_COMPRESSED) {
        bool compressed = action->kind ==
            PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_COMPRESSED;
        if (action->proof_machine_id >= plan->proof_machine_len ||
            action->proof_label_role_id >= fold->role_len ||
            action->proof_formula_role_id >= fold->role_len ||
            action->proof_step_role_id >= fold->role_len ||
            (compressed && action->proof_code_role_id >= fold->role_len) ||
            (!compressed && action->proof_code_role_id != UINT32_MAX) ||
            action->proof_label_role_id == action->proof_formula_role_id ||
            action->proof_label_role_id == action->proof_step_role_id ||
            action->proof_formula_role_id == action->proof_step_role_id ||
            (compressed &&
             (action->proof_code_role_id == action->proof_label_role_id ||
              action->proof_code_role_id == action->proof_formula_role_id ||
              action->proof_code_role_id == action->proof_step_role_id)) ||
            !ppstate_v1_role_allowed(
                fold, action->operation_id,
                action->proof_label_role_id) ||
            !ppstate_v1_role_allowed(
                fold, action->operation_id,
                action->proof_formula_role_id) ||
            !ppstate_v1_role_allowed(
                fold, action->operation_id,
                action->proof_step_role_id) ||
            (compressed && !ppstate_v1_role_allowed(
                fold, action->operation_id,
                action->proof_code_role_id)) ||
            action->role_id != UINT32_MAX ||
            action->table_id != UINT32_MAX ||
            action->source_table_id != UINT32_MAX ||
            action->condition_table_id != UINT32_MAX ||
            action->operand_len != 0u ||
            action->condition_operand_len != 0u ||
            action->skip_completed_sources ||
            action->reject_active_source_cycles)
            return false;
        return true;
    }
    if (action->proof_machine_id != UINT32_MAX ||
        action->proof_label_role_id != UINT32_MAX ||
        action->proof_formula_role_id != UINT32_MAX ||
            action->proof_step_role_id != UINT32_MAX ||
            action->proof_code_role_id != UINT32_MAX)
        return false;
    if (action->skip_completed_sources ||
        action->reject_active_source_cycles)
        return false;
    driver_action =
        action->kind == PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH ||
        action->kind == PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH ||
        action->kind ==
            PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH_KEY_ABSENT ||
        action->kind ==
            PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING;
    row_action =
        action->kind == PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW ||
        action->kind ==
            PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW_KEY_ABSENT ||
        action->kind == PPRELATIONAL_STATE_ACTION_V1_INSERT_ROW ||
        action->kind == PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW ||
        action->kind ==
            PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING;
    if (!driver_action && !row_action &&
        action->kind !=
            PPRELATIONAL_STATE_ACTION_V1_INSERT_UNORDERED_PAIRS) {
        return action->role_id == UINT32_MAX &&
               action->table_id == UINT32_MAX &&
               action->source_table_id == UINT32_MAX &&
               action->condition_table_id == UINT32_MAX &&
               action->condition_operand_len == 0u &&
               action->operand_len == 0u;
    }
    if (action->table_id >= plan->table_len)
        return false;
    if (driver_action ||
        action->kind ==
            PPRELATIONAL_STATE_ACTION_V1_INSERT_UNORDERED_PAIRS) {
        if (action->role_id >= fold->role_len ||
            !ppstate_v1_role_allowed(
                fold, action->operation_id, action->role_id))
            return false;
    } else if (action->role_id != UINT32_MAX) {
        return false;
    }
    table = &plan->tables[action->table_id];
    if (action->kind ==
        PPRELATIONAL_STATE_ACTION_V1_INSERT_UNORDERED_PAIRS) {
        return table->arity == 2u && table->key_arity == 2u &&
               action->operand_len == 0u &&
               action->source_table_id == UINT32_MAX &&
               action->condition_table_id == UINT32_MAX &&
               action->condition_operand_len == 0u &&
               action->write_policy <=
                   PPRELATIONAL_STATE_WRITE_V1_INSERT_OR_REQUIRE_EQUAL;
    }
    if (action->kind ==
        PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING) {
        const PPRelationalStateTableV1 *condition;
        if (action->source_table_id != UINT32_MAX ||
            action->condition_table_id >= plan->table_len)
            return false;
        condition = &plan->tables[action->condition_table_id];
        if (action->condition_operand_len != condition->arity ||
            action->write_policy >
                PPRELATIONAL_STATE_WRITE_V1_INSERT_OR_REQUIRE_EQUAL)
            return false;
        for (index = 0u; index < action->condition_operand_len; index++) {
            if (!ppstate_v1_operand_validate(
                    fold, action, &action->condition_operands[index], true))
                return false;
        }
    } else if (action->kind ==
               PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW) {
        if (action->source_table_id >= plan->table_len ||
            action->source_table_id == action->table_id ||
            action->condition_table_id != UINT32_MAX ||
            action->condition_operand_len != 0u ||
            action->write_policy >
                PPRELATIONAL_STATE_WRITE_V1_INSERT_OR_REQUIRE_EQUAL)
            return false;
    } else if (action->kind ==
               PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING) {
        const PPRelationalStateTableV1 *condition;
        if (action->source_table_id >= plan->table_len ||
            action->source_table_id == action->table_id ||
            action->condition_table_id >= plan->table_len ||
            action->condition_table_id == action->table_id ||
            action->write_policy >
                PPRELATIONAL_STATE_WRITE_V1_INSERT_OR_REQUIRE_EQUAL)
            return false;
        condition = &plan->tables[action->condition_table_id];
        if (action->condition_operand_len != condition->arity)
            return false;
        for (index = 0u; index < action->condition_operand_len; index++) {
            if (!ppstate_v1_operand_validate(
                    fold, action, &action->condition_operands[index], false))
                return false;
            if (action->condition_operands[index].kind ==
                    PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN &&
                action->condition_operands[index].input_index >=
                    plan->tables[action->source_table_id].arity)
                return false;
        }
    } else if (action->source_table_id != UINT32_MAX ||
               action->condition_table_id != UINT32_MAX ||
               action->condition_operand_len != 0u) {
        return false;
    }
    required_len =
        action->kind ==
                    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH_KEY_ABSENT ||
                action->kind ==
                    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW_KEY_ABSENT
            ? table->key_arity
            : table->arity;
    if (action->operand_len != required_len ||
        ((action->kind == PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH ||
          action->kind == PPRELATIONAL_STATE_ACTION_V1_INSERT_ROW ||
          action->kind ==
              PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING ||
          action->kind ==
              PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW ||
          action->kind ==
              PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING) &&
         action->write_policy >
             PPRELATIONAL_STATE_WRITE_V1_INSERT_OR_REQUIRE_EQUAL) ||
        ((action->kind == PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH ||
          action->kind ==
              PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH_KEY_ABSENT ||
          action->kind == PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW ||
          action->kind ==
              PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW_KEY_ABSENT) &&
         action->write_policy !=
             PPRELATIONAL_STATE_WRITE_V1_REQUIRE_KEY_ABSENT))
        return false;
    for (index = 0u; index < action->operand_len; index++) {
        if (!ppstate_v1_operand_validate(
                fold, action, &action->operands[index], driver_action))
            return false;
        if (action->operands[index].kind ==
                PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN &&
            action->operands[index].input_index >=
                plan->tables[action->source_table_id].arity)
            return false;
    }
    return true;
}

static bool ppstate_v1_literal_equal(
    const PPRelationalStateLiteralV1 *left,
    const PPRelationalStateLiteralV1 *right) {
    return left->len == right->len && left->bytes && right->bytes &&
           memcmp(left->bytes, right->bytes, left->len) == 0;
}

static bool ppstate_v1_proof_table_shape(
    const PPRelationalStateProgramV1Plan *plan, uint32_t table_id,
    uint32_t arity, uint32_t key_arity) {
    return table_id < plan->table_len &&
           plan->tables[table_id].arity == arity &&
           plan->tables[table_id].key_arity == key_arity;
}

static bool ppstate_v1_proof_machine_validate_plan(
    const PPRelationalStateProgramV1Plan *plan,
    const PPRelationalStateProofMachineV1 *machine) {
    const PPRelationalStateLiteralV1 *literals[] = {
        &machine->binder_hypothesis_kind,
        &machine->matching_hypothesis_kind,
        &machine->rule_kind_first,
        &machine->rule_kind_second,
        &machine->variable_symbol_kind,
        &machine->unknown_token,
    };
    uint32_t index;

    if (!machine->name ||
        !ppstate_v1_proof_table_shape(
            plan, machine->label_kind_table, 2u, 1u) ||
        !ppstate_v1_proof_table_shape(
            plan, machine->formula_table, 2u, 1u) ||
        !ppstate_v1_proof_table_shape(
            plan, machine->binder_variable_table, 2u, 1u) ||
        !ppstate_v1_proof_table_shape(
            plan, machine->mandatory_variable_table, 2u, 2u) ||
        !ppstate_v1_proof_table_shape(
            plan, machine->assertion_hypothesis_table, 3u, 2u) ||
        !ppstate_v1_proof_table_shape(
            plan, machine->assertion_disjoint_table, 3u, 3u) ||
        !ppstate_v1_proof_table_shape(
            plan, machine->active_hypothesis_table, 4u, 1u) ||
        !ppstate_v1_proof_table_shape(
            plan, machine->active_disjoint_table, 2u, 2u) ||
        !ppstate_v1_proof_table_shape(
            plan, machine->symbol_kind_table, 2u, 1u))
        return false;
    for (index = 0u; index < sizeof(literals) / sizeof(literals[0]);
         index++) {
        if (!literals[index]->bytes || literals[index]->len == 0u)
            return false;
    }
    return !ppstate_v1_literal_equal(
               &machine->binder_hypothesis_kind,
               &machine->matching_hypothesis_kind) &&
           !ppstate_v1_literal_equal(
               &machine->rule_kind_first, &machine->rule_kind_second) &&
           machine->terminal_low <= machine->terminal_high &&
           machine->continuation_low <= machine->continuation_high &&
           (machine->terminal_high < machine->continuation_low ||
            machine->continuation_high < machine->terminal_low) &&
           !(machine->save_byte >= machine->terminal_low &&
             machine->save_byte <= machine->terminal_high) &&
           !(machine->save_byte >= machine->continuation_low &&
             machine->save_byte <= machine->continuation_high) &&
           !(machine->unknown_byte >= machine->terminal_low &&
             machine->unknown_byte <= machine->terminal_high) &&
           !(machine->unknown_byte >= machine->continuation_low &&
             machine->unknown_byte <= machine->continuation_high) &&
           machine->save_byte != machine->unknown_byte &&
           machine->terminal_radix > 0u &&
           machine->continuation_radix > 0u &&
           machine->unknown_policy <=
               PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM &&
           (machine->save_placement ==
                CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1 ||
            machine->save_placement ==
                CETTA_GSLT_INDEXED_SAVE_REPEATABLE_AFTER_USE_V1) &&
           (machine->header_hypothesis_policy ==
                CETTA_GSLT_HEADER_HYPOTHESIS_NONMANDATORY_ONLY_V1 ||
            machine->header_hypothesis_policy ==
                CETTA_GSLT_HEADER_HYPOTHESIS_ANY_ACTIVE_V1);
}

bool pprelational_state_program_v1_plan_validate(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;
    uint32_t covered = 0u;
    char digest[65];

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!occurrence_plan || !plan || plan->table_len == 0u ||
        !plan->tables || !plan->operations ||
        (plan->proof_machine_len > 0u && !plan->proof_machines) ||
        plan->operation_len != occurrence_plan->operation_len ||
        (plan->action_len > 0u && !plan->actions) ||
        (plan->final_action_len > 0u && !plan->final_actions) ||
        !ppstate_v1_digest_valid(plan->occurrence_fold_plan_digest) ||
        !ppstate_v1_digest_valid(plan->compiler_answer_digest) ||
        !ppstate_v1_digest_valid(plan->plan_digest) ||
        strcmp(plan->occurrence_fold_plan_digest,
               occurrence_plan->plan_digest) != 0) {
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "invalid relational state plan header");
        return false;
    }
    for (index = 0u; index < plan->table_len; index++) {
        const PPRelationalStateTableV1 *table = &plan->tables[index];
        if (!table->name || table->arity == 0u ||
            table->arity > PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY ||
            table->key_arity == 0u || table->key_arity > table->arity ||
            table->lifetime >
                PPRELATIONAL_STATE_LIFETIME_V1_TRANSACTIONAL ||
            (index > 0u &&
             strcmp(plan->tables[index - 1u].name, table->name) >= 0)) {
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "invalid relational state table");
            return false;
        }
    }
    for (index = 0u; index < plan->proof_machine_len; index++) {
        if (!ppstate_v1_proof_machine_validate_plan(
                plan, &plan->proof_machines[index]) ||
            (index > 0u && strcmp(
                plan->proof_machines[index - 1u].name,
                plan->proof_machines[index].name) >= 0)) {
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "invalid relational proof machine");
            return false;
        }
    }
    for (index = 0u; index < plan->operation_len; index++) {
        const PPRelationalStateOperationV1 *program =
            &plan->operations[index];
        uint32_t action_index;
        if (program->action_len == 0u) {
            if (program->action_begin != UINT32_MAX)
                return false;
            continue;
        }
        if (program->action_begin != covered ||
            program->action_begin > plan->action_len ||
            program->action_len >
                plan->action_len - program->action_begin) {
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state operation program is not dense");
            return false;
        }
        for (action_index = 0u; action_index < program->action_len;
             action_index++) {
            const PPRelationalStateActionV1 *action =
                &plan->actions[program->action_begin + action_index];
            if (action->operation_id != index ||
                !ppstate_v1_action_validate(
                    occurrence_plan, plan, action, false)) {
                ppstate_v1_set_error(error_buf, error_buf_size,
                                     "invalid state operation action");
                return false;
            }
        }
        covered += program->action_len;
    }
    if (covered != plan->action_len)
        return false;
    for (index = 0u; index < plan->final_action_len; index++) {
        if (!ppstate_v1_action_validate(
                occurrence_plan, plan, &plan->final_actions[index], true)) {
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "invalid final state action");
            return false;
        }
    }
    if (!ppstate_v1_plan_digest(plan, digest) ||
        strcmp(digest, plan->plan_digest) != 0) {
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "relational state plan digest changed");
        return false;
    }
    return true;
}

bool pprelational_state_program_v1_operation_resolves_source(
    const PPRelationalStateProgramV1Plan *plan,
    uint32_t operation_id) {
    const PPRelationalStateOperationV1 *program;
    uint32_t index;

    if (!plan || operation_id >= plan->operation_len)
        return false;
    program = &plan->operations[operation_id];
    if (program->action_len == 0u ||
        program->action_begin > plan->action_len ||
        program->action_len > plan->action_len - program->action_begin)
        return false;
    for (index = 0u; index < program->action_len; index++) {
        if (plan->actions[program->action_begin + index].kind ==
            PPRELATIONAL_STATE_ACTION_V1_RESOLVE_SOURCE)
            return true;
    }
    return false;
}

bool pprelational_state_program_v1_plan_build(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    Atom *const *records,
    size_t record_len,
    const char *compiler_answer_digest,
    PPRelationalStateProgramV1Plan *out,
    char *error_buf,
    size_t error_buf_size) {
    PPRelationalStateProgramV1Plan result;
    PPRelationalStateRawTableV1 *tables = NULL;
    PPRelationalStateRawProofMachineV1 *proof_machines = NULL;
    PPRelationalStateRawActionV1 *actions = NULL;
    PPRelationalStateRawActionV1 *final_actions = NULL;
    uint32_t table_len = 0u;
    uint32_t proof_machine_len = 0u;
    uint32_t action_len = 0u;
    uint32_t final_action_len = 0u;
    uint32_t table_write = 0u;
    uint32_t proof_machine_write = 0u;
    uint32_t action_write = 0u;
    uint32_t final_write = 0u;
    uint32_t index;
    char computed_answers[65];
    bool ok = false;

    pprelational_state_program_v1_plan_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!occurrence_plan || !out || !records || record_len == 0u ||
        record_len > PPRELATIONAL_STATE_V1_MAX_RECORDS ||
        record_len > UINT32_MAX ||
        !ppstate_v1_digest_valid(compiler_answer_digest) ||
        !ppstate_v1_answer_set_digest(
            records, (uint32_t)record_len, computed_answers) ||
        strcmp(computed_answers, compiler_answer_digest) != 0) {
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "bad relational state compiler input");
        goto done;
    }
    for (index = 0u; index < (uint32_t)record_len; index++) {
        if (ppstate_v1_expr_head(records[index], "state-table-v1", 4u))
            table_len++;
        else if (ppstate_v1_expr_head(
                     records[index], "state-proof-machine-v1", 2u))
            proof_machine_len++;
        else if (ppstate_v1_expr_head(
                     records[index], "state-action-v1", 3u))
            action_len++;
        else if (ppstate_v1_expr_head(
                     records[index], "state-final-action-v1", 2u))
            final_action_len++;
        else {
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "unknown relational state record");
            goto done;
        }
    }
    if (table_len == 0u ||
        !ppstate_v1_array_fits(table_len, sizeof(*tables)) ||
        !ppstate_v1_array_fits(
            proof_machine_len, sizeof(*proof_machines)) ||
        !ppstate_v1_array_fits(action_len, sizeof(*actions)) ||
        !ppstate_v1_array_fits(final_action_len,
                               sizeof(*final_actions)))
        goto done;
    tables = calloc(table_len, sizeof(*tables));
    proof_machines = calloc(
        proof_machine_len ? proof_machine_len : 1u,
        sizeof(*proof_machines));
    actions = calloc(action_len ? action_len : 1u, sizeof(*actions));
    final_actions = calloc(final_action_len ? final_action_len : 1u,
                           sizeof(*final_actions));
    if (!tables || !proof_machines || !actions || !final_actions)
        goto done;
    for (index = 0u; index < (uint32_t)record_len; index++) {
        Atom *record = records[index];
        if (ppstate_v1_expr_head(record, "state-table-v1", 4u)) {
            PPRelationalStateRawTableV1 *raw = &tables[table_write++];
            const char *name = ppstate_v1_symbol(record->expr.elems[1]);
            if (!name ||
                !ppstate_v1_u32(record->expr.elems[2], &raw->table.arity) ||
                !ppstate_v1_u32(record->expr.elems[3],
                                &raw->table.key_arity) ||
                !ppstate_v1_parse_lifetime(record->expr.elems[4],
                                           &raw->table.lifetime) ||
                !(raw->table.name = ppstate_v1_text_dup(name)))
                goto done;
        } else if (ppstate_v1_expr_head(
                       record, "state-proof-machine-v1", 2u)) {
            PPRelationalStateRawProofMachineV1 *raw =
                &proof_machines[proof_machine_write++];
            const char *name = ppstate_v1_symbol(record->expr.elems[1]);
            if (!name || !(raw->machine.name = ppstate_v1_text_dup(name)) ||
                !ppstate_v1_parse_proof_machine(
                    record->expr.elems[2], raw))
                goto done;
        } else {
            PPRelationalStateRawActionV1 *raw;
            const char *operation = NULL;
            if (ppstate_v1_expr_head(record, "state-action-v1", 3u)) {
                raw = &actions[action_write++];
                operation = ppstate_v1_symbol(record->expr.elems[1]);
                if (!operation ||
                    !(raw->operation_name =
                          ppstate_v1_text_dup(operation)) ||
                    !ppstate_v1_u32(record->expr.elems[2], &raw->index) ||
                    !ppstate_v1_parse_action(record->expr.elems[3], raw))
                    goto done;
            } else {
                raw = &final_actions[final_write++];
                if (!ppstate_v1_u32(record->expr.elems[1], &raw->index) ||
                    !ppstate_v1_parse_action(record->expr.elems[2], raw))
                    goto done;
            }
        }
    }
    qsort(tables, table_len, sizeof(*tables), ppstate_v1_table_compare);
    qsort(proof_machines, proof_machine_len, sizeof(*proof_machines),
          ppstate_v1_proof_machine_compare);
    qsort(actions, action_len, sizeof(*actions), ppstate_v1_action_compare);
    qsort(final_actions, final_action_len, sizeof(*final_actions),
          ppstate_v1_final_compare);
    result.tables = calloc(table_len, sizeof(*result.tables));
    result.proof_machines = calloc(
        proof_machine_len ? proof_machine_len : 1u,
        sizeof(*result.proof_machines));
    result.actions = calloc(action_len ? action_len : 1u,
                            sizeof(*result.actions));
    result.final_actions = calloc(final_action_len ? final_action_len : 1u,
                                  sizeof(*result.final_actions));
    result.operations = calloc(occurrence_plan->operation_len,
                               sizeof(*result.operations));
    if (!result.tables || !result.proof_machines || !result.actions ||
        !result.final_actions || !result.operations)
        goto done;
    result.table_len = table_len;
    result.proof_machine_len = proof_machine_len;
    result.action_len = action_len;
    result.final_action_len = final_action_len;
    result.operation_len = occurrence_plan->operation_len;
    for (index = 0u; index < result.operation_len; index++)
        result.operations[index].action_begin = UINT32_MAX;
    for (index = 0u; index < table_len; index++) {
        if (index > 0u &&
            strcmp(result.tables[index - 1u].name,
                   tables[index].table.name) == 0) {
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state table is repeated");
            goto done;
        }
        result.tables[index] = tables[index].table;
        memset(&tables[index].table, 0, sizeof(tables[index].table));
    }
    for (index = 0u; index < proof_machine_len; index++) {
        PPRelationalStateRawProofMachineV1 *raw = &proof_machines[index];
        PPRelationalStateProofMachineV1 *compiled =
            &result.proof_machines[index];
        uint32_t *table_fields[PPSTATE_V1_PROOF_TABLE_LEN] = {
            &raw->machine.label_kind_table,
            &raw->machine.formula_table,
            &raw->machine.binder_variable_table,
            &raw->machine.mandatory_variable_table,
            &raw->machine.assertion_hypothesis_table,
            &raw->machine.assertion_disjoint_table,
            &raw->machine.active_hypothesis_table,
            &raw->machine.active_disjoint_table,
            &raw->machine.symbol_kind_table,
        };
        uint32_t table_index;
        if (index > 0u && strcmp(
                result.proof_machines[index - 1u].name,
                raw->machine.name) == 0) {
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state proof machine is repeated");
            goto done;
        }
        for (table_index = 0u;
             table_index < PPSTATE_V1_PROOF_TABLE_LEN; table_index++) {
            int32_t table_id = ppstate_v1_table_find(
                &result, raw->table_names[table_index]);
            if (table_id < 0)
                goto done;
            *table_fields[table_index] = (uint32_t)table_id;
        }
        *compiled = raw->machine;
        memset(&raw->machine, 0, sizeof(raw->machine));
    }
    index = 0u;
    while (index < action_len) {
        uint32_t group_begin = index;
        uint32_t expected_index = 0u;
        int32_t operation_id = ppstate_v1_name_find(
            occurrence_plan->operations, occurrence_plan->operation_len,
            actions[index].operation_name);
        if (operation_id < 0) {
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state action uses an unknown operation");
            goto done;
        }
        while (index < action_len &&
               strcmp(actions[group_begin].operation_name,
                      actions[index].operation_name) == 0) {
            PPRelationalStateRawActionV1 *raw = &actions[index];
            PPRelationalStateActionV1 *compiled = &result.actions[index];
            int32_t role_id = -1;
            int32_t table_id = -1;
            uint32_t operand_index;
            if (raw->index != expected_index++) {
                ppstate_v1_set_error(error_buf, error_buf_size,
                                     "state action indices are not dense");
                goto done;
            }
            if (raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH_KEY_ABSENT ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_INSERT_UNORDERED_PAIRS ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_RESOLVE_SOURCE) {
                role_id = ppstate_v1_name_find(
                    occurrence_plan->roles, occurrence_plan->role_len,
                    raw->role_name);
                if (role_id < 0)
                    goto done;
                raw->action.role_id = (uint32_t)role_id;
            }
            if (raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH_KEY_ABSENT ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_INSERT_UNORDERED_PAIRS ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW_KEY_ABSENT ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_INSERT_ROW ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING) {
                table_id = ppstate_v1_table_find(&result, raw->table_name);
                if (table_id < 0)
                    goto done;
                raw->action.table_id = (uint32_t)table_id;
            }
            if (raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING) {
                table_id = ppstate_v1_table_find(
                    &result, raw->source_table_name);
                if (table_id < 0)
                    goto done;
                raw->action.source_table_id = (uint32_t)table_id;
            }
            if (raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING) {
                table_id = ppstate_v1_table_find(
                    &result, raw->condition_table_name);
                if (table_id < 0)
                    goto done;
                raw->action.condition_table_id = (uint32_t)table_id;
            }
            if (raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_NORMAL ||
                raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_COMPRESSED) {
                int32_t machine_id = ppstate_v1_proof_machine_find(
                    &result, raw->proof_machine_name);
                uint32_t role_len = raw->action.kind ==
                    PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_COMPRESSED
                    ? 4u : 3u;
                uint32_t *role_fields[4] = {
                    &raw->action.proof_label_role_id,
                    &raw->action.proof_formula_role_id,
                    &raw->action.proof_step_role_id,
                    &raw->action.proof_code_role_id,
                };
                uint32_t proof_role_index;
                if (machine_id < 0)
                    goto done;
                raw->action.proof_machine_id = (uint32_t)machine_id;
                for (proof_role_index = 0u;
                     proof_role_index < role_len; proof_role_index++) {
                    int32_t proof_role_id = ppstate_v1_name_find(
                        occurrence_plan->roles, occurrence_plan->role_len,
                        raw->proof_role_names[proof_role_index]);
                    if (proof_role_id < 0)
                        goto done;
                    *role_fields[proof_role_index] =
                        (uint32_t)proof_role_id;
                }
            }
            for (operand_index = 0u;
                 operand_index < raw->action.operand_len;
                 operand_index++) {
                PPRelationalStateOperandV1 *operand =
                    &raw->action.operands[operand_index];
                if (operand->kind !=
                        PPRELATIONAL_STATE_OPERAND_V1_ROLE_AT &&
                    operand->kind !=
                        PPRELATIONAL_STATE_OPERAND_V1_ROLE_LIST)
                    continue;
                role_id = ppstate_v1_name_find(
                    occurrence_plan->roles, occurrence_plan->role_len,
                    raw->operand_role_names[operand_index]);
                if (role_id < 0)
                    goto done;
                operand->role_id = (uint32_t)role_id;
            }
            for (operand_index = 0u;
                 operand_index < raw->action.condition_operand_len;
                 operand_index++) {
                PPRelationalStateOperandV1 *operand =
                    &raw->action.condition_operands[operand_index];
                if (operand->kind !=
                        PPRELATIONAL_STATE_OPERAND_V1_ROLE_AT &&
                    operand->kind !=
                        PPRELATIONAL_STATE_OPERAND_V1_ROLE_LIST)
                    continue;
                role_id = ppstate_v1_name_find(
                    occurrence_plan->roles, occurrence_plan->role_len,
                    raw->condition_operand_role_names[operand_index]);
                if (role_id < 0)
                    goto done;
                operand->role_id = (uint32_t)role_id;
            }
            raw->action.operation_id = (uint32_t)operation_id;
            *compiled = raw->action;
            memset(&raw->action, 0, sizeof(raw->action));
            index++;
        }
        result.operations[operation_id].action_begin = group_begin;
        result.operations[operation_id].action_len = index - group_begin;
    }
    for (index = 0u; index < final_action_len; index++) {
        if (final_actions[index].index != index) {
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "final state action indices are not dense");
            goto done;
        }
        result.final_actions[index] = final_actions[index].action;
        memset(&final_actions[index].action, 0,
               sizeof(final_actions[index].action));
    }
    memcpy(result.occurrence_fold_plan_digest,
           occurrence_plan->plan_digest, 65u);
    memcpy(result.compiler_answer_digest, compiler_answer_digest, 65u);
    if (!ppstate_v1_plan_digest(&result, result.plan_digest) ||
        !pprelational_state_program_v1_plan_validate(
            occurrence_plan, &result, error_buf, error_buf_size))
        goto done;
    pprelational_state_program_v1_plan_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0')
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "failed to build relational state plan");
    for (index = 0u; index < table_write; index++)
        free(tables[index].table.name);
    for (index = 0u; index < proof_machine_write; index++)
        ppstate_v1_raw_proof_machine_free(&proof_machines[index]);
    for (index = 0u; index < action_write; index++)
        ppstate_v1_raw_action_free(&actions[index]);
    for (index = 0u; index < final_write; index++)
        ppstate_v1_raw_action_free(&final_actions[index]);
    free(tables);
    free(proof_machines);
    free(actions);
    free(final_actions);
    pprelational_state_program_v1_plan_free(&result);
    return ok;
}

typedef struct {
    uint8_t *bytes;
    uint32_t len;
    uint64_t hash;
} PPRelationalStateValueV1;

typedef struct {
    uint32_t values[PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY];
} PPRelationalStateRowV1;

typedef struct {
    uint32_t operand_ids[PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY];
    uint32_t condition_operand_ids[
        PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY];
} PPRelationalStatePreparedActionV1;

typedef enum {
    PPSTATE_V1_COMPACT_COLUMN_FIXED = 0,
    PPSTATE_V1_COMPACT_COLUMN_SOURCE = 1,
    PPSTATE_V1_COMPACT_COLUMN_ROW_INDEX = 2
} PPRelationalStateCompactColumnKindV1;

typedef struct {
    uint32_t prefix;
    uint32_t row_begin;
    uint32_t row_len;
    uint32_t value_begin;
    uint32_t fixed_values[PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY];
    uint8_t column_kinds[PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY];
    uint8_t source_slots[PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY];
    uint8_t source_value_len;
} PPRelationalStateCompactRunV1;

typedef struct {
    PPRelationalStateRowV1 *rows;
    uint32_t row_len;
    uint32_t row_cap;
    uint32_t *buckets;
    uint32_t *prefix_buckets;
    uint32_t *prefix_tails;
    uint32_t *prefix_next;
    uint32_t prefix_next_cap;
    uint32_t bucket_len;
    PPRelationalStateCompactRunV1 *compact_runs;
    uint32_t compact_run_len;
    uint32_t compact_run_cap;
    uint32_t *compact_run_buckets;
    uint32_t compact_run_bucket_len;
    uint32_t *compact_values;
    uint32_t compact_value_len;
    uint32_t compact_value_cap;
    uint32_t compact_row_len;
    bool compact_eligible;
} PPRelationalStateTableStateV1;

typedef struct {
    const PPOccurrenceFoldV1Plan *occurrence_plan;
    const PPRelationalStateProgramV1Plan *plan;
    PPRelationalStateValueV1 *values;
    uint32_t value_len;
    uint32_t value_cap;
    uint32_t *value_buckets;
    uint32_t value_bucket_len;
    uint32_t *prepared_step_value_ids;
    uint32_t prepared_step_value_len;
    uint32_t prepared_step_value_cap;
    PPRelationalStateTableStateV1 *tables;
    PPRelationalStatePreparedActionV1 *prepared_actions;
    PPRelationalStatePreparedActionV1 *prepared_final_actions;
    PPRelationalStackProofV1Machine *proof_machines;
    PPRelationalStackProofV1Cache *proof_frame_caches;
    uint32_t *scope_lengths;
    uint32_t scope_len;
    uint32_t scope_cap;
    PPOccurrenceSourceResolverV1 source_resolver;
    PPOccurrenceFoldV1Backend nested_backend;
    PPRelationalStateProofV1Backend proof_backend;
    PPRelationalStateObservationV1 observation;
    uint64_t store_identity;
    bool source_resolver_ready;
    bool proof_backend_ready;
    bool proof_frame_caches_ready;
} PPRelationalStateRunImplV1;

typedef enum {
    PPRELATIONAL_STATE_INSERT_V1_OK = 0,
    PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT = 1,
    PPRELATIONAL_STATE_INSERT_V1_RESOURCE = 2
} PPRelationalStateInsertV1;

static void ppstate_v1_failure_set(
    PPRelationalStateProgramV1Run *run,
    PPRelationalStateFailureV1 failure) {
    if (run && run->receipt.failure == PPRELATIONAL_STATE_FAILURE_V1_NONE)
        run->receipt.failure = failure;
}

static uint64_t ppstate_v1_hash_bytes(const uint8_t *bytes, uint32_t len) {
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t index;

    for (index = 0u; index < len; index++) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    hash ^= len;
    hash *= UINT64_C(1099511628211);
    return hash;
}

static bool ppstate_v1_grow_raw(void **data, uint32_t *capacity,
                                uint32_t needed, size_t item_size) {
    uint32_t next_capacity;
    void *next;

    if (needed <= *capacity)
        return true;
    next_capacity = *capacity ? *capacity
                              : PPRELATIONAL_STATE_V1_INITIAL_CAPACITY;
    while (next_capacity < needed) {
        if (next_capacity > UINT32_MAX / 2u)
            return false;
        next_capacity *= 2u;
    }
    if (!ppstate_v1_array_fits(next_capacity, item_size))
        return false;
    next = realloc(*data, (size_t)next_capacity * item_size);
    if (!next)
        return false;
    *data = next;
    *capacity = next_capacity;
    return true;
}

static bool ppstate_v1_value_rehash(PPRelationalStateRunImplV1 *impl,
                                    uint32_t bucket_len) {
    uint32_t *buckets;
    uint32_t index;

    if (bucket_len < PPRELATIONAL_STATE_V1_INITIAL_CAPACITY ||
        (bucket_len & (bucket_len - 1u)) != 0u ||
        !ppstate_v1_array_fits(bucket_len, sizeof(*buckets)))
        return false;
    buckets = calloc(bucket_len, sizeof(*buckets));
    if (!buckets)
        return false;
    for (index = 0u; index < impl->value_len; index++) {
        uint32_t slot =
            (uint32_t)impl->values[index].hash & (bucket_len - 1u);
        while (buckets[slot] != 0u)
            slot = (slot + 1u) & (bucket_len - 1u);
        buckets[slot] = index + 1u;
    }
    free(impl->value_buckets);
    impl->value_buckets = buckets;
    impl->value_bucket_len = bucket_len;
    return true;
}

static int32_t ppstate_v1_value_find(
    const PPRelationalStateRunImplV1 *impl, const uint8_t *bytes,
    uint32_t len, uint64_t hash) {
    uint32_t slot;
    uint32_t probes;

    if (!impl->value_bucket_len)
        return -1;
    slot = (uint32_t)hash & (impl->value_bucket_len - 1u);
    for (probes = 0u; probes < impl->value_bucket_len; probes++) {
        uint32_t encoded = impl->value_buckets[slot];
        const PPRelationalStateValueV1 *value;
        if (encoded == 0u)
            return -1;
        value = &impl->values[encoded - 1u];
        if (value->hash == hash && value->len == len &&
            memcmp(value->bytes, bytes, len) == 0)
            return (int32_t)(encoded - 1u);
        slot = (slot + 1u) & (impl->value_bucket_len - 1u);
    }
    return -1;
}

static bool ppstate_v1_value_intern(PPRelationalStateRunImplV1 *impl,
                                    const uint8_t *bytes, uint32_t len,
                                    uint32_t *out) {
    uint64_t hash;
    int32_t found;
    uint8_t *copy;
    uint32_t slot;

    if (!impl || !bytes || len == 0u || !out)
        return false;
    hash = ppstate_v1_hash_bytes(bytes, len);
    found = ppstate_v1_value_find(impl, bytes, len, hash);
    if (found >= 0) {
        *out = (uint32_t)found;
        return true;
    }
    if (!impl->value_bucket_len &&
        !ppstate_v1_value_rehash(
            impl, PPRELATIONAL_STATE_V1_INITIAL_CAPACITY))
        return false;
    if ((uint64_t)(impl->value_len + 1u) * 10u >=
            (uint64_t)impl->value_bucket_len * 7u &&
        !ppstate_v1_value_rehash(impl, impl->value_bucket_len * 2u))
        return false;
    if (!ppstate_v1_grow_raw(
            (void **)&impl->values, &impl->value_cap,
            impl->value_len + 1u, sizeof(*impl->values)))
        return false;
    copy = ppstate_v1_bytes_dup(bytes, len);
    if (!copy)
        return false;
    impl->values[impl->value_len] = (PPRelationalStateValueV1){
        .bytes = copy, .len = len, .hash = hash,
    };
    slot = (uint32_t)hash & (impl->value_bucket_len - 1u);
    while (impl->value_buckets[slot] != 0u)
        slot = (slot + 1u) & (impl->value_bucket_len - 1u);
    impl->value_buckets[slot] = impl->value_len + 1u;
    *out = impl->value_len++;
    return true;
}

static bool ppstate_v1_prepare_operand_ids(
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateOperandV1 *operands, uint32_t operand_len,
    uint32_t ids[PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY]) {
    uint32_t index;

    if (!impl || (!operands && operand_len != 0u) ||
        operand_len > PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY || !ids)
        return false;
    for (index = 0u;
         index < PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY; index++)
        ids[index] = UINT32_MAX;
    for (index = 0u; index < operand_len; index++) {
        if (operands[index].kind !=
            PPRELATIONAL_STATE_OPERAND_V1_LITERAL)
            continue;
        if (!ppstate_v1_value_intern(
                impl, operands[index].literal,
                operands[index].literal_len, &ids[index]))
            return false;
    }
    return true;
}

static bool ppstate_v1_prepare_action_array(
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateActionV1 *actions, uint32_t action_len,
    PPRelationalStatePreparedActionV1 **prepared_out) {
    PPRelationalStatePreparedActionV1 *prepared;
    uint32_t index;

    if (!impl || !prepared_out || (!actions && action_len != 0u))
        return false;
    *prepared_out = NULL;
    if (action_len == 0u)
        return true;
    if (!ppstate_v1_array_fits(action_len, sizeof(*prepared)) ||
        !(prepared = calloc(action_len, sizeof(*prepared))))
        return false;
    for (index = 0u; index < action_len; index++) {
        if (!ppstate_v1_prepare_operand_ids(
                impl, actions[index].operands,
                actions[index].operand_len,
                prepared[index].operand_ids) ||
            !ppstate_v1_prepare_operand_ids(
                impl, actions[index].condition_operands,
                actions[index].condition_operand_len,
                prepared[index].condition_operand_ids)) {
            free(prepared);
            return false;
        }
    }
    *prepared_out = prepared;
    return true;
}

static bool ppstate_v1_prepare_actions(
    PPRelationalStateRunImplV1 *impl) {
    if (!impl || !impl->plan)
        return false;
    return ppstate_v1_prepare_action_array(
               impl, impl->plan->actions, impl->plan->action_len,
               &impl->prepared_actions) &&
           ppstate_v1_prepare_action_array(
               impl, impl->plan->final_actions,
               impl->plan->final_action_len,
               &impl->prepared_final_actions);
}

static bool ppstate_v1_prepare_step_values(
    PPRelationalStateRunImplV1 *impl,
    const PPOccurrenceFoldV1Step *step) {
    if (!impl || !step || (step->value_len != 0u && !step->values) ||
        !ppstate_v1_grow_raw(
            (void **)&impl->prepared_step_value_ids,
            &impl->prepared_step_value_cap, step->value_len,
            sizeof(*impl->prepared_step_value_ids)))
        return false;
    impl->prepared_step_value_len = step->value_len;
    if (step->value_len != 0u) {
        memset(impl->prepared_step_value_ids, 0xff,
               (size_t)step->value_len *
                   sizeof(*impl->prepared_step_value_ids));
    }
    return true;
}

static int ppstate_v1_value_compare(
    const PPRelationalStateRunImplV1 *impl,
    uint32_t left, uint32_t right) {
    const PPRelationalStateValueV1 *lhs;
    const PPRelationalStateValueV1 *rhs;
    uint32_t shared;
    int comparison;

    if (!impl || left >= impl->value_len || right >= impl->value_len)
        return 0;
    lhs = &impl->values[left];
    rhs = &impl->values[right];
    shared = lhs->len < rhs->len ? lhs->len : rhs->len;
    comparison = memcmp(lhs->bytes, rhs->bytes, shared);
    if (comparison != 0)
        return comparison;
    return lhs->len < rhs->len ? -1 : lhs->len > rhs->len ? 1 : 0;
}

static uint64_t ppstate_v1_row_hash(
    const PPRelationalStateRowV1 *row, uint32_t key_arity) {
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t index;

    for (index = 0u; index < key_arity; index++) {
        hash ^= row->values[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool ppstate_v1_row_key_equal(
    const PPRelationalStateRowV1 *left,
    const PPRelationalStateRowV1 *right, uint32_t key_arity) {
    uint32_t index;
    for (index = 0u; index < key_arity; index++) {
        if (left->values[index] != right->values[index])
            return false;
    }
    return true;
}

static bool ppstate_v1_row_equal(
    const PPRelationalStateRowV1 *left,
    const PPRelationalStateRowV1 *right, uint32_t arity) {
    uint32_t index;
    for (index = 0u; index < arity; index++) {
        if (left->values[index] != right->values[index])
            return false;
    }
    return true;
}

static bool ppstate_v1_table_rehash(
    PPRelationalStateTableStateV1 *state,
    const PPRelationalStateTableV1 *table, uint32_t bucket_len) {
    uint32_t *buckets = NULL;
    uint32_t *prefix_buckets = NULL;
    uint32_t *prefix_tails = NULL;
    uint32_t *prefix_next = NULL;
    uint32_t index;

    if (bucket_len < PPRELATIONAL_STATE_V1_INITIAL_CAPACITY ||
        (bucket_len & (bucket_len - 1u)) != 0u ||
        !ppstate_v1_array_fits(bucket_len, sizeof(*buckets)))
        return false;
    buckets = calloc(bucket_len, sizeof(*buckets));
    prefix_buckets = calloc(bucket_len, sizeof(*prefix_buckets));
    prefix_tails = calloc(bucket_len, sizeof(*prefix_tails));
    if (state->row_cap > 0u &&
        ppstate_v1_array_fits(state->row_cap, sizeof(*prefix_next)))
        prefix_next = calloc(state->row_cap, sizeof(*prefix_next));
    if (!buckets || !prefix_buckets || !prefix_tails ||
        (state->row_cap > 0u && !prefix_next))
        goto fail;
    for (index = 0u; index < state->row_len; index++) {
        uint32_t slot = (uint32_t)ppstate_v1_row_hash(
                            &state->rows[index], table->key_arity) &
                        (bucket_len - 1u);
        uint32_t prefix_slot = (uint32_t)ppstate_v1_row_hash(
                                   &state->rows[index], 1u) &
                               (bucket_len - 1u);
        while (buckets[slot] != 0u)
            slot = (slot + 1u) & (bucket_len - 1u);
        buckets[slot] = index + 1u;
        while (prefix_buckets[prefix_slot] != 0u &&
               state->rows[prefix_buckets[prefix_slot] - 1u].values[0] !=
                   state->rows[index].values[0])
            prefix_slot = (prefix_slot + 1u) & (bucket_len - 1u);
        if (prefix_buckets[prefix_slot] == 0u) {
            prefix_buckets[prefix_slot] = index + 1u;
            prefix_tails[prefix_slot] = index + 1u;
        } else {
            prefix_next[prefix_tails[prefix_slot] - 1u] = index + 1u;
            prefix_tails[prefix_slot] = index + 1u;
        }
    }
    free(state->buckets);
    free(state->prefix_buckets);
    free(state->prefix_tails);
    free(state->prefix_next);
    state->buckets = buckets;
    state->prefix_buckets = prefix_buckets;
    state->prefix_tails = prefix_tails;
    state->prefix_next = prefix_next;
    state->prefix_next_cap = state->row_cap;
    state->bucket_len = bucket_len;
    return true;

fail:
    free(buckets);
    free(prefix_buckets);
    free(prefix_tails);
    free(prefix_next);
    return false;
}

static int32_t ppstate_v1_table_find_row_hashed(
    const PPRelationalStateTableStateV1 *state,
    const PPRelationalStateTableV1 *table,
    const PPRelationalStateRowV1 *row,
    uint64_t hash) {
    uint32_t slot;
    uint32_t probes;

    if (!state->bucket_len)
        return -1;
    slot = (uint32_t)hash & (state->bucket_len - 1u);
    for (probes = 0u; probes < state->bucket_len; probes++) {
        uint32_t encoded = state->buckets[slot];
        if (encoded == 0u)
            return -1;
        if (ppstate_v1_row_key_equal(
                &state->rows[encoded - 1u], row, table->key_arity))
            return (int32_t)(encoded - 1u);
        slot = (slot + 1u) & (state->bucket_len - 1u);
    }
    return -1;
}

static int32_t ppstate_v1_table_find_row(
    const PPRelationalStateTableStateV1 *state,
    const PPRelationalStateTableV1 *table,
    const PPRelationalStateRowV1 *row) {
    return ppstate_v1_table_find_row_hashed(
        state, table, row,
        ppstate_v1_row_hash(row, table->key_arity));
}

static PPRelationalStateInsertV1 ppstate_v1_table_insert(
    PPRelationalStateTableStateV1 *state,
    const PPRelationalStateTableV1 *table,
    const PPRelationalStateRowV1 *row,
    PPRelationalStateWriteV1 policy) {
    uint64_t key_hash = ppstate_v1_row_hash(row, table->key_arity);
    int32_t found = ppstate_v1_table_find_row_hashed(
        state, table, row, key_hash);
    uint32_t slot;
    uint32_t prefix_slot;

    if (found >= 0) {
        return policy ==
                       PPRELATIONAL_STATE_WRITE_V1_INSERT_OR_REQUIRE_EQUAL &&
                   ppstate_v1_row_equal(
                       &state->rows[found], row, table->arity)
                   ? PPRELATIONAL_STATE_INSERT_V1_OK
                   : PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
    }
    if (!state->bucket_len &&
        !ppstate_v1_table_rehash(
            state, table, PPRELATIONAL_STATE_V1_INITIAL_CAPACITY))
        return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
    if ((uint64_t)(state->row_len + 1u) * 10u >=
            (uint64_t)state->bucket_len * 7u &&
        !ppstate_v1_table_rehash(state, table, state->bucket_len * 2u))
        return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
    if (!ppstate_v1_grow_raw(
            (void **)&state->rows, &state->row_cap,
            state->row_len + 1u, sizeof(*state->rows)))
        return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
    if (!ppstate_v1_grow_raw(
            (void **)&state->prefix_next, &state->prefix_next_cap,
            state->row_len + 1u, sizeof(*state->prefix_next)))
        return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
    state->rows[state->row_len] = *row;
    state->prefix_next[state->row_len] = 0u;
    slot = (uint32_t)key_hash & (state->bucket_len - 1u);
    while (state->buckets[slot] != 0u)
        slot = (slot + 1u) & (state->bucket_len - 1u);
    state->buckets[slot] = state->row_len + 1u;
    prefix_slot = (uint32_t)ppstate_v1_row_hash(row, 1u) &
                  (state->bucket_len - 1u);
    while (state->prefix_buckets[prefix_slot] != 0u &&
           state->rows[state->prefix_buckets[prefix_slot] - 1u].values[0] !=
               row->values[0])
        prefix_slot = (prefix_slot + 1u) & (state->bucket_len - 1u);
    if (state->prefix_buckets[prefix_slot] == 0u) {
        state->prefix_buckets[prefix_slot] = state->row_len + 1u;
        state->prefix_tails[prefix_slot] = state->row_len + 1u;
    } else {
        state->prefix_next[state->prefix_tails[prefix_slot] - 1u] =
            state->row_len + 1u;
        state->prefix_tails[prefix_slot] = state->row_len + 1u;
    }
    state->row_len++;
    return PPRELATIONAL_STATE_INSERT_V1_OK;
}

static bool ppstate_v1_compact_run_rehash(
    PPRelationalStateTableStateV1 *state, uint32_t bucket_len) {
    uint32_t *buckets;
    uint32_t index;

    if (!state || bucket_len < PPRELATIONAL_STATE_V1_INITIAL_CAPACITY ||
        (bucket_len & (bucket_len - 1u)) != 0u ||
        !ppstate_v1_array_fits(bucket_len, sizeof(*buckets)) ||
        !(buckets = calloc(bucket_len, sizeof(*buckets))))
        return false;
    for (index = 0u; index < state->compact_run_len; index++) {
        uint32_t slot = (uint32_t)ppstate_v1_row_hash(
                            &(PPRelationalStateRowV1){
                                {state->compact_runs[index].prefix}},
                            1u) &
                        (bucket_len - 1u);
        while (buckets[slot] != 0u)
            slot = (slot + 1u) & (bucket_len - 1u);
        buckets[slot] = index + 1u;
    }
    free(state->compact_run_buckets);
    state->compact_run_buckets = buckets;
    state->compact_run_bucket_len = bucket_len;
    return true;
}

static int32_t ppstate_v1_compact_run_find(
    const PPRelationalStateTableStateV1 *state, uint32_t prefix) {
    PPRelationalStateRowV1 query = {{prefix}};
    uint32_t slot;
    uint32_t probes;

    if (!state || state->compact_run_bucket_len == 0u)
        return -1;
    slot = (uint32_t)ppstate_v1_row_hash(&query, 1u) &
           (state->compact_run_bucket_len - 1u);
    for (probes = 0u; probes < state->compact_run_bucket_len; probes++) {
        uint32_t encoded = state->compact_run_buckets[slot];
        if (encoded == 0u)
            return -1;
        if (state->compact_runs[encoded - 1u].prefix == prefix)
            return (int32_t)(encoded - 1u);
        slot = (slot + 1u) & (state->compact_run_bucket_len - 1u);
    }
    return -1;
}

static uint32_t ppstate_v1_column_mask(uint32_t arity) {
    return arity >= 32u
               ? UINT32_MAX
               : (UINT32_C(1) << arity) - 1u;
}

static bool ppstate_v1_compact_row_projected(
    PPRelationalStateRunImplV1 *impl, uint32_t table_id,
    const PPRelationalStateCompactRunV1 *run, uint32_t row_offset,
    uint32_t column_mask,
    PPRelationalStateRowV1 *row_out) {
    const PPRelationalStateTableV1 *table;
    uint32_t column;
    uint32_t source_base;

    if (!impl || table_id >= impl->plan->table_len || !run || !row_out ||
        row_offset >= run->row_len)
        return false;
    table = &impl->plan->tables[table_id];
    if (run->source_value_len > 0u &&
        (row_offset > UINT32_MAX / run->source_value_len ||
         run->value_begin > UINT32_MAX -
             row_offset * run->source_value_len))
        return false;
    source_base = run->value_begin + row_offset * run->source_value_len;
    memset(row_out, 0, sizeof(*row_out));
    for (column = 0u; column < table->arity; column++) {
        if ((column_mask & (UINT32_C(1) << column)) == 0u)
            continue;
        switch ((PPRelationalStateCompactColumnKindV1)
                    run->column_kinds[column]) {
        case PPSTATE_V1_COMPACT_COLUMN_FIXED:
            row_out->values[column] = run->fixed_values[column];
            break;
        case PPSTATE_V1_COMPACT_COLUMN_SOURCE:
            if (run->source_slots[column] >= run->source_value_len ||
                source_base + run->source_slots[column] >=
                    impl->tables[table_id].compact_value_len)
                return false;
            row_out->values[column] =
                impl->tables[table_id]
                    .compact_values[source_base + run->source_slots[column]];
            break;
        case PPSTATE_V1_COMPACT_COLUMN_ROW_INDEX:
            {
                char rendered[11];
                int rendered_len = snprintf(
                    rendered, sizeof(rendered), "%u", row_offset);
                if (rendered_len <= 0 ||
                    (size_t)rendered_len >= sizeof(rendered) ||
                    !ppstate_v1_value_intern(
                        impl, (const uint8_t *)rendered,
                        (uint32_t)rendered_len,
                        &row_out->values[column]))
                    return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

static bool ppstate_v1_compact_row(
    PPRelationalStateRunImplV1 *impl, uint32_t table_id,
    const PPRelationalStateCompactRunV1 *run, uint32_t row_offset,
    PPRelationalStateRowV1 *row_out) {
    if (!impl || table_id >= impl->plan->table_len)
        return false;
    return ppstate_v1_compact_row_projected(
        impl, table_id, run, row_offset,
        ppstate_v1_column_mask(impl->plan->tables[table_id].arity),
        row_out);
}

static bool ppstate_v1_compact_table_row(
    PPRelationalStateRunImplV1 *impl, uint32_t table_id,
    uint32_t row_index, PPRelationalStateRowV1 *row_out) {
    const PPRelationalStateTableStateV1 *state;
    uint32_t low = 0u;
    uint32_t high;

    if (!impl || table_id >= impl->plan->table_len || !row_out)
        return false;
    state = &impl->tables[table_id];
    high = state->compact_run_len;
    if (row_index >= state->compact_row_len)
        return false;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPRelationalStateCompactRunV1 *run =
            &state->compact_runs[middle];
        if (row_index < run->row_begin)
            high = middle;
        else if (row_index >= run->row_begin + run->row_len)
            low = middle + 1u;
        else
            return ppstate_v1_compact_row(
                impl, table_id, run, row_index - run->row_begin, row_out);
    }
    return false;
}

static bool ppstate_v1_store_table_shape(
    void *context, uint32_t table_id, uint32_t *arity_out,
    uint32_t *key_arity_out, uint32_t *row_len_out) {
    PPRelationalStateRunImplV1 *impl = context;
    if (!impl || table_id >= impl->plan->table_len || !arity_out ||
        !key_arity_out || !row_len_out)
        return false;
    *arity_out = impl->plan->tables[table_id].arity;
    *key_arity_out = impl->plan->tables[table_id].key_arity;
    *row_len_out = impl->tables[table_id].compact_eligible
                       ? impl->tables[table_id].compact_row_len
                       : impl->tables[table_id].row_len;
    return true;
}

static bool ppstate_v1_store_table_row(
    void *context, uint32_t table_id, uint32_t row_index,
    uint32_t *values_out, uint32_t value_capacity) {
    PPRelationalStateRunImplV1 *impl = context;
    uint32_t arity;
    if (!impl || table_id >= impl->plan->table_len || !values_out)
        return false;
    arity = impl->plan->tables[table_id].arity;
    if (value_capacity < arity)
        return false;
    if (impl->tables[table_id].compact_eligible) {
        PPRelationalStateRowV1 row;
        if (!ppstate_v1_compact_table_row(
                impl, table_id, row_index, &row))
            return false;
        memcpy(values_out, row.values,
               (size_t)arity * sizeof(*values_out));
        return true;
    }
    if (row_index >= impl->tables[table_id].row_len)
        return false;
    memcpy(values_out, impl->tables[table_id].rows[row_index].values,
           (size_t)arity * sizeof(*values_out));
    return true;
}

static bool ppstate_v1_store_table_find(
    void *context, uint32_t table_id, const uint32_t *key,
    uint32_t key_len, uint32_t *values_out, uint32_t value_capacity) {
    PPRelationalStateRunImplV1 *impl = context;
    PPRelationalStateRowV1 query = {{0u}};
    const PPRelationalStateTableV1 *table;
    int32_t found;
    if (!impl || table_id >= impl->plan->table_len || !key ||
        !values_out)
        return false;
    table = &impl->plan->tables[table_id];
    if (key_len != table->key_arity || value_capacity < table->arity)
        return false;
    memcpy(query.values, key, (size_t)key_len * sizeof(*key));
    if (impl->tables[table_id].compact_eligible) {
        PPRelationalStateTableStateV1 *state = &impl->tables[table_id];
        int32_t run_index = ppstate_v1_compact_run_find(state, key[0]);
        uint32_t row_offset;
        if (run_index < 0)
            return false;
        for (row_offset = 0u;
             row_offset < state->compact_runs[run_index].row_len;
             row_offset++) {
            PPRelationalStateRowV1 row;
            if (!ppstate_v1_compact_row(
                    impl, table_id, &state->compact_runs[run_index],
                    row_offset, &row))
                return false;
            if (ppstate_v1_row_key_equal(
                    &row, &query, table->key_arity)) {
                memcpy(values_out, row.values,
                       (size_t)table->arity * sizeof(*values_out));
                return true;
            }
        }
        return false;
    }
    found = ppstate_v1_table_find_row(
        &impl->tables[table_id], table, &query);
    if (found < 0)
        return false;
    memcpy(values_out, impl->tables[table_id].rows[found].values,
           (size_t)table->arity * sizeof(*values_out));
    return true;
}

static bool ppstate_v1_store_table_prefix_next(
    void *context, uint32_t table_id, const uint32_t *prefix,
    uint32_t prefix_len, uint32_t column_mask,
    uint64_t *cursor_io, uint32_t *values_out,
    uint32_t value_capacity, bool *found_out) {
    PPRelationalStateRunImplV1 *impl = context;
    PPRelationalStateTableStateV1 *state;
    const PPRelationalStateTableV1 *table;
    uint32_t encoded;

    if (!impl || table_id >= impl->plan->table_len || !prefix ||
        prefix_len != 1u || !cursor_io || !values_out || !found_out)
        return false;
    table = &impl->plan->tables[table_id];
    state = &impl->tables[table_id];
    if (table->arity == 0u || value_capacity < table->arity ||
        column_mask == 0u ||
        (table->arity < 32u &&
         (column_mask >> table->arity) != 0u))
        return false;
    *found_out = false;
    if (state->compact_eligible) {
        uint32_t run_index;
        uint32_t row_offset;
        const PPRelationalStateCompactRunV1 *run;
        if (*cursor_io == UINT64_MAX) {
            int32_t found = ppstate_v1_compact_run_find(
                state, prefix[0]);
            if (found < 0) {
                *cursor_io = 0u;
                return true;
            }
            run_index = (uint32_t)found;
            row_offset = 0u;
        } else {
            uint32_t run_link;
            uint32_t row_link;
            if (*cursor_io == 0u)
                return true;
            run_link = (uint32_t)(*cursor_io >> 32u);
            row_link = (uint32_t)*cursor_io;
            if (run_link == 0u || row_link == 0u)
                return false;
            run_index = run_link - 1u;
            row_offset = row_link - 1u;
        }
        if (run_index >= state->compact_run_len)
            return false;
        run = &state->compact_runs[run_index];
        if (run->prefix != prefix[0] || row_offset >= run->row_len)
            return false;
        {
            PPRelationalStateRowV1 row;
            if (!ppstate_v1_compact_row_projected(
                    impl, table_id, run, row_offset,
                    column_mask, &row))
                return false;
            memcpy(values_out, row.values,
                   (size_t)table->arity * sizeof(*values_out));
        }
        if (row_offset + 1u < run->row_len) {
            *cursor_io = ((uint64_t)(run_index + 1u) << 32u) |
                         (uint64_t)(row_offset + 2u);
        } else {
            *cursor_io = 0u;
        }
        *found_out = true;
        return true;
    }
    if (*cursor_io == UINT64_MAX) {
        uint32_t slot;
        uint32_t probes;
        PPRelationalStateRowV1 query = {{prefix[0]}};
        if (state->bucket_len == 0u) {
            *cursor_io = 0u;
            return true;
        }
        slot = (uint32_t)ppstate_v1_row_hash(&query, 1u) &
               (state->bucket_len - 1u);
        encoded = 0u;
        for (probes = 0u; probes < state->bucket_len; probes++) {
            uint32_t head = state->prefix_buckets[slot];
            if (head == 0u)
                break;
            if (head <= state->row_len &&
                state->rows[head - 1u].values[0] == prefix[0]) {
                encoded = head;
                break;
            }
            slot = (slot + 1u) & (state->bucket_len - 1u);
        }
    } else {
        if (*cursor_io > UINT32_MAX)
            return false;
        encoded = (uint32_t)*cursor_io;
    }
    if (encoded == 0u) {
        *cursor_io = 0u;
        return true;
    }
    if (encoded > state->row_len ||
        state->rows[encoded - 1u].values[0] != prefix[0])
        return false;
    memset(values_out, 0, (size_t)table->arity * sizeof(*values_out));
    if (column_mask == ppstate_v1_column_mask(table->arity)) {
        memcpy(values_out, state->rows[encoded - 1u].values,
               (size_t)table->arity * sizeof(*values_out));
    } else {
        uint32_t column;
        for (column = 0u; column < table->arity; column++) {
            if ((column_mask & (UINT32_C(1) << column)) != 0u) {
                values_out[column] =
                    state->rows[encoded - 1u].values[column];
            }
        }
    }
    *cursor_io = state->prefix_next[encoded - 1u];
    *found_out = true;
    return true;
}

static bool ppstate_v1_store_value_intern(
    void *context, const uint8_t *bytes, uint32_t len,
    uint32_t *value_out) {
    return ppstate_v1_value_intern(context, bytes, len, value_out);
}

static bool ppstate_v1_store_value_bytes(
    void *context, uint32_t value, const uint8_t **bytes_out,
    uint32_t *len_out) {
    PPRelationalStateRunImplV1 *impl = context;
    if (!impl || value >= impl->value_len || !bytes_out || !len_out)
        return false;
    *bytes_out = impl->values[value].bytes;
    *len_out = impl->values[value].len;
    return true;
}

static bool ppstate_v1_store_table_immutable_prefix(
    void *context, uint32_t table_id, uint32_t *row_len_out) {
    const PPRelationalStateRunImplV1 *impl = context;
    const PPRelationalStateTableStateV1 *state;

    if (!impl || !row_len_out || table_id >= impl->plan->table_len ||
        impl->plan->tables[table_id].lifetime !=
            PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT)
        return false;
    state = &impl->tables[table_id];
    *row_len_out = state->compact_eligible
                       ? state->compact_row_len : state->row_len;
    return true;
}

static PPRelationalStoreV1 ppstate_v1_store(
    PPRelationalStateRunImplV1 *impl) {
    return (PPRelationalStoreV1){
        .context = impl,
        .identity = impl->store_identity,
        .table_immutable_prefix =
            ppstate_v1_store_table_immutable_prefix,
        .table_shape = ppstate_v1_store_table_shape,
        .table_row = ppstate_v1_store_table_row,
        .table_find = ppstate_v1_store_table_find,
        .table_prefix_next = ppstate_v1_store_table_prefix_next,
        .value_intern = ppstate_v1_store_value_intern,
        .value_bytes = ppstate_v1_store_value_bytes,
    };
}

static bool ppstate_v1_proof_machine_runtime_init(
    PPRelationalStateRunImplV1 *impl, uint32_t index,
    char *error_buf, size_t error_buf_size) {
    const PPRelationalStateProofMachineV1 *source =
        &impl->plan->proof_machines[index];
    PPRelationalStackProofV1Machine *target = &impl->proof_machines[index];
    PPRelationalStoreV1 store = ppstate_v1_store(impl);

    *target = (PPRelationalStackProofV1Machine){
        .label_kind_table = source->label_kind_table,
        .formula_table = source->formula_table,
        .binder_variable_table = source->binder_variable_table,
        .mandatory_variable_table = source->mandatory_variable_table,
        .assertion_hypothesis_table = source->assertion_hypothesis_table,
        .assertion_disjoint_table = source->assertion_disjoint_table,
        .active_hypothesis_table = source->active_hypothesis_table,
        .active_disjoint_table = source->active_disjoint_table,
        .symbol_kind_table = source->symbol_kind_table,
        .terminal_low = source->terminal_low,
        .terminal_high = source->terminal_high,
        .continuation_low = source->continuation_low,
        .continuation_high = source->continuation_high,
        .save_byte = source->save_byte,
        .unknown_byte = source->unknown_byte,
        .terminal_radix = source->terminal_radix,
        .terminal_digit_bias = source->terminal_digit_bias,
        .continuation_radix = source->continuation_radix,
        .continuation_digit_bias = source->continuation_digit_bias,
        .unknown_policy = source->unknown_policy,
        .save_placement = source->save_placement,
        .header_hypothesis_policy = source->header_hypothesis_policy,
    };
    if (!ppstate_v1_value_intern(
            impl, source->binder_hypothesis_kind.bytes,
            source->binder_hypothesis_kind.len,
            &target->binder_hypothesis_kind) ||
        !ppstate_v1_value_intern(
            impl, source->matching_hypothesis_kind.bytes,
            source->matching_hypothesis_kind.len,
            &target->matching_hypothesis_kind) ||
        !ppstate_v1_value_intern(
            impl, source->rule_kind_first.bytes,
            source->rule_kind_first.len, &target->rule_kind_first) ||
        !ppstate_v1_value_intern(
            impl, source->rule_kind_second.bytes,
            source->rule_kind_second.len, &target->rule_kind_second) ||
        !ppstate_v1_value_intern(
            impl, source->variable_symbol_kind.bytes,
            source->variable_symbol_kind.len,
            &target->variable_symbol_kind) ||
        !ppstate_v1_value_intern(
            impl, source->unknown_token.bytes, source->unknown_token.len,
            &target->unknown_token) ||
        !pprelational_stack_proof_v1_machine_validate(
            &store, target, error_buf, error_buf_size))
        return false;
    return true;
}

static bool ppstate_v1_scope_push(PPRelationalStateRunImplV1 *impl) {
    uint32_t table_len = impl->plan->table_len;
    uint32_t next_cap;
    uint32_t *next;
    uint32_t index;

    if (impl->scope_len == impl->scope_cap) {
        next_cap = impl->scope_cap ? impl->scope_cap * 2u
                                   : PPRELATIONAL_STATE_V1_INITIAL_CAPACITY;
        if (next_cap < impl->scope_cap ||
            !ppstate_v1_array_fits(next_cap, table_len) ||
            !ppstate_v1_array_fits(
                (size_t)next_cap * table_len, sizeof(*next)))
            return false;
        next = realloc(impl->scope_lengths,
                       (size_t)next_cap * table_len * sizeof(*next));
        if (!next)
            return false;
        impl->scope_lengths = next;
        impl->scope_cap = next_cap;
    }
    for (index = 0u; index < table_len; index++) {
        impl->scope_lengths[
            (size_t)impl->scope_len * table_len + index] =
            impl->tables[index].row_len;
    }
    impl->scope_len++;
    return true;
}

static bool ppstate_v1_scope_pop(PPRelationalStateRunImplV1 *impl) {
    uint32_t table_len = impl->plan->table_len;
    uint32_t index;

    if (impl->scope_len == 0u)
        return false;
    impl->scope_len--;
    for (index = 0u; index < table_len; index++) {
        PPRelationalStateTableStateV1 *state = &impl->tables[index];
        const PPRelationalStateTableV1 *table = &impl->plan->tables[index];
        uint32_t retained = impl->scope_lengths[
            (size_t)impl->scope_len * table_len + index];
        if (retained > state->row_len)
            return false;
        if (table->lifetime == PPRELATIONAL_STATE_LIFETIME_V1_SCOPED &&
            retained != state->row_len) {
            state->row_len = retained;
            if (!ppstate_v1_table_rehash(
                    state, table,
                    state->bucket_len
                        ? state->bucket_len
                        : PPRELATIONAL_STATE_V1_INITIAL_CAPACITY))
                return false;
        }
    }
    return true;
}

static bool ppstate_v1_transaction_clear(
    PPRelationalStateRunImplV1 *impl) {
    uint32_t index;

    if (!impl)
        return false;
    for (index = 0u; index < impl->plan->table_len; index++) {
        PPRelationalStateTableStateV1 *state = &impl->tables[index];
        const PPRelationalStateTableV1 *table = &impl->plan->tables[index];
        if (table->lifetime !=
                PPRELATIONAL_STATE_LIFETIME_V1_TRANSACTIONAL ||
            state->row_len == 0u)
            continue;
        if (state->compact_eligible)
            return false;
        state->row_len = 0u;
        if (!ppstate_v1_table_rehash(
                state, table,
                state->bucket_len
                    ? state->bucket_len
                    : PPRELATIONAL_STATE_V1_INITIAL_CAPACITY))
            return false;
    }
    return true;
}

typedef enum {
    PPRELATIONAL_STATE_QUERY_V1_MATCH = 0,
    PPRELATIONAL_STATE_QUERY_V1_ABSENT = 1,
    PPRELATIONAL_STATE_QUERY_V1_INVALID = 2
} PPRelationalStateQueryV1;

static bool ppstate_v1_operand_bytes(
    const PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateOperandV1 *operand,
    const PPOccurrenceFoldV1Step *step,
    const PPOccurrenceFoldV1Value *input,
    const PPRelationalStateRowV1 *source_row,
    uint32_t source_arity, uint32_t source_row_index,
    uint32_t source_match_index,
    const uint8_t **bytes_out, uint32_t *len_out,
    uint8_t **owned_out) {
    uint32_t index;
    uint32_t matched = 0u;

    if (!operand || !bytes_out || !len_out || !owned_out)
        return false;
    *owned_out = NULL;
    if (operand->kind == PPRELATIONAL_STATE_OPERAND_V1_INPUT) {
        if (!input || input->byte_len > UINT32_MAX)
            return false;
        *bytes_out = input->bytes;
        *len_out = (uint32_t)input->byte_len;
        return true;
    }
    if (operand->kind == PPRELATIONAL_STATE_OPERAND_V1_LITERAL) {
        *bytes_out = operand->literal;
        *len_out = operand->literal_len;
        return true;
    }
    if (operand->kind == PPRELATIONAL_STATE_OPERAND_V1_ROLE_LIST) {
        if (!pprelational_value_list_v1_encode_role(
                step, operand->role_id, operand->input_index,
                owned_out, len_out))
            return false;
        *bytes_out = *owned_out;
        return true;
    }
    if (operand->kind ==
        PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN) {
        uint32_t value_id;
        if (!impl || !source_row || operand->input_index >= source_arity)
            return false;
        value_id = source_row->values[operand->input_index];
        if (value_id >= impl->value_len)
            return false;
        *bytes_out = impl->values[value_id].bytes;
        *len_out = impl->values[value_id].len;
        return true;
    }
    if (operand->kind ==
        PPRELATIONAL_STATE_OPERAND_V1_SOURCE_ROW_INDEX) {
        char rendered[11];
        int rendered_len;
        if (source_row_index == UINT32_MAX)
            return false;
        rendered_len = snprintf(rendered, sizeof(rendered), "%u",
                                source_row_index);
        if (rendered_len <= 0 || (size_t)rendered_len >= sizeof(rendered))
            return false;
        *owned_out = malloc((size_t)rendered_len);
        if (!*owned_out)
            return false;
        memcpy(*owned_out, rendered, (size_t)rendered_len);
        *bytes_out = *owned_out;
        *len_out = (uint32_t)rendered_len;
        return true;
    }
    if (operand->kind ==
        PPRELATIONAL_STATE_OPERAND_V1_SOURCE_MATCH_INDEX) {
        char rendered[11];
        int rendered_len;
        if (source_match_index == UINT32_MAX)
            return false;
        rendered_len = snprintf(rendered, sizeof(rendered), "%u",
                                source_match_index);
        if (rendered_len <= 0 || (size_t)rendered_len >= sizeof(rendered))
            return false;
        *owned_out = malloc((size_t)rendered_len);
        if (!*owned_out)
            return false;
        memcpy(*owned_out, rendered, (size_t)rendered_len);
        *bytes_out = *owned_out;
        *len_out = (uint32_t)rendered_len;
        return true;
    }
    if (operand->kind != PPRELATIONAL_STATE_OPERAND_V1_ROLE_AT || !step)
        return false;
    for (index = 0u; index < step->value_len; index++) {
        const PPOccurrenceFoldV1Value *value = &step->values[index];
        if (value->role_id != operand->role_id)
            continue;
        if (matched++ != operand->input_index)
            continue;
        if (value->byte_len > UINT32_MAX)
            return false;
        *bytes_out = value->bytes;
        *len_out = (uint32_t)value->byte_len;
        return true;
    }
    return false;
}

static bool ppstate_v1_occurrence_operand_index(
    const PPRelationalStateOperandV1 *operand,
    const PPOccurrenceFoldV1Step *step,
    const PPOccurrenceFoldV1Value *input,
    bool *recognized_out, uint32_t *index_out) {
    uint32_t index;
    uint32_t matched = 0u;

    if (!operand || !recognized_out || !index_out)
        return false;
    *recognized_out = false;
    if (operand->kind == PPRELATIONAL_STATE_OPERAND_V1_INPUT) {
        if (!step || !input)
            return false;
        for (index = 0u; index < step->value_len; index++) {
            if (&step->values[index] != input)
                continue;
            *recognized_out = true;
            *index_out = index;
            return true;
        }
        return false;
    }
    if (operand->kind != PPRELATIONAL_STATE_OPERAND_V1_ROLE_AT)
        return true;
    if (!step)
        return false;
    for (index = 0u; index < step->value_len; index++) {
        if (step->values[index].role_id != operand->role_id)
            continue;
        if (matched++ != operand->input_index)
            continue;
        *recognized_out = true;
        *index_out = index;
        return true;
    }
    return false;
}

/* Prepared literals, occurrence values, and source columns already name
 * values in this run's canonical table.  Preserve those identities instead
 * of lowering them to bytes and immediately looking them up again. */
static bool ppstate_v1_operand_canonical_value_id(
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateOperandV1 *operand,
    uint32_t prepared_id,
    const PPOccurrenceFoldV1Step *step,
    const PPOccurrenceFoldV1Value *input,
    bool intern_missing,
    const PPRelationalStateRowV1 *source_row,
    uint32_t source_arity,
    bool *direct_out,
    bool *absent_out,
    uint32_t *value_id_out) {
    bool occurrence_operand;
    uint32_t occurrence_index;
    uint32_t value_id;

    if (!operand || !direct_out || !absent_out || !value_id_out)
        return false;
    *direct_out = false;
    *absent_out = false;
    if (prepared_id != UINT32_MAX) {
        if (!impl ||
            operand->kind != PPRELATIONAL_STATE_OPERAND_V1_LITERAL ||
            prepared_id >= impl->value_len)
            return false;
        *direct_out = true;
        *value_id_out = prepared_id;
        return true;
    }
    if (!ppstate_v1_occurrence_operand_index(
            operand, step, input, &occurrence_operand,
            &occurrence_index))
        return false;
    if (occurrence_operand) {
        const PPOccurrenceFoldV1Value *value;
        int32_t found;
        uint64_t hash;
        if (!impl || occurrence_index >= impl->prepared_step_value_len ||
            occurrence_index >= step->value_len)
            return false;
        value_id = impl->prepared_step_value_ids[occurrence_index];
        if (value_id != UINT32_MAX) {
            if (value_id >= impl->value_len)
                return false;
            *direct_out = true;
            *value_id_out = value_id;
            return true;
        }
        value = &step->values[occurrence_index];
        if (value->byte_len > UINT32_MAX)
            return false;
        hash = ppstate_v1_hash_bytes(
            value->bytes, (uint32_t)value->byte_len);
        found = ppstate_v1_value_find(
            impl, value->bytes, (uint32_t)value->byte_len, hash);
        if (found < 0 && intern_missing) {
            if (!ppstate_v1_value_intern(
                    impl, value->bytes, (uint32_t)value->byte_len,
                    &value_id))
                return false;
        } else if (found < 0) {
            *absent_out = true;
            return true;
        } else {
            value_id = (uint32_t)found;
        }
        impl->prepared_step_value_ids[occurrence_index] = value_id;
        *direct_out = true;
        *value_id_out = value_id;
        return true;
    }
    if (operand->kind != PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN)
        return true;
    if (!impl || !source_row || operand->input_index >= source_arity)
        return false;
    value_id = source_row->values[operand->input_index];
    if (value_id >= impl->value_len)
        return false;
    *direct_out = true;
    *value_id_out = value_id;
    return true;
}

static PPRelationalStateInsertV1 ppstate_v1_row_intern_values(
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateOperandV1 *operands,
    const uint32_t *prepared_ids,
    uint32_t operand_len,
    const PPOccurrenceFoldV1Step *step,
    const PPOccurrenceFoldV1Value *input,
    const PPRelationalStateRowV1 *source_row,
    uint32_t source_arity, uint32_t source_row_index,
    uint32_t source_match_index,
    PPRelationalStateRowV1 *row) {
    uint32_t index;

    memset(row, 0, sizeof(*row));
    for (index = 0u; index < operand_len; index++) {
        bool direct;
        bool absent;
        uint32_t value_id;
        const uint8_t *bytes;
        uint32_t len;
        uint8_t *owned;
        uint32_t prepared_id = prepared_ids
                                   ? prepared_ids[index]
                                   : UINT32_MAX;
        if (!ppstate_v1_operand_canonical_value_id(
                impl, &operands[index], prepared_id,
                step, input, true,
                source_row, source_arity,
                &direct, &absent, &value_id) || absent)
            return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
        if (direct) {
            row->values[index] = value_id;
            continue;
        }
        if (!ppstate_v1_operand_bytes(
                impl, &operands[index], step, input,
                source_row, source_arity, source_row_index,
                source_match_index,
                &bytes, &len, &owned))
            return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
        if (!ppstate_v1_value_intern(impl, bytes, len,
                                     &row->values[index])) {
            free(owned);
            return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
        }
        free(owned);
    }
    return PPRELATIONAL_STATE_INSERT_V1_OK;
}

static PPRelationalStateInsertV1 ppstate_v1_row_intern(
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateActionV1 *action,
    const PPRelationalStatePreparedActionV1 *prepared,
    const PPOccurrenceFoldV1Step *step,
    const PPOccurrenceFoldV1Value *input,
    PPRelationalStateRowV1 *row) {
    return ppstate_v1_row_intern_values(
        impl, action->operands,
        prepared ? prepared->operand_ids : NULL, action->operand_len,
        step, input, NULL, 0u, UINT32_MAX, UINT32_MAX, row);
}

static bool ppstate_v1_action_writes_table(
    const PPRelationalStateActionV1 *action, uint32_t table_id) {
    if (!action || action->table_id != table_id)
        return false;
    switch (action->kind) {
    case PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH:
    case PPRELATIONAL_STATE_ACTION_V1_INSERT_UNORDERED_PAIRS:
    case PPRELATIONAL_STATE_ACTION_V1_INSERT_ROW:
    case PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING:
    case PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW:
    case PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING:
        return true;
    default:
        return false;
    }
}

static bool ppstate_v1_action_reads_table(
    const PPRelationalStateActionV1 *action, uint32_t table_id) {
    if (!action)
        return false;
    switch (action->kind) {
    case PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH:
    case PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH_KEY_ABSENT:
    case PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW:
    case PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW_KEY_ABSENT:
        return action->table_id == table_id;
    case PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING:
        return action->condition_table_id == table_id;
    case PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW:
        return action->source_table_id == table_id;
    case PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING:
        return action->source_table_id == table_id ||
               action->condition_table_id == table_id;
    default:
        return false;
    }
}

static bool ppstate_v1_compact_copy_action_valid(
    const PPRelationalStateProgramV1Plan *plan,
    const PPRelationalStateActionV1 *action, uint32_t table_id) {
    const PPRelationalStateTableV1 *target;
    const PPRelationalStateTableV1 *source;
    bool source_keys[PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY] = {false};
    bool row_index_key = false;
    uint32_t column;

    if (!plan || !action ||
        action->kind != PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW ||
        action->table_id != table_id || table_id >= plan->table_len ||
        action->source_table_id >= plan->table_len)
        return false;
    target = &plan->tables[table_id];
    source = &plan->tables[action->source_table_id];
    if (target->lifetime != PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT ||
        target->arity == 0u || target->key_arity == 0u ||
        action->operand_len != target->arity ||
        action->operands[0].kind ==
            PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN ||
        action->operands[0].kind ==
            PPRELATIONAL_STATE_OPERAND_V1_SOURCE_ROW_INDEX ||
        action->operands[0].kind == PPRELATIONAL_STATE_OPERAND_V1_INPUT)
        return false;
    for (column = 0u; column < target->key_arity; column++) {
        const PPRelationalStateOperandV1 *operand =
            &action->operands[column];
        if (operand->kind ==
            PPRELATIONAL_STATE_OPERAND_V1_SOURCE_ROW_INDEX) {
            row_index_key = true;
        } else if (operand->kind ==
                       PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN &&
                   operand->input_index < source->key_arity) {
            source_keys[operand->input_index] = true;
        }
    }
    if (row_index_key)
        return true;
    if (source->key_arity == 0u)
        return false;
    for (column = 0u; column < source->key_arity; column++) {
        if (!source_keys[column])
            return false;
    }
    return true;
}

static bool ppstate_v1_compact_table_valid(
    const PPRelationalStateProgramV1Plan *plan, uint32_t table_id) {
    const PPRelationalStateActionV1 *action_sets[2];
    const uint32_t action_lens[2] = {
        plan ? plan->action_len : 0u,
        plan ? plan->final_action_len : 0u,
    };
    bool wrote = false;
    uint32_t set_index;
    uint32_t index;

    if (!plan || table_id >= plan->table_len)
        return false;
    action_sets[0] = plan->actions;
    action_sets[1] = plan->final_actions;
    for (set_index = 0u; set_index < 2u; set_index++) {
        for (index = 0u; index < action_lens[set_index]; index++) {
            const PPRelationalStateActionV1 *action =
                &action_sets[set_index][index];
            if (ppstate_v1_action_reads_table(action, table_id))
                return false;
            if (!ppstate_v1_action_writes_table(action, table_id))
                continue;
            wrote = true;
            if (!ppstate_v1_compact_copy_action_valid(
                    plan, action, table_id))
                return false;
        }
    }
    return wrote;
}

static void ppstate_v1_compact_layouts_init(
    PPRelationalStateRunImplV1 *impl) {
    uint32_t table_id;
    if (!impl)
        return;
    for (table_id = 0u; table_id < impl->plan->table_len; table_id++)
        impl->tables[table_id].compact_eligible =
            ppstate_v1_compact_table_valid(impl->plan, table_id);
}

static PPRelationalStateInsertV1 ppstate_v1_compact_copy(
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateActionV1 *action,
    const PPRelationalStatePreparedActionV1 *prepared,
    const PPOccurrenceFoldV1Step *step, uint32_t *matched_out) {
    PPRelationalStateTableStateV1 *target_state;
    const PPRelationalStateTableStateV1 *source_state;
    const PPRelationalStateTableV1 *target_table;
    const PPRelationalStateTableV1 *source_table;
    PPRelationalStateCompactRunV1 run;
    uint32_t target_column;
    uint32_t source_row;
    uint32_t needed_values;
    int32_t existing;

    if (!impl || !action || !step || !matched_out ||
        action->table_id >= impl->plan->table_len ||
        action->source_table_id >= impl->plan->table_len)
        return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
    target_state = &impl->tables[action->table_id];
    source_state = &impl->tables[action->source_table_id];
    target_table = &impl->plan->tables[action->table_id];
    source_table = &impl->plan->tables[action->source_table_id];
    *matched_out = source_state->row_len;
    if (!target_state->compact_eligible)
        return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
    if (source_state->row_len == 0u)
        return PPRELATIONAL_STATE_INSERT_V1_OK;
    memset(&run, 0, sizeof(run));
    run.row_begin = target_state->compact_row_len;
    run.row_len = source_state->row_len;
    run.value_begin = target_state->compact_value_len;
    for (target_column = 0u; target_column < target_table->arity;
         target_column++) {
        const PPRelationalStateOperandV1 *operand =
            &action->operands[target_column];
        if (operand->kind ==
            PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN) {
            if (operand->input_index >= source_table->arity ||
                run.source_value_len == UINT8_MAX)
                return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
            run.column_kinds[target_column] =
                PPSTATE_V1_COMPACT_COLUMN_SOURCE;
            run.source_slots[target_column] = run.source_value_len++;
        } else if (operand->kind ==
                   PPRELATIONAL_STATE_OPERAND_V1_SOURCE_ROW_INDEX) {
            run.column_kinds[target_column] =
                PPSTATE_V1_COMPACT_COLUMN_ROW_INDEX;
        } else {
            run.column_kinds[target_column] =
                PPSTATE_V1_COMPACT_COLUMN_FIXED;
            if (prepared &&
                prepared->operand_ids[target_column] != UINT32_MAX) {
                uint32_t value_id =
                    prepared->operand_ids[target_column];
                if (operand->kind !=
                        PPRELATIONAL_STATE_OPERAND_V1_LITERAL ||
                    value_id >= impl->value_len)
                    return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
                run.fixed_values[target_column] = value_id;
            } else {
                const uint8_t *bytes;
                uint32_t len;
                uint8_t *owned;
                if (!ppstate_v1_operand_bytes(
                        impl, operand, step, NULL, NULL, 0u,
                        UINT32_MAX, UINT32_MAX, &bytes, &len, &owned))
                    return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
                if (!ppstate_v1_value_intern(
                        impl, bytes, len,
                        &run.fixed_values[target_column])) {
                    free(owned);
                    return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
                }
                free(owned);
            }
        }
    }
    if (run.column_kinds[0] != PPSTATE_V1_COMPACT_COLUMN_FIXED)
        return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
    run.prefix = run.fixed_values[0];
    existing = ppstate_v1_compact_run_find(target_state, run.prefix);
    if (existing >= 0) {
        const PPRelationalStateCompactRunV1 *prior =
            &target_state->compact_runs[existing];
        if (action->write_policy ==
                PPRELATIONAL_STATE_WRITE_V1_REQUIRE_KEY_ABSENT ||
            prior->row_len != source_state->row_len)
            return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
        for (source_row = 0u; source_row < source_state->row_len;
             source_row++) {
            PPRelationalStateRowV1 candidate;
            PPRelationalStateRowV1 retained;
            PPRelationalStateInsertV1 evaluated =
                ppstate_v1_row_intern_values(
                    impl, action->operands,
                    prepared ? prepared->operand_ids : NULL,
                    action->operand_len, step,
                    NULL, &source_state->rows[source_row],
                    source_table->arity, source_row, UINT32_MAX,
                    &candidate);
            if (evaluated != PPRELATIONAL_STATE_INSERT_V1_OK)
                return evaluated;
            if (!ppstate_v1_compact_row(
                    impl, action->table_id, prior, source_row,
                    &retained))
                return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
            if (!ppstate_v1_row_equal(
                    &candidate, &retained, target_table->arity))
                return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
        }
        return PPRELATIONAL_STATE_INSERT_V1_OK;
    }
    if (run.source_value_len > 0u &&
        source_state->row_len > UINT32_MAX / run.source_value_len)
        return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
    needed_values = source_state->row_len * run.source_value_len;
    if (target_state->compact_value_len > UINT32_MAX - needed_values ||
        target_state->compact_row_len > UINT32_MAX - source_state->row_len ||
        !ppstate_v1_grow_raw(
            (void **)&target_state->compact_runs,
            &target_state->compact_run_cap,
            target_state->compact_run_len + 1u,
            sizeof(*target_state->compact_runs)) ||
        !ppstate_v1_grow_raw(
            (void **)&target_state->compact_values,
            &target_state->compact_value_cap,
            target_state->compact_value_len + needed_values,
            sizeof(*target_state->compact_values)))
        return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
    if (target_state->compact_run_bucket_len == 0u) {
        if (!ppstate_v1_compact_run_rehash(
                target_state, PPRELATIONAL_STATE_V1_INITIAL_CAPACITY))
            return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
    } else if ((uint64_t)(target_state->compact_run_len + 1u) * 10u >=
                   (uint64_t)target_state->compact_run_bucket_len * 7u &&
               !ppstate_v1_compact_run_rehash(
                   target_state,
                   target_state->compact_run_bucket_len * 2u)) {
        return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
    }
    for (source_row = 0u; source_row < source_state->row_len;
         source_row++) {
        uint32_t source_slot = 0u;
        for (target_column = 0u; target_column < target_table->arity;
             target_column++) {
            const PPRelationalStateOperandV1 *operand =
                &action->operands[target_column];
            if (operand->kind !=
                PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN)
                continue;
            target_state->compact_values[
                target_state->compact_value_len +
                source_row * run.source_value_len + source_slot++] =
                source_state->rows[source_row]
                    .values[operand->input_index];
        }
    }
    {
        uint32_t slot = (uint32_t)ppstate_v1_row_hash(
                            &(PPRelationalStateRowV1){{run.prefix}}, 1u) &
                        (target_state->compact_run_bucket_len - 1u);
        while (target_state->compact_run_buckets[slot] != 0u)
            slot = (slot + 1u) &
                   (target_state->compact_run_bucket_len - 1u);
        target_state->compact_runs[target_state->compact_run_len] = run;
        target_state->compact_run_buckets[slot] =
            target_state->compact_run_len + 1u;
    }
    target_state->compact_run_len++;
    target_state->compact_value_len += needed_values;
    target_state->compact_row_len += source_state->row_len;
    return PPRELATIONAL_STATE_INSERT_V1_OK;
}

static PPRelationalStateQueryV1 ppstate_v1_row_query_values(
    PPRelationalStateRunImplV1 *impl,
    uint32_t table_id,
    const PPRelationalStateOperandV1 *operands,
    const uint32_t *prepared_ids,
    uint32_t operand_len,
    const PPOccurrenceFoldV1Step *step,
    const PPOccurrenceFoldV1Value *input,
    const PPRelationalStateRowV1 *source_row,
    uint32_t source_arity,
    uint32_t source_row_index,
    uint32_t source_match_index,
    bool exact) {
    PPRelationalStateRowV1 row = {{0u, 0u}};
    const PPRelationalStateTableStateV1 *state =
        &impl->tables[table_id];
    const PPRelationalStateTableV1 *table =
        &impl->plan->tables[table_id];
    uint32_t index;
    int32_t found;

    for (index = 0u; index < operand_len; index++) {
        bool direct;
        bool absent;
        uint32_t value_id;
        const uint8_t *bytes;
        uint32_t len;
        uint8_t *owned;
        uint64_t hash;
        uint32_t prepared_id = prepared_ids
                                   ? prepared_ids[index]
                                   : UINT32_MAX;
        if (!ppstate_v1_operand_canonical_value_id(
                impl, &operands[index], prepared_id,
                step, input, false,
                source_row, source_arity,
                &direct, &absent, &value_id))
            return PPRELATIONAL_STATE_QUERY_V1_INVALID;
        if (absent)
            return PPRELATIONAL_STATE_QUERY_V1_ABSENT;
        if (direct) {
            row.values[index] = value_id;
            continue;
        }
        if (!ppstate_v1_operand_bytes(
                impl, &operands[index], step, input, source_row,
                source_arity, source_row_index, source_match_index,
                &bytes, &len, &owned))
            return PPRELATIONAL_STATE_QUERY_V1_INVALID;
        hash = ppstate_v1_hash_bytes(bytes, len);
        found = ppstate_v1_value_find(impl, bytes, len, hash);
        free(owned);
        if (found < 0)
            return PPRELATIONAL_STATE_QUERY_V1_ABSENT;
        row.values[index] = (uint32_t)found;
    }
    found = ppstate_v1_table_find_row(state, table, &row);
    if (found < 0)
        return PPRELATIONAL_STATE_QUERY_V1_ABSENT;
    if (exact && !ppstate_v1_row_equal(
                     &state->rows[found], &row, table->arity))
        return PPRELATIONAL_STATE_QUERY_V1_ABSENT;
    return PPRELATIONAL_STATE_QUERY_V1_MATCH;
}

static PPRelationalStateQueryV1 ppstate_v1_row_query(
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateActionV1 *action,
    const PPRelationalStatePreparedActionV1 *prepared,
    const PPOccurrenceFoldV1Step *step,
    const PPOccurrenceFoldV1Value *input,
    bool exact) {
    return ppstate_v1_row_query_values(
        impl, action->table_id, action->operands,
        prepared ? prepared->operand_ids : NULL,
        action->operand_len, step, input, NULL, 0u, UINT32_MAX,
        UINT32_MAX, exact);
}

static PPRelationalStateInsertV1 ppstate_v1_insert_input(
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateActionV1 *action,
    const PPRelationalStatePreparedActionV1 *prepared,
    const PPOccurrenceFoldV1Step *step,
    const PPOccurrenceFoldV1Value *input) {
    PPRelationalStateRowV1 row = {{0u, 0u}};
    PPRelationalStateTableStateV1 *state =
        &impl->tables[action->table_id];
    const PPRelationalStateTableV1 *table =
        &impl->plan->tables[action->table_id];
    PPRelationalStateInsertV1 evaluated = ppstate_v1_row_intern(
        impl, action, prepared, step, input, &row);
    if (evaluated != PPRELATIONAL_STATE_INSERT_V1_OK)
        return evaluated;
    return ppstate_v1_table_insert(
        state, table, &row, action->write_policy);
}

static bool ppstate_v1_require_input(
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateActionV1 *action,
    const PPRelationalStatePreparedActionV1 *prepared,
    const PPOccurrenceFoldV1Step *step,
    const PPOccurrenceFoldV1Value *input) {
    return ppstate_v1_row_query(
               impl, action, prepared, step, input, true) ==
           PPRELATIONAL_STATE_QUERY_V1_MATCH;
}

static PPRelationalStateInsertV1
ppstate_v1_insert_unordered_pairs(
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateActionV1 *action,
    const PPOccurrenceFoldV1Step *step,
    uint32_t *matched_out) {
    PPRelationalStateTableStateV1 *state =
        &impl->tables[action->table_id];
    const PPRelationalStateTableV1 *table =
        &impl->plan->tables[action->table_id];
    uint32_t *values = NULL;
    uint32_t matched = 0u;
    uint32_t left;
    uint32_t right;
    PPRelationalStateInsertV1 result =
        PPRELATIONAL_STATE_INSERT_V1_RESOURCE;

    if (!matched_out)
        return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
    *matched_out = 0u;
    for (left = 0u; left < step->value_len; left++) {
        if (step->values[left].role_id == action->role_id)
            matched++;
    }
    if (matched < 2u)
        return PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
    if (!ppstate_v1_array_fits(matched, sizeof(*values)) ||
        !(values = malloc((size_t)matched * sizeof(*values))))
        return PPRELATIONAL_STATE_INSERT_V1_RESOURCE;
    matched = 0u;
    for (left = 0u; left < step->value_len; left++) {
        const PPOccurrenceFoldV1Value *input = &step->values[left];
        if (input->role_id != action->role_id)
            continue;
        if (!ppstate_v1_value_intern(
                impl, input->bytes, input->byte_len, &values[matched]))
            goto done;
        for (right = 0u; right < matched; right++) {
            if (values[right] == values[matched]) {
                result = PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT;
                goto done;
            }
        }
        matched++;
    }
    for (left = 0u; left < matched; left++) {
        for (right = left + 1u; right < matched; right++) {
            PPRelationalStateRowV1 row = {{values[left], values[right]}};
            if (ppstate_v1_value_compare(
                    impl, row.values[0], row.values[1]) > 0) {
                uint32_t swap = row.values[0];
                row.values[0] = row.values[1];
                row.values[1] = swap;
            }
            result = ppstate_v1_table_insert(
                state, table, &row, action->write_policy);
            if (result != PPRELATIONAL_STATE_INSERT_V1_OK)
                goto done;
        }
    }
    result = PPRELATIONAL_STATE_INSERT_V1_OK;
    *matched_out = matched;

done:
    free(values);
    return result;
}

static bool ppstate_v1_role_slices(
    const PPOccurrenceFoldV1Step *step, uint32_t role_id,
    PPRelationalValueV1Slice **slices_out, uint32_t *len_out) {
    PPRelationalValueV1Slice *slices = NULL;
    uint32_t count = 0u;
    uint32_t index;

    if (!step || !slices_out || !len_out)
        return false;
    *slices_out = NULL;
    *len_out = 0u;
    for (index = 0u; index < step->value_len; index++) {
        if (step->values[index].role_id == role_id) {
            if (step->values[index].byte_len > UINT32_MAX ||
                count == UINT32_MAX)
                return false;
            count++;
        }
    }
    if (count == 0u)
        return true;
    if (!ppstate_v1_array_fits(count, sizeof(*slices)) ||
        !(slices = calloc(count, sizeof(*slices))))
        return false;
    count = 0u;
    for (index = 0u; index < step->value_len; index++) {
        if (step->values[index].role_id != role_id)
            continue;
        slices[count++] = (PPRelationalValueV1Slice){
            .bytes = step->values[index].bytes,
            .len = (uint32_t)step->values[index].byte_len,
        };
    }
    *slices_out = slices;
    *len_out = count;
    return true;
}

static bool ppstate_v1_execute_proof(
    PPRelationalStateProgramV1Run *run,
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateActionV1 *action,
    uint32_t action_index,
    const PPOccurrenceFoldV1Step *step,
    uint32_t *matched_out, char *error_buf, size_t error_buf_size) {
    PPRelationalValueV1Slice *labels = NULL;
    PPRelationalValueV1Slice *formula = NULL;
    PPRelationalValueV1Slice *proof = NULL;
    PPRelationalValueV1Slice *code = NULL;
    uint32_t label_len = 0u;
    uint32_t formula_len = 0u;
    uint32_t proof_len = 0u;
    uint32_t code_len = 0u;
    PPRelationalStoreV1 store = ppstate_v1_store(impl);
    PPRelationalStackProofV1Result result =
        PPRELATIONAL_STACK_PROOF_V1_INVALID;
    PPRelationalStateProofV1Result proof_result =
        PPRELATIONAL_STATE_PROOF_V1_INVALID;
    bool compressed = action->kind ==
        PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_COMPRESSED;
    const char *operation =
        impl && impl->occurrence_plan && action &&
                action->operation_id < impl->occurrence_plan->operation_len
            ? impl->occurrence_plan->operations[action->operation_id]
            : NULL;
    const char *header_role =
        impl && impl->occurrence_plan && action &&
                action->proof_step_role_id < impl->occurrence_plan->role_len
            ? impl->occurrence_plan->roles[action->proof_step_role_id]
            : NULL;
    const char *code_role =
        compressed && impl && impl->occurrence_plan && action &&
                action->proof_code_role_id < impl->occurrence_plan->role_len
            ? impl->occurrence_plan->roles[action->proof_code_role_id]
            : NULL;
    bool ok = false;

    if (!step || !operation || !header_role ||
        (compressed && !code_role) ||
        action_index == UINT32_MAX ||
        action->proof_machine_id >= impl->plan->proof_machine_len ||
        !ppstate_v1_role_slices(
            step, action->proof_label_role_id, &labels, &label_len) ||
        !ppstate_v1_role_slices(
            step, action->proof_formula_role_id, &formula, &formula_len) ||
        !ppstate_v1_role_slices(
            step, action->proof_step_role_id, &proof, &proof_len) ||
        (compressed && !ppstate_v1_role_slices(
            step, action->proof_code_role_id, &code, &code_len)) ||
        label_len != 1u || formula_len == 0u ||
        (!compressed && proof_len == 0u) ||
        (compressed && code_len == 0u)) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "proof occurrence violates its generated role contract");
        goto done;
    }
    if (impl->proof_backend_ready) {
        PPRelationalStateProofV1Request request = {
            .store = &store,
            .state_plan = impl->plan,
            .operation = operation,
            .action_index = action_index,
            .header_role = header_role,
            .code_role = code_role,
            .proof_machine_id = action->proof_machine_id,
            .compressed = compressed,
            .label = labels[0],
            .claim = formula,
            .claim_len = formula_len,
            .proof = proof,
            .proof_len = proof_len,
            .code = code,
            .code_len = code_len,
        };
        proof_result = impl->proof_backend.execute(
            impl->proof_backend.context, &request,
            error_buf, error_buf_size);
    } else if (compressed) {
        PPRelationalStackProofV1CompressedInput input = {
            .label = labels[0],
            .claim = formula,
            .claim_len = formula_len,
            .header = proof,
            .header_len = proof_len,
            .code = code,
            .code_len = code_len,
        };
        result = impl->proof_frame_caches_ready
            ? pprelational_stack_proof_v1_compressed_cached(
                  &store,
                  &impl->proof_machines[action->proof_machine_id],
                  &impl->proof_frame_caches[action->proof_machine_id],
                  &input, error_buf, error_buf_size)
            : pprelational_stack_proof_v1_compressed(
                  &store,
                  &impl->proof_machines[action->proof_machine_id],
                  &input, error_buf, error_buf_size);
    } else {
        PPRelationalStackProofV1NormalInput input = {
            .label = labels[0],
            .claim = formula,
            .claim_len = formula_len,
            .steps = proof,
            .step_len = proof_len,
        };
        result = impl->proof_frame_caches_ready
            ? pprelational_stack_proof_v1_normal_cached(
                  &store,
                  &impl->proof_machines[action->proof_machine_id],
                  &impl->proof_frame_caches[action->proof_machine_id],
                  &input, error_buf, error_buf_size)
            : pprelational_stack_proof_v1_normal(
                  &store,
                  &impl->proof_machines[action->proof_machine_id],
                  &input, error_buf, error_buf_size);
    }
    if (!impl->proof_backend_ready) {
        proof_result = result == PPRELATIONAL_STACK_PROOF_V1_OK
            ? PPRELATIONAL_STATE_PROOF_V1_VERIFIED
            : result == PPRELATIONAL_STACK_PROOF_V1_INCOMPLETE
                ? PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE
                : result == PPRELATIONAL_STACK_PROOF_V1_REJECTED
                    ? PPRELATIONAL_STATE_PROOF_V1_REJECTED
                    : result == PPRELATIONAL_STACK_PROOF_V1_RESOURCE
                        ? PPRELATIONAL_STATE_PROOF_V1_RESOURCE
                        : PPRELATIONAL_STATE_PROOF_V1_INVALID;
    }
    if (proof_result == PPRELATIONAL_STATE_PROOF_V1_VERIFIED) {
        if (run->receipt.verified_proof_len == UINT32_MAX) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            goto done;
        }
        run->receipt.verified_proof_len++;
    } else if (proof_result == PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE) {
        if (run->receipt.incomplete_proof_len == UINT32_MAX) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            goto done;
        }
        run->receipt.incomplete_proof_len++;
    } else {
        PPRelationalStateFailureV1 failure =
            proof_result == PPRELATIONAL_STATE_PROOF_V1_REJECTED
                ? PPRELATIONAL_STATE_FAILURE_V1_REJECTED
                : proof_result == PPRELATIONAL_STATE_PROOF_V1_UNSUPPORTED
                    ? PPRELATIONAL_STATE_FAILURE_V1_UNSUPPORTED
                    : proof_result == PPRELATIONAL_STATE_PROOF_V1_RESOURCE
                        ? PPRELATIONAL_STATE_FAILURE_V1_RESOURCE
                        : PPRELATIONAL_STATE_FAILURE_V1_INVALID;
        ppstate_v1_failure_set(run, failure);
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0')
            ppstate_v1_set_error(
                error_buf, error_buf_size,
                impl->proof_backend_ready
                    ? "configured proof backend rejected the generated request"
                    : "generated proof execution failed");
        goto done;
    }
    if (UINT32_MAX - label_len < formula_len ||
        UINT32_MAX - label_len - formula_len < proof_len ||
        UINT32_MAX - label_len - formula_len - proof_len < code_len) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
        goto done;
    }
    *matched_out = label_len + formula_len + proof_len + code_len;
    ok = true;

done:
    free(labels);
    free(formula);
    free(proof);
    free(code);
    return ok;
}

static bool ppstate_v1_execute_action(
    PPRelationalStateProgramV1Run *run,
    PPRelationalStateRunImplV1 *impl,
    const PPRelationalStateActionV1 *action,
    const PPRelationalStatePreparedActionV1 *prepared,
    uint32_t action_index,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf, size_t error_buf_size) {
    uint32_t index;
    uint32_t matched = 0u;

    switch (action->kind) {
    case PPRELATIONAL_STATE_ACTION_V1_REQUIRE_DEPTH:
        if (impl->scope_len != action->required_depth) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_REJECTED);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state scope-depth requirement failed");
            return false;
        }
        break;
    case PPRELATIONAL_STATE_ACTION_V1_PUSH_SCOPE:
        if (!ppstate_v1_scope_push(impl)) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state scope push failed");
            return false;
        }
        if (impl->scope_len > run->receipt.maximum_scope_depth)
            run->receipt.maximum_scope_depth = impl->scope_len;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_POP_SCOPE:
        if (!ppstate_v1_scope_pop(impl)) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_REJECTED);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state scope pop failed");
            return false;
        }
        break;
    case PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH:
        if (!step) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state insertion lacks an occurrence");
            return false;
        }
        for (index = 0u; index < step->value_len; index++) {
            if (step->values[index].role_id != action->role_id)
                continue;
            matched++;
            {
                PPRelationalStateInsertV1 inserted =
                    ppstate_v1_insert_input(
                        impl, action, prepared, step,
                        &step->values[index]);
                if (inserted == PPRELATIONAL_STATE_INSERT_V1_OK)
                    continue;
                ppstate_v1_failure_set(
                    run,
                    inserted == PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                        ? PPRELATIONAL_STATE_FAILURE_V1_REJECTED
                        : PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    inserted == PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                        ? "state relation insertion constraint failed"
                        : "state relation insertion allocation failed");
                return false;
            }
        }
        if (matched == 0u) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state action input role is absent");
            return false;
        }
        if (UINT32_MAX - run->receipt.input_len < matched) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len += matched;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH:
        if (!step) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state requirement lacks an occurrence");
            return false;
        }
        for (index = 0u; index < step->value_len; index++) {
            if (step->values[index].role_id != action->role_id)
                continue;
            matched++;
            if (!ppstate_v1_require_input(
                    impl, action, prepared, step,
                    &step->values[index])) {
                ppstate_v1_failure_set(
                    run, PPRELATIONAL_STATE_FAILURE_V1_REJECTED);
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    "state relation membership requirement failed");
                return false;
            }
        }
        if (matched == 0u) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state action input role is absent");
            return false;
        }
        if (UINT32_MAX - run->receipt.input_len < matched) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len += matched;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH_KEY_ABSENT:
        if (!step) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state absence requirement lacks an occurrence");
            return false;
        }
        for (index = 0u; index < step->value_len; index++) {
            PPRelationalStateQueryV1 query;
            if (step->values[index].role_id != action->role_id)
                continue;
            matched++;
            query = ppstate_v1_row_query(
                impl, action, prepared, step,
                &step->values[index], false);
            if (query == PPRELATIONAL_STATE_QUERY_V1_INVALID) {
                ppstate_v1_failure_set(
                    run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    "state key-absence operand is invalid");
                return false;
            }
            if (query == PPRELATIONAL_STATE_QUERY_V1_MATCH) {
                ppstate_v1_failure_set(
                    run, PPRELATIONAL_STATE_FAILURE_V1_REJECTED);
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    "state key-absence requirement failed");
                return false;
            }
        }
        if (matched == 0u) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state action input role is absent");
            return false;
        }
        if (UINT32_MAX - run->receipt.input_len < matched) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len += matched;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW:
    case PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW_KEY_ABSENT:
        if (!step) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state row requirement lacks an occurrence");
            return false;
        }
        {
            bool absent = action->kind ==
                PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW_KEY_ABSENT;
            PPRelationalStateQueryV1 query = ppstate_v1_row_query(
                impl, action, prepared, step, NULL, !absent);
            if (query == PPRELATIONAL_STATE_QUERY_V1_INVALID) {
                ppstate_v1_failure_set(
                    run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
                ppstate_v1_set_error(error_buf, error_buf_size,
                                     "state row operand is invalid");
                return false;
            }
            if ((!absent && query != PPRELATIONAL_STATE_QUERY_V1_MATCH) ||
                (absent && query != PPRELATIONAL_STATE_QUERY_V1_ABSENT)) {
                ppstate_v1_failure_set(
                    run, PPRELATIONAL_STATE_FAILURE_V1_REJECTED);
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    absent ? "state row key-absence requirement failed"
                           : "state row membership requirement failed");
                return false;
            }
        }
        matched = action->operand_len;
        if (UINT32_MAX - run->receipt.input_len < matched) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len += matched;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_INSERT_ROW:
        if (!step) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state row insertion lacks an occurrence");
            return false;
        }
        {
            PPRelationalStateRowV1 row = {{0u, 0u}};
            PPRelationalStateInsertV1 inserted = ppstate_v1_row_intern(
                impl, action, prepared, step, NULL, &row);
            if (inserted == PPRELATIONAL_STATE_INSERT_V1_OK) {
                inserted = ppstate_v1_table_insert(
                    &impl->tables[action->table_id],
                    &impl->plan->tables[action->table_id], &row,
                    action->write_policy);
            }
            if (inserted != PPRELATIONAL_STATE_INSERT_V1_OK) {
                ppstate_v1_failure_set(
                    run,
                    inserted == PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                        ? PPRELATIONAL_STATE_FAILURE_V1_REJECTED
                        : PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    inserted == PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                        ? "state row insertion constraint failed"
                        : "state row insertion allocation failed");
                return false;
            }
        }
        matched = action->operand_len;
        if (UINT32_MAX - run->receipt.input_len < matched) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len += matched;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING:
        if (!step) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state filtered insertion lacks an occurrence");
            return false;
        }
        for (index = 0u; index < step->value_len; index++) {
            const PPOccurrenceFoldV1Value *input = &step->values[index];
            PPRelationalStateQueryV1 query;
            PPRelationalStateRowV1 row = {{0u, 0u}};
            PPRelationalStateInsertV1 inserted;
            if (input->role_id != action->role_id)
                continue;
            matched++;
            query = ppstate_v1_row_query_values(
                impl, action->condition_table_id,
                action->condition_operands,
                prepared ? prepared->condition_operand_ids : NULL,
                action->condition_operand_len, step, input, NULL, 0u,
                UINT32_MAX, UINT32_MAX, true);
            if (query == PPRELATIONAL_STATE_QUERY_V1_ABSENT)
                continue;
            if (query == PPRELATIONAL_STATE_QUERY_V1_INVALID) {
                ppstate_v1_failure_set(
                    run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    "state filtered-insertion condition is invalid");
                return false;
            }
            inserted = ppstate_v1_row_intern(
                impl, action, prepared, step, input, &row);
            if (inserted == PPRELATIONAL_STATE_INSERT_V1_OK) {
                inserted = ppstate_v1_table_insert(
                    &impl->tables[action->table_id],
                    &impl->plan->tables[action->table_id], &row,
                    action->write_policy);
            }
            if (inserted != PPRELATIONAL_STATE_INSERT_V1_OK) {
                ppstate_v1_failure_set(
                    run,
                    inserted == PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                        ? PPRELATIONAL_STATE_FAILURE_V1_REJECTED
                        : PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    inserted == PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                        ? "state filtered insertion constraint failed"
                        : "state filtered insertion allocation failed");
                return false;
            }
        }
        if (matched == 0u) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state action input role is absent");
            return false;
        }
        if (UINT32_MAX - run->receipt.input_len < matched) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len += matched;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW:
        if (!step) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state row copy lacks an occurrence");
            return false;
        }
        {
            if (impl->tables[action->table_id].compact_eligible) {
                PPRelationalStateInsertV1 inserted =
                    ppstate_v1_compact_copy(
                        impl, action, prepared, step, &matched);
                if (inserted != PPRELATIONAL_STATE_INSERT_V1_OK) {
                    ppstate_v1_failure_set(
                        run,
                        inserted ==
                                PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                            ? PPRELATIONAL_STATE_FAILURE_V1_REJECTED
                            : PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
                    ppstate_v1_set_error(
                        error_buf, error_buf_size,
                        inserted ==
                                PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                            ? "compact state row-copy constraint failed"
                            : "compact state row-copy allocation failed");
                    return false;
                }
                goto ppstate_v1_copy_complete;
            }
            const PPRelationalStateTableStateV1 *source_state =
                &impl->tables[action->source_table_id];
            const PPRelationalStateTableV1 *source_table =
                &impl->plan->tables[action->source_table_id];
            const uint32_t source_row_len = source_state->row_len;

            for (index = 0u; index < source_row_len; index++) {
                PPRelationalStateRowV1 row = {{0u}};
                PPRelationalStateInsertV1 inserted =
                    ppstate_v1_row_intern_values(
                        impl, action->operands,
                        prepared ? prepared->operand_ids : NULL,
                        action->operand_len,
                        step, NULL, &source_state->rows[index],
                        source_table->arity, index, UINT32_MAX, &row);
                if (inserted == PPRELATIONAL_STATE_INSERT_V1_OK) {
                    inserted = ppstate_v1_table_insert(
                        &impl->tables[action->table_id],
                        &impl->plan->tables[action->table_id], &row,
                        action->write_policy);
                }
                if (inserted != PPRELATIONAL_STATE_INSERT_V1_OK) {
                    ppstate_v1_failure_set(
                        run,
                        inserted ==
                                PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                            ? PPRELATIONAL_STATE_FAILURE_V1_REJECTED
                            : PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
                    ppstate_v1_set_error(
                        error_buf, error_buf_size,
                        inserted ==
                                PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                            ? "state row-copy constraint failed"
                            : "state row-copy allocation failed");
                    return false;
                }
            }
            matched = source_row_len;
        }
ppstate_v1_copy_complete:
        if (UINT32_MAX - run->receipt.input_len < matched) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len += matched;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING:
        if (!step) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(
                error_buf, error_buf_size,
                "state filtered row copy lacks an occurrence");
            return false;
        }
        {
            const PPRelationalStateTableStateV1 *source_state =
                &impl->tables[action->source_table_id];
            const PPRelationalStateTableV1 *source_table =
                &impl->plan->tables[action->source_table_id];
            const uint32_t source_row_len = source_state->row_len;
            uint32_t accepted = 0u;

            for (index = 0u; index < source_row_len; index++) {
                const PPRelationalStateRowV1 *source_row =
                    &source_state->rows[index];
                PPRelationalStateQueryV1 query =
                    ppstate_v1_row_query_values(
                        impl, action->condition_table_id,
                        action->condition_operands,
                        prepared ? prepared->condition_operand_ids : NULL,
                        action->condition_operand_len, step, NULL,
                        source_row, source_table->arity, index,
                        accepted, true);
                PPRelationalStateRowV1 row = {{0u}};
                PPRelationalStateInsertV1 inserted;
                if (query == PPRELATIONAL_STATE_QUERY_V1_ABSENT)
                    continue;
                if (query == PPRELATIONAL_STATE_QUERY_V1_INVALID) {
                    ppstate_v1_failure_set(
                        run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
                    ppstate_v1_set_error(
                        error_buf, error_buf_size,
                        "state filtered row-copy condition is invalid");
                    return false;
                }
                inserted = ppstate_v1_row_intern_values(
                    impl, action->operands,
                    prepared ? prepared->operand_ids : NULL,
                    action->operand_len,
                    step, NULL, source_row, source_table->arity,
                    index, accepted, &row);
                if (inserted == PPRELATIONAL_STATE_INSERT_V1_OK) {
                    inserted = ppstate_v1_table_insert(
                        &impl->tables[action->table_id],
                        &impl->plan->tables[action->table_id], &row,
                        action->write_policy);
                }
                if (inserted != PPRELATIONAL_STATE_INSERT_V1_OK) {
                    ppstate_v1_failure_set(
                        run,
                        inserted ==
                                PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                            ? PPRELATIONAL_STATE_FAILURE_V1_REJECTED
                            : PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
                    ppstate_v1_set_error(
                        error_buf, error_buf_size,
                        inserted ==
                                PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                            ? "state filtered row-copy constraint failed"
                            : "state filtered row-copy allocation failed");
                    return false;
                }
                accepted++;
            }
            matched = source_row_len;
        }
        if (UINT32_MAX - run->receipt.input_len < matched) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len += matched;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_INSERT_UNORDERED_PAIRS:
        if (!step) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state pair insertion lacks an occurrence");
            return false;
        }
        {
            PPRelationalStateInsertV1 inserted =
                ppstate_v1_insert_unordered_pairs(
                    impl, action, step, &matched);
            if (inserted != PPRELATIONAL_STATE_INSERT_V1_OK) {
                ppstate_v1_failure_set(
                    run,
                    inserted == PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                        ? PPRELATIONAL_STATE_FAILURE_V1_REJECTED
                        : PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    inserted == PPRELATIONAL_STATE_INSERT_V1_CONSTRAINT
                        ? "state unordered-pair constraint failed"
                        : "state unordered-pair allocation failed");
                return false;
            }
        }
        if (UINT32_MAX - run->receipt.input_len < matched) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len += matched;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_NORMAL:
    case PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_COMPRESSED:
        if (!ppstate_v1_execute_proof(
                run, impl, action, action_index, step, &matched,
                error_buf, error_buf_size))
            return false;
        if (UINT32_MAX - run->receipt.input_len < matched) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len += matched;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_RESOLVE_SOURCE:
        if (!step || !impl->source_resolver_ready) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_UNSUPPORTED);
            ppstate_v1_set_error(
                error_buf, error_buf_size,
                "state source resolution lacks an environment capability");
            return false;
        }
        {
            const PPOccurrenceFoldV1Value *source_path = NULL;
            PPOccurrenceSourceResolutionV1 resolution;
            for (index = 0u; index < step->value_len; index++) {
                if (step->values[index].role_id != action->role_id)
                    continue;
                if (source_path) {
                    ppstate_v1_failure_set(
                        run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
                    ppstate_v1_set_error(
                        error_buf, error_buf_size,
                        "source-resolution role is not singular");
                    return false;
                }
                source_path = &step->values[index];
            }
            if (!source_path) {
                ppstate_v1_failure_set(
                    run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    "source-resolution role is absent");
                return false;
            }
            resolution = impl->source_resolver.resolve(
                impl->source_resolver.context, source_path,
                action->skip_completed_sources,
                action->reject_active_source_cycles,
                &impl->nested_backend, error_buf, error_buf_size);
            if (resolution !=
                PPOCCURRENCE_SOURCE_RESOLUTION_V1_ACCEPTED) {
                PPRelationalStateFailureV1 failure =
                    resolution ==
                            PPOCCURRENCE_SOURCE_RESOLUTION_V1_REJECTED
                        ? PPRELATIONAL_STATE_FAILURE_V1_REJECTED
                        : resolution ==
                                  PPOCCURRENCE_SOURCE_RESOLUTION_V1_RESOURCE
                              ? PPRELATIONAL_STATE_FAILURE_V1_RESOURCE
                              : PPRELATIONAL_STATE_FAILURE_V1_INVALID;
                ppstate_v1_failure_set(run, failure);
                if (error_buf && error_buf_size > 0u &&
                    error_buf[0] == '\0')
                    ppstate_v1_set_error(
                        error_buf, error_buf_size,
                        "source resolution failed");
                return false;
            }
        }
        if (run->receipt.input_len == UINT32_MAX) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "state receipt input count overflow");
            return false;
        }
        run->receipt.input_len++;
        break;
    case PPRELATIONAL_STATE_ACTION_V1_NOOP:
        break;
    default:
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
        return false;
    }
    if (run->receipt.action_len == UINT32_MAX) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "state receipt action count overflow");
        return false;
    }
    run->receipt.action_len++;
    return true;
}

static bool ppstate_v1_backend_apply(void *context,
                                     const PPOccurrenceFoldV1Step *step,
                                     char *error_buf,
                                     size_t error_buf_size) {
    PPRelationalStateProgramV1Run *run = context;
    PPRelationalStateRunImplV1 *impl;
    const PPRelationalStateOperationV1 *program;
    uint32_t index;

    if (!run || !run->active || !step ||
        !(impl = run->implementation) ||
        step->operation_id >= impl->plan->operation_len) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "invalid relational state occurrence");
        return false;
    }
    program = &impl->plan->operations[step->operation_id];
    if (program->action_len == 0u) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_UNSUPPORTED);
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "occurrence has no generated state program");
        return false;
    }
    if (!ppstate_v1_prepare_step_values(impl, step)) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "state occurrence preparation failed");
        return false;
    }
    if (!ppstate_v1_transaction_clear(impl)) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "state transaction reset failed");
        return false;
    }
    for (index = 0u; index < program->action_len; index++) {
        uint32_t action_index = program->action_begin + index;
        if (!ppstate_v1_execute_action(
                run, impl,
                &impl->plan->actions[action_index],
                &impl->prepared_actions[action_index], index, step,
                error_buf, error_buf_size))
            return false;
    }
    if (!ppstate_v1_transaction_clear(impl)) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "state transaction release failed");
        return false;
    }
    if (run->receipt.step_len == UINT32_MAX) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_RESOURCE);
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "state receipt step count overflow");
        return false;
    }
    run->receipt.step_len++;
    return true;
}

static bool ppstate_v1_state_digest(
    PPRelationalStateRunImplV1 *impl,
    const char *source_digest, char digest[65],
    uint32_t *row_len_out) {
    static const uint8_t domain[] = "RelationalStateExecutionV1";
    CettaNativeSha256 sha;
    uint32_t table_index;
    uint32_t total = 0u;

    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    ppstate_v1_sha_text(&sha, impl->plan->plan_digest);
    ppstate_v1_sha_text(&sha, source_digest);
    ppstate_v1_sha_u32(&sha, impl->plan->table_len);
    for (table_index = 0u; table_index < impl->plan->table_len;
         table_index++) {
        const PPRelationalStateTableV1 *table =
            &impl->plan->tables[table_index];
        const PPRelationalStateTableStateV1 *state =
            &impl->tables[table_index];
        uint32_t logical_row_len = state->compact_eligible
                                       ? state->compact_row_len
                                       : state->row_len;
        uint32_t row_index;
        ppstate_v1_sha_text(&sha, table->name);
        ppstate_v1_sha_u32(&sha, logical_row_len);
        if (UINT32_MAX - total < logical_row_len)
            return false;
        total += logical_row_len;
        for (row_index = 0u; row_index < logical_row_len; row_index++) {
            PPRelationalStateRowV1 compact_row;
            const PPRelationalStateRowV1 *row =
                state->compact_eligible
                    ? (ppstate_v1_compact_table_row(
                           impl, table_index, row_index, &compact_row)
                           ? &compact_row
                           : NULL)
                    : &state->rows[row_index];
            uint32_t column;
            if (!row)
                return false;
            for (column = 0u; column < table->arity; column++) {
                uint32_t value_id = row->values[column];
                if (value_id >= impl->value_len)
                    return false;
                ppstate_v1_sha_bytes(
                    &sha, impl->values[value_id].bytes,
                    impl->values[value_id].len);
            }
        }
    }
    cetta_native_sha256_finish_hex(&sha, digest);
    *row_len_out = total;
    return true;
}

static bool ppstate_v1_state_row_count(
    const PPRelationalStateRunImplV1 *impl,
    uint32_t *row_len_out) {
    uint32_t table_index;
    uint32_t total = 0u;

    if (!impl || !row_len_out)
        return false;
    for (table_index = 0u; table_index < impl->plan->table_len;
         table_index++) {
        uint32_t row_len = impl->tables[table_index].compact_eligible
                               ? impl->tables[table_index].compact_row_len
                               : impl->tables[table_index].row_len;
        if (UINT32_MAX - total < row_len)
            return false;
        total += row_len;
    }
    *row_len_out = total;
    return true;
}

static bool ppstate_v1_backend_commit(void *context, char *error_buf,
                                      size_t error_buf_size) {
    PPRelationalStateProgramV1Run *run = context;
    PPRelationalStateRunImplV1 *impl;
    uint32_t index;

    if (!run || !run->active || run->aborted ||
        !(impl = run->implementation)) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "relational state run cannot commit");
        return false;
    }
    for (index = 0u; index < impl->plan->final_action_len; index++) {
        if (!ppstate_v1_execute_action(
                run, impl, &impl->plan->final_actions[index],
                &impl->prepared_final_actions[index], UINT32_MAX, NULL,
                error_buf, error_buf_size))
            return false;
    }
    if (impl->observation ==
            PPRELATIONAL_STATE_OBSERVATION_V1_EXACT_RECEIPT) {
        if (impl->source_resolver_ready &&
            !impl->source_resolver.digest(
                impl->source_resolver.context,
                run->receipt.source_digest, error_buf,
                error_buf_size)) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            if (error_buf && error_buf_size > 0u && error_buf[0] == '\0')
                ppstate_v1_set_error(
                    error_buf, error_buf_size,
                    "source-resolution digest failed");
            return false;
        }
        if (!ppstate_v1_state_digest(
                impl, run->receipt.source_digest,
                run->receipt.state_digest, &run->receipt.row_len)) {
            ppstate_v1_failure_set(
                run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
            ppstate_v1_set_error(error_buf, error_buf_size,
                                 "relational state digest failed");
            return false;
        }
    } else if (!ppstate_v1_state_row_count(
                   impl, &run->receipt.row_len)) {
        ppstate_v1_failure_set(
            run, PPRELATIONAL_STATE_FAILURE_V1_INVALID);
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "relational state row count failed");
        return false;
    }
    run->receipt.committed = true;
    run->active = false;
    run->committed = true;
    return true;
}

static void ppstate_v1_backend_abort(void *context) {
    PPRelationalStateProgramV1Run *run = context;
    if (!run || !run->active)
        return;
    run->active = false;
    run->aborted = true;
}

static bool ppstate_v1_run_init(
    PPRelationalStateProgramV1Run *run,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    const PPOccurrenceSourceResolverV1 *source_resolver,
    const PPOccurrenceFoldV1Backend *nested_backend,
    const PPRelationalStateProofV1Backend *proof_backend,
    const PPRelationalStackProofV1CacheAdmission *cache_admissions,
    uint32_t cache_admission_len,
    PPRelationalStateObservationV1 observation,
    char *error_buf,
    size_t error_buf_size) {
    PPRelationalStateRunImplV1 *impl;
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!run || !occurrence_plan || !plan ||
        ((source_resolver != NULL) != (nested_backend != NULL)) ||
        (source_resolver &&
         (!source_resolver->context || !source_resolver->resolve ||
          !source_resolver->digest || !nested_backend->context ||
          !nested_backend->apply || !nested_backend->commit ||
          !nested_backend->abort)) ||
        (proof_backend && !proof_backend->execute) ||
        (proof_backend && cache_admission_len != 0u) ||
        ((cache_admission_len != 0u) !=
         (cache_admissions != NULL)) ||
        (cache_admission_len != 0u &&
         cache_admission_len != plan->proof_machine_len) ||
        observation >
            PPRELATIONAL_STATE_OBSERVATION_V1_EXACT_RECEIPT ||
        !pprelational_state_program_v1_plan_validate(
            occurrence_plan, plan, error_buf, error_buf_size))
        return false;
    memset(run, 0, sizeof(*run));
    impl = calloc(1u, sizeof(*impl));
    if (!impl)
        return false;
    if (!ppstate_v1_fresh_store_identity(&impl->store_identity)) {
        free(impl);
        return false;
    }
    impl->tables = calloc(plan->table_len, sizeof(*impl->tables));
    if (!impl->tables) {
        free(impl);
        return false;
    }
    impl->occurrence_plan = occurrence_plan;
    impl->plan = plan;
    impl->observation = observation;
    if (cache_admission_len != 0u)
        ppstate_v1_compact_layouts_init(impl);
    if (source_resolver) {
        impl->source_resolver = *source_resolver;
        impl->nested_backend = *nested_backend;
        impl->source_resolver_ready = true;
    }
    if (proof_backend) {
        impl->proof_backend = *proof_backend;
        impl->proof_backend_ready = true;
    }
    run->implementation = impl;
    if (!ppstate_v1_prepare_actions(impl)) {
        ppstate_v1_set_error(error_buf, error_buf_size,
                             "state action preparation failed");
        pprelational_state_program_v1_run_free(run);
        return false;
    }
    if (!impl->proof_backend_ready && plan->proof_machine_len > 0u) {
        impl->proof_machines = calloc(
            plan->proof_machine_len, sizeof(*impl->proof_machines));
        if (!impl->proof_machines) {
            pprelational_state_program_v1_run_free(run);
            return false;
        }
        for (index = 0u; index < plan->proof_machine_len; index++) {
            if (!ppstate_v1_proof_machine_runtime_init(
                    impl, index, error_buf, error_buf_size)) {
                pprelational_state_program_v1_run_free(run);
                return false;
            }
        }
        if (cache_admission_len != 0u) {
            PPRelationalStoreV1 store = ppstate_v1_store(impl);
            impl->proof_frame_caches = calloc(
                plan->proof_machine_len,
                sizeof(*impl->proof_frame_caches));
            if (!impl->proof_frame_caches) {
                pprelational_state_program_v1_run_free(run);
                return false;
            }
            for (index = 0u; index < plan->proof_machine_len; index++) {
                if (!pprelational_stack_proof_v1_cache_init(
                        &impl->proof_frame_caches[index], &store,
                        &impl->proof_machines[index],
                        &cache_admissions[index],
                        error_buf, error_buf_size)) {
                    pprelational_state_program_v1_run_free(run);
                    return false;
                }
            }
            impl->proof_frame_caches_ready = true;
        }
    }
    run->active = true;
    return true;
}

bool pprelational_state_program_v1_run_init_with_proof_backend(
    PPRelationalStateProgramV1Run *run,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    const PPOccurrenceSourceResolverV1 *source_resolver,
    const PPOccurrenceFoldV1Backend *nested_backend,
    const PPRelationalStateProofV1Backend *proof_backend,
    PPRelationalStateObservationV1 observation,
    char *error_buf,
    size_t error_buf_size) {
    return ppstate_v1_run_init(
        run, occurrence_plan, plan, source_resolver, nested_backend,
        proof_backend, NULL, 0u, observation,
        error_buf, error_buf_size);
}

bool pprelational_state_program_v1_run_init_with_frame_cache(
    PPRelationalStateProgramV1Run *run,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    const PPOccurrenceSourceResolverV1 *source_resolver,
    const PPOccurrenceFoldV1Backend *nested_backend,
    const PPRelationalStackProofV1CacheAdmission *admissions,
    uint32_t admission_len,
    PPRelationalStateObservationV1 observation,
    char *error_buf,
    size_t error_buf_size) {
    return ppstate_v1_run_init(
        run, occurrence_plan, plan, source_resolver, nested_backend,
        NULL, admissions, admission_len, observation,
        error_buf, error_buf_size);
}

bool pprelational_state_program_v1_run_init(
    PPRelationalStateProgramV1Run *run,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    const PPOccurrenceSourceResolverV1 *source_resolver,
    const PPOccurrenceFoldV1Backend *nested_backend,
    PPRelationalStateObservationV1 observation,
    char *error_buf,
    size_t error_buf_size) {
    return pprelational_state_program_v1_run_init_with_proof_backend(
        run, occurrence_plan, plan, source_resolver, nested_backend,
        NULL, observation, error_buf, error_buf_size);
}

void pprelational_state_program_v1_run_free(
    PPRelationalStateProgramV1Run *run) {
    PPRelationalStateRunImplV1 *impl;
    uint32_t index;

    if (!run)
        return;
    impl = run->implementation;
    if (impl) {
        if (impl->proof_frame_caches) {
            for (index = 0u;
                 index < impl->plan->proof_machine_len; index++)
                pprelational_stack_proof_v1_cache_free(
                    &impl->proof_frame_caches[index]);
        }
        for (index = 0u; index < impl->value_len; index++)
            free(impl->values[index].bytes);
        for (index = 0u; index < impl->plan->table_len; index++) {
            free(impl->tables[index].rows);
            free(impl->tables[index].buckets);
            free(impl->tables[index].prefix_buckets);
            free(impl->tables[index].prefix_tails);
            free(impl->tables[index].prefix_next);
            free(impl->tables[index].compact_runs);
            free(impl->tables[index].compact_run_buckets);
            free(impl->tables[index].compact_values);
        }
        free(impl->values);
        free(impl->value_buckets);
        free(impl->prepared_step_value_ids);
        free(impl->tables);
        free(impl->prepared_actions);
        free(impl->prepared_final_actions);
        free(impl->proof_machines);
        free(impl->proof_frame_caches);
        free(impl->scope_lengths);
        free(impl);
    }
    memset(run, 0, sizeof(*run));
}

PPOccurrenceFoldV1Backend pprelational_state_program_v1_backend(
    PPRelationalStateProgramV1Run *run) {
    return (PPOccurrenceFoldV1Backend){
        .context = run,
        .apply = ppstate_v1_backend_apply,
        .commit = ppstate_v1_backend_commit,
        .abort = ppstate_v1_backend_abort,
    };
}

bool pprelational_state_program_v1_store(
    PPRelationalStateProgramV1Run *run,
    PPRelationalStoreV1 *store_out) {
    PPRelationalStateRunImplV1 *impl;

    if (!run || !store_out || run->aborted ||
        !(impl = run->implementation))
        return false;
    *store_out = ppstate_v1_store(impl);
    return pprelational_store_v1_valid(store_out);
}

bool pprelational_state_program_v1_frame_cache_stats(
    const PPRelationalStateProgramV1Run *run,
    uint32_t proof_machine_id,
    PPRelationalStackProofV1CacheStats *stats_out) {
    const PPRelationalStateRunImplV1 *impl;

    if (!run || !(impl = run->implementation) || run->aborted ||
        !impl->proof_frame_caches_ready ||
        proof_machine_id >= impl->plan->proof_machine_len)
        return false;
    return pprelational_stack_proof_v1_cache_stats(
        &impl->proof_frame_caches[proof_machine_id], stats_out);
}

typedef struct {
    FILE *output;
    const char *prefix;
    char *error_buf;
    size_t error_buf_size;
    bool failed;
} PPRelationalStateCEmitterV1;

static void ppstate_v1_c_error(PPRelationalStateCEmitterV1 *emitter,
                               const char *format, ...) {
    va_list arguments;

    if (!emitter || emitter->failed)
        return;
    emitter->failed = true;
    if (!emitter->error_buf || emitter->error_buf_size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(emitter->error_buf, emitter->error_buf_size,
                    format, arguments);
    va_end(arguments);
}

static void ppstate_v1_c_write(PPRelationalStateCEmitterV1 *emitter,
                               const char *format, ...) {
    va_list arguments;

    if (!emitter || emitter->failed)
        return;
    va_start(arguments, format);
    if (vfprintf(emitter->output, format, arguments) < 0)
        ppstate_v1_c_error(emitter, "failed to write generated state C");
    va_end(arguments);
}

static bool ppstate_v1_c_identifier_valid(const char *identifier) {
    size_t index;

    if (!identifier || !identifier[0] ||
        !(identifier[0] == '_' ||
          isalpha((unsigned char)identifier[0])))
        return false;
    for (index = 1u; identifier[index]; index++) {
        if (!(identifier[index] == '_' ||
              isalnum((unsigned char)identifier[index])))
            return false;
    }
    return true;
}

static void ppstate_v1_c_bytes(PPRelationalStateCEmitterV1 *emitter,
                               const uint8_t *bytes, uint32_t len) {
    uint32_t index;

    ppstate_v1_c_write(emitter, "\"");
    for (index = 0u; index < len; index++) {
        unsigned char ch = bytes[index];
        if (ch == '\\' || ch == '"')
            ppstate_v1_c_write(emitter, "\\%c", (int)ch);
        else if (ch >= 0x20u && ch <= 0x7eu)
            ppstate_v1_c_write(emitter, "%c", (int)ch);
        else
            ppstate_v1_c_write(emitter, "\\%03o", (unsigned int)ch);
    }
    ppstate_v1_c_write(emitter, "\"");
}

static void ppstate_v1_c_text(PPRelationalStateCEmitterV1 *emitter,
                              const char *text) {
    ppstate_v1_c_bytes(emitter, (const uint8_t *)text,
                       (uint32_t)strlen(text));
}

static void ppstate_v1_c_emit_operand(
    PPRelationalStateCEmitterV1 *emitter, const char *array,
    uint32_t action_index, const char *field, uint32_t operand_index,
    const PPRelationalStateOperandV1 *value) {
    ppstate_v1_c_write(
        emitter,
        "    result.%s[UINT32_C(%u)].%s[UINT32_C(%u)].kind = "
        "(PPRelationalStateOperandKindV1)%u;\n"
        "    result.%s[UINT32_C(%u)].%s[UINT32_C(%u)].role_id = "
        "UINT32_C(%u);\n"
        "    result.%s[UINT32_C(%u)].%s[UINT32_C(%u)].input_index = "
        "UINT32_C(%u);\n",
        array, action_index, field, operand_index,
        (unsigned int)value->kind,
        array, action_index, field, operand_index, value->role_id,
        array, action_index, field, operand_index, value->input_index);
    if (value->kind == PPRELATIONAL_STATE_OPERAND_V1_LITERAL) {
        ppstate_v1_c_write(
            emitter,
            "    result.%s[UINT32_C(%u)].%s[UINT32_C(%u)]."
            "literal = %s_state_bytes_dup((const uint8_t *)",
            array, action_index, field, operand_index, emitter->prefix);
        ppstate_v1_c_bytes(emitter, value->literal, value->literal_len);
        ppstate_v1_c_write(
            emitter, ", UINT32_C(%u));\n"
                     "    if (!result.%s[UINT32_C(%u)].%s["
                     "UINT32_C(%u)].literal) goto fail;\n"
                     "    result.%s[UINT32_C(%u)].%s["
                     "UINT32_C(%u)].literal_len = UINT32_C(%u);\n",
            value->literal_len, array, action_index, field, operand_index,
            array, action_index, field, operand_index, value->literal_len);
    }
}

static void ppstate_v1_c_emit_action(
    PPRelationalStateCEmitterV1 *emitter, const char *array,
    uint32_t index, const PPRelationalStateActionV1 *action) {
    uint32_t operand;

    ppstate_v1_c_write(
        emitter,
        "    result.%s[UINT32_C(%u)] = (PPRelationalStateActionV1){\n"
        "        .kind = (PPRelationalStateActionKindV1)%u,\n"
        "        .operation_id = UINT32_C(%u),\n"
        "        .role_id = UINT32_C(%u),\n"
        "        .table_id = UINT32_C(%u),\n"
        "        .source_table_id = UINT32_C(%u),\n"
        "        .condition_table_id = UINT32_C(%u),\n"
        "        .write_policy = (PPRelationalStateWriteV1)%u,\n"
        "        .required_depth = UINT32_C(%u),\n"
        "        .proof_machine_id = UINT32_C(%u),\n"
        "        .proof_label_role_id = UINT32_C(%u),\n"
        "        .proof_formula_role_id = UINT32_C(%u),\n"
        "        .proof_step_role_id = UINT32_C(%u),\n"
        "        .proof_code_role_id = UINT32_C(%u),\n"
        "        .skip_completed_sources = %s,\n"
        "        .reject_active_source_cycles = %s,\n"
        "        .operand_len = UINT32_C(%u),\n"
        "        .condition_operand_len = UINT32_C(%u),\n"
        "    };\n",
        array, index, (unsigned int)action->kind,
        action->operation_id, action->role_id, action->table_id,
        action->source_table_id,
        action->condition_table_id,
        (unsigned int)action->write_policy, action->required_depth,
        action->proof_machine_id, action->proof_label_role_id,
        action->proof_formula_role_id, action->proof_step_role_id,
        action->proof_code_role_id,
        action->skip_completed_sources ? "true" : "false",
        action->reject_active_source_cycles ? "true" : "false",
        action->operand_len, action->condition_operand_len);
    for (operand = 0u; operand < action->operand_len; operand++) {
        const PPRelationalStateOperandV1 *value =
            &action->operands[operand];
        ppstate_v1_c_emit_operand(
            emitter, array, index, "operands", operand, value);
    }
    for (operand = 0u; operand < action->condition_operand_len;
         operand++) {
        const PPRelationalStateOperandV1 *value =
            &action->condition_operands[operand];
        ppstate_v1_c_emit_operand(
            emitter, array, index, "condition_operands", operand, value);
    }
}

static void ppstate_v1_c_emit_machine_literal(
    PPRelationalStateCEmitterV1 *emitter, uint32_t machine_index,
    const char *field, const PPRelationalStateLiteralV1 *literal) {
    ppstate_v1_c_write(
        emitter,
        "    result.proof_machines[UINT32_C(%u)].%s.bytes = "
        "%s_state_bytes_dup((const uint8_t *)",
        machine_index, field, emitter->prefix);
    ppstate_v1_c_bytes(emitter, literal->bytes, literal->len);
    ppstate_v1_c_write(
        emitter,
        ", UINT32_C(%u));\n"
        "    if (!result.proof_machines[UINT32_C(%u)].%s.bytes) "
        "goto fail;\n"
        "    result.proof_machines[UINT32_C(%u)].%s.len = "
        "UINT32_C(%u);\n",
        literal->len, machine_index, field, machine_index, field,
        literal->len);
}

static void ppstate_v1_c_emit_proof_machine(
    PPRelationalStateCEmitterV1 *emitter, uint32_t index,
    const PPRelationalStateProofMachineV1 *machine) {
    ppstate_v1_c_write(
        emitter,
        "    result.proof_machines[UINT32_C(%u)].name = "
        "%s_state_text_dup(",
        index, emitter->prefix);
    ppstate_v1_c_text(emitter, machine->name);
    ppstate_v1_c_write(
        emitter,
        ");\n"
        "    if (!result.proof_machines[UINT32_C(%u)].name) goto fail;\n"
        "    result.proof_machines[UINT32_C(%u)].label_kind_table = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].formula_table = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].binder_variable_table = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].mandatory_variable_table = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].assertion_hypothesis_table = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].assertion_disjoint_table = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].active_hypothesis_table = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].active_disjoint_table = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].symbol_kind_table = UINT32_C(%u);\n",
        index,
        index, machine->label_kind_table,
        index, machine->formula_table,
        index, machine->binder_variable_table,
        index, machine->mandatory_variable_table,
        index, machine->assertion_hypothesis_table,
        index, machine->assertion_disjoint_table,
        index, machine->active_hypothesis_table,
        index, machine->active_disjoint_table,
        index, machine->symbol_kind_table);
    ppstate_v1_c_emit_machine_literal(
        emitter, index, "binder_hypothesis_kind",
        &machine->binder_hypothesis_kind);
    ppstate_v1_c_emit_machine_literal(
        emitter, index, "matching_hypothesis_kind",
        &machine->matching_hypothesis_kind);
    ppstate_v1_c_emit_machine_literal(
        emitter, index, "rule_kind_first", &machine->rule_kind_first);
    ppstate_v1_c_emit_machine_literal(
        emitter, index, "rule_kind_second", &machine->rule_kind_second);
    ppstate_v1_c_emit_machine_literal(
        emitter, index, "variable_symbol_kind",
        &machine->variable_symbol_kind);
    ppstate_v1_c_emit_machine_literal(
        emitter, index, "unknown_token", &machine->unknown_token);
    ppstate_v1_c_write(
        emitter,
        "    result.proof_machines[UINT32_C(%u)].terminal_low = UINT8_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].terminal_high = UINT8_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].continuation_low = UINT8_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].continuation_high = UINT8_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].save_byte = UINT8_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].unknown_byte = UINT8_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].terminal_radix = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].terminal_digit_bias = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].continuation_radix = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].continuation_digit_bias = UINT32_C(%u);\n"
        "    result.proof_machines[UINT32_C(%u)].unknown_policy = "
        "(PPRelationalStackProofV1UnknownPolicy)%u;\n"
        "    result.proof_machines[UINT32_C(%u)].save_placement = "
        "(CettaGsltIndexedSavePlacementV1)%u;\n"
        "    result.proof_machines[UINT32_C(%u)].header_hypothesis_policy = "
        "(CettaGsltHeaderHypothesisPolicyV1)%u;\n",
        index, (unsigned int)machine->terminal_low,
        index, (unsigned int)machine->terminal_high,
        index, (unsigned int)machine->continuation_low,
        index, (unsigned int)machine->continuation_high,
        index, (unsigned int)machine->save_byte,
        index, (unsigned int)machine->unknown_byte,
        index, machine->terminal_radix,
        index, machine->terminal_digit_bias,
        index, machine->continuation_radix,
        index, machine->continuation_digit_bias,
        index, (unsigned int)machine->unknown_policy,
        index, (unsigned int)machine->save_placement,
        index, (unsigned int)machine->header_hypothesis_policy);
}

bool pprelational_state_program_v1_emit_c(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size) {
    PPRelationalStateCEmitterV1 emitter;
    char validation_error[256] = {0};
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    memset(&emitter, 0, sizeof(emitter));
    emitter.output = output;
    emitter.prefix = identifier_prefix;
    emitter.error_buf = error_buf;
    emitter.error_buf_size = error_buf_size;
    if (!occurrence_plan || !plan || !output ||
        !ppstate_v1_c_identifier_valid(identifier_prefix) ||
        !pprelational_state_program_v1_plan_validate(
            occurrence_plan, plan, validation_error,
            sizeof(validation_error))) {
        ppstate_v1_c_error(
            &emitter, "cannot emit invalid relational state plan: %s",
            validation_error[0] ? validation_error : "bad arguments");
        return false;
    }
    ppstate_v1_c_write(
        &emitter,
        "\n/* Generated from RelationalStateProgramV1. */\n"
        "#include \"relational_state_program_v1.h\"\n\n"
        "static char *%s_state_text_dup(const char *text) {\n"
        "    size_t len; char *copy;\n"
        "    if (!text) return NULL;\n"
        "    len = strlen(text); copy = malloc(len + 1u);\n"
        "    if (copy) memcpy(copy, text, len + 1u);\n"
        "    return copy;\n"
        "}\n\n"
        "static uint8_t *%s_state_bytes_dup(const uint8_t *bytes, "
        "uint32_t len) {\n"
        "    uint8_t *copy;\n"
        "    if (!bytes || len == 0u) return NULL;\n"
        "    copy = malloc(len); if (copy) memcpy(copy, bytes, len);\n"
        "    return copy;\n"
        "}\n\n"
        "const char *%s_state_program_plan_digest(void) { return ",
        identifier_prefix, identifier_prefix, identifier_prefix);
    ppstate_v1_c_text(&emitter, plan->plan_digest);
    ppstate_v1_c_write(
        &emitter,
        "; }\n\n"
        "bool %s_state_program_plan_init(\n"
        "    const PPOccurrenceFoldV1Plan *occurrence_plan,\n"
        "    PPRelationalStateProgramV1Plan *out,\n"
        "    char *error_buf, size_t error_buf_size) {\n"
        "    PPRelationalStateProgramV1Plan result;\n"
        "    if (error_buf && error_buf_size > 0u) error_buf[0] = '\\0';\n"
        "    if (!occurrence_plan || !out) return false;\n"
        "    pprelational_state_program_v1_plan_init(&result);\n"
        "    result.table_len = UINT32_C(%u);\n"
        "    result.proof_machine_len = UINT32_C(%u);\n"
        "    result.action_len = UINT32_C(%u);\n"
        "    result.final_action_len = UINT32_C(%u);\n"
        "    result.operation_len = UINT32_C(%u);\n"
        "    result.tables = calloc(result.table_len, "
        "sizeof(*result.tables));\n"
        "    result.operations = calloc(result.operation_len, "
        "sizeof(*result.operations));\n"
        "    if (!result.tables || !result.operations) goto fail;\n",
        identifier_prefix, plan->table_len, plan->proof_machine_len,
        plan->action_len,
        plan->final_action_len, plan->operation_len);
    if (plan->proof_machine_len > 0u)
        ppstate_v1_c_write(
            &emitter,
            "    result.proof_machines = calloc(result.proof_machine_len, "
            "sizeof(*result.proof_machines));\n"
            "    if (!result.proof_machines) goto fail;\n");
    if (plan->action_len > 0u)
        ppstate_v1_c_write(
            &emitter,
            "    result.actions = calloc(result.action_len, "
            "sizeof(*result.actions));\n"
            "    if (!result.actions) goto fail;\n");
    if (plan->final_action_len > 0u)
        ppstate_v1_c_write(
            &emitter,
            "    result.final_actions = calloc(result.final_action_len, "
            "sizeof(*result.final_actions));\n"
            "    if (!result.final_actions) goto fail;\n");
    for (index = 0u; index < plan->table_len; index++) {
        const PPRelationalStateTableV1 *table = &plan->tables[index];
        ppstate_v1_c_write(
            &emitter,
            "    result.tables[UINT32_C(%u)].name = "
            "%s_state_text_dup(",
            index, identifier_prefix);
        ppstate_v1_c_text(&emitter, table->name);
        ppstate_v1_c_write(
            &emitter,
            ");\n"
            "    if (!result.tables[UINT32_C(%u)].name) goto fail;\n"
            "    result.tables[UINT32_C(%u)].arity = UINT32_C(%u);\n"
            "    result.tables[UINT32_C(%u)].key_arity = UINT32_C(%u);\n"
            "    result.tables[UINT32_C(%u)].lifetime = "
            "(PPRelationalStateLifetimeV1)%u;\n",
            index, index, table->arity, index, table->key_arity,
            index, (unsigned int)table->lifetime);
    }
    for (index = 0u; index < plan->proof_machine_len; index++)
        ppstate_v1_c_emit_proof_machine(
            &emitter, index, &plan->proof_machines[index]);
    for (index = 0u; index < plan->action_len; index++)
        ppstate_v1_c_emit_action(
            &emitter, "actions", index, &plan->actions[index]);
    for (index = 0u; index < plan->final_action_len; index++)
        ppstate_v1_c_emit_action(
            &emitter, "final_actions", index,
            &plan->final_actions[index]);
    for (index = 0u; index < plan->operation_len; index++) {
        ppstate_v1_c_write(
            &emitter,
            "    result.operations[UINT32_C(%u)] = "
            "(PPRelationalStateOperationV1){ UINT32_C(%u), "
            "UINT32_C(%u) };\n",
            index, plan->operations[index].action_begin,
            plan->operations[index].action_len);
    }
    ppstate_v1_c_write(&emitter, "    memcpy(result.occurrence_fold_plan_digest, ");
    ppstate_v1_c_text(&emitter, plan->occurrence_fold_plan_digest);
    ppstate_v1_c_write(&emitter, ", 65u);\n    memcpy(result.compiler_answer_digest, ");
    ppstate_v1_c_text(&emitter, plan->compiler_answer_digest);
    ppstate_v1_c_write(&emitter, ", 65u);\n    memcpy(result.plan_digest, ");
    ppstate_v1_c_text(&emitter, plan->plan_digest);
    ppstate_v1_c_write(
        &emitter,
        ", 65u);\n"
        "    if (!pprelational_state_program_v1_plan_validate(\n"
        "            occurrence_plan, &result, error_buf, "
        "error_buf_size)) goto fail;\n"
        "    pprelational_state_program_v1_plan_free(out);\n"
        "    *out = result; return true;\n"
        "fail:\n"
        "    pprelational_state_program_v1_plan_free(&result);\n"
        "    return false;\n"
        "}\n");
    if (!emitter.failed && ferror(output))
        ppstate_v1_c_error(&emitter, "generated state C stream failed");
    return !emitter.failed;
}
