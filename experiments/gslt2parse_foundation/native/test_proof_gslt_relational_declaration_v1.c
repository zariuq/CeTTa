#include "proof_gslt_relational_machine_v1.h"

#include "atom.h"
#include "relational_value_list_v1.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *bytes;
    uint32_t len;
} FakeValueV1;

typedef struct {
    uint32_t arity;
    uint32_t key_arity;
    uint32_t *rows;
    uint32_t row_len;
    uint32_t row_cap;
} FakeTableV1;

enum { TEST_TABLE_LEN = PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN };

typedef struct {
    FakeValueV1 *values;
    uint32_t value_len;
    uint32_t value_cap;
    FakeTableV1 tables[TEST_TABLE_LEN];
    bool reject_immutable_snapshot;
} FakeStoreV1;

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

static bool grow(void **items, uint32_t *capacity,
                 uint32_t required, size_t item_size) {
    uint32_t next_capacity;
    void *next;

    if (required <= *capacity)
        return true;
    next_capacity = *capacity ? *capacity * 2u : 16u;
    if (next_capacity < required)
        next_capacity = required;
    if ((size_t)next_capacity > SIZE_MAX / item_size)
        return false;
    next = realloc(*items, (size_t)next_capacity * item_size);
    if (!next)
        return false;
    *items = next;
    *capacity = next_capacity;
    return true;
}

static void fake_store_free(FakeStoreV1 *store) {
    uint32_t index;

    if (!store)
        return;
    for (index = 0u; index < store->value_len; index++)
        free(store->values[index].bytes);
    for (index = 0u; index < TEST_TABLE_LEN; index++)
        free(store->tables[index].rows);
    free(store->values);
    memset(store, 0, sizeof(*store));
}

static bool fake_value_intern(void *context, const uint8_t *bytes,
                              uint32_t len, uint32_t *value_out) {
    FakeStoreV1 *store = context;
    uint8_t *copied;
    uint32_t index;

    if (!store || !bytes || len == 0u || !value_out)
        return false;
    for (index = 0u; index < store->value_len; index++) {
        if (store->values[index].len == len &&
            memcmp(store->values[index].bytes, bytes, len) == 0) {
            *value_out = index;
            return true;
        }
    }
    if (!grow((void **)&store->values, &store->value_cap,
              store->value_len + 1u, sizeof(*store->values)))
        return false;
    copied = malloc(len);
    if (!copied)
        return false;
    memcpy(copied, bytes, len);
    store->values[store->value_len] = (FakeValueV1){copied, len};
    *value_out = store->value_len++;
    return true;
}

static bool fake_value_bytes(void *context, uint32_t value,
                             const uint8_t **bytes_out,
                             uint32_t *len_out) {
    FakeStoreV1 *store = context;
    if (!store || value >= store->value_len || !bytes_out || !len_out)
        return false;
    *bytes_out = store->values[value].bytes;
    *len_out = store->values[value].len;
    return true;
}

static bool fake_table_shape(void *context, uint32_t table_id,
                             uint32_t *arity_out,
                             uint32_t *key_arity_out,
                             uint32_t *row_len_out) {
    FakeStoreV1 *store = context;
    FakeTableV1 *table;
    if (!store || table_id >= TEST_TABLE_LEN ||
        !arity_out || !key_arity_out || !row_len_out)
        return false;
    table = &store->tables[table_id];
    *arity_out = table->arity;
    *key_arity_out = table->key_arity;
    *row_len_out = table->row_len;
    return true;
}

static bool fake_table_row(void *context, uint32_t table_id,
                           uint32_t row_index, uint32_t *values_out,
                           uint32_t value_capacity) {
    FakeStoreV1 *store = context;
    FakeTableV1 *table;
    if (!store || table_id >= TEST_TABLE_LEN ||
        !values_out)
        return false;
    table = &store->tables[table_id];
    if (row_index >= table->row_len || value_capacity < table->arity)
        return false;
    memcpy(values_out, table->rows + (size_t)row_index * table->arity,
           (size_t)table->arity * sizeof(*values_out));
    return true;
}

static bool fake_table_find(void *context, uint32_t table_id,
                            const uint32_t *key, uint32_t key_len,
                            uint32_t *values_out,
                            uint32_t value_capacity) {
    FakeStoreV1 *store = context;
    FakeTableV1 *table;
    uint32_t index;

    if (!store || table_id >= TEST_TABLE_LEN ||
        !key || !values_out)
        return false;
    table = &store->tables[table_id];
    if (key_len != table->key_arity || value_capacity < table->arity)
        return false;
    for (index = 0u; index < table->row_len; index++) {
        uint32_t *row = table->rows + (size_t)index * table->arity;
        if (memcmp(row, key, (size_t)key_len * sizeof(*key)) == 0) {
            memcpy(values_out, row,
                   (size_t)table->arity * sizeof(*values_out));
            return true;
        }
    }
    return false;
}

static bool fake_table_prefix_next(
    void *context, uint32_t table_id,
    const uint32_t *prefix, uint32_t prefix_len,
    uint32_t column_mask,
    uint64_t *cursor_io, uint32_t *values_out,
    uint32_t value_capacity, bool *found_out) {
    FakeStoreV1 *store = context;
    FakeTableV1 *table;
    uint32_t index;

    if (!store || table_id >= TEST_TABLE_LEN ||
        !prefix || prefix_len == 0u || !cursor_io || !values_out ||
        !found_out)
        return false;
    table = &store->tables[table_id];
    if (prefix_len > table->key_arity || value_capacity < table->arity ||
        column_mask == 0u || table->arity > 32u ||
        (table->arity < 32u && (column_mask >> table->arity) != 0u))
        return false;
    if (*cursor_io == UINT64_MAX) {
        index = 0u;
    } else {
        if (*cursor_io > UINT32_MAX)
            return false;
        index = (uint32_t)*cursor_io;
    }
    *found_out = false;
    while (index < table->row_len) {
        uint32_t *row = table->rows + (size_t)index * table->arity;
        index++;
        if (memcmp(row, prefix,
                   (size_t)prefix_len * sizeof(*prefix)) == 0) {
            uint32_t column;
            memset(values_out, 0,
                   (size_t)table->arity * sizeof(*values_out));
            for (column = 0u; column < table->arity; column++) {
                if ((column_mask & (UINT32_C(1) << column)) != 0u)
                    values_out[column] = row[column];
            }
            *cursor_io = index;
            *found_out = true;
            return true;
        }
    }
    *cursor_io = table->row_len;
    return true;
}

static bool fake_table_immutable_prefix(
    void *context, uint32_t table_id, uint32_t *row_len_out) {
    FakeStoreV1 *store = context;

    if (!store || store->reject_immutable_snapshot || !row_len_out ||
        table_id >= TEST_TABLE_LEN)
        return false;
    *row_len_out = store->tables[table_id].row_len;
    return true;
}

static PPRelationalStoreV1 fake_store_interface(FakeStoreV1 *store) {
    return (PPRelationalStoreV1){
        .context = store,
        .identity = UINT64_C(1),
        .table_immutable_prefix = fake_table_immutable_prefix,
        .table_shape = fake_table_shape,
        .table_row = fake_table_row,
        .table_find = fake_table_find,
        .table_prefix_next = fake_table_prefix_next,
        .value_intern = fake_value_intern,
        .value_bytes = fake_value_bytes,
    };
}

static bool fake_add_row(FakeStoreV1 *store, uint32_t table_id,
                         const uint32_t *values,
                         uint32_t *row_index_out) {
    FakeTableV1 *table;
    uint32_t index;

    if (!store || table_id >= TEST_TABLE_LEN ||
        !values)
        return false;
    table = &store->tables[table_id];
    if (table->row_len == UINT32_MAX ||
        !grow((void **)&table->rows, &table->row_cap,
              (table->row_len + 1u) * table->arity,
              sizeof(*table->rows)))
        return false;
    index = table->row_len++;
    memcpy(table->rows + (size_t)index * table->arity, values,
           (size_t)table->arity * sizeof(*values));
    if (row_index_out)
        *row_index_out = index;
    return true;
}

static bool fake_text(FakeStoreV1 *store, const char *text,
                      uint32_t *value_out) {
    return fake_value_intern(
        store, (const uint8_t *)text, (uint32_t)strlen(text), value_out);
}

static bool fake_formula(FakeStoreV1 *store,
                         const uint32_t *items, uint32_t item_len,
                         uint32_t *formula_out) {
    PPRelationalValueV1Slice *slices;
    uint8_t *encoded = NULL;
    uint32_t encoded_len = 0u;
    uint32_t index;
    bool result = false;

    if (!store || !items || item_len == 0u || !formula_out ||
        (size_t)item_len > SIZE_MAX / sizeof(*slices))
        return false;
    slices = calloc(item_len, sizeof(*slices));
    if (!slices)
        return false;
    for (index = 0u; index < item_len; index++) {
        if (!fake_value_bytes(
                store, items[index], &slices[index].bytes,
                &slices[index].len))
            goto done;
    }
    if (pprelational_value_list_v1_encode_items(
            slices, item_len, &encoded, &encoded_len) &&
        fake_value_intern(store, encoded, encoded_len, formula_out))
        result = true;
done:
    free(encoded);
    free(slices);
    return result;
}

static bool add_pair(FakeStoreV1 *store, uint32_t table,
                     uint32_t first, uint32_t second,
                     uint32_t *row_index_out) {
    uint32_t row[2] = {first, second};
    return fake_add_row(store, table, row, row_index_out);
}

static bool add_triple(FakeStoreV1 *store, uint32_t table,
                       uint32_t first, uint32_t second,
                       uint32_t third, uint32_t *row_index_out) {
    uint32_t row[3] = {first, second, third};
    return fake_add_row(store, table, row, row_index_out);
}

static bool setup_machine_rows(
    FakeStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *bridge,
    PPRelationalStateProofMachineV1 *machine,
    uint32_t rule,
    uint32_t claim,
    uint32_t *claim_bind_c_row_out) {
    uint32_t typecode;
    uint32_t literal;
    uint32_t variable_a;
    uint32_t variable_c;
    uint32_t bind_a;
    uint32_t bind_c;
    uint32_t bind_z;
    uint32_t actual_rule;
    uint32_t rule_kind;
    uint32_t floating_kind;
    uint32_t formula_bind_a;
    uint32_t formula_bind_c;
    uint32_t formula_actual_rule;
    uint32_t position_10;
    uint32_t position_20;
    uint32_t position_30;
    uint32_t items[3];

    memset(machine, 0, sizeof(*machine));
    machine->name = "CanaryProofMachineV1";
    machine->label_kind_table = bridge->resolved_table_ids[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND];
    machine->formula_table = bridge->resolved_table_ids[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA];
    machine->binder_variable_table = bridge->resolved_table_ids[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_FLOATING_VARIABLE];
    machine->mandatory_variable_table = bridge->resolved_table_ids[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE];
    machine->assertion_hypothesis_table = bridge->resolved_table_ids[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS];
    machine->assertion_disjoint_table = bridge->resolved_table_ids[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_DISJOINT];
    machine->active_disjoint_table = bridge->resolved_table_ids[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_ACTIVE_APARTNESS];
    machine->symbol_kind_table = bridge->resolved_table_ids[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND];
    machine->binder_hypothesis_kind = (PPRelationalStateLiteralV1){
        .bytes = (uint8_t *)bridge->selectors[
            PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING].bytes,
        .len = bridge->selectors[
            PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING].len,
    };
    machine->matching_hypothesis_kind = (PPRelationalStateLiteralV1){
        .bytes = (uint8_t *)bridge->selectors[
            PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL].bytes,
        .len = bridge->selectors[
            PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL].len,
    };
    machine->rule_kind_first = (PPRelationalStateLiteralV1){
        .bytes = (uint8_t *)"CanaryRuleFirstV1",
        .len = (uint32_t)(sizeof("CanaryRuleFirstV1") - 1u),
    };
    machine->rule_kind_second = (PPRelationalStateLiteralV1){
        .bytes = (uint8_t *)"CanaryRuleSecondV1",
        .len = (uint32_t)(sizeof("CanaryRuleSecondV1") - 1u),
    };
    machine->variable_symbol_kind = (PPRelationalStateLiteralV1){
        .bytes = (uint8_t *)bridge->selectors[
            PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE].bytes,
        .len = bridge->selectors[
            PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE].len,
    };
    machine->unknown_token = (PPRelationalStateLiteralV1){
        .bytes = (uint8_t *)"?",
        .len = 1u,
    };
    machine->terminal_low = (uint8_t)'A';
    machine->terminal_high = (uint8_t)'T';
    machine->continuation_low = (uint8_t)'U';
    machine->continuation_high = (uint8_t)'Y';
    machine->save_byte = (uint8_t)'Z';
    machine->unknown_byte = (uint8_t)'?';
    machine->terminal_radix = 20u;
    machine->terminal_digit_bias = 0u;
    machine->continuation_radix = 5u;
    machine->continuation_digit_bias = 1u;
    machine->unknown_policy =
        PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM;
    machine->save_placement =
        CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1;
    machine->header_hypothesis_policy =
        CETTA_GSLT_HEADER_HYPOTHESIS_NONMANDATORY_ONLY_V1;

    if (!fake_text(store, "CanaryType", &typecode) ||
        !fake_text(store, "CanaryLiteral", &literal) ||
        !fake_text(store, "CanaryA", &variable_a) ||
        !fake_text(store, "CanaryC", &variable_c) ||
        !fake_text(store, "CanaryBindA", &bind_a) ||
        !fake_text(store, "CanaryBindC", &bind_c) ||
        !fake_text(store, "CanaryBindZ", &bind_z) ||
        !fake_text(store, "CanaryActualRule", &actual_rule) ||
        !fake_text(store, "10", &position_10) ||
        !fake_text(store, "20", &position_20) ||
        !fake_text(store, "30", &position_30) ||
        !fake_value_intern(
            store, machine->rule_kind_first.bytes,
            machine->rule_kind_first.len, &rule_kind) ||
        !fake_value_intern(
            store, machine->binder_hypothesis_kind.bytes,
            machine->binder_hypothesis_kind.len, &floating_kind))
        return false;

    items[0] = typecode;
    items[1] = variable_a;
    if (!fake_formula(store, items, 2u, &formula_bind_a))
        return false;
    items[1] = variable_c;
    if (!fake_formula(store, items, 2u, &formula_bind_c))
        return false;
    items[0] = literal;
    items[1] = variable_a;
    items[2] = variable_c;
    if (!fake_formula(store, items, 3u, &formula_actual_rule))
        return false;

    if (!add_pair(store, machine->binder_variable_table,
                  bind_a, variable_a, NULL) ||
        !add_pair(store, machine->binder_variable_table,
                  bind_c, variable_c, NULL) ||
        !add_pair(store, machine->formula_table,
                  bind_a, formula_bind_a, NULL) ||
        !add_pair(store, machine->formula_table,
                  bind_c, formula_bind_c, NULL) ||
        !add_pair(store, machine->formula_table,
                  actual_rule, formula_actual_rule, NULL) ||
        !add_pair(store, machine->label_kind_table,
                  bind_a, floating_kind, NULL) ||
        !add_pair(store, machine->label_kind_table,
                  bind_c, floating_kind, NULL) ||
        !add_pair(store, machine->label_kind_table,
                  rule, rule_kind, NULL) ||
        !add_pair(store, machine->label_kind_table,
                  actual_rule, rule_kind, NULL) ||
        !add_pair(
            store,
            bridge->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE],
            actual_rule, variable_a, NULL) ||
        !add_pair(
            store,
            bridge->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE],
            actual_rule, variable_c, NULL) ||
        !add_pair(
            store,
            bridge->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_ACTIVE_HYPOTHESIS],
            claim, bind_z, NULL) ||
        !add_pair(
            store,
            bridge->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_ACTIVE_HYPOTHESIS],
            claim, bind_a, NULL) ||
        !add_pair(
            store,
            bridge->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_ACTIVE_HYPOTHESIS],
            claim, bind_c, claim_bind_c_row_out) ||
        !add_triple(
            store,
            bridge->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
            actual_rule, position_10, bind_a, NULL) ||
        !add_triple(
            store,
            bridge->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
            actual_rule, position_20, bind_c, NULL) ||
        !add_triple(
            store,
            bridge->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
            claim, position_10, bind_z, NULL) ||
        !add_triple(
            store,
            bridge->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
            claim, position_20, bind_a, NULL) ||
        !add_triple(
            store,
            bridge->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
            claim, position_30, bind_c, NULL))
        return false;
    return true;
}

static bool setup_store(
    FakeStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *bridge,
    uint32_t *rule_out, uint32_t *claim_out,
    uint32_t actual_formula_values[3],
    uint32_t *active_apartness_row_out,
    uint32_t *formal_x_kind_row_out,
    uint32_t *ordered_y_row_out,
    uint32_t *stored_variable_out,
    uint32_t *dummy_variable_out) {
    uint32_t selectors[PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN];
    uint32_t tc, literal, x, y, z, a, c;
    uint32_t bind_x, bind_y, bind_z, essential;
    uint32_t rule, claim;
    uint32_t formula_bind_x, formula_bind_y, formula_bind_z;
    uint32_t formula_essential, formula_rule;
    uint32_t position_10, position_15, position_20, position_30;
    uint32_t items[4];
    uint32_t index;

    static const uint32_t arities[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN] = {
            2u, 2u, 2u, 2u, 3u, 2u, 3u, 2u, 2u,
        };
    static const uint32_t key_arities[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN] = {
            1u, 1u, 1u, 2u, 2u, 2u, 3u, 2u, 1u,
        };

    for (index = 0u;
         index < PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN; index++) {
        uint32_t table = bridge->resolved_table_ids[index];
        store->tables[table].arity = arities[index];
        store->tables[table].key_arity = key_arities[index];
    }
    for (index = 0u;
         index < PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN; index++) {
        if (!fake_value_intern(
                store, bridge->selectors[index].bytes,
                bridge->selectors[index].len, &selectors[index]))
            return false;
    }
    if (!fake_text(store, "CanaryType", &tc) ||
        !fake_text(store, "CanaryLiteral", &literal) ||
        !fake_text(store, "CanaryX", &x) ||
        !fake_text(store, "CanaryY", &y) ||
        !fake_text(store, "CanaryZ", &z) ||
        !fake_text(store, "CanaryA", &a) ||
        !fake_text(store, "CanaryC", &c) ||
        !fake_text(store, "CanaryBindX", &bind_x) ||
        !fake_text(store, "CanaryBindY", &bind_y) ||
        !fake_text(store, "CanaryBindZ", &bind_z) ||
        !fake_text(store, "CanaryEssential", &essential) ||
        !fake_text(store, "CanaryRule", &rule) ||
        !fake_text(store, "CanaryClaim", &claim) ||
        /* Intern out of numeric order so sorting private value IDs would
         * observably produce the wrong hypothesis order. */
        !fake_text(store, "30", &position_30) ||
        !fake_text(store, "10", &position_10) ||
        !fake_text(store, "20", &position_20) ||
        !fake_text(store, "15", &position_15))
        return false;

    if (!add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND],
                  tc, selectors[
                          PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_LITERAL],
                  NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND],
                  literal, selectors[
                               PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_LITERAL],
                  NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND],
                  x, selectors[
                         PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE],
                  formal_x_kind_row_out) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND],
                  y, selectors[
                         PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE],
                  NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND],
                  z, selectors[
                         PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE],
                  NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND],
                  a, selectors[
                         PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE],
                  NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND],
                  c, selectors[
                         PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE],
                  NULL))
        return false;

    if (!add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_FLOATING_VARIABLE],
                  bind_x, x, NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_FLOATING_VARIABLE],
                  bind_y, y, NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_FLOATING_VARIABLE],
                  bind_z, z, NULL))
        return false;

    items[0] = tc;
    items[1] = x;
    if (!fake_formula(store, items, 2u, &formula_bind_x))
        return false;
    items[1] = y;
    if (!fake_formula(store, items, 2u, &formula_bind_y))
        return false;
    items[1] = z;
    if (!fake_formula(store, items, 2u, &formula_bind_z))
        return false;
    items[0] = literal;
    items[1] = x;
    items[2] = y;
    if (!fake_formula(store, items, 3u, &formula_essential))
        return false;
    items[0] = tc;
    items[1] = literal;
    items[2] = x;
    if (!fake_formula(store, items, 3u, &formula_rule))
        return false;

    if (!add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA],
                  bind_x, formula_bind_x, NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA],
                  bind_y, formula_bind_y, NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA],
                  bind_z, formula_bind_z, NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA],
                  essential, formula_essential, NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA],
                  rule, formula_rule, NULL))
        return false;

    if (!add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
                  bind_x, selectors[
                              PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING],
                  NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
                  bind_y, selectors[
                              PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING],
                  NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
                  bind_z, selectors[
                              PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING],
                  NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
                  essential, selectors[
                                 PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL],
                  NULL))
        return false;

    if (!add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE],
                  rule, x, NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE],
                  rule, y, NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                       PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE],
                  claim, a, NULL))
        return false;

    if (!add_triple(store, bridge->resolved_table_ids[
                         PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
                    rule, position_30, essential, NULL) ||
        !add_triple(store, bridge->resolved_table_ids[
                         PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
                    rule, position_10, bind_z, NULL) ||
        !add_triple(store, bridge->resolved_table_ids[
                         PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
                    rule, position_20, bind_y, ordered_y_row_out) ||
        !add_triple(store, bridge->resolved_table_ids[
                         PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
                    rule, position_15, bind_x, NULL))
        return false;

    if (!add_triple(store, bridge->resolved_table_ids[
                         PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_DISJOINT],
                    rule, x, y, NULL) ||
        !add_triple(store, bridge->resolved_table_ids[
                         PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_DISJOINT],
                    rule, x, z, NULL) ||
        !add_pair(store, bridge->resolved_table_ids[
                            PPPROOF_GSLT_RELATIONAL_TABLE_V1_ACTIVE_APARTNESS],
                  a, c, active_apartness_row_out))
        return false;

    items[0] = tc;
    items[1] = a;
    if (!fake_formula(store, items, 2u, &actual_formula_values[0]))
        return false;
    items[1] = c;
    if (!fake_formula(store, items, 2u, &actual_formula_values[1]))
        return false;
    items[0] = literal;
    items[1] = a;
    items[2] = c;
    if (!fake_formula(store, items, 3u, &actual_formula_values[2]))
        return false;

    *rule_out = rule;
    *claim_out = claim;
    *stored_variable_out = a;
    *dummy_variable_out = c;
    return true;
}

static bool setup_repeated_identity(
    FakeStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *bridge) {
    uint32_t literal_selector;
    uint32_t essential_selector;
    uint32_t rule_selector;
    uint32_t type;
    uint32_t literal;
    uint32_t essential;
    uint32_t rule;
    uint32_t claim;
    uint32_t position;
    uint32_t formula;
    uint32_t items[2];

    if (!store || !bridge ||
        !fake_value_intern(
            store,
            bridge->selectors[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_LITERAL].bytes,
            bridge->selectors[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_LITERAL].len,
            &literal_selector) ||
        !fake_value_intern(
            store,
            bridge->selectors[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL]
                .bytes,
            bridge->selectors[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL]
                .len,
            &essential_selector) ||
        !fake_text(store, "CanaryRuleFirstV1", &rule_selector) ||
        !fake_text(store, "CanaryIdentityType", &type) ||
        !fake_text(store, "CanaryIdentityLiteral", &literal) ||
        !fake_text(store, "CanaryIdentityEssential", &essential) ||
        !fake_text(store, "CanaryIdentityRule", &rule) ||
        !fake_text(store, "CanaryIdentityClaim", &claim) ||
        !fake_text(store, "10", &position))
        return false;

    items[0] = type;
    items[1] = literal;
    if (!fake_formula(store, items, 2u, &formula))
        return false;

    return add_pair(
               store,
               bridge->resolved_table_ids[
                   PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND],
               type, literal_selector, NULL) &&
           add_pair(
               store,
               bridge->resolved_table_ids[
                   PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND],
               literal, literal_selector, NULL) &&
           add_pair(
               store,
               bridge->resolved_table_ids[
                   PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA],
               essential, formula, NULL) &&
           add_pair(
               store,
               bridge->resolved_table_ids[
                   PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA],
               rule, formula, NULL) &&
           add_pair(
               store,
               bridge->resolved_table_ids[
                   PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
               essential, essential_selector, NULL) &&
           add_pair(
               store,
               bridge->resolved_table_ids[
                   PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
               rule, rule_selector, NULL) &&
           add_pair(
               store,
               bridge->resolved_table_ids[
                   PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_ACTIVE_HYPOTHESIS],
               claim, essential, NULL) &&
           add_triple(
               store,
               bridge->resolved_table_ids[
                   PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
               rule, position, essential, NULL);
}

static PPProofGSLTArticleV1Result check_application(
    const PPProofGSLTPlanV1 *proof_plan,
    PPProofGSLTRelationalContextV1 *context,
    const PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTSequenceProofV1 proof,
    PPProofGSLTArticleV1Receipt *receipt,
    char *error, size_t error_size) {
    const PPProofGSLTPatternV1 *premises = NULL;
    uint32_t premise_len = 0u;
    PPProofGSLTArticleV1 article;
    PPProofGSLTArticleV1Result result;

    result = ppproof_gslt_relational_context_v1_view(
        context, &premises, &premise_len, error, error_size);
    if (result != PPPROOF_GSLT_ARTICLE_V1_OK)
        return result;
    article = (PPProofGSLTArticleV1){
        .version = 1u,
        .nodes = producer->nodes,
        .node_len = producer->node_len,
        .root_id = proof.evidence.index,
        .target = proof.goal,
    };
    return ppproof_gslt_article_v1_check_open(
        &proof_plan->presentation, premises, premise_len,
        &article, true, NULL, receipt, error, error_size);
}

int main(int argc, char **argv) {
    static const uint32_t arities[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN] = {
            2u, 2u, 2u, 2u, 3u, 2u, 3u, 2u, 2u,
        };
    static const uint32_t key_arities[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN] = {
            1u, 1u, 1u, 2u, 2u, 2u, 3u, 2u, 1u,
        };
    SymbolTable symbols;
    PPProofGSLTPlanV1 proof_plan;
    PPProofGSLTSequenceEvidenceABIV1 evidence_abi;
    PPProofGSLTRelationalAssertionPlanV1 bridge;
    PPRelationalStateProgramV1Plan state_plan;
    PPRelationalStateProofMachineV1 proof_machine;
    PPProofIndexedProgramPlanV1 indexed_program_plan;
    PPProofPreparedActionCaseV1 action_cases[5];
    PPProofPreparedActionCaseV1 small_action_cases[2];
    PPProofFrameIndexPlanV1 frame_index_plan;
    PPRelationalStateTableV1 state_tables[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN];
    FakeStoreV1 fake_store = {0};
    PPRelationalStoreV1 store;
    PPProofGSLTRelationalContextV1 context;
    PPProofGSLTRelationalDeclarationV1 schema;
    PPProofGSLTRelationalPreparedAssertionV1 prepared;
    PPProofGSLTRelationalActualHypothesisV1 actuals[3];
    PPProofGSLTRelationalEvidenceV1 relational_evidence;
    PPProofGSLTSequenceEvidenceSourcesV1 sources;
    PPProofGSLTSequenceEvidenceProducerV1 producer;
    PPProofGSLTAssertionApplicationV1 application;
    PPProofGSLTReferenceV1 declaration_reference;
    PPProofGSLTArticleV1Receipt receipt;
    PPProofGSLTArticleV1Result result;
    uint32_t rule = 0u;
    uint32_t claim = 0u;
    uint32_t formula_values[3] = {0u};
    uint32_t active_apartness_row = 0u;
    uint32_t formal_x_kind_row = 0u;
    uint32_t ordered_y_row = 0u;
    uint32_t stored_variable = 0u;
    uint32_t dummy_variable = 0u;
    uint32_t claim_bind_c_row = 0u;
    uint32_t index;
    char error[512] = {0};

    if (argc != 5 + PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN) {
        fprintf(stderr,
                "usage: %s PROOF_PLAN EVIDENCE_ABI BRIDGE NO_APARTNESS TABLE...\n",
                argv[0]);
        return 2;
    }
    ppproof_gslt_plan_v1_init(&proof_plan);
    ppproof_gslt_sequence_evidence_abi_v1_init(&evidence_abi);
    ppproof_gslt_relational_assertion_v1_init(&bridge);
    ppproof_gslt_relational_context_v1_init(&context);
    ppproof_gslt_relational_declaration_v1_init(&schema);
    ppproof_gslt_relational_prepared_assertion_v1_init(&prepared);
    ppproof_gslt_sequence_evidence_producer_v1_init(&producer);
    memset(&state_plan, 0, sizeof(state_plan));
    memset(&proof_machine, 0, sizeof(proof_machine));
    memset(&indexed_program_plan, 0, sizeof(indexed_program_plan));
    memset(&frame_index_plan, 0, sizeof(frame_index_plan));
    memset(state_tables, 0, sizeof(state_tables));
    for (index = 0u;
         index < PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN; index++) {
        state_tables[index].name = argv[5u + index];
        state_tables[index].arity = arities[index];
        state_tables[index].key_arity = key_arities[index];
    }
    state_plan.tables = state_tables;
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
          "relational declaration proof plan loads");
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_sequence_evidence_abi_v1_load(
            &evidence_abi, argv[2], &proof_plan, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "relational declaration evidence ABI loads");
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_relational_assertion_v1_load(
            &bridge, argv[3], &proof_plan, &state_plan,
            error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "relational declaration bridge loads");

    if (result == PPPROOF_GSLT_ARTICLE_V1_OK &&
        setup_store(
            &fake_store, &bridge, &rule, &claim, formula_values,
            &active_apartness_row, &formal_x_kind_row,
            &ordered_y_row, &stored_variable, &dummy_variable)) {
        if (!setup_repeated_identity(&fake_store, &bridge) ||
            !setup_machine_rows(
                &fake_store, &bridge, &proof_machine, rule, claim,
                &claim_bind_c_row)) {
            result = PPPROOF_GSLT_ARTICLE_V1_INVALID;
        }
        state_plan.proof_machines = &proof_machine;
        state_plan.proof_machine_len = 1u;
        indexed_program_plan.operation = "fixture-operation-v1";
        indexed_program_plan.action_index = 0u;
        indexed_program_plan.machine = bridge.execution.machine;
        indexed_program_plan.header_role = "fixture-header-v1";
        indexed_program_plan.code_role = "fixture-code-v1";
        indexed_program_plan.terminal_low = proof_machine.terminal_low;
        indexed_program_plan.terminal_high = proof_machine.terminal_high;
        indexed_program_plan.continuation_low =
            proof_machine.continuation_low;
        indexed_program_plan.continuation_high =
            proof_machine.continuation_high;
        indexed_program_plan.save_byte = proof_machine.save_byte;
        indexed_program_plan.unknown_byte = proof_machine.unknown_byte;
        indexed_program_plan.terminal_radix = proof_machine.terminal_radix;
        indexed_program_plan.terminal_digit_bias =
            proof_machine.terminal_digit_bias;
        indexed_program_plan.continuation_radix =
            proof_machine.continuation_radix;
        indexed_program_plan.continuation_digit_bias =
            proof_machine.continuation_digit_bias;
        indexed_program_plan.save_placement = proof_machine.save_placement;
        indexed_program_plan.header_hypothesis_policy =
            proof_machine.header_hypothesis_policy;
        action_cases[0] = (PPProofPreparedActionCaseV1){
            bridge.execution.machine,
            "mm-label-floating",
            PPPROOF_PREPARED_ACTION_V1_PUSH_DECLARED,
        };
        action_cases[1] = (PPProofPreparedActionCaseV1){
            bridge.execution.machine,
            "mm-label-essential",
            PPPROOF_PREPARED_ACTION_V1_PUSH_DECLARED,
        };
        action_cases[2] = (PPProofPreparedActionCaseV1){
            bridge.execution.machine,
            "CanaryRuleFirstV1",
            PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME,
        };
        action_cases[3] = (PPProofPreparedActionCaseV1){
            bridge.execution.machine,
            "CanaryRuleSecondV1",
            PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME,
        };
        action_cases[4] = (PPProofPreparedActionCaseV1){
            bridge.execution.machine,
            "CanaryAdditionalKindV1",
            PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME,
        };
        small_action_cases[0] = action_cases[0];
        small_action_cases[1] = action_cases[2];
        indexed_program_plan.action_cases = action_cases;
        indexed_program_plan.action_case_len = 5u;
        indexed_program_plan.indexed_carrier =
            "prepared-classified-value-table-v1";
        indexed_program_plan.effect_carrier = "indexed-effect-machine-v1";
        indexed_program_plan.prepared_effect = "use-prepared-value-v1";
        indexed_program_plan.saved_effect = "use-saved-value-v1";
        indexed_program_plan.save_effect = "save-top-value-v1";
        indexed_program_plan.unknown_effect =
            PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE;
        indexed_program_plan.region = "proof-call-region-v1";
        check(ppproof_indexed_program_plan_v1_admits(
                  &indexed_program_plan, indexed_program_plan.operation,
                  indexed_program_plan.action_index, bridge.execution.machine,
                  indexed_program_plan.header_role,
                  indexed_program_plan.code_role,
                  indexed_program_plan.region,
                  PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE,
                  indexed_program_plan.save_placement,
                  indexed_program_plan.header_hypothesis_policy),
              "indexed-program admission follows the generated call region");
        check(!ppproof_indexed_program_plan_v1_admits(
                  &indexed_program_plan, indexed_program_plan.operation,
                  indexed_program_plan.action_index, bridge.execution.machine,
                  indexed_program_plan.header_role,
                  indexed_program_plan.code_role,
                  indexed_program_plan.region,
                  PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE,
                  indexed_program_plan.save_placement ==
                          CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1
                      ? CETTA_GSLT_INDEXED_SAVE_REPEATABLE_AFTER_USE_V1
                      : CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1,
                  indexed_program_plan.header_hypothesis_policy),
              "indexed-program admission rejects a different generated save policy");
        check(!ppproof_indexed_program_plan_v1_admits(
                  &indexed_program_plan, indexed_program_plan.operation,
                  indexed_program_plan.action_index, bridge.execution.machine,
                  indexed_program_plan.header_role,
                  indexed_program_plan.code_role,
                  "different-generated-region-v1",
                  PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE,
                  indexed_program_plan.save_placement,
                  indexed_program_plan.header_hypothesis_policy),
              "indexed-program admission rejects a different generated region");
        action_cases[2].action =
            PPPROOF_PREPARED_ACTION_V1_INVALID;
        check(!ppproof_indexed_program_plan_v1_admits(
                  &indexed_program_plan, indexed_program_plan.operation,
                  indexed_program_plan.action_index, bridge.execution.machine,
                  indexed_program_plan.header_role,
                  indexed_program_plan.code_role,
                  indexed_program_plan.region,
                  PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE,
                  indexed_program_plan.save_placement,
                  indexed_program_plan.header_hypothesis_policy),
              "indexed-program admission rejects an invalid generated action");
        action_cases[2].action =
            PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME;
        action_cases[3].source_kind = action_cases[2].source_kind;
        check(!ppproof_indexed_program_plan_v1_admits(
                  &indexed_program_plan, indexed_program_plan.operation,
                  indexed_program_plan.action_index, bridge.execution.machine,
                  indexed_program_plan.header_role,
                  indexed_program_plan.code_role,
                  indexed_program_plan.region,
                  PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE,
                  indexed_program_plan.save_placement,
                  indexed_program_plan.header_hypothesis_policy),
              "indexed-program admission rejects a duplicate generated source kind");
        action_cases[3].source_kind = "CanaryRuleSecondV1";
        indexed_program_plan.action_cases = small_action_cases;
        indexed_program_plan.action_case_len = 2u;
        check(ppproof_indexed_program_plan_v1_admits(
                  &indexed_program_plan, indexed_program_plan.operation,
                  indexed_program_plan.action_index, bridge.execution.machine,
                  indexed_program_plan.header_role,
                  indexed_program_plan.code_role,
                  indexed_program_plan.region,
                  PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE,
                  indexed_program_plan.save_placement,
                  indexed_program_plan.header_hypothesis_policy),
              "indexed-program admission is independent of action inventory size");
        indexed_program_plan.action_cases = action_cases;
        indexed_program_plan.action_case_len = 5u;
        frame_index_plan.operation = indexed_program_plan.operation;
        frame_index_plan.action_index = indexed_program_plan.action_index;
        frame_index_plan.machine = bridge.execution.machine;
        frame_index_plan.carrier = "u32-open-addressed-index-v1";
        frame_index_plan.validation = "duplicate-reject-v1";
        frame_index_plan.region = "proof-call-region-v1";
        store = fake_store_interface(&fake_store);
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
            result = ppproof_gslt_relational_context_v1_begin(
                &context, &store, &bridge, &evidence_abi,
                NULL, error, sizeof(error));
    } else {
        result = PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "generic relational declaration context starts");
    {
        PPProofGSLTRelationalDeclarationSnapshotV1 snapshot;
        uint32_t formula_table = bridge.resolved_table_ids[
            PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA];
        uint32_t saved_formula_len = fake_store.tables[formula_table].row_len;

        check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
                  ppproof_gslt_relational_context_v1_declaration_snapshot(
                      &context, &snapshot) &&
                  ppproof_gslt_relational_context_v1_declaration_snapshot_matches(
                      &context, &snapshot),
          "declaration inputs admit a live immutable-prefix snapshot");
        fake_store.tables[formula_table].row_len++;
        check(!ppproof_gslt_relational_context_v1_declaration_snapshot_matches(
                  &context, &snapshot),
              "declaration snapshot rejects an appended input row");
        fake_store.tables[formula_table].row_len = saved_formula_len;
        fake_store.reject_immutable_snapshot = true;
        check(!ppproof_gslt_relational_context_v1_declaration_snapshot_matches(
                  &context, &snapshot),
              "declaration caching rejects a store without immutable-prefix evidence");
        fake_store.reject_immutable_snapshot = false;
    }
    {
        uint32_t stored_key[2] = {claim, stored_variable};
        uint32_t dummy_key[2] = {claim, dummy_variable};
        uint32_t row[2];
        uint32_t table = bridge.resolved_table_ids[
            PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE];

        check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
                  fake_table_find(
                      &fake_store, table, stored_key, 2u, row, 2u) &&
                  !fake_table_find(
                      &fake_store, table, dummy_key, 2u, row, 2u),
              "stored frame excludes the proof-local dummy variable");
    }
    {
        CettaGsltReusableBufferV1 required_binders = {0};
        CettaGsltReusableBufferV1 ordered_rows = {0};
        CettaGsltReusableBufferV1 binding_schemas = {0};
        CettaGsltReusableBufferV1 premise_schemas = {0};
        CettaGsltReusableBufferV1 apartness_pairs = {0};
        CettaGsltReusableBufferV1 ordered_premises = {0};
        PPProofGSLTRelationalDeclarationWorkspaceV1 workspace = {
            .required_binder_ids = &required_binders,
            .ordered_rows = &ordered_rows,
            .binding_schemas = &binding_schemas,
            .premise_schemas = &premise_schemas,
            .apartness_pairs = &apartness_pairs,
            .ordered_premises = &ordered_premises,
        };
        PPProofGSLTRelationalDeclarationWorkspaceV1 partial_workspace = {
            .required_binder_ids = &required_binders,
        };
        PPProofGSLTRelationalDeclarationV1 workspace_schema;
        PPProofGSLTArticleV1Result workspace_result;
        bool workspace_ready =
            cetta_gslt_reusable_buffer_init_v1(
                &required_binders, sizeof(uint32_t)) &&
            cetta_gslt_reusable_buffer_init_v1(
                &ordered_rows,
                sizeof(PPProofGSLTRelationalOrderedRowV1)) &&
            cetta_gslt_reusable_buffer_init_v1(
                &binding_schemas,
                sizeof(PPProofGSLTRelationalBindingSchemaV1)) &&
            cetta_gslt_reusable_buffer_init_v1(
                &premise_schemas,
                sizeof(PPProofGSLTRelationalEssentialSchemaV1)) &&
            cetta_gslt_reusable_buffer_init_v1(
                &apartness_pairs,
                sizeof(PPProofGSLTAssertionDisjointV1)) &&
            cetta_gslt_reusable_buffer_init_v1(
                &ordered_premises,
                sizeof(PPProofGSLTRelationalOrderedHypothesisV1)) &&
            cetta_gslt_reusable_buffer_acquire_v1(&required_binders) ==
                CETTA_GSLT_REUSABLE_BUFFER_OK_V1 &&
            cetta_gslt_reusable_buffer_acquire_v1(&ordered_rows) ==
                CETTA_GSLT_REUSABLE_BUFFER_OK_V1 &&
            cetta_gslt_reusable_buffer_acquire_v1(&binding_schemas) ==
                CETTA_GSLT_REUSABLE_BUFFER_OK_V1 &&
            cetta_gslt_reusable_buffer_acquire_v1(&premise_schemas) ==
                CETTA_GSLT_REUSABLE_BUFFER_OK_V1 &&
            cetta_gslt_reusable_buffer_acquire_v1(&apartness_pairs) ==
                CETTA_GSLT_REUSABLE_BUFFER_OK_V1 &&
            cetta_gslt_reusable_buffer_acquire_v1(&ordered_premises) ==
                CETTA_GSLT_REUSABLE_BUFFER_OK_V1;

        ppproof_gslt_relational_declaration_v1_init(&workspace_schema);
        workspace_result = workspace_ready
            ? ppproof_gslt_relational_declaration_v1_elaborate_with_workspace(
                  &context, rule, &workspace, &workspace_schema,
                  error, sizeof(error))
            : PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
        check(workspace_result == PPPROOF_GSLT_ARTICLE_V1_OK &&
                  workspace_schema.binding_len == 2u &&
                  workspace_schema.ordered_len == 3u &&
                  required_binders.capacity >= 2u &&
                  ordered_rows.capacity >= 3u &&
                  binding_schemas.capacity >= 2u &&
                  premise_schemas.capacity >= 1u &&
                  apartness_pairs.capacity >= 1u &&
                  ordered_premises.capacity >= 3u,
              "admitted declaration scratch preserves the elaborated frame");
        ppproof_gslt_relational_declaration_v1_free(&workspace_schema);
        workspace_result = workspace_ready
            ? ppproof_gslt_relational_declaration_v1_elaborate_with_workspace(
                  &context, rule, &partial_workspace, &workspace_schema,
                  error, sizeof(error))
            : PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
        check(workspace_result == PPPROOF_GSLT_ARTICLE_V1_INVALID,
              "partial declaration scratch fails closed");
        if (ordered_premises.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &ordered_premises);
        if (apartness_pairs.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&apartness_pairs);
        if (premise_schemas.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&premise_schemas);
        if (binding_schemas.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&binding_schemas);
        if (ordered_rows.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&ordered_rows);
        if (required_binders.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&required_binders);
        cetta_gslt_reusable_buffer_free_v1(&ordered_rows);
        cetta_gslt_reusable_buffer_free_v1(&required_binders);
        cetta_gslt_reusable_buffer_free_v1(&ordered_premises);
        cetta_gslt_reusable_buffer_free_v1(&apartness_pairs);
        cetta_gslt_reusable_buffer_free_v1(&premise_schemas);
        cetta_gslt_reusable_buffer_free_v1(&binding_schemas);
    }
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_relational_declaration_v1_elaborate(
            &context, rule, &schema, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "generated state rows elaborate to an assertion schema");
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
              schema.binding_len == 2u && schema.essential_len == 1u &&
              schema.ordered_len == 3u && schema.disjoint_len == 1u,
          "mandatory filtering preserves the exact assertion frame");
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
              schema.ordered[0].kind ==
                  PPPROOF_GSLT_RELATIONAL_HYPOTHESIS_V1_BINDING &&
              schema.ordered[0].source_position == 15u &&
              schema.ordered[1].source_position == 20u &&
              schema.ordered[2].kind ==
                  PPPROOF_GSLT_RELATIONAL_HYPOTHESIS_V1_ESSENTIAL,
          "explicit hypothesis positions determine stack order");

    memset(actuals, 0, sizeof(actuals));
    for (index = 0u;
         result == PPPROOF_GSLT_ARTICLE_V1_OK && index < 3u; index++) {
        result = ppproof_gslt_relational_context_v1_formula(
            &context, formula_values[index], &actuals[index].formula,
            error, sizeof(error));
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
            result =
                ppproof_gslt_relational_context_v1_add_provable_premise(
                    &context, actuals[index].formula,
                    &actuals[index].proof, error, sizeof(error));
    }
    {
        CettaGsltReusableBufferV1 prepared_bindings = {0};
        CettaGsltReusableBufferV1 prepared_premises = {0};
        PPProofGSLTRelationalPreparedWorkspaceV1 workspace = {
            .bindings = &prepared_bindings,
            .premises = &prepared_premises,
        };
        PPProofGSLTRelationalPreparedWorkspaceV1 partial_workspace = {
            .bindings = &prepared_bindings,
        };
        PPProofGSLTRelationalPreparedAssertionV1 workspace_prepared;
        PPProofGSLTArticleV1Result workspace_result;
        bool workspace_ready =
            cetta_gslt_reusable_buffer_init_v1(
                &prepared_bindings,
                sizeof(PPProofGSLTAssertionBindingV1)) &&
            cetta_gslt_reusable_buffer_init_v1(
                &prepared_premises,
                sizeof(PPProofGSLTAssertionEssentialV1)) &&
            cetta_gslt_reusable_buffer_acquire_v1(&prepared_bindings) ==
                CETTA_GSLT_REUSABLE_BUFFER_OK_V1 &&
            cetta_gslt_reusable_buffer_acquire_v1(&prepared_premises) ==
                CETTA_GSLT_REUSABLE_BUFFER_OK_V1;

        ppproof_gslt_relational_prepared_assertion_v1_init(
            &workspace_prepared);
        workspace_result =
            result == PPPROOF_GSLT_ARTICLE_V1_OK && workspace_ready
                ? ppproof_gslt_relational_prepared_assertion_v1_build_with_workspace(
                      &schema, actuals, 3u, &workspace,
                      &workspace_prepared, error, sizeof(error))
                : PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
        check(workspace_result == PPPROOF_GSLT_ARTICLE_V1_OK &&
                  workspace_prepared.declaration.binding_len == 2u &&
                  workspace_prepared.declaration.essential_len == 1u &&
                  prepared_bindings.capacity >= 2u &&
                  prepared_premises.capacity >= 1u,
              "admitted preparation scratch preserves the assertion input");
        ppproof_gslt_relational_prepared_assertion_v1_free(
            &workspace_prepared);
        workspace_result = workspace_ready
            ? ppproof_gslt_relational_prepared_assertion_v1_build_with_workspace(
                  &schema, actuals, 3u, &partial_workspace,
                  &workspace_prepared, error, sizeof(error))
            : PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
        check(workspace_result == PPPROOF_GSLT_ARTICLE_V1_INVALID,
              "partial preparation scratch fails closed");
        if (prepared_premises.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &prepared_premises);
        if (prepared_bindings.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &prepared_bindings);
        cetta_gslt_reusable_buffer_free_v1(&prepared_premises);
        cetta_gslt_reusable_buffer_free_v1(&prepared_bindings);
    }
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_relational_prepared_assertion_v1_build(
            &schema, actuals, 3u, &prepared, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "ordered proof-stack entries prepare an assertion application");

    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_relational_context_v1_reserve_premise(
            &context, &declaration_reference, error, sizeof(error));
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_relational_context_v1_evidence_sources(
            &context, &relational_evidence, &sources,
            error, sizeof(error));
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_sequence_evidence_producer_v1_begin(
            &producer, &evidence_abi, 0u, NULL,
            error, sizeof(error));
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_sequence_evidence_producer_v1_apply_assertion(
            &producer, &prepared.declaration,
            declaration_reference.index, &sources, &application,
            error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "generic producer consumes the relational assertion");
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_relational_context_v1_fill_premise(
            &context, declaration_reference, application.declared_goal,
            error, sizeof(error));
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = check_application(
            &proof_plan, &context, &producer, application.proof,
            &receipt, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK && receipt.rooted,
          "full active frame admits the proof-local dummy substitution");

    {
        FakeTableV1 *apartness_table = &fake_store.tables[
            proof_machine.active_disjoint_table];
        uint32_t *row = apartness_table->rows +
            (size_t)active_apartness_row * apartness_table->arity;
        uint32_t saved_left = row[0];
        PPProofGSLTSequenceEvidenceProducerV1 negative_producer;
        PPProofGSLTAssertionApplicationV1 negative_application;

        row[0] = rule;
        ppproof_gslt_sequence_evidence_producer_v1_init(
            &negative_producer);
        result = ppproof_gslt_sequence_evidence_producer_v1_begin(
            &negative_producer, &evidence_abi, 0u, NULL,
            error, sizeof(error));
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
            result =
                ppproof_gslt_sequence_evidence_producer_v1_apply_assertion(
                    &negative_producer, &prepared.declaration,
                    declaration_reference.index, &sources,
                    &negative_application, error, sizeof(error));
        check(result == PPPROOF_GSLT_ARTICLE_V1_REJECTED,
              "deleting current active apartness kills the application");
        ppproof_gslt_sequence_evidence_producer_v1_free(
            &negative_producer);
        row[0] = saved_left;
    }

    {
        FakeTableV1 *symbol_table = &fake_store.tables[
            bridge.resolved_table_ids[PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND]];
        uint32_t *kind_row = symbol_table->rows +
            (size_t)formal_x_kind_row * symbol_table->arity;
        uint32_t saved_kind = kind_row[1];
        PPProofGSLTRelationalContextV1 negative_context;
        PPProofGSLTRelationalDeclarationV1 negative_schema;

        kind_row[1] = symbol_table->rows[1];
        ppproof_gslt_relational_context_v1_init(&negative_context);
        ppproof_gslt_relational_declaration_v1_init(&negative_schema);
        result = ppproof_gslt_relational_context_v1_begin(
            &negative_context, &store, &bridge, &evidence_abi,
            NULL, error, sizeof(error));
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
            result = ppproof_gslt_relational_declaration_v1_elaborate(
                &negative_context, rule, &negative_schema,
                error, sizeof(error));
        check(result == PPPROOF_GSLT_ARTICLE_V1_INVALID,
              "falsifying a mandatory variable class kills elaboration");
        ppproof_gslt_relational_declaration_v1_free(&negative_schema);
        ppproof_gslt_relational_context_v1_free(&negative_context);
        kind_row[1] = saved_kind;
    }

    {
        FakeTableV1 *ordered_table = &fake_store.tables[
            bridge.resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS]];
        uint32_t *row = ordered_table->rows +
            (size_t)ordered_y_row * ordered_table->arity;
        uint32_t saved_assertion = row[0];
        PPProofGSLTRelationalContextV1 negative_context;
        PPProofGSLTRelationalDeclarationV1 negative_schema;

        row[0] = claim;
        ppproof_gslt_relational_context_v1_init(&negative_context);
        ppproof_gslt_relational_declaration_v1_init(&negative_schema);
        result = ppproof_gslt_relational_context_v1_begin(
            &negative_context, &store, &bridge, &evidence_abi,
            NULL, error, sizeof(error));
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
            result = ppproof_gslt_relational_declaration_v1_elaborate(
                &negative_context, rule, &negative_schema,
                error, sizeof(error));
        check(result == PPPROOF_GSLT_ARTICLE_V1_INVALID,
              "deleting an ordered mandatory hypothesis kills elaboration");
        ppproof_gslt_relational_declaration_v1_free(&negative_schema);
        ppproof_gslt_relational_context_v1_free(&negative_context);
        row[0] = saved_assertion;
    }

    {
        static const uint8_t label_bytes[] = "CanaryClaim";
        static const uint8_t type_bytes[] = "CanaryType";
        static const uint8_t literal_bytes[] = "CanaryLiteral";
        static const uint8_t actual_a_bytes[] = "CanaryA";
        static const uint8_t actual_c_bytes[] = "CanaryC";
        static const uint8_t bind_a_bytes[] = "CanaryBindA";
        static const uint8_t bind_c_bytes[] = "CanaryBindC";
        static const uint8_t actual_rule_bytes[] = "CanaryActualRule";
        static const uint8_t rule_bytes[] = "CanaryRule";
        static const uint8_t unknown_bytes[] = "?";
        PPRelationalValueV1Slice claim_slices[3] = {
            {type_bytes, (uint32_t)(sizeof(type_bytes) - 1u)},
            {literal_bytes, (uint32_t)(sizeof(literal_bytes) - 1u)},
            {actual_a_bytes, (uint32_t)(sizeof(actual_a_bytes) - 1u)},
        };
        PPRelationalValueV1Slice proof_steps[6] = {
            {bind_a_bytes, (uint32_t)(sizeof(bind_a_bytes) - 1u)},
            {bind_c_bytes, (uint32_t)(sizeof(bind_c_bytes) - 1u)},
            {bind_a_bytes, (uint32_t)(sizeof(bind_a_bytes) - 1u)},
            {bind_c_bytes, (uint32_t)(sizeof(bind_c_bytes) - 1u)},
            {actual_rule_bytes,
             (uint32_t)(sizeof(actual_rule_bytes) - 1u)},
            {rule_bytes, (uint32_t)(sizeof(rule_bytes) - 1u)},
        };
        PPProofGSLTRelationalNormalInputV1 machine_input = {
            .label = {
                label_bytes, (uint32_t)(sizeof(label_bytes) - 1u)},
            .claim = claim_slices,
            .claim_len = 3u,
            .steps = proof_steps,
            .step_len = 6u,
        };
        PPProofGSLTRelationalMachineV1Receipt machine_receipt;
        PPProofGSLTRelationalMachineV1Receipt normal_receipt;
        PPProofGSLTRelationalMachineV1Result machine_result;
        PPProofGSLTRelationalMachineV1Workspace workspace = {0};
        bool workspace_ready =
            ppproof_gslt_relational_machine_v1_workspace_init(&workspace) &&
            ppproof_gslt_relational_machine_v1_workspace_set_repetition_policy(
                &workspace,
                CETTA_GSLT_REPETITION_POLICY_SECOND_OCCURRENCE_V1);

        check(workspace_ready,
              "generic proof workspace initializes without guest data");

        machine_result = ppproof_gslt_relational_machine_v1_normal(
            &store, &state_plan, &proof_plan, &evidence_abi,
            &bridge, action_cases, 4u, &machine_input, NULL, &machine_receipt,
            error, sizeof(error));
        if (machine_result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
            fprintf(stderr, "normal machine diagnostic: %s\n", error);
        check(machine_result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK &&
                  machine_receipt.complete &&
                  machine_receipt.proof_step_len == 6u &&
                  machine_receipt.execution ==
                      PPPROOF_GSLT_RELATIONAL_EXECUTION_V1_LABEL_STREAM &&
                  machine_receipt.workspace_action_index_capacity == 0u &&
                  machine_receipt.decoded_byte_len == 0u &&
                  machine_receipt.decoded_instruction_len == 0u &&
                  machine_receipt.article.rooted,
              "generic normal machine checks a generated proof article");
        normal_receipt = machine_receipt;
        {
            PPRelationalStateProofMachineV1 saved_proof_machine =
                proof_machine;
            proof_machine.name = NULL;
            proof_machine.formula_table = UINT32_MAX;
            proof_machine.active_disjoint_table = UINT32_MAX;
            proof_machine.unknown_token.bytes = NULL;
            proof_machine.unknown_token.len = 0u;
            proof_machine.unknown_policy =
                (PPRelationalStackProofV1UnknownPolicy)UINT32_MAX;
            proof_machine.terminal_low = 0u;
            proof_machine.terminal_high = 0u;
            proof_machine.continuation_low = 0u;
            proof_machine.continuation_high = 0u;
            proof_machine.save_byte = 0u;
            proof_machine.unknown_byte = 0u;
            proof_machine.terminal_radix = 0u;
            proof_machine.continuation_radix = 0u;
            machine_result = ppproof_gslt_relational_machine_v1_normal(
                &store, &state_plan, &proof_plan, &evidence_abi,
                &bridge, action_cases, 4u, &machine_input, NULL,
                &machine_receipt, error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK,
                  "generated relational descriptor owns public execution and tables");
            proof_machine = saved_proof_machine;
        }
        if (workspace_ready) {
            machine_result =
                ppproof_gslt_relational_machine_v1_normal_with_workspace(
                    &store, &state_plan, &proof_plan, &evidence_abi,
                    &bridge, action_cases, 4u, &workspace,
                    &machine_input, NULL,
                    &machine_receipt, error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK &&
                      machine_receipt.workspace_used &&
                      !machine_receipt.workspace_reused &&
                      machine_receipt.workspace_stack_capacity >= 1u &&
                      machine_receipt.workspace_action_index_capacity >= 4u &&
                      machine_receipt.workspace_actual_capacity >= 1u &&
                      machine_receipt.workspace_required_binder_capacity >=
                          1u &&
                      machine_receipt.workspace_ordered_row_capacity >= 1u &&
                      machine_receipt.workspace_binding_schema_capacity >=
                          1u &&
                      machine_receipt.workspace_premise_schema_capacity >=
                          1u &&
                      machine_receipt.workspace_apartness_capacity >= 1u &&
                      machine_receipt.workspace_ordered_premise_capacity >=
                          1u &&
                      machine_receipt.workspace_prepared_binding_capacity >=
                          1u &&
                      machine_receipt.workspace_prepared_premise_capacity >=
                          1u &&
                      machine_receipt.workspace_declaration_cache_capacity ==
                          0u &&
                      machine_receipt.declaration_cache_hit_len == 0u &&
                      machine_receipt.declaration_cache_miss_len == 2u &&
                      machine_receipt.declaration_cache_promotion_len == 0u &&
                      !machine_receipt.declaration_cache_snapshot_rejected &&
                      machine_receipt.workspace_call_control_count == 2u &&
                      machine_receipt.workspace_evidence_arena_capacity >=
                          64u * 1024u &&
                      machine_receipt.workspace_evidence_node_capacity >=
                          1u &&
                      machine_receipt.workspace_evidence_cache_capacity >=
                          1u &&
                      machine_receipt.article.checked_node_len ==
                          normal_receipt.article.checked_node_len,
                  "first admitted proof call retains a generic workspace");
            machine_result =
                ppproof_gslt_relational_machine_v1_normal_with_workspace(
                    &store, &state_plan, &proof_plan, &evidence_abi,
                    &bridge, action_cases, 4u, &workspace,
                    &machine_input, NULL,
                    &machine_receipt, error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK &&
                      machine_receipt.workspace_used &&
                      machine_receipt.workspace_reused &&
                      machine_receipt.article.checked_node_len ==
                          normal_receipt.article.checked_node_len,
                  "second admitted proof call reuses the reset workspace");

            {
                static const uint8_t identity_label_bytes[] =
                    "CanaryIdentityClaim";
                static const uint8_t identity_type_bytes[] =
                    "CanaryIdentityType";
                static const uint8_t identity_literal_bytes[] =
                    "CanaryIdentityLiteral";
                static const uint8_t identity_essential_bytes[] =
                    "CanaryIdentityEssential";
                static const uint8_t identity_rule_bytes[] =
                    "CanaryIdentityRule";
                PPRelationalValueV1Slice identity_claim[2] = {
                    {identity_type_bytes,
                     (uint32_t)(sizeof(identity_type_bytes) - 1u)},
                    {identity_literal_bytes,
                     (uint32_t)(sizeof(identity_literal_bytes) - 1u)},
                };
                PPRelationalValueV1Slice identity_steps[4] = {
                    {identity_essential_bytes,
                     (uint32_t)(sizeof(identity_essential_bytes) - 1u)},
                    {identity_rule_bytes,
                     (uint32_t)(sizeof(identity_rule_bytes) - 1u)},
                    {identity_rule_bytes,
                     (uint32_t)(sizeof(identity_rule_bytes) - 1u)},
                    {identity_rule_bytes,
                     (uint32_t)(sizeof(identity_rule_bytes) - 1u)},
                };
                PPProofGSLTRelationalNormalInputV1 identity_input = {
                    .label = {
                        identity_label_bytes,
                        (uint32_t)(sizeof(identity_label_bytes) - 1u)},
                    .claim = identity_claim,
                    .claim_len = 2u,
                    .steps = identity_steps,
                    .step_len = 4u,
                };
                const CettaGsltRepetitionCostModelV1 cost_model = {
                    1u, 100u, 2u, 20u, 10u
                };
                uint64_t cached_cost = 0u;
                uint64_t fresh_cost = 0u;

                machine_result =
                    ppproof_gslt_relational_machine_v1_normal_with_workspace(
                        &store, &state_plan, &proof_plan, &evidence_abi,
                        &bridge, action_cases, 4u, &workspace,
                        &identity_input, NULL, &machine_receipt,
                        error, sizeof(error));
                if (machine_result !=
                    PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
                    fprintf(stderr,
                            "repetition cache diagnostic: %s\n", error);
                check(machine_result ==
                          PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK &&
                          machine_receipt.complete &&
                          machine_receipt.proof_step_len == 4u &&
                          machine_receipt.workspace_used &&
                          machine_receipt.workspace_reused &&
                          machine_receipt
                                  .workspace_declaration_cache_capacity >=
                              1u &&
                          machine_receipt.declaration_cache_hit_len == 1u &&
                          machine_receipt.declaration_cache_miss_len == 2u &&
                          machine_receipt
                                  .declaration_cache_promotion_len == 1u &&
                          !machine_receipt
                               .declaration_cache_snapshot_rejected &&
                          machine_receipt.article.rooted,
                      "public proof machine admits promotion then cache hit");
                check(machine_result ==
                          PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK &&
                          ppproof_gslt_relational_machine_v1_receipt_repetition_cost_qualify(
                              &machine_receipt, &cost_model,
                              &cached_cost, &fresh_cost) ==
                              CETTA_GSLT_REPETITION_COST_PROFITABLE_V1 &&
                          cached_cost == 235u && fresh_cost == 300u,
                      "public receipt feeds the generic cost qualifier");
            }
        }

        {
            static const uint8_t compressed_code_bytes[] = "ABABCD";
            static const uint8_t compressed_saved_code_bytes[] = "ABABCDZ";
            static const uint8_t compressed_saved_use_bytes[] = "AZBZEFCD";
            static const uint8_t bad_saved_index_bytes[] = "ABABCE";
            static const uint8_t interrupted_index_bytes[] = "UZ";
            static const uint8_t open_index_at_end_bytes[] = "U";
            PPRelationalValueV1Slice compressed_header[3] = {
                {bind_c_bytes,
                 (uint32_t)(sizeof(bind_c_bytes) - 1u)},
                {actual_rule_bytes,
                 (uint32_t)(sizeof(actual_rule_bytes) - 1u)},
                {rule_bytes, (uint32_t)(sizeof(rule_bytes) - 1u)},
            };
            PPRelationalValueV1Slice repeated_mandatory_header[3] = {
                {bind_a_bytes,
                 (uint32_t)(sizeof(bind_a_bytes) - 1u)},
                {actual_rule_bytes,
                 (uint32_t)(sizeof(actual_rule_bytes) - 1u)},
                {rule_bytes, (uint32_t)(sizeof(rule_bytes) - 1u)},
            };
            PPRelationalValueV1Slice compressed_code = {
                compressed_code_bytes,
                (uint32_t)(sizeof(compressed_code_bytes) - 1u),
            };
            PPProofGSLTRelationalCompressedInputV1 compressed_input = {
                .label = machine_input.label,
                .claim = claim_slices,
                .claim_len = 3u,
                .header = compressed_header,
                .header_len = 3u,
                .code = &compressed_code,
                .code_len = 1u,
            };
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed_with_workspace(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan,
                    workspace_ready ? &workspace : NULL,
                    &compressed_input,
                    NULL, &machine_receipt, error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK &&
                      machine_receipt.complete &&
                      machine_receipt.proof_step_len == 6u &&
                      machine_receipt.execution ==
                          PPPROOF_GSLT_RELATIONAL_EXECUTION_V1_INDEXED_INSTRUCTION_STREAM &&
                      machine_receipt.decoded_byte_len == 6u &&
                      machine_receipt.decoded_instruction_len == 6u &&
                      machine_receipt.prepared_value_len == 4u &&
                      machine_receipt.prepared_classification_len == 4u &&
                      machine_receipt.prepared_classification_use_len == 6u &&
                      machine_receipt.saved_value_len == 0u &&
                      (!workspace_ready ||
                       (machine_receipt.workspace_used &&
                        machine_receipt.workspace_reused &&
                        machine_receipt.workspace_prepared_capacity >= 4u &&
                        machine_receipt.workspace_action_index_capacity >= 4u &&
                        machine_receipt.workspace_actual_capacity >= 1u &&
                        machine_receipt.workspace_required_binder_capacity >=
                            1u &&
                        machine_receipt.workspace_ordered_row_capacity >=
                            1u &&
                        machine_receipt.workspace_binder_count_capacity >=
                            1u &&
                        machine_receipt.declaration_cache_hit_len == 0u &&
                        machine_receipt.declaration_cache_miss_len == 2u &&
                        machine_receipt.declaration_cache_promotion_len == 0u &&
                        !machine_receipt
                             .declaration_cache_snapshot_rejected &&
                        machine_receipt.workspace_call_control_count == 2u &&
                        machine_receipt
                                .workspace_required_binder_index_capacity >=
                            16u &&
                        machine_receipt
                                .workspace_premise_label_index_capacity >=
                            16u)) &&
                      machine_receipt.article.rooted,
                  "compressed machine preloads mandatory hypotheses before its header");
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK &&
                      machine_receipt.proof_step_len ==
                          normal_receipt.proof_step_len &&
                      machine_receipt.context_premise_len ==
                          normal_receipt.context_premise_len &&
                      machine_receipt.article_node_len ==
                          normal_receipt.article_node_len &&
                      machine_receipt.complete == normal_receipt.complete &&
                      machine_receipt.article.checked_node_len ==
                          normal_receipt.article.checked_node_len &&
                      machine_receipt.article.materialized_pattern_node_len ==
                          normal_receipt.article.materialized_pattern_node_len &&
                      machine_receipt.article.used_capabilities ==
                          normal_receipt.article.used_capabilities &&
                      machine_receipt.article.rooted ==
                          normal_receipt.article.rooted,
                  "normal and compressed execution produce the same proof receipt");

            compressed_input.header = repeated_mandatory_header;
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan, &compressed_input, NULL,
                    &machine_receipt, error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED &&
                      strstr(
                          error,
                          "compressed header repeats an implicitly prepared hypothesis") !=
                          NULL,
                  "strict generated header policy rejects a repeated mandatory hypothesis");
            compressed_input.header = compressed_header;

            compressed_code = (PPRelationalValueV1Slice){
                compressed_saved_code_bytes,
                (uint32_t)(sizeof(compressed_saved_code_bytes) - 1u),
            };
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan,
                    &compressed_input,
                    NULL, &machine_receipt, error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK &&
                      machine_receipt.complete &&
                      machine_receipt.proof_step_len == 6u &&
                      machine_receipt.decoded_byte_len == 7u &&
                      machine_receipt.decoded_instruction_len == 7u &&
                      machine_receipt.prepared_value_len == 4u &&
                      machine_receipt.saved_value_len == 1u,
                  "compressed save appends outside the prepared dictionary");

            compressed_code = (PPRelationalValueV1Slice){
                compressed_saved_use_bytes,
                (uint32_t)(sizeof(compressed_saved_use_bytes) - 1u),
            };
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan,
                    &compressed_input, NULL, &machine_receipt,
                    error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK &&
                      machine_receipt.complete &&
                      machine_receipt.proof_step_len == 6u &&
                      machine_receipt.decoded_byte_len == 8u &&
                      machine_receipt.decoded_instruction_len == 8u &&
                      machine_receipt.prepared_value_len == 4u &&
                      machine_receipt.saved_value_len == 2u &&
                      machine_receipt.article.rooted &&
                      machine_receipt.article.used_capabilities ==
                          normal_receipt.article.used_capabilities &&
                      machine_receipt.article.checked_node_len <=
                          normal_receipt.article.checked_node_len,
                  "compressed saved-result suffix replays prepared values exactly");

            compressed_code = (PPRelationalValueV1Slice){
                compressed_code_bytes,
                (uint32_t)(sizeof(compressed_code_bytes) - 1u),
            };

            indexed_program_plan.indexed_carrier =
                "unadmitted-indexed-value-v1";
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan,
                    &compressed_input, NULL, &machine_receipt,
                    error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
                      strstr(error, "does not admit") != NULL,
                  "compressed backend rejects an unadmitted indexed carrier");
            indexed_program_plan.indexed_carrier =
                "prepared-classified-value-table-v1";

            indexed_program_plan.effect_carrier =
                "unadmitted-indexed-effect-machine-v1";
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan,
                    &compressed_input, NULL, &machine_receipt,
                    error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
                      strstr(error, "indexed-program plan does not admit") !=
                          NULL,
                  "compressed backend rejects an unadmitted effect machine");
            indexed_program_plan.effect_carrier =
                "indexed-effect-machine-v1";

            frame_index_plan.carrier = "unadmitted-frame-index-v1";
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan,
                    &compressed_input, NULL, &machine_receipt,
                    error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
                      strstr(error, "frame-index plan does not admit") != NULL,
                  "compressed backend rejects an unadmitted frame index");
            frame_index_plan.carrier = "u32-open-addressed-index-v1";

            {
                PPProofGSLTArticleV1Limits small_limits =
                    ppproof_gslt_article_v1_default_limits();
                small_limits.maximum_article_nodes = 16u;
                machine_result =
                    ppproof_gslt_relational_machine_v1_compressed(
                        &store, &state_plan, &proof_plan,
                        &evidence_abi, &bridge, &indexed_program_plan,
                        &frame_index_plan,
                        &compressed_input,
                        &small_limits, &machine_receipt,
                        error, sizeof(error));
                check(machine_result ==
                          PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE &&
                          strstr(error,
                                 "sequence evidence nodes exceed their limit") !=
                              NULL,
                      "compressed evidence exhaustion is a resource result");
            }

            compressed_code = (PPRelationalValueV1Slice){
                bad_saved_index_bytes,
                (uint32_t)(sizeof(bad_saved_index_bytes) - 1u),
            };
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan,
                    &compressed_input,
                    NULL, &machine_receipt, error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED,
                  "compressed heap index is checked after mandatory offset");

            compressed_code = (PPRelationalValueV1Slice){
                interrupted_index_bytes,
                (uint32_t)(sizeof(interrupted_index_bytes) - 1u),
            };
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan,
                    &compressed_input,
                    NULL, &machine_receipt, error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED,
                  "compressed save cannot interrupt a U-Y index");

            compressed_code = (PPRelationalValueV1Slice){
                open_index_at_end_bytes,
                (uint32_t)(sizeof(open_index_at_end_bytes) - 1u),
            };
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan,
                    &compressed_input,
                    NULL, &machine_receipt, error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED &&
                      strstr(error, "ends inside an index") != NULL,
                  "a continuation at end still leaves an index open");

            compressed_code = (PPRelationalValueV1Slice){
                unknown_bytes,
                (uint32_t)(sizeof(unknown_bytes) - 1u),
            };
            machine_result =
                ppproof_gslt_relational_machine_v1_compressed(
                    &store, &state_plan, &proof_plan,
                    &evidence_abi, &bridge, &indexed_program_plan,
                    &frame_index_plan,
                    &compressed_input,
                    NULL, &machine_receipt, error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE &&
                      !machine_receipt.complete &&
                      machine_receipt.execution ==
                          PPPROOF_GSLT_RELATIONAL_EXECUTION_V1_INDEXED_INSTRUCTION_STREAM &&
                      machine_receipt.decoded_byte_len == 1u &&
                      machine_receipt.decoded_instruction_len == 1u,
                  "compressed unknown is incomplete rather than verified");
        }

        {
            PPRelationalValueV1Slice bad_claim[3];
            memcpy(bad_claim, claim_slices, sizeof(bad_claim));
            bad_claim[2] = (PPRelationalValueV1Slice){
                actual_c_bytes,
                (uint32_t)(sizeof(actual_c_bytes) - 1u),
            };
            machine_input.claim = bad_claim;
            machine_result = ppproof_gslt_relational_machine_v1_normal(
                &store, &state_plan, &proof_plan, &evidence_abi,
                &bridge, action_cases, 4u, &machine_input, NULL,
                &machine_receipt,
                error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED,
                  "falsifying the final claim kills the normal article");
            machine_input.claim = claim_slices;
        }

        {
            FakeTableV1 *active_table = &fake_store.tables[
                bridge.resolved_table_ids[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_ACTIVE_HYPOTHESIS]];
            uint32_t *row = active_table->rows +
                (size_t)claim_bind_c_row * active_table->arity;
            uint32_t saved_label = row[1];
            row[1] = rule;
            machine_result = ppproof_gslt_relational_machine_v1_normal(
                &store, &state_plan, &proof_plan, &evidence_abi,
                &bridge, action_cases, 4u, &machine_input, NULL,
                &machine_receipt,
                error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED,
                  "deleting full active-frame authority kills premise use");
            row[1] = saved_label;
        }

        {
            PPRelationalValueV1Slice future_step = {
                label_bytes, (uint32_t)(sizeof(label_bytes) - 1u)};
            machine_input.steps = &future_step;
            machine_input.step_len = 1u;
            machine_result = ppproof_gslt_relational_machine_v1_normal(
                &store, &state_plan, &proof_plan, &evidence_abi,
                &bridge, action_cases, 4u, &machine_input, NULL,
                &machine_receipt,
                error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED,
                  "current or future labels have no chronological authority");
        }

        {
            PPRelationalValueV1Slice unknown_step = {
                unknown_bytes, (uint32_t)(sizeof(unknown_bytes) - 1u)};
            machine_input.steps = &unknown_step;
            machine_input.step_len = 1u;
            machine_result = ppproof_gslt_relational_machine_v1_normal(
                &store, &state_plan, &proof_plan, &evidence_abi,
                &bridge, action_cases, 4u, &machine_input, NULL,
                &machine_receipt,
                error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE &&
                      !machine_receipt.complete,
                  "unknown proof is accepted only as incomplete");
        }

        machine_input.steps = proof_steps;
        machine_input.step_len = 6u;
        {
            char saved_digest_byte = state_plan.plan_digest[0];
            state_plan.plan_digest[0] =
                saved_digest_byte == '2' ? '3' : '2';
            machine_result = ppproof_gslt_relational_machine_v1_normal(
                &store, &state_plan, &proof_plan, &evidence_abi,
                &bridge, action_cases, 4u, &machine_input, NULL,
                &machine_receipt,
                error, sizeof(error));
            check(machine_result ==
                      PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID,
                  "composition digest mismatch fails closed");
            state_plan.plan_digest[0] = saved_digest_byte;
        }
        if (workspace_ready)
            ppproof_gslt_relational_machine_v1_workspace_free(&workspace);
    }

    {
        PPProofGSLTRelationalAssertionPlanV1 no_apartness_bridge;
        PPProofGSLTRelationalContextV1 no_apartness_context;
        PPProofGSLTRelationalEvidenceV1 no_apartness_evidence;
        PPProofGSLTSequenceEvidenceSourcesV1 no_apartness_sources;

        ppproof_gslt_relational_assertion_v1_init(&no_apartness_bridge);
        ppproof_gslt_relational_context_v1_init(&no_apartness_context);
        result = ppproof_gslt_relational_assertion_v1_load(
            &no_apartness_bridge, argv[4], &proof_plan, &state_plan,
            error, sizeof(error));
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
            result = ppproof_gslt_relational_context_v1_begin(
                &no_apartness_context, &store, &no_apartness_bridge,
                &evidence_abi, NULL, error, sizeof(error));
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
            result = ppproof_gslt_relational_context_v1_evidence_sources(
                &no_apartness_context, &no_apartness_evidence,
                &no_apartness_sources, error, sizeof(error));
        if (result != PPPROOF_GSLT_ARTICLE_V1_OK ||
            no_apartness_bridge.table_binding_len + 2u !=
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN ||
            no_apartness_evidence.active_apartness_table != UINT32_MAX) {
            fprintf(
                stderr,
                "no-apartness diagnostic: result=%d bindings=%u "
                "apartness=%u error=%s\n",
                (int)result, no_apartness_bridge.table_binding_len,
                no_apartness_evidence.active_apartness_table, error);
        }
        check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
                  no_apartness_bridge.table_binding_len + 2u ==
                      PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN &&
                  no_apartness_evidence.active_apartness_table == UINT32_MAX,
              "variable capability inventory executes without apartness relations");
        ppproof_gslt_relational_context_v1_free(&no_apartness_context);
        ppproof_gslt_relational_assertion_v1_free(&no_apartness_bridge);
    }

    ppproof_gslt_sequence_evidence_producer_v1_free(&producer);
    ppproof_gslt_relational_prepared_assertion_v1_free(&prepared);
    ppproof_gslt_relational_declaration_v1_free(&schema);
    ppproof_gslt_relational_context_v1_free(&context);
    fake_store_free(&fake_store);
    ppproof_gslt_relational_assertion_v1_free(&bridge);
    ppproof_gslt_sequence_evidence_abi_v1_free(&evidence_abi);
    ppproof_gslt_plan_v1_free(&proof_plan);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    printf("---\n%u passed, %u failed\n", passed, failed);
    return failed == 0u ? 0 : 1;
}
